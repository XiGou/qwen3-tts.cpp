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

## 2. Build Vendored GGML

GGML must be built before the main project. On Windows there is no Metal, so omit `-DGGML_METAL=ON`.

```powershell
# Configure
cmake -S ggml -B ggml/build -DGGML_BUILD_TESTS=OFF -DGGML_BUILD_EXAMPLES=OFF -DBUILD_SHARED_LIBS=OFF

# Build (Release config)
cmake --build ggml/build --config Release --target ggml ggml-base ggml-cpu -j4
```

Expected outputs under `ggml\build\src\Release\`:

```
ggml.lib
ggml-base.lib
ggml-cpu.lib
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
qwen3tts.dll
test_tokenizer.exe  test_encoder.exe  test_transformer.exe  test_decoder.exe
```

### Optional: Enable timing instrumentation

```powershell
cmake -S . -B build -DQWEN3_TTS_TIMING=ON
cmake --build build --config Release -j4
```

---

## 3b. Build with xmake (alternative)

[xmake](https://xmake.io) is also supported. Install it first:

```powershell
winget install xmake
# or: Invoke-Expression (Invoke-Webrequest 'https://xmake.io/psget.text' -UseBasicParsing).Content
```

Then, after GGML is built (step 2 above):

```powershell
xmake
```

Binaries will be in `build\windows\x64\release\`.

Optional flags:

```powershell
xmake f --timing=y   # Enable timing instrumentation
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

```powershell
# CMake build
.\build\Release\qwen3-tts-cli.exe -m models -t "Hello from Windows." -o hello.wav

# xmake build
.\build\windows\x64\release\qwen3-tts-cli.exe -m models -t "Hello from Windows." -o hello.wav
```

### Voice cloning

```powershell
.\build\Release\qwen3-tts-cli.exe `
    -m models `
    -r examples\readme_clone_input.wav `
    --ref-text "Okay. Yeah. I resent you. I love you. I respect you. But you know what? You blew it! And thanks to you." `
    -t "This is a voice cloning example on Windows." `
    -o cloned.wav
```

### Voice design

```powershell
.\build\Release\qwen3-tts-cli.exe `
    -m models `
    --voice-description "Female, calm, warm, and confident, with a broadcast-style delivery." `
    -t "This example uses a natural-language voice design prompt." `
    -o designed.wav
```

### Built-in speaker selection

```powershell
.\build\Release\qwen3-tts-cli.exe `
    -m models `
    --speaker Cherry `
    -t "This example asks the runtime to use a built-in speaker preset." `
    -o builtin.wav
```

### CLI options

| Flag | Description | Default |
|------|-------------|---------|
| `-m, --model <dir>` | Model directory containing GGUF files | (required) |
| `-t, --text <text>` | Text to synthesize | (required) |
| `-o, --output <file>` | Output WAV file path | `output.wav` |
| `-r, --reference <file>` | Reference audio for voice cloning | (none) |
| `--ref-text <text>` | Transcript for the reference audio prompt | (none) |
| `--voice-description <text>` | Natural-language voice design prompt | (none) |
| `--speaker <name>` | Built-in speaker name for CustomVoice-style prompting | (none) |
| `--temperature <val>` | Sampling temperature (0 = greedy) | 0.9 |
| `--top-k <n>` | Top-k sampling (0 = disabled) | 50 |
| `--max-tokens <n>` | Maximum audio frames to generate | 4096 |
| `--repetition-penalty <val>` | Repetition penalty on codebook-0 | 1.05 |
| `-j, --threads <n>` | Number of compute threads | 4 |

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
