# obs-av-sync

**Automatic multi-camera AV sync for OBS Studio.**

`obs-av-sync` measures the audio offset between each camera source and a reference "house" audio mix, then drives OBS's per-source sync offset to keep all cameras aligned with the reference automatically — no more manually tuning sliders per camera.

> Status: **early development**. The plugin skeleton builds; the sync engine is being implemented. See [docs/ROADMAP.md](docs/ROADMAP.md).

## Why

Multi-camera OBS productions routinely suffer from per-camera audio/video latency that differs from the house audio feed (consoles, FOH mixer, RTMP ingest, NDI sources, etc.). OBS already exposes a per-source `sync_offset`, but tuning it by hand is tedious, drifts over time, and is error-prone across a live show.

`obs-av-sync` does it automatically: pick a reference audio source, flag which cameras should track it, and the plugin continuously measures offsets via cross-correlation and applies them.

## How it works (short version)

1. User designates one audio source as the **reference** (e.g. the house audio mix).
2. User flags which sources should be **synced** against the reference.
3. The plugin taps each source's audio via OBS audio callbacks, runs **GCC-PHAT** (generalized cross-correlation with phase transform) against the reference on a sliding window, and produces a per-source offset estimate.
4. Estimates are smoothed (hysteresis + slew-rate limiting) and applied via `obs_source_set_sync_offset()`.

Details in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Requirements

- OBS Studio 31.x (matches `buildspec.json`)
- Build tooling per obs-plugintemplate: Visual Studio 2022 (Windows), Xcode 16 (macOS), CMake 3.28+
- Qt6 (for the UI/dock — optional at first; see `ENABLE_QT` in `CMakeLists.txt`)

## Platform support

| Platform | Status |
|---|---|
| Windows x64 | primary target |
| macOS | template-supported; untested so far |
| Linux | template-supported; untested so far |

## Build

Standard obs-plugintemplate flow. See `.github/workflows/` for the canonical steps. Quick local build (Windows):

```sh
cmake --preset windows-x64
cmake --build --preset windows-x64
```

## License

GPL-2.0-or-later. See [LICENSE](LICENSE).

## Credits

Built on the official [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate).
