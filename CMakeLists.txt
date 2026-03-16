cmake_minimum_required(VERSION 3.14)
project(qwen3-tts-ggml VERSION 0.1.0 LANGUAGES CXX)

# C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -march=native")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
endif()

# ---------------------------------------------------------------------------
# Project-level options
# ---------------------------------------------------------------------------
option(QWEN3_TTS_TIMING  "Enable detailed timing instrumentation"              OFF)
option(QWEN3_TTS_COREML  "Enable CoreML code predictor bridge on macOS"        ON)
option(QWEN3_TTS_BUILD_GGML "Build vendored GGML as part of this project"      ON)
option(QWEN3_TTS_VULKAN  "Enable Vulkan GPU acceleration in GGML"              OFF)
option(QWEN3_TTS_WEBUI   "Build the HTTP WebUI server (qwen3-tts-server)"      ON)

if(QWEN3_TTS_TIMING)
    add_compile_definitions(QWEN3_TTS_TIMING)
endif()

# CoreML code predictor bridge (macOS only)
if(APPLE AND QWEN3_TTS_COREML)
    enable_language(OBJCXX)
endif()

# Find threads
find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# GGML: build from source (add_subdirectory) or link pre-built libraries
# ---------------------------------------------------------------------------
set(GGML_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ggml")

if(QWEN3_TTS_BUILD_GGML AND EXISTS "${GGML_DIR}/CMakeLists.txt")
    # ---- Build GGML as a CMake sub-project ----
    message(STATUS "Building vendored GGML from source")

    # Disable everything we don't need from ggml's own build
    set(GGML_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GGML_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS   OFF CACHE BOOL "" FORCE)

    # GPU backend selection
    if(QWEN3_TTS_VULKAN)
        set(GGML_VULKAN ON CACHE BOOL "" FORCE)
        message(STATUS "GGML Vulkan backend enabled")
    endif()
    # Metal is auto-detected by GGML on macOS; no extra flag needed.

    add_subdirectory(${GGML_DIR} ggml_build)

    # Collect the GGML link targets produced by the subdirectory
    set(GGML_LINK_TARGETS ggml ggml-base ggml-cpu)
    if(QWEN3_TTS_VULKAN)
        list(APPEND GGML_LINK_TARGETS ggml-vulkan)
    endif()
    if(APPLE)
        # ggml auto-enables Metal on macOS when available
        if(TARGET ggml-metal)
            list(APPEND GGML_LINK_TARGETS ggml-metal)
        endif()
    endif()

    # Include dirs come via the ggml targets themselves; also expose the
    # top-level ggml/include for direct #include <ggml.h> use.
    set(GGML_INCLUDE_DIRS "${GGML_DIR}/include")
    set(GGML_LINK_DIRS "")          # not needed — targets carry their paths

    set(GGML_BUILT_FROM_SOURCE ON)
else()
    # ---- Link against pre-built GGML libraries (legacy / Windows fallback) ----
    if(NOT EXISTS "${GGML_DIR}/CMakeLists.txt")
        message(STATUS "GGML submodule not initialised — using pre-built libraries in ggml/build")
        message(STATUS "  Run: git submodule update --init --recursive  to enable source build")
    else()
        message(STATUS "QWEN3_TTS_BUILD_GGML=OFF — using pre-built GGML libraries")
    endif()

    set(GGML_BUILD_DIR "${GGML_DIR}/build")
    set(GGML_INCLUDE_DIRS "${GGML_DIR}/include")

    # Detect available pre-built backends
    set(GGML_LINK_DIRS
        "${GGML_BUILD_DIR}/src"
        "${GGML_BUILD_DIR}/src/Release"
        "${GGML_BUILD_DIR}/src/Debug"
        "${GGML_BUILD_DIR}/src/ggml-metal"
    )
    set(GGML_LINK_TARGETS ggml ggml-base ggml-cpu)

    if(APPLE AND EXISTS "${GGML_BUILD_DIR}/src/ggml-metal/libggml-metal.dylib")
        list(APPEND GGML_LINK_TARGETS ggml-metal)
        set(GGML_HAS_METAL ON)
        message(STATUS "GGML Metal backend found — GPU acceleration enabled")
    else()
        set(GGML_HAS_METAL OFF)
    endif()

    if(QWEN3_TTS_VULKAN)
        # Check for pre-built Vulkan backend library
        if(EXISTS "${GGML_BUILD_DIR}/src/ggml-vulkan")
            list(APPEND GGML_LINK_DIRS "${GGML_BUILD_DIR}/src/ggml-vulkan")
            list(APPEND GGML_LINK_TARGETS ggml-vulkan)
            message(STATUS "GGML Vulkan backend (pre-built) enabled")
        else()
            message(WARNING "QWEN3_TTS_VULKAN=ON but pre-built ggml-vulkan not found; "
                            "set QWEN3_TTS_BUILD_GGML=ON or build GGML with GGML_VULKAN=ON first")
        endif()
    endif()

    set(GGML_BUILT_FROM_SOURCE OFF)
endif()

# ---------------------------------------------------------------------------
# Helper macro: apply GGML include / link settings to a target
# ---------------------------------------------------------------------------
macro(target_link_ggml TARGET_NAME)
    target_include_directories(${TARGET_NAME} PUBLIC ${GGML_INCLUDE_DIRS})
    if(GGML_LINK_DIRS)
        target_link_directories(${TARGET_NAME} PUBLIC ${GGML_LINK_DIRS})
    endif()
    target_link_libraries(${TARGET_NAME} PUBLIC ${GGML_LINK_TARGETS} Threads::Threads)
    if(APPLE AND GGML_HAS_METAL)
        target_link_libraries(${TARGET_NAME} PUBLIC "-framework Metal" "-framework MetalKit")
    endif()
endmacro()

# ---------------------------------------------------------------------------
# Component libraries
# ---------------------------------------------------------------------------

# Text tokenizer
add_library(text_tokenizer STATIC
    src/text_tokenizer.cpp
    src/gguf_loader.cpp
)
target_include_directories(text_tokenizer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_ggml(text_tokenizer)

# TTS Transformer (+ optional CoreML bridge)
set(TTS_TRANSFORMER_SOURCES
    src/gguf_loader.cpp
    src/tts_transformer.cpp
)
if(APPLE AND QWEN3_TTS_COREML)
    list(APPEND TTS_TRANSFORMER_SOURCES src/coreml_code_predictor.mm)
    set_source_files_properties(src/coreml_code_predictor.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
else()
    list(APPEND TTS_TRANSFORMER_SOURCES src/coreml_code_predictor_stub.cpp)
endif()

add_library(tts_transformer STATIC ${TTS_TRANSFORMER_SOURCES})
target_include_directories(tts_transformer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_ggml(tts_transformer)
if(APPLE AND QWEN3_TTS_COREML)
    target_link_libraries(tts_transformer PUBLIC "-framework Foundation" "-framework CoreML")
endif()

# Audio tokenizer encoder
add_library(audio_tokenizer_encoder STATIC
    src/audio_tokenizer_encoder.cpp
    src/gguf_loader.cpp
)
target_include_directories(audio_tokenizer_encoder PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_ggml(audio_tokenizer_encoder)

# Audio tokenizer decoder
add_library(audio_tokenizer_decoder STATIC
    src/audio_tokenizer_decoder.cpp
    src/gguf_loader.cpp
)
target_include_directories(audio_tokenizer_decoder PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_ggml(audio_tokenizer_decoder)

# Full pipeline library
add_library(qwen3_tts STATIC
    src/qwen3_tts.cpp
)
target_include_directories(qwen3_tts PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_ggml(qwen3_tts)
target_link_libraries(qwen3_tts PUBLIC
    text_tokenizer
    tts_transformer
    audio_tokenizer_encoder
    audio_tokenizer_decoder
)

# ---------------------------------------------------------------------------
# Shared library with C API (for FFI)
# ---------------------------------------------------------------------------
add_library(qwen3tts_shared SHARED
    src/qwen3tts_c_api.cpp
)
set_target_properties(qwen3tts_shared PROPERTIES
    OUTPUT_NAME "qwen3tts"
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
)
target_include_directories(qwen3tts_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(qwen3tts_shared PRIVATE qwen3_tts)

# ---------------------------------------------------------------------------
# CLI executable
# ---------------------------------------------------------------------------
add_executable(qwen3-tts-cli src/main.cpp)
target_link_libraries(qwen3-tts-cli PRIVATE qwen3_tts)

# ---------------------------------------------------------------------------
# WebUI server executable
# ---------------------------------------------------------------------------
if(QWEN3_TTS_WEBUI)
    add_executable(qwen3-tts-server
        src/main_server.cpp
        src/http_server.cpp
    )
    target_include_directories(qwen3-tts-server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(qwen3-tts-server PRIVATE qwen3_tts)
    if(WIN32)
        target_link_libraries(qwen3-tts-server PRIVATE ws2_32)
    endif()
endif()

# ---------------------------------------------------------------------------
# Test executables
# ---------------------------------------------------------------------------
add_executable(test_tokenizer   tests/test_tokenizer.cpp)
target_link_libraries(test_tokenizer   PRIVATE text_tokenizer   Threads::Threads)

add_executable(test_encoder     tests/test_encoder.cpp)
target_link_libraries(test_encoder     PRIVATE audio_tokenizer_encoder Threads::Threads)

add_executable(test_transformer tests/test_transformer.cpp)
target_link_libraries(test_transformer PRIVATE tts_transformer   Threads::Threads)

add_executable(test_decoder     tests/test_decoder.cpp)
target_link_libraries(test_decoder     PRIVATE audio_tokenizer_decoder Threads::Threads)

# ---------------------------------------------------------------------------
# Install targets
# ---------------------------------------------------------------------------
set(INSTALL_TARGETS
    text_tokenizer tts_transformer audio_tokenizer_encoder audio_tokenizer_decoder
    qwen3_tts qwen3tts_shared qwen3-tts-cli
)
if(QWEN3_TTS_WEBUI)
    list(APPEND INSTALL_TARGETS qwen3-tts-server)
endif()

install(TARGETS ${INSTALL_TARGETS}
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
install(FILES
    src/gguf_loader.h
    src/text_tokenizer.h
    src/tts_transformer.h
    src/coreml_code_predictor.h
    src/audio_tokenizer_encoder.h
    src/audio_tokenizer_decoder.h
    src/qwen3_tts.h
    src/qwen3tts_c_api.h
    DESTINATION include
)

# ---------------------------------------------------------------------------
# CTest support
# ---------------------------------------------------------------------------
enable_testing()
add_test(NAME tokenizer_test
    COMMAND test_tokenizer
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME encoder_test
    COMMAND test_encoder --tokenizer models/qwen3-tts-0.6b-f16.gguf --audio clone.wav --reference reference/ref_audio_embedding.bin
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME transformer_test
    COMMAND test_transformer
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME decoder_test
    COMMAND test_decoder
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
