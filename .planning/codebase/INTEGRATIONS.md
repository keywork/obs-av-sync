# Integrations

## OBS Studio API

All OBS API calls go through `<obs-module.h>` and `<util/bmem.h>`. Currently used APIs (phases 1–2b):

| API | File | Purpose |
|---|---|---|
| `OBS_DECLARE_MODULE()` | `plugin-main.c` | Declares the OBS module entry point (macro) |
| `OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")` | `plugin-main.c` | Wires up locale string lookup |
| `obs_log(LOG_INFO, ...)` | `plugin-main.c`, `av_sync_filter.c` | Structured logging to OBS log system |
| `obs_register_source(&av_sync_filter_info)` | `av_sync_filter.c` | Registers the filter type with OBS |
| `obs_filter_get_parent(source)` | `av_sync_filter.c` | Gets the source this filter is attached to |
| `obs_source_get_name(parent)` | `av_sync_filter.c` | Reads source display name for logging |
| `obs_get_audio_info(&oai)` | `av_sync_filter.c` | Reads global audio settings (sample rate, speaker layout) |
| `bzalloc(size)` / `bfree(ptr)` | `av_sync_filter.c`, `ring_buffer.c` | OBS heap allocator (via `<util/bmem.h>`) |

### Planned APIs (not yet called, documented in `docs/ARCHITECTURE.md` and roadmap):

| API | Phase | Purpose |
|---|---|---|
| `obs_source_add_audio_capture_callback` | Phase 3 | Tap reference audio outside the filter chain |
| `obs_source_set_sync_offset(source, offset_ns)` | Phase 4 | Apply measured AV offset in nanoseconds |
| `obs_source_get_sync_offset(source)` | Phase 4 | Read current applied offset |
| `obs_source_enum_sources` | Phase 5 | Populate reference-source dropdown in UI |
| `obs_properties_add_list` | Phase 5 | Build filter properties UI |
| `obs_frontend_add_save_callback` | Phase 5 | Persist settings across OBS scene saves |

## Audio Pipeline

### Filter registration approach

The plugin uses the **filter-chain approach** (not standalone capture callbacks for the per-source tap). A filter is registered with:

```c
static struct obs_source_info av_sync_filter_info = {
    .id           = "obs_av_sync_filter",
    .type         = OBS_SOURCE_TYPE_FILTER,
    .output_flags = OBS_SOURCE_AUDIO,
    .get_name     = av_sync_filter_get_name,
    .create       = av_sync_filter_create,
    .destroy      = av_sync_filter_destroy,
    .filter_audio = av_sync_filter_audio,
};
```

This gives each source its own per-instance state and settings UI without requiring a separate capture callback registration per source.

### `struct obs_audio_data` usage

`av_sync_filter_audio()` receives `struct obs_audio_data *audio` on the OBS audio thread:
- `audio->frames` — sample count per chunk (~480 at 48 kHz / 10 ms)
- `audio->timestamp` — `uint64_t` nanosecond timestamp of the first sample in the chunk
- `audio->data[MAX_AV_PLANES]` — array of `uint8_t*` plane pointers; each plane is a `float*` channel

The filter **returns the audio unchanged** (pure pass-through); it only observes samples, never modifies them.

### Downmix to mono

In `av_sync_filter_audio`, all active planes are averaged (`sum / planes`) into a `float scratch[2048]` stack buffer before writing to the ring. This avoids dynamic allocation on the audio thread. Chunks exceeding 2048 frames are skipped and counted in `oversize_skips`.

### Ring buffer

`av_sync_ring_t` (in `ring_buffer.c`) is a circular buffer of mono `float` samples:
- Capacity: `sample_rate * 10` samples (10 seconds at detected sample rate; lazily allocated on first callback)
- Write: `av_sync_ring_write(ring, samples, n, timestamp_ns)` — oldest samples overwritten when full
- Stats: `av_sync_ring_get_stats()` returns `capacity`, `filled`, `total_written`, `oldest_timestamp_ns`, `newest_timestamp_ns`, `sample_rate`
- Thread safety: **single-producer only** (current phase); cross-thread atomic access to be added in Phase 3 when the analysis thread reads.

### Planned reference tap

The architecture calls for a **separate** `obs_source_add_audio_capture_callback` on the designated reference source to feed a shared reference ring buffer. This is distinct from the per-source filter approach and will be implemented as `src/reference_tap.{cpp,h}` in Phase 3.

## Plugin Registration

`plugin-main.c` is kept as C (not C++) to avoid surprises with `OBS_DECLARE_MODULE` macro expansion. The module lifecycle:

```c
OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

bool obs_module_load(void) {
    av_sync_register_filter();   // registers obs_av_sync_filter type
    obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
    return true;
}

void obs_module_unload(void) {
    obs_log(LOG_INFO, "plugin unloaded");
}
```

`av_sync_register_filter()` is the only public symbol exported from `av_sync_filter.c`; all other functions are `static`.

## External Systems

### Current (no third-party libs beyond OBS)

No external libraries are currently linked. All DSP is deferred to Phase 3.

### Planned (Phase 3+)

- **PFFFT** (Prettiest Fast FFT in the West) — BSD-like licence, small, SIMD-accelerated, cross-platform. Will be vendored as a git submodule. Used for the GCC-PHAT analysis engine in `src/gcc_phat.{cpp,h}`.
- **KissFFT** — fallback option if PFFFT presents integration issues. Also permissively licensed.
- **FFTW** — explicitly excluded; GPL licence would pollute the plugin's dependencies.

### Qt6 (conditional, Phase 6)

Qt6 is available via the pre-built obs-deps snapshot (`2025-07-11`) but gated behind `ENABLE_QT=ON`. When enabled, `CMakeLists.txt` links `Qt6::Core` and `Qt6::Widgets` and enables `AUTOMOC`/`AUTOUIC`/`AUTORCC`. A Clang-specific warning suppression (`-Wno-quoted-include-in-framework-header -Wno-comma`) is applied on Apple compilers.

## CI/CD

All CI is from the obs-plugintemplate; workflows live in `.github/workflows/`. None have been modified from the template defaults.

### `push.yaml`
Triggered on pushes to `main`. Calls `build-project` and `check-format` as reusable workflows. Packages artifacts. Code-signs on push; notarizes only when the ref matches a semver tag pattern (`X.Y.Z[-rc|beta...]`), at which point the build config switches from `RelWithDebInfo` to `Release`.

### `pr-pull.yaml`
Triggered on pull requests. Calls `build-project` and `check-format`. Does not package by default; packaging and signing are enabled only if the PR has the `"Seeking Testers"` label.

### `dispatch.yaml`
Manual `workflow_dispatch` trigger. Builds and code-signs but does not package.

### `check-format.yaml` (reusable)
Two jobs, both on `ubuntu-24.04`:
1. **clang-format** — runs `.github/actions/run-clang-format`; fails on any formatting error
2. **gersemi** — runs `.github/actions/run-gersemi`; fails on any CMake formatting error

### `build-project.yaml` (reusable)
Three parallel build jobs after a `check-event` setup job:
- **`macos-build`** — runs on `macos-15`, uses `Xcode 16.1`, builds Universal binary, optionally code-signs (Apple Developer certs from secrets) and notarizes. ccache enabled.
- **`ubuntu-build`** — runs on `ubuntu-24.04`, Ninja generator, uploads `.deb` package and source tarball. ccache enabled.
- **`windows-build`** — runs on `windows-2022`, PowerShell, Visual Studio 17 2022, uploads `.zip` installer artifact.

All three jobs check out submodules recursively (`submodules: recursive`) in anticipation of PFFFT being vendored.

Artifacts are named `{pluginName}-{pluginVersion}-{platform}-{arch}-{commitHash[0:9]}`.
