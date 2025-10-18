# Panorama Capture UE Plugin

This repository contains a Windows-only Unreal Engine plugin that implements a full 360° capture workflow. The plugin targets UE 5.4–5.6 and provides:

- Automatic six-camera rig generation for mono and stereo capture modes.
- A render dependency graph (RDG) compute shader that stitches the cube captures into an equirectangular panorama.
- Dual output pipelines covering 16-bit PNG sequences and NVENC zero-copy hardware encoding with optional HEVC, selectable NV12/P010/BGRA color formats, stereo layout packing (top/bottom or side-by-side), and automatic gamma management.
- AudioMixer submix recording, unified timestamps with drift compensation, and FFmpeg-based muxing into MP4/MKV containers with VR metadata (including projection and color primaries).
- An in-editor control panel with codec controls (HEVC, bitrate, GOP, B-frames, rate-control presets), capture mode, gamma, color-format and stereo-layout selectors, live preview toggles, fallback warnings, and buffer health indicators.
- Preflight diagnostics that validate NVENC availability, ffmpeg presence, and disk space before recording, automatically falling back to safe PNG output when needed.

> **Note**: PNG sequence rendering, WAV capture via AudioMixer, and FFmpeg muxing are implemented but assume that `ffmpeg.exe` is present under `Plugins/PanoramaCapture/ThirdParty/Win64`. NVENC hardware encoding paths remain guarded by `PANORAMA_WITH_NVENC` and require NVIDIA's SDK libraries to be deployed.

## Repository Layout

```
Plugins/
  PanoramaCapture/
    PanoramaCapture.uplugin
    Shaders/
      PanoramaEquirectCS.usf
    Source/
      PanoramaCapture/
        Public/   – runtime API (component, manager, types, logging)
        Private/  – render pipeline, audio capture, NVENC/FFmpeg bridges
      PanoramaCaptureEditor/
        Private/  – editor module and Slate control panel
```

## Building the Plugin

1. Copy the `Plugins/PanoramaCapture` folder into your Unreal project.
2. Regenerate project files (right-click `.uproject` → **Generate Visual Studio project files**).
3. Build the project in Visual Studio 2022.
4. Enable **Panorama Capture** in the project’s plugin settings (Windows only).

### Troubleshooting build failures on Windows

Large modules such as the capture pipeline can exhaust the compiler's precompiled-header
allocation when Visual Studio is configured with a small paging file. If you encounter
errors similar to:

```
c1xx : error C3859: Failed to create PCH's virtual memory
c1xx : fatal error C1076: compiler limit: internal heap limit reached
```

increase the Windows page file size (System Properties → Advanced → Performance →
Settings → Advanced → Virtual memory) or lower Unreal Build Tool's parallel compile
count by adding `-MaxParallelActions=4` to your build command. Unreal Build Accelerator
logs will also indicate when paging space is exhausted; once the system has more virtual
memory the build will proceed normally.

## Using the Plugin

1. Drag an actor with a `Panorama Capture Component` into the level.
2. Configure video and audio settings in the details panel (resolution, bitrate, codec, gamma, capture mode).
3. Use the **Panorama Capture** tab (Window → Panorama Capture) to start/stop recording, toggle previews, and monitor buffer usage.
4. Recorded output (PNG/NVENC elementary streams and WAV) is written to `Saved/PanoramaCaptures` by default.

The capture manager coordinates rendering, audio, encoding, and muxing, while the editor UI exposes live statistics and controls tailored for VR panoramic production workflows.
