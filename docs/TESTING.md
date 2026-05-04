# Testing obs-av-sync

This document describes how to build, install, and validate the **obs-av-sync** plugin on a Windows test machine.

---

## Prerequisites

| Requirement | Details |
|-------------|---------|
| OS | Windows 10 or Windows 11 (64-bit) |
| OBS Studio | Version 30 or later (Qt6 build) |
| Build artifact | Local compile or CI-generated `.zip` |
| Permissions | Administrator rights (for `C:\Program Files\obs-studio`) |

> **Note:** If you already have a CI artifact (e.g., from GitHub Actions), skip to [Installing the plugin](#installing-the-plugin).

---

## Building from source (optional)

The repository uses CMake presets. On Windows:

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64
```

> The template also supports `macos` and `ubuntu-x86_64` presets for cross-platform builds.

Build artifacts are written to:

```
build_x64\RelWithDebInfo\
```

---

## Installing the plugin

1. Close OBS Studio if it is running.
2. Copy the plugin binary:

   ```
   build_x64\RelWithDebInfo\obs-av-sync.dll
   ```

   to:

   ```
   C:\Program Files\obs-studio\obs-plugins\64bit\
   ```

---

## Installing locale data

1. Create the plugin data directory:

   ```
   C:\Program Files\obs-studio\data\obs-plugins\obs-av-sync\locale\
   ```

2. Copy the locale file into that folder:

   ```
   data\locale\en-US.ini   →   C:\Program Files\obs-studio\data\obs-plugins\obs-av-sync\locale\en-US.ini
   ```

---

## Step-by-step testing guide

1. **Launch OBS Studio**
   - Start OBS normally and confirm it loads without errors.

2. **Add a video source**
   - In the **Sources** panel, click the **+** button.
   - Choose **Media Source** or **Video Capture Device**.
   - Select a clip or camera that also carries an audio track.

3. **Add the AV Sync Tracker filter**
   - Right-click the source and choose **Filters**.
   - Under **Effect Filters**, click **+** and select **AV Sync Tracker**.
   - In the filter settings:
     - Choose a **Reference Audio Source** (e.g., your house mix or another source you trust as baseline).
     - Check **Enable sync tracking**.

4. **Open the AV Sync Status dock**
   - From the top menu, choose **Docks → AV Sync Status**.
   - The dock panel should appear, listing tracked sources.

5. **Verify the display**
   - The source should appear in the dock with a color-coded status indicator.

---

## Expected behavior

After enabling tracking, the status follows this flow:

1. **Measuring** (yellow) — The plugin is collecting audio samples and computing the GCC-PHAT cross-correlation.
2. **Synced** (green) — A stable offset has been found and applied.

While synced, you should observe:

- **Offset** updates in milliseconds (ms).
- **Confidence** value displayed (higher is better).

The offset is applied automatically via OBS’s per-source `sync_offset` setting.

---

## Troubleshooting

| Symptom | Action |
|---------|--------|
| Source does not appear in the dock | Ensure the source has an **audio track** and the filter is enabled. |
| Status stays "Measuring" indefinitely | Check that the **reference source** is actively outputting audio. Verify both sources are not muted. |
| Offset values jump erratically | Increase the smoothing window or verify stable audio levels on both sources. |
| Plugin does not load | Check the OBS log (`Help → Log Files → View Current Log`) for load errors. Ensure `obs-av-sync.dll` and `en-US.ini` are in the correct paths. |
| Wrong reference chosen | Re-open the filter settings and select the correct **Reference Audio Source**. |

---

## Running tests

If unit tests were built, run them with `ctest`:

```powershell
ctest -C RelWithDebInfo
```

> A passing run reports no failures. If tests fail, review the output for DSP or ring-buffer assertions.

---

## Pre-flight checklist

- [ ] Windows 10/11 64-bit machine ready.
- [ ] OBS Studio 30+ (Qt6) installed.
- [ ] `obs-av-sync.dll` copied to `obs-plugins\64bit\`.
- [ ] `en-US.ini` copied to `data\obs-plugins\obs-av-sync\locale\`.
- [ ] OBS launches without plugin load errors.
- [ ] Video source added and AV Sync Tracker filter applied.
- [ ] Reference audio source selected and tracking enabled.
- [ ] AV Sync Status dock opened from **Docks** menu.
- [ ] Source appears in dock with status transitioning from **Measuring** to **Synced**.
- [ ] Offset (ms) and confidence values update in the dock.
