-- xmake.lua for qwen3-tts.cpp
-- Prerequisites (mirrors AGENTS.md — build GGML once before xmake):
--
--   CPU-only (default):
--     cmake -S ggml -B ggml/build -DGGML_BUILD_TESTS=OFF -DGGML_BUILD_EXAMPLES=OFF -DBUILD_SHARED_LIBS=OFF
--     cmake --build ggml/build --config Release --target ggml ggml-base ggml-cpu -j4
--
--   Vulkan (GPU) build:
--     cmake -S ggml -B ggml/build -DGGML_BUILD_TESTS=OFF -DGGML_BUILD_EXAMPLES=OFF -DBUILD_SHARED_LIBS=OFF -DGGML_VULKAN=ON
--     cmake --build ggml/build --config Release --target ggml ggml-base ggml-cpu ggml-vulkan -j4
--     xmake f --vulkan=y && xmake
--
-- Note: with CMake (recommended), set -DQWEN3_TTS_BUILD_GGML=ON to have
--       CMake build GGML automatically; -DQWEN3_TTS_VULKAN=ON for Vulkan.

set_project("qwen3-tts-ggml")
set_version("0.1.0")
set_languages("c++17")

-- ---------------------------------------------------------------------------
-- Global compile fixes
-- ---------------------------------------------------------------------------
if is_plat("windows") then
    add_defines("_USE_MATH_DEFINES", "_CRT_SECURE_NO_WARNINGS")
end

-- ---------------------------------------------------------------------------
-- Options
-- ---------------------------------------------------------------------------
option("timing", {description = "Enable detailed timing instrumentation", default = false})
option("coreml", {description = "Enable CoreML code predictor bridge (macOS only)", default = true})
option("vulkan", {description = "Enable Vulkan GPU acceleration (requires GGML built with GGML_VULKAN=ON)", default = false})
option("webui",  {description = "Build the HTTP WebUI server (qwen3-tts-server)", default = true})

-- ---------------------------------------------------------------------------
-- GGML paths
-- ---------------------------------------------------------------------------
local GGML_DIR   = path.join(os.scriptdir(), "ggml")
local GGML_INC   = path.join(GGML_DIR, "include")
local GGML_BUILD = path.join(GGML_DIR, "build")

-- MSVC (Visual Studio generator) puts .lib under build/src/<Config>/
-- Ninja / Makefiles put them directly under build/src/
local GGML_LIB_DIRS_ALL = {
    path.join(GGML_BUILD, "src"),
    path.join(GGML_BUILD, "src", "Release"),
    path.join(GGML_BUILD, "src", "Debug"),
    path.join(GGML_BUILD, "src", "ggml-cpu"),
    path.join(GGML_BUILD, "src", "ggml-cpu", "Release"),
    path.join(GGML_BUILD, "src", "ggml-cpu", "Debug"),
}
-- Only keep dirs that actually exist (avoids xmake "linkdir not found" warnings)
local GGML_LIB_DIRS = {}
for _, d in ipairs(GGML_LIB_DIRS_ALL) do
    if os.isdir(d) then
        table.insert(GGML_LIB_DIRS, d)
    end
end

-- macOS Metal detection
local has_metal = is_plat("macosx") and
    os.isfile(path.join(GGML_BUILD, "src", "ggml-metal", "libggml-metal.dylib"))

-- Vulkan: check for pre-built backend library
local has_vulkan_lib = get_config("vulkan") and
    (os.isdir(path.join(GGML_BUILD, "src", "ggml-vulkan")) or
     os.isfile(path.join(GGML_BUILD, "src", "libggml-vulkan.a")) or
     os.isfile(path.join(GGML_BUILD, "src", "ggml-vulkan.lib")))

if get_config("vulkan") then
    if has_vulkan_lib then
        table.insert(GGML_LIB_DIRS_ALL, path.join(GGML_BUILD, "src", "ggml-vulkan"))
        table.insert(GGML_LIB_DIRS_ALL, path.join(GGML_BUILD, "src", "ggml-vulkan", "Release"))
    else
        print("WARNING: vulkan=y but pre-built ggml-vulkan not found in ggml/build — "
              .. "rebuild GGML with -DGGML_VULKAN=ON first")
    end
end

-- ---------------------------------------------------------------------------
-- Helper: apply GGML settings + project-wide flags to current target.
-- Uses {public=true} so that consumers of this static lib (via add_deps)
-- automatically inherit the include paths and link dirs.
-- ---------------------------------------------------------------------------
local function apply_common()
    -- PUBLIC: propagated to any target that add_deps() this one
    add_includedirs(GGML_INC, "src", {public = true})
    add_linkdirs(table.unpack(GGML_LIB_DIRS))
    add_links("ggml", "ggml-base", "ggml-cpu")
    if get_config("vulkan") and has_vulkan_lib then
        add_links("ggml-vulkan")
    end
    if is_plat("windows") then
        add_syslinks("advapi32", "user32", "kernel32")
    elseif is_plat("macosx") then
        add_syslinks("pthread", "dl", "m")
        add_frameworks("Accelerate")
        if has_metal then
            add_linkdirs(path.join(GGML_BUILD, "src", "ggml-metal"))
            add_links("ggml-metal")
            add_frameworks("Metal", "MetalKit")
        end
    else
        add_syslinks("pthread", "dl", "m")
        if get_config("vulkan") and has_vulkan_lib then
            -- Vulkan loader required on Linux
            add_syslinks("vulkan")
        end
    end
    if get_config("timing") then
        add_defines("QWEN3_TTS_TIMING")
    end
end

-- ---------------------------------------------------------------------------
-- text_tokenizer
-- ---------------------------------------------------------------------------
target("text_tokenizer")
    set_kind("static")
    add_files("src/text_tokenizer.cpp", "src/gguf_loader.cpp")
    apply_common()
target_end()

-- ---------------------------------------------------------------------------
-- tts_transformer  (+ optional CoreML bridge on macOS)
-- ---------------------------------------------------------------------------
target("tts_transformer")
    set_kind("static")
    add_files("src/tts_transformer.cpp", "src/gguf_loader.cpp")
    apply_common()
    if is_plat("macosx") and get_config("coreml") then
        add_files("src/coreml_code_predictor.mm")
        add_mxflags("-fobjc-arc")
        add_frameworks("Foundation", "CoreML")
    else
        add_files("src/coreml_code_predictor_stub.cpp")
    end
target_end()

-- ---------------------------------------------------------------------------
-- audio_tokenizer_encoder
-- ---------------------------------------------------------------------------
target("audio_tokenizer_encoder")
    set_kind("static")
    add_files("src/audio_tokenizer_encoder.cpp", "src/gguf_loader.cpp")
    apply_common()
target_end()

-- ---------------------------------------------------------------------------
-- audio_tokenizer_decoder
-- ---------------------------------------------------------------------------
target("audio_tokenizer_decoder")
    set_kind("static")
    add_files("src/audio_tokenizer_decoder.cpp", "src/gguf_loader.cpp")
    apply_common()
target_end()

-- ---------------------------------------------------------------------------
-- qwen3_tts  (full pipeline, aggregates all component libs)
-- ---------------------------------------------------------------------------
target("qwen3_tts")
    set_kind("static")
    add_files("src/qwen3_tts.cpp")
    apply_common()
    add_deps("text_tokenizer", "tts_transformer",
             "audio_tokenizer_encoder", "audio_tokenizer_decoder",
             {public = true})
target_end()

-- ---------------------------------------------------------------------------
-- qwen3tts_shared  (C API shared lib — for Nim FFI etc.)
-- ---------------------------------------------------------------------------
target("qwen3tts_shared")
    set_kind("shared")
    set_basename("qwen3tts")
    add_files("src/qwen3tts_c_api.cpp")
    add_deps("qwen3_tts")
target_end()

-- ---------------------------------------------------------------------------
-- CLI executable
-- ---------------------------------------------------------------------------
target("qwen3-tts-cli")
    set_kind("binary")
    add_files("src/main.cpp")
    add_deps("qwen3_tts")
target_end()

-- ---------------------------------------------------------------------------
-- WebUI server executable (HTTP server + embedded static page)
-- ---------------------------------------------------------------------------
if get_config("webui") then
    target("qwen3-tts-server")
        set_kind("binary")
        add_files("src/main_server.cpp", "src/http_server.cpp")
        add_deps("qwen3_tts")
        add_includedirs("src", {public = true})
        if is_plat("windows") then
            add_syslinks("ws2_32")
        end
    target_end()
end

-- ---------------------------------------------------------------------------
-- Test executables
-- ---------------------------------------------------------------------------
local tests = {
    {"test_tokenizer",   "tests/test_tokenizer.cpp",   "text_tokenizer"},
    {"test_encoder",     "tests/test_encoder.cpp",     "audio_tokenizer_encoder"},
    {"test_transformer", "tests/test_transformer.cpp", "tts_transformer"},
    {"test_decoder",     "tests/test_decoder.cpp",     "audio_tokenizer_decoder"},
    {"test_codebook",    "tests/test_codebook.cpp",    "audio_tokenizer_decoder"},
    {"test_vq_only",     "tests/test_vq_only.cpp",     "audio_tokenizer_decoder"},
}

for _, info in ipairs(tests) do
    local tname, src, dep = info[1], info[2], info[3]
    target(tname)
        set_kind("binary")
        add_files(src)
        add_deps(dep)
        set_group("tests")
    target_end()
end
