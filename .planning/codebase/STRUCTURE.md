# Repository Structure

## Directory Layout

```
obs_av_sync/
├── src/                  Plugin source code (C, future C++)
├── docs/                 Design documentation (architecture, roadmap)
├── data/                 OBS runtime data (locale strings)
├── cmake/                CMake helper modules from obs-plugintemplate
├── build-aux/            Format-check scripts from obs-plugintemplate
├── .github/              GitHub Actions CI workflows from obs-plugintemplate
├── _artifact/            Build artifact staging (template-managed)
├── .deps/                Downloaded OBS prebuilt dependencies (gitignored)
├── build_x64/            Local Windows x64 CMake build output (gitignored)
├── .planning/            GSD project planning documents (not shipped)
├── .claude/              Claude AI project configuration (not shipped)
├── CMakeLists.txt        Plugin-level build definition
├── CMakePresets.json     Cross-platform preset definitions (from template)
├── CMakeUserPresets.json Local user overrides (gitignored)
├── buildspec.json        Plugin identity + pinned OBS/dep versions and hashes
├── .clang-format         C/C++ code style rules
├── .gersemirc            CMake formatter configuration
├── CLAUDE.md             AI assistant guidance file
├── README.md             User-facing project readme
└── LICENSE               GPL-2.0-or-later
```

---

## Source Files

### `src/plugin-main.c`
**Role:** OBS module entry point. Kept as plain C for `OBS_DECLARE_MODULE()` compatibility.

Key symbols:
- `OBS_DECLARE_MODULE()` — macro that defines the module descriptor OBS looks for in the shared library.
- `OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")` — wires locale loading.
- `bool obs_module_load(void)` — called by OBS on plugin load; invokes `av_sync_register_filter()`, logs version banner.
- `void obs_module_unload(void)` — called on unload; logs shutdown.

### `src/av_sync_filter.h`
**Role:** Public header for the filter registration function. Includes C linkage guards so it can be included from future C++ translation units.

Key symbols:
- `void av_sync_register_filter(void)` — sole public function; registers the `obs_source_info` with OBS.

### `src/av_sync_filter.c`
**Role:** Per-source audio filter implementation. Contains all filter lifecycle callbacks and the audio tap/downmix/ring-write path.

Key symbols:
- `#define AV_SYNC_FILTER_ID "obs_av_sync_filter"` — OBS filter type ID string.
- `#define AV_SYNC_RING_SECONDS 10` — ring buffer duration.
- `#define AV_SYNC_DOWNMIX_SCRATCH 2048` — max frames handled per callback without skipping.
- `struct av_sync_filter_data` — per-instance state (see ARCHITECTURE.md for all fields).
- `static const char *av_sync_filter_get_name(void *)` — returns localized filter display name.
- `static void *av_sync_filter_create(obs_data_t *, obs_source_t *)` — allocates `av_sync_filter_data` via `bzalloc`, logs creation.
- `static void av_sync_filter_destroy(void *)` — destroys ring, frees struct.
- `static struct obs_audio_data *av_sync_filter_audio(void *, struct obs_audio_data *)` — main audio callback; downmix → ring write → diagnostic logging; returns audio unchanged.
- `static struct obs_source_info av_sync_filter_info` — OBS source descriptor struct binding all callbacks.
- `void av_sync_register_filter(void)` — calls `obs_register_source(&av_sync_filter_info)`.

### `src/ring_buffer.h`
**Role:** Public interface for the mono float32 ring buffer. Opaque type hides implementation details.

Key symbols:
- `typedef struct av_sync_ring av_sync_ring_t` — opaque handle.
- `av_sync_ring_t *av_sync_ring_create(size_t capacity_samples, uint32_t sample_rate)`
- `void av_sync_ring_destroy(av_sync_ring_t *ring)`
- `void av_sync_ring_write(av_sync_ring_t *ring, const float *samples, size_t n, uint64_t timestamp_ns)`
- `typedef struct { size_t capacity; size_t filled; uint64_t total_written; uint64_t oldest_timestamp_ns; uint64_t newest_timestamp_ns; uint32_t sample_rate; } av_sync_ring_stats_t`
- `void av_sync_ring_get_stats(const av_sync_ring_t *ring, av_sync_ring_stats_t *out)`

### `src/ring_buffer.c`
**Role:** Ring buffer implementation. Single-producer, no locking (Phase 2; atomics deferred to Phase 3).

Key symbols:
- `struct av_sync_ring` — concrete (private) struct with `float *samples`, `size_t capacity`, `uint32_t sample_rate`, `uint64_t total_written`, `uint64_t newest_timestamp_ns`.
- Write path: two-part `memcpy` for wrap-around; special-cases `n >= capacity` by copying only the last `capacity` samples.
- Timestamp tracking: `newest_timestamp_ns = chunk_start_ns + (n-1) * 1e9 / sample_rate`.
- Stats: derives `oldest_timestamp_ns` from `newest_timestamp_ns` minus the span of `filled` samples.

### `src/plugin-support.h`
**Role:** Logging and version-info shim provided by obs-plugintemplate. Bridges the template's `blogva` to a variadic `obs_log()` helper.

Key symbols:
- `extern const char *PLUGIN_NAME` — set by CMake to `"obs-av-sync"`.
- `extern const char *PLUGIN_VERSION` — set by CMake to `"0.1.0"`.
- `void obs_log(int log_level, const char *format, ...)` — prefixes OBS log with plugin name.

---

## Build Artifacts

| Preset name | Output directory | Platform |
|---|---|---|
| `windows-x64` | `build_x64/` | Windows 64-bit |
| `macos` | `build_x86_64/` (or arm64) | macOS |
| `ubuntu-x86_64` | `build_x86_64/` | Linux |

Build commands:
```sh
cmake --preset windows-x64
cmake --build --preset windows-x64
```

The compiled plugin is a shared library (`.dll` on Windows, `.dylib` on macOS, `.so` on Linux) named `obs-av-sync`. The template's `set_target_properties_plugin` macro handles output naming and installation paths.

---

## Configuration Files

### `CMakeLists.txt`
Defines the plugin as a `MODULE` library. Key toggles:
- `ENABLE_FRONTEND_API OFF` — must be set `ON` for Phase 5 UI (reference source dropdown).
- `ENABLE_QT OFF` — must be set `ON` for Phase 6 dock UI.
- `target_sources(... PRIVATE src/plugin-main.c src/av_sync_filter.c src/ring_buffer.c)` — add new `.cpp` files here.
- Links `OBS::libobs`; optionally adds `OBS::obs-frontend-api` and `Qt6::Core Qt6::Widgets`.

### `CMakePresets.json`
Cross-platform preset file from obs-plugintemplate. Defines configure/build/test presets for Windows, macOS, and Linux. Not manually edited.

### `CMakeUserPresets.json`
Local user overrides (gitignored). Used to point CMake at the local OBS install or prebuilt deps path.

### `buildspec.json`
Plugin identity consumed by the template's GitHub Actions workflows:
- `name`: `"obs-av-sync"`, `version`: `"0.1.0"`, `author`: `"Sean Mahoney"`.
- Pinned dependency versions and SHA-256 hashes for `obs-studio` (31.1.1), `prebuilt` obs-deps (2025-07-11), and `qt6` (2025-07-11).
- macOS `bundleId`: `"com.seanmahoney.obs-av-sync"`.

### `.clang-format`
Enforces C/C++ code style. Checked in CI by `build-aux/run-clang-format`.

### `.gersemirc`
Enforces CMake file formatting. Checked in CI by `build-aux/run-gersemi`.

---

## Documentation

### `docs/ARCHITECTURE.md`
Full system design: problem statement, ASCII data-flow diagram, component responsibilities (including planned `reference_tap`, `gcc_phat`, `smoother` modules not yet implemented), threading model, data-flow invariants, OBS API touchpoints table, open design questions.

### `docs/ROADMAP.md`
Phased build plan (Phases 0–7):
- Phase 0: project setup (done).
- Phase 1: skeleton plugin loads (done — plugin registers and logs).
- Phase 2: audio tap + ring buffer (done — filter + ring implemented).
- Phase 3: offline GCC-PHAT offset measurement.
- Phase 4: continuous sync engine with analysis thread + smoother.
- Phase 5: filter properties UI (reference picker, status fields).
- Phase 6: optional Qt6 dock panel.
- Phase 7: release packaging and signing.

### `CLAUDE.md`
AI assistant guidance: language conventions (C entry point, C++ allowed for DSP), OBS API patterns (`obs_source_add_audio_capture_callback`, `obs_source_set_sync_offset`), cross-platform constraints, FFT library guidance (KissFFT/PFFFT; no FFTW), commit conventions.

---

## Template vs Custom Code

### From `obs-plugintemplate` (do not restructure)
- `cmake/` — all CMake helper modules (`bootstrap.cmake`, `compilerconfig`, `defaults`, `helpers`, `set_target_properties_plugin`).
- `build-aux/` — `run-clang-format`, `run-gersemi` format-check scripts.
- `.github/` — GitHub Actions CI workflows (build matrix, format check, release).
- `CMakePresets.json` — preset definitions for all platforms.
- `src/plugin-support.h` — logging/version shim.
- `data/locale/en-US.ini` — locale file structure (keys added for this plugin).

### Plugin-specific (custom code)
- `src/plugin-main.c` — entry point customized for obs-av-sync.
- `src/av_sync_filter.h` / `src/av_sync_filter.c` — new; the audio filter.
- `src/ring_buffer.h` / `src/ring_buffer.c` — new; the mono ring buffer.
- `docs/ARCHITECTURE.md` / `docs/ROADMAP.md` — new; design docs.
- `buildspec.json` — identity fields updated from template defaults.
- `CMakeLists.txt` — `target_sources` line updated; options commented.
- `CLAUDE.md` — project-specific AI guidance.
