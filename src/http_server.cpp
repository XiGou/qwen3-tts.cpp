/*
 * http_server.cpp — minimal, dependency-free HTTP/1.1 server.
 *
 * Supports POSIX (Linux / macOS) and Windows (Winsock2).
 * Single-threaded: handles one request at a time (sufficient for a local
 * TTS WebUI where each inference is already long-running).
 */

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include "http_server.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
   using sock_t = SOCKET;
#  define SOCK_INVALID INVALID_SOCKET
#  define sock_close   closesocket
#  define sock_error   WSAGetLastError()
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
   using sock_t = int;
#  define SOCK_INVALID (-1)
#  define sock_close   close
#  define sock_error   errno
#endif

namespace http_server {

// ---------------------------------------------------------------------------
// Portable read / write wrappers
// ---------------------------------------------------------------------------

static int sock_read(sock_t fd, char * buf, int len) {
#ifdef _WIN32
    return recv(fd, buf, len, 0);
#else
    return static_cast<int>(read(fd, buf, static_cast<size_t>(len)));
#endif
}

static int sock_write(sock_t fd, const char * buf, int len) {
#ifdef _WIN32
    return send(fd, buf, len, 0);
#else
    return static_cast<int>(write(fd, buf, static_cast<size_t>(len)));
#endif
}

static void sock_write_str(sock_t fd, const std::string & s) {
    const char * p   = s.data();
    int          rem = static_cast<int>(s.size());
    while (rem > 0) {
        int n = sock_write(fd, p, rem);
        if (n <= 0) break;
        p   += n;
        rem -= n;
    }
}

// ---------------------------------------------------------------------------
// URL decode
// ---------------------------------------------------------------------------

std::string Server::url_decode(const std::string & s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            unsigned int v = 0;
            if (sscanf(s.c_str() + i + 1, "%2x", &v) == 1) {
                out += static_cast<char>(v);
                i += 2;
            } else {
                out += s[i];
            }
        } else if (s[i] == '+') {
            out += ' ';
        } else {
            out += s[i];
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Parse query string  "key=val&key2=val2"
// ---------------------------------------------------------------------------

static void parse_query(const std::string & qs,
                        std::unordered_map<std::string, std::string> & out) {
    std::istringstream ss(qs);
    std::string pair;
    while (std::getline(ss, pair, '&')) {
        auto eq = pair.find('=');
        if (eq == std::string::npos) {
            out[Server::url_decode(pair)] = "";
        } else {
            out[Server::url_decode(pair.substr(0, eq))] =
                Server::url_decode(pair.substr(eq + 1));
        }
    }
}

// ---------------------------------------------------------------------------
// Read an HTTP request from a connected socket
// ---------------------------------------------------------------------------

bool Server::parse_request(int raw_fd, Request & out) {
    sock_t fd = static_cast<sock_t>(raw_fd);

    // Read until we have the full header block (\r\n\r\n)
    std::string raw;
    raw.reserve(4096);
    char buf[4096];
    static const size_t MAX_HEADER_SIZE = 1024 * 1024; // 1 MiB
    while (true) {
        int n = sock_read(fd, buf, sizeof(buf));
        if (n <= 0) return false;
        raw.append(buf, static_cast<size_t>(n));
        if (raw.find("\r\n\r\n") != std::string::npos) break;
        if (raw.size() > MAX_HEADER_SIZE) return false; // safety limit
    }

    // Split header / body
    size_t header_end = raw.find("\r\n\r\n");
    std::string header_part = raw.substr(0, header_end);
    std::string body_part   = raw.substr(header_end + 4);

    // Parse request line
    std::istringstream hss(header_part);
    std::string request_line;
    if (!std::getline(hss, request_line)) return false;
    if (!request_line.empty() && request_line.back() == '\r')
        request_line.pop_back();

    std::istringstream rls(request_line);
    std::string method, full_path, version;
    rls >> method >> full_path >> version;
    out.method = method;

    // Split path and query string
    auto qpos = full_path.find('?');
    if (qpos == std::string::npos) {
        out.path = full_path;
    } else {
        out.path = full_path.substr(0, qpos);
        parse_query(full_path.substr(qpos + 1), out.query);
    }

    // Parse headers
    int content_length = 0;
    std::string line;
    while (std::getline(hss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key   = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        // Trim leading space from value
        while (!value.empty() && value.front() == ' ') value.erase(value.begin());
        // Lowercase key for case-insensitive lookup
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        out.headers[key] = value;
        if (key == "content-length") {
            content_length = std::stoi(value);
        }
    }

    // Read remaining body bytes if Content-Length says there are more
    while ((int)body_part.size() < content_length) {
        int need = content_length - static_cast<int>(body_part.size());
        int n = sock_read(fd, buf, std::min(need, (int)sizeof(buf)));
        if (n <= 0) break;
        body_part.append(buf, static_cast<size_t>(n));
    }
    out.body = body_part;

    return true;
}

// ---------------------------------------------------------------------------
// Send an HTTP response over the socket
// ---------------------------------------------------------------------------

void Server::send_response(int raw_fd, const Response & resp) {
    sock_t fd = static_cast<sock_t>(raw_fd);

    std::string status_text = "OK";
    switch (resp.status) {
        case 200: status_text = "OK";                    break;
        case 400: status_text = "Bad Request";           break;
        case 404: status_text = "Not Found";             break;
        case 405: status_text = "Method Not Allowed";    break;
        case 500: status_text = "Internal Server Error"; break;
        default:  status_text = "Unknown";               break;
    }

    std::ostringstream oss;
    oss << "HTTP/1.1 " << resp.status << " " << status_text << "\r\n";
    oss << "Content-Type: "   << resp.content_type << "; charset=utf-8\r\n";
    oss << "Content-Length: " << resp.body.size()  << "\r\n";
    oss << "Access-Control-Allow-Origin: *\r\n";
    oss << "Connection: close\r\n";
    // Write any additional headers provided by the handler
    for (const auto & kv : resp.headers) {
        oss << kv.first << ": " << kv.second << "\r\n";
    }
    oss << "\r\n";

    sock_write_str(fd, oss.str());
    sock_write_str(fd, resp.body);
}

// ---------------------------------------------------------------------------
// Server constructor / destructor
// ---------------------------------------------------------------------------

Server::Server() {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
}

Server::~Server() {
    stop();
#ifdef _WIN32
    WSACleanup();
#endif
}

// ---------------------------------------------------------------------------
// Route registration
// ---------------------------------------------------------------------------

void Server::handle(const std::string & path, Handler handler) {
    routes_[path].push_back({"", std::move(handler)});
}

void Server::handle(const std::string & method, const std::string & path,
                    Handler handler) {
    routes_[path].push_back({method, std::move(handler)});
}

// ---------------------------------------------------------------------------
// Dispatch a parsed request to the appropriate handler
// ---------------------------------------------------------------------------

Response Server::dispatch(const Request & req) {
    auto it = routes_.find(req.path);
    if (it == routes_.end()) {
        return Response::error(404, "not found");
    }
    for (const auto & route : it->second) {
        if (route.method.empty() || route.method == req.method) {
            return route.handler(req);
        }
    }
    return Response::error(405, "method not allowed");
}

// ---------------------------------------------------------------------------
// Handle a single client connection
// ---------------------------------------------------------------------------

void Server::handle_client(int fd) {
    Request req;
    if (!parse_request(fd, req)) {
        sock_close(static_cast<sock_t>(fd));
        return;
    }
    Response resp = dispatch(req);
    send_response(fd, resp);
    sock_close(static_cast<sock_t>(fd));
}

// ---------------------------------------------------------------------------
// Main listen / accept loop
// ---------------------------------------------------------------------------

bool Server::listen(uint16_t port) {
    sock_t sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd == SOCK_INVALID) {
        fprintf(stderr, "http_server: socket() failed (%d)\n", sock_error);
        return false;
    }

    int opt = 1;
#ifdef _WIN32
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char *>(&opt), sizeof(opt));
#else
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sfd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        fprintf(stderr, "http_server: bind() failed on port %u (%d)\n",
                (unsigned)port, sock_error);
        sock_close(sfd);
        return false;
    }

    if (::listen(sfd, 16) < 0) {
        fprintf(stderr, "http_server: listen() failed (%d)\n", sock_error);
        sock_close(sfd);
        return false;
    }

    server_fd_ = static_cast<int>(sfd);
    running_   = true;

    fprintf(stderr, "WebUI server listening on http://127.0.0.1:%u\n", (unsigned)port);

    while (running_) {
        sockaddr_in client_addr{};
#ifdef _WIN32
        int client_len = sizeof(client_addr);
#else
        socklen_t client_len = sizeof(client_addr);
#endif
        sock_t client_fd = accept(sfd,
                                  reinterpret_cast<sockaddr *>(&client_addr),
                                  &client_len);
        if (client_fd == SOCK_INVALID) {
            if (!running_) break;
            continue;
        }
        handle_client(static_cast<int>(client_fd));
    }

    sock_close(sfd);
    server_fd_ = -1;
    return true;
}

void Server::stop() {
    running_ = false;
    if (server_fd_ != -1) {
        sock_close(static_cast<sock_t>(server_fd_));
        server_fd_ = -1;
    }
}

} // namespace http_server
