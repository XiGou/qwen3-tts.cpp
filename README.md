# Windows Build Manual

This guide covers building and running `qwen3-tts.cpp` on Windows using MSVC (Visual Studio 2022).

## Prerequisites

| Tool | Version | Notes |
|------|---------|-------|
| Visual Studio 2022 | Community or better | C++ Desktop workload required |
| CMake | 3.14+ | Add to `PATH` during install |
| Python | 3.10+ | For model conversion only |
| [uv](https://github.com/astral-sh/uv) | latest | Python package manager |
| Git | any | With Git for Windows |

> [!IMPORTANT]
> All commands below are for **PowerShell** (not `cmd`). Open a **Developer PowerShell for VS 2022** so that `cl.exe` and `cmake` are on your `PATH`.

---

## 1. Clone the Repository

```powershell
git clone https://github.com/predict-woo/qwen3-tts.cpp.git
cd qwen3-tts.cpp
git submodule update --init --recursive
```

---

## 2. Build Options

### 2a. Automatic GGML Build (recommended — one-step)

With `QWEN3_TTS_BUILD_GGML=ON` (the default when the submodule is present),
CMake will build GGML automatically as part of the main project.

**CPU-only build (default)**

```powershell
cmake -S . -B build
cmake --build build --config Release -j4
```

**Vulkan GPU build** (requires Vulkan SDK installed)

```powershell
cmake -S . -B build -DQWEN3_TTS_VULKAN=ON
cmake --build build --config Release -j4
```

> [!NOTE]
> Install the [Vulkan SDK](https://vulkan.lunarg.com/) first. The `VULKAN_SDK`
> environment variable must be set (the installer does this automatically).

### 2b. Manual GGML Build (legacy / advanced)

Build GGML separately first, then build the main project with
`-DQWEN3_TTS_BUILD_GGML=OFF`.

**CPU-only:**

```powershell
cmake -S ggml -B ggml/build -DGGML_BUILD_TESTS=OFF -DGGML_BUILD_EXAMPLES=OFF -DBUILD_SHARED_LIBS=OFF
cmake --build ggml/build --config Release --target ggml ggml-base ggml-cpu -j4
cmake -S . -B build -DQWEN3_TTS_BUILD_GGML=OFF
cmake --build build --config Release -j4
```

**Vulkan GPU:**

```powershell
cmake -S ggml -B ggml/build -DGGML_BUILD_TESTS=OFF -DGGML_BUILD_EXAMPLES=OFF -DBUILD_SHARED_LIBS=OFF -DGGML_VULKAN=ON
cmake --build ggml/build --config Release --target ggml ggml-base ggml-cpu ggml-vulkan -j4
cmake -S . -B build -DQWEN3_TTS_BUILD_GGML=OFF -DQWEN3_TTS_VULKAN=ON
cmake --build build --config Release -j4
```

Expected outputs under `ggml\build\src\Release\`:

```
ggml.lib
ggml-base.lib
ggml-cpu.lib
ggml-vulkan.lib   ← only when GGML_VULKAN=ON
```

---

## 3a. Build with CMake (traditional)

```powershell
cmake -S . -B build
cmake --build build --config Release -j4
```

Binaries will be in `build\Release\`:

```
qwen3-tts-cli.exe
qwen3-tts-server.exe    ← HTTP WebUI server
qwen3tts.dll
test_tokenizer.exe  test_encoder.exe  test_transformer.exe  test_decoder.exe
```

### Optional CMake flags

```powershell
# Enable timing instrumentation
cmake -S . -B build -DQWEN3_TTS_TIMING=ON

# Vulkan GPU acceleration
cmake -S . -B build -DQWEN3_TTS_VULKAN=ON

# Disable WebUI server
cmake -S . -B build -DQWEN3_TTS_WEBUI=OFF

cmake --build build --config Release -j4
```

---

## 3b. Build with xmake (alternative)

[xmake](https://xmake.io) is also supported. Install it first:

```powershell
winget install xmake
# or: Invoke-Expression (Invoke-Webrequest 'https://xmake.io/psget.text' -UseBasicParsing).Content
```

> [!NOTE]
> xmake uses **pre-built GGML** (build step 2b above first).

```powershell
xmake
```

Binaries will be in `build\windows\x64\release\`.

Optional flags:

```powershell
xmake f --timing=y   # Enable timing instrumentation
xmake f --vulkan=y   # Enable Vulkan GPU acceleration
xmake f --webui=n    # Disable WebUI server
xmake               # Rebuild
```

---

## 4. Python Environment Setup

Required only for model download and conversion, not for inference.

```powershell
uv venv .venv
.venv\Scripts\activate

uv pip install --upgrade pip
uv pip install huggingface_hub gguf torch safetensors numpy tqdm
```

> [!NOTE]
> `coremltools` is macOS-only and can be skipped on Windows.

---

## 5. Download and Convert Models

### Automated (recommended)

```powershell
python scripts/setup_pipeline_models.py
```

This downloads the HuggingFace model and converts it to GGUF. Pass `--force` to re-run from scratch.

### Manual

```powershell
# Download the HuggingFace model
huggingface-cli download Qwen/Qwen3-TTS-12Hz-0.6B-Base `
    --local-dir models/Qwen3-TTS-12Hz-0.6B-Base

# Convert TTS model (transformer + encoder + tokenizer)
python scripts/convert_tts_to_gguf.py `
    models/Qwen3-TTS-12Hz-0.6B-Base `
    models/qwen3-tts-0.6b-f16.gguf

# Convert vocoder (audio decoder)
python scripts/convert_tokenizer_to_gguf.py `
    models/Qwen3-TTS-12Hz-0.6B-Base `
    models/qwen3-tts-tokenizer-f16.gguf
```

Expected model files:

```
models\
  qwen3-tts-0.6b-f16.gguf       (~1.2 GB)
  qwen3-tts-tokenizer-f16.gguf  (~660 MB)
```

---

## 6. Run Inference

### CLI

```powershell
# Basic synthesis (English)
.\build\Release\qwen3-tts-cli.exe -m models -t "Hello from Windows." -o hello.wav

# Chinese synthesis — always pass -l zh for Chinese input
.\build\Release\qwen3-tts-cli.exe -m models -t "你好，世界！" -l zh -o chinese.wav

# xmake build
.\build\windows\x64\release\qwen3-tts-cli.exe -m models -t "Hello from Windows." -o hello.wav
```

### Voice cloning

```powershell
.\build\Release\qwen3-tts-cli.exe `
    -m models `
    -r examples\readme_clone_input.wav `
    -t "This is a voice cloning example on Windows." `
    -o cloned.wav
```

### WebUI server

```powershell
.\build\Release\qwen3-tts-server.exe -m models -p 8080
```

Then open **http://localhost:8080** in your browser. The page supports text
input in any language including Chinese, voice cloning, and audio playback.

### CLI options

| Flag | Description | Default |
|------|-------------|---------|
| `-m, --model <dir>` | Model directory containing GGUF files | (required) |
| `-t, --text <text>` | Text to synthesize | (required) |
| `-o, --output <file>` | Output WAV file path | `output.wav` |
| `-r, --reference <file>` | Reference audio for voice cloning | (none) |
| `-l, --language <lang>` | Language code: `en`,`zh`,`ja`,`ko`,`de`,`fr`,`es`,`ru`,`it`,`pt` | `en` |
| `--temperature <val>` | Sampling temperature (0 = greedy) | 0.9 |
| `--top-k <n>` | Top-k sampling (0 = disabled) | 50 |
| `--max-tokens <n>` | Maximum audio frames to generate | 4096 |
| `--repetition-penalty <val>` | Repetition penalty on codebook-0 | 1.05 |
| `-j, --threads <n>` | Number of compute threads | 4 |

> [!IMPORTANT]
> **Chinese text**: Always specify `-l zh` when synthesizing Chinese (`-l en`
> is the default). On Windows the CLI now uses the Windows wide-char API to
> parse arguments so Chinese characters in `-t` are handled correctly.

### WebUI server options (`qwen3-tts-server`)

| Flag | Description | Default |
|------|-------------|---------|
| `-m, --model <dir>` | Model directory | (required) |
| `-p, --port <n>` | HTTP listen port | 8080 |
| `-j, --threads <n>` | Default compute threads | 4 |

### Environment variable flags

| Variable | Effect |
|----------|--------|
| `QWEN3_TTS_LOW_MEM=1` | Lazy-load vocoder; unload transformer after generation (lower peak RAM) |

> [!NOTE]
> `QWEN3_TTS_USE_COREML` has no effect on Windows — CoreML is macOS-only.

---

## 7. Running Tests

```powershell
# CMake build
.\build\Release\test_tokenizer.exe --model models\qwen3-tts-0.6b-f16.gguf

.\build\Release\test_encoder.exe `
    --tokenizer models\qwen3-tts-0.6b-f16.gguf `
    --audio examples\readme_clone_input.wav `
    --reference reference\ref_audio_embedding.bin

.\build\Release\test_transformer.exe --ref-dir reference\

.\build\Release\test_decoder.exe `
    --tokenizer models\qwen3-tts-tokenizer-f16.gguf `
    --codes reference\speech_codes.bin `
    --reference reference\decoded_audio.bin
```

---

## Troubleshooting

### `M_PI` undeclared identifier
Add `/D_USE_MATH_DEFINES` to your compile flags, or ensure you are using the provided `xmake.lua` / `CMakeLists.txt` which adds this automatically on Windows.

### `LNK1181: cannot open input file 'ggml-base.lib'`
GGML was not built before the main project. Complete step 2 first and verify that `ggml\build\src\Release\*.lib` exist.

### `sys/resource.h: No such file or directory`
This POSIX header does not exist on Windows. It is already guarded in `src/qwen3_tts.cpp` with `#elif !defined(_WIN32)`. If you see this, ensure you are using the latest version of the source.

### Linker errors about `__imp_*` symbols from GGML
Make sure you built GGML with `-DBUILD_SHARED_LIBS=OFF`. Mixing static and dynamic GGML builds causes this.

### Slow generation
Windows has no Metal/GPU backend by default. All computation runs on CPU. Use `-j` to increase threads and consider the Q8_0 quantised model for lower memory bandwidth:

```powershell
# Use Q8_0 model (faster on CPU than F16)
python scripts/convert_tts_to_gguf.py `
    models/Qwen3-TTS-12Hz-0.6B-Base `
    models/qwen3-tts-0.6b-q8_0.gguf `
    --quantize q8_0
```

The CLI auto-selects `q8_0` over `f16` when both are present in the model directory.
