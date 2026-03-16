/*
 * main_server.cpp — HTTP WebUI server for qwen3-tts.cpp
 *
 * Usage:
 *   qwen3-tts-server -m <model_dir> [options]
 *
 * Then open http://localhost:8080 in a browser.
 *
 * The server handles:
 *   GET  /          → embedded HTML page
 *   POST /api/synthesize  → multipart/form-data: returns WAV audio
 */

#include "qwen3_tts.h"
#include "http_server.h"
#include "webui.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

// ---------------------------------------------------------------------------
// Simple multipart/form-data parser (enough for our form)
// ---------------------------------------------------------------------------

struct FormField {
    std::string name;
    std::string filename; // non-empty if this is a file upload
    std::string content_type;
    std::string data;
};

static std::string get_header_value(const std::string & headers,
                                     const std::string & key) {
    // Case-insensitive search
    std::string low_headers = headers;
    std::string low_key     = key;
    for (auto & c : low_headers) c = static_cast<char>(tolower(c));
    for (auto & c : low_key)     c = static_cast<char>(tolower(c));

    auto pos = low_headers.find(low_key + ":");
    if (pos == std::string::npos) return "";
    pos += key.size() + 1;
    while (pos < headers.size() && headers[pos] == ' ') ++pos;
    auto end = headers.find("\r\n", pos);
    return headers.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
}

static std::string get_param(const std::string & text, const std::string & param) {
    // Extract param="value" or param=value from a header value
    auto pos = text.find(param + "=");
    if (pos == std::string::npos) return "";
    pos += param.size() + 1;
    if (pos < text.size() && text[pos] == '"') {
        ++pos;
        auto end = text.find('"', pos);
        return text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    }
    auto end = text.find_first_of("; \r\n", pos);
    return text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
}

static std::vector<FormField> parse_multipart(const std::string & body,
                                               const std::string & boundary) {
    std::vector<FormField> fields;
    std::string delim    = "--" + boundary;
    std::string end_mark = delim + "--";

    size_t pos = 0;
    while (pos < body.size()) {
        // Find next boundary
        auto bpos = body.find(delim, pos);
        if (bpos == std::string::npos) break;
        pos = bpos + delim.size();

        // Check for end marker
        if (pos < body.size() && body.substr(pos, 2) == "--") break;

        // Skip CRLF after boundary
        if (pos < body.size() && body.substr(pos, 2) == "\r\n") pos += 2;

        // Read part headers (up to blank line \r\n\r\n)
        auto header_end = body.find("\r\n\r\n", pos);
        if (header_end == std::string::npos) break;
        std::string part_headers = body.substr(pos, header_end - pos);
        pos = header_end + 4; // skip \r\n\r\n

        // Find next boundary to determine part body length
        auto next_boundary = body.find("\r\n" + delim, pos);
        size_t data_len = (next_boundary == std::string::npos)
                              ? body.size() - pos
                              : next_boundary - pos;

        FormField f;
        std::string cd = get_header_value(part_headers, "Content-Disposition");
        f.name         = get_param(cd, "name");
        f.filename     = get_param(cd, "filename");
        f.content_type = get_header_value(part_headers, "Content-Type");
        f.data         = body.substr(pos, data_len);
        fields.push_back(std::move(f));

        pos += data_len;
    }
    return fields;
}

// ---------------------------------------------------------------------------
// In-memory WAV writer (PCM 16-bit, little-endian)
// ---------------------------------------------------------------------------

static std::string samples_to_wav(const std::vector<float> & samples,
                                   int sample_rate) {
    // Convert float32 [-1, 1] to int16
    std::vector<int16_t> pcm;
    pcm.reserve(samples.size());
    for (float s : samples) {
        float c = s < -1.f ? -1.f : (s > 1.f ? 1.f : s);
        pcm.push_back(static_cast<int16_t>(c * 32767.f));
    }

    uint32_t data_size   = static_cast<uint32_t>(pcm.size() * sizeof(int16_t));
    uint32_t chunk_size  = 36 + data_size;
    uint16_t audio_fmt   = 1;  // PCM
    uint16_t num_chan    = 1;
    uint32_t byte_rate   = static_cast<uint32_t>(sample_rate * 2);
    uint16_t block_align = 2;
    uint16_t bits        = 16;

    std::string wav;
    wav.reserve(44 + data_size);

    auto write16 = [&](uint16_t v) {
        wav += static_cast<char>(v & 0xFF);
        wav += static_cast<char>((v >> 8) & 0xFF);
    };
    auto write32 = [&](uint32_t v) {
        wav += static_cast<char>(v & 0xFF);
        wav += static_cast<char>((v >> 8) & 0xFF);
        wav += static_cast<char>((v >> 16) & 0xFF);
        wav += static_cast<char>((v >> 24) & 0xFF);
    };

    // RIFF header
    wav += "RIFF";
    write32(chunk_size);
    wav += "WAVE";
    // fmt chunk
    wav += "fmt ";
    write32(16);
    write16(audio_fmt);
    write16(num_chan);
    write32(static_cast<uint32_t>(sample_rate));
    write32(byte_rate);
    write16(block_align);
    write16(bits);
    // data chunk
    wav += "data";
    write32(data_size);
    for (int16_t s : pcm) write16(static_cast<uint16_t>(s));

    return wav;
}

// ---------------------------------------------------------------------------
// Temporary WAV file helper (for voice cloning reference audio)
// ---------------------------------------------------------------------------

static std::string write_temp_wav(const std::string & data) {
#ifdef _WIN32
    char tmp_dir[MAX_PATH];
    if (!GetTempPathA(MAX_PATH, tmp_dir)) return "";
    char tmp_path[MAX_PATH];
    // GetTempFileNameA creates a unique, empty file and returns its path.
    // We write WAV data into it directly (no extension needed — load_audio_file
    // inspects the RIFF header, not the extension).
    if (!GetTempFileNameA(tmp_dir, "qw3", 0, tmp_path)) return "";
    FILE * f = fopen(tmp_path, "wb");
#else
    char tmp_path[] = "/tmp/qw3_ref_XXXXXX.wav";
    // mkstemps creates and opens the file atomically (no TOCTOU race).
    int fd_tmp = mkstemps(tmp_path, 4 /* len(".wav") */);
    FILE * f = (fd_tmp >= 0) ? fdopen(fd_tmp, "wb") : nullptr;
#endif
    if (!f) return "";
    fwrite(data.data(), 1, data.size(), f);
    fclose(f);
    return tmp_path;
}

// ---------------------------------------------------------------------------
// Language string → language_id
// ---------------------------------------------------------------------------

static int32_t lang_to_id(const std::string & lang) {
    if (lang == "en" || lang == "english")       return 2050;
    if (lang == "ru" || lang == "russian")        return 2069;
    if (lang == "zh" || lang == "chinese")        return 2055;
    if (lang == "ja" || lang == "japanese")       return 2058;
    if (lang == "ko" || lang == "korean")         return 2064;
    if (lang == "de" || lang == "german")         return 2053;
    if (lang == "fr" || lang == "french")         return 2061;
    if (lang == "es" || lang == "spanish")        return 2054;
    if (lang == "it" || lang == "italian")        return 2070;
    if (lang == "pt" || lang == "portuguese")     return 2071;
    return 2050; // default: English
}

// ---------------------------------------------------------------------------
// Usage
// ---------------------------------------------------------------------------

static void print_usage(const char * prog) {
    fprintf(stderr, "Usage: %s -m <model_dir> [options]\n\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -m, --model <dir>   Model directory (required)\n");
    fprintf(stderr, "  -p, --port <n>      HTTP port (default: 8080)\n");
    fprintf(stderr, "  -j, --threads <n>   Default compute threads (default: 4)\n");
    fprintf(stderr, "  -h, --help          Show this help\n");
    fprintf(stderr, "\nExample:\n");
    fprintf(stderr, "  %s -m ./models\n", prog);
    fprintf(stderr, "  Then open http://localhost:8080 in your browser.\n");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

// Core argument-parsing and server logic.
// argv is expected to be UTF-8 on all platforms.
static int server_run(int argc, char ** argv) {
    std::string model_dir;
    uint16_t    port       = 8080;
    int32_t     n_threads  = 4;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if ((arg == "-m" || arg == "--model") && i + 1 < argc) {
            model_dir = argv[++i];
        } else if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if ((arg == "-j" || arg == "--threads") && i + 1 < argc) {
            n_threads = std::stoi(argv[++i]);
        } else {
            fprintf(stderr, "Error: unknown argument: %s\n", arg.c_str());
            print_usage(argv[0]);
            return 1;
        }
    }

    if (model_dir.empty()) {
        fprintf(stderr, "Error: model directory is required (-m)\n");
        print_usage(argv[0]);
        return 1;
    }

    // Load TTS models
    qwen3_tts::Qwen3TTS tts;
    fprintf(stderr, "Loading models from: %s\n", model_dir.c_str());
    if (!tts.load_models(model_dir)) {
        fprintf(stderr, "Error loading models: %s\n", tts.get_error().c_str());
        return 1;
    }
    fprintf(stderr, "Models loaded successfully.\n");

    // Build HTTP server
    http_server::Server server;

    // GET / → serve the embedded HTML page
    server.handle("GET", "/", [](const http_server::Request &) {
        http_server::Response r;
        r.status       = 200;
        r.content_type = "text/html";
        r.body         = webui::INDEX_HTML;
        return r;
    });

    // POST /api/synthesize → multipart form → WAV audio
    server.handle("POST", "/api/synthesize",
        [&tts, n_threads](const http_server::Request & req) -> http_server::Response {

        // Extract multipart boundary
        auto ct_it = req.headers.find("content-type");
        if (ct_it == req.headers.end()) {
            return http_server::Response::error(400, "missing Content-Type");
        }
        std::string boundary = get_param(ct_it->second, "boundary");
        if (boundary.empty()) {
            return http_server::Response::error(400, "missing boundary in Content-Type");
        }

        // Parse form fields
        auto fields = parse_multipart(req.body, boundary);

        std::string text, language = "en", ref_wav_data;
        qwen3_tts::tts_params params;
        params.n_threads = n_threads;

        for (const auto & f : fields) {
            if (f.name == "text")        text          = f.data;
            else if (f.name == "language")    language  = f.data;
            else if (f.name == "temperature") params.temperature      = std::stof(f.data);
            else if (f.name == "top_k")       params.top_k            = std::stoi(f.data);
            else if (f.name == "max_tokens")  params.max_audio_tokens = std::stoi(f.data);
            else if (f.name == "rep_penalty") params.repetition_penalty = std::stof(f.data);
            else if (f.name == "threads")     params.n_threads        = std::stoi(f.data);
            else if (f.name == "ref_audio" && !f.filename.empty()) ref_wav_data = f.data;
        }

        if (text.empty()) {
            return http_server::Response::error(400, "text is required");
        }

        params.language_id   = lang_to_id(language);
        params.print_timing  = false;

        fprintf(stderr, "Synthesizing: \"%s\" (lang=%s)\n",
                text.c_str(), language.c_str());

        qwen3_tts::tts_result result;
        if (!ref_wav_data.empty()) {
            // Write reference audio to a temp file and use voice cloning
            std::string tmp_path = write_temp_wav(ref_wav_data);
            if (tmp_path.empty()) {
                return http_server::Response::error(500, "failed to create temp file");
            }
            result = tts.synthesize_with_voice(text, tmp_path, params);
            // Clean up temp file
            remove(tmp_path.c_str());
        } else {
            result = tts.synthesize(text, params);
        }

        if (!result.success) {
            return http_server::Response::error(500, result.error_msg);
        }

        // Encode to WAV bytes
        std::string wav = samples_to_wav(result.audio, result.sample_rate);
        float duration = static_cast<float>(result.audio.size()) /
                         static_cast<float>(result.sample_rate);

        http_server::Response resp;
        resp.status       = 200;
        resp.content_type = "audio/wav";
        resp.body         = wav;
        // Informational headers consumed by the JavaScript UI
        resp.headers["X-Audio-Duration"] = std::to_string(duration);
        resp.headers["X-Timing-Ms"]      = std::to_string(result.t_generate_ms);

        fprintf(stderr, "Done. Duration: %.2f s  |  Generate: %lld ms\n",
                duration, (long long)result.t_generate_ms);

        return resp;
    });

    // Start server (blocks)
    if (!server.listen(port)) {
        fprintf(stderr, "Failed to start server on port %u\n", (unsigned)port);
        return 1;
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Platform entry points — see main.cpp for rationale on wmain vs main.
// ---------------------------------------------------------------------------

#ifdef _WIN32

static std::vector<std::string> wargv_to_utf8(int argc, wchar_t ** wargv) {
    std::vector<std::string> out;
    out.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        int n = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1,
                                    nullptr, 0, nullptr, nullptr);
        if (n <= 0) { out.emplace_back(); continue; }
        std::string s(static_cast<size_t>(n), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1,
                            &s[0], n, nullptr, nullptr);
        if (!s.empty() && s.back() == '\0') s.pop_back();
        out.push_back(std::move(s));
    }
    return out;
}

int wmain(int argc, wchar_t ** wargv) {
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

    auto args = wargv_to_utf8(argc, wargv);
    std::vector<char *> argv_ptrs;
    argv_ptrs.reserve(args.size());
    for (auto & s : args) argv_ptrs.push_back(&s[0]);

    return server_run(static_cast<int>(argv_ptrs.size()), argv_ptrs.data());
}

#else // POSIX

int main(int argc, char ** argv) {
    return server_run(argc, argv);
}

#endif
