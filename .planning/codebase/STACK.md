# Tech Stack

## Language & Runtime

- **Primary language**: C (C11 implied by OBS conventions; `plugin-main.c` and all current source files are `.c`)
- **C++ planned**: CLAUDE.md explicitly sanctions C++ for DSP and state code; `.cpp` files to be added via `target_sources` in `CMakeLists.txt`. `OBS_DECLARE_MODULE` must remain in `plugin-main.c` as C.
- **Headers guard for mixed-language linking**: all public headers (`av_sync_filter.h`, `ring_buffer.h`) use `extern "C"` guards so they compile cleanly from both C and C++ translation units.
- **Float precision**: audio samples are `float` (32-bit IEEE 754); timestamps are `uint64_t` nanoseconds.

## Build System

- **CMake minimum**: `3.28` (range `3.28...3.30` declared in `CMakeLists.txt`)
- **Preset schema version**: `8` (`CMakePresets.json`)
- **Key CMake options** (both default `OFF` in presets):
  - `ENABLE_FRONTEND_API` — links `OBS::obs-frontend-api`; needed for Phase 5 (filter properties UI)
  - `ENABLE_QT` — links `Qt6::Core Qt6::Widgets`, enables `AUTOMOC`/`AUTOUIC`/`AUTORCC`; needed for Phase 6 (dock UI)
- **Build configuration**: `RelWithDebInfo` for all platforms in both local and CI presets
- **Output**: shared module (`.dll` / `.so` / `.dylib`) via `add_library(... MODULE)`
- **Helper CMake scripts**: `cmake/common/bootstrap.cmake`, `compilerconfig`, `defaults`, `helpers` (from obs-plugintemplate — do not edit)

## Core Dependencies

- **OBS Studio**: `31.1.1` (pinned in `buildspec.json`; hash-verified per platform)
  - macOS SHA-256: `39751f067bacc13d44b116c5138491b5f1391f91516d3d590d874edd21292291`
  - windows-x64 SHA-256: `2c8427c10b55ac6d68008df2e9a3e82f4647aaad18f105e30d4713c2de678ccf`
- **obs-deps (prebuilt)**: `2025-07-11` snapshot — platform libs bundled with OBS deps releases
- **Qt6 (prebuilt)**: `2025-07-11` snapshot — only pulled in when `ENABLE_QT=ON`
- **OBS internal headers used**: `<obs-module.h>`, `<util/bmem.h>`, `<plugin-support.h>`
- **Standard C headers**: `<inttypes.h>` (in `av_sync_filter.c`), `<stddef.h>`, `<stdint.h>`, `<string.h>` (in `ring_buffer.c`)

### Planned future dependencies (not yet added)

- **PFFFT** (BSD-like licence): FFT library for GCC-PHAT analysis engine (Phase 3). To be vendored as a git submodule. KissFFT is a stated fallback. FFTW is explicitly excluded (GPL licence pollution risk).

## Toolchain

- **C/C++ formatter**: `clang-format` **version 16 or later** (stated in `.clang-format` header comment)
  - Column limit: 120
  - Indent width: 8 (tabs for continuation in C; 4-space for ObjC section)
  - Standard set to `c++17` (used by clang-format's parser even for C files)
  - Brace wrapping: custom (`AfterFunction: true`)
- **CMake formatter**: `gersemi` (any version; config in `.gersemirc`)
  - Line length: 120
  - Indent: 2 spaces
  - List expansion: `favour-inlining`
- **Windows CI compiler**: MSVC via `Visual Studio 17 2022` generator (Windows SDK `10.0.22621`)
- **macOS CI compiler**: Apple Clang via `Xcode 16.1` (`Xcode` generator)
- **Linux CI compiler**: GCC/Clang via `Ninja` generator on ubuntu-24.04
- **ccache**: enabled on macOS and Linux CI (`ENABLE_CCACHE=true`)
- **Warning-as-error**: all CI presets set `CMAKE_COMPILE_WARNING_AS_ERROR=true`

## Platform Support

| Platform | Preset | Generator | Architecture | Min OS |
|---|---|---|---|---|
| Windows | `windows-x64` | Visual Studio 17 2022 | x64 | Windows 10 (SDK 22621) |
| macOS | `macos` | Xcode | arm64 + x86_64 (Universal) | macOS 12.0 |
| Linux | `ubuntu-x86_64` | Ninja | x86_64 | Ubuntu 24.04 |

macOS Universal binary: `CMAKE_OSX_ARCHITECTURES=arm64;x86_64`, deployment target `12.0`.

## Standards & Compatibility

- **C standard**: C11 (implicit; OBS ecosystem standard; `_Bool`, `stdint.h`, `stdbool.h` available)
- **C++ standard**: C++17 planned (`.clang-format` `Standard: c++17`; needed for DSP `.cpp` files)
- **No Win32-only APIs** in core: cross-platform portability is a stated requirement (CLAUDE.md)
- **Licence**: GPL-2.0-or-later (inherited from OBS); all dependencies must be GPL-compatible
- **Locale**: `en-US` default via `OBS_MODULE_USE_DEFAULT_LOCALE`; strings in `data/locale/en-US.ini`
- **Plugin version**: `0.1.0` (in `buildspec.json`; exposed at runtime via `PLUGIN_VERSION` macro)
