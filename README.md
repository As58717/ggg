# Panorama Capture UE Plugin

This repository contains a Windows-only Unreal Engine plugin that implements a full 360° capture workflow. The plugin targets UE 5.4–5.6 and provides:

- Automatic six-camera rig generation for mono and stereo capture modes.
- A render dependency graph (RDG) compute shader that stitches the cube captures into an equirectangular panorama.
- Dual output pipelines covering 16-bit PNG sequences and NVENC hardware encoding with optional HEVC, selectable NV12/P010/BGRA color formats, stereo layout packing (top/bottom or side-by-side), and automatic gamma management.
- NVENC zero-copy submission is currently limited to **BGRA8**. NV12 always relies on the CPU copy path, and P010 will only attempt zero-copy once the renderer grows dedicated 10-bit UAV support. These diagnostics surface in the editor panel so you can confirm which path is active at runtime.
- Runtime diagnostics that negotiate the requested NVENC color format, report whether zero-copy is active, and fall back to CPU copy paths (with UI/log messaging) whenever the GPU or driver cannot satisfy BGRA8 or P010 requests.
- AudioMixer submix recording, unified timestamps with drift compensation, and FFmpeg-based muxing into MP4/MKV containers with VR metadata (including projection and color primaries).
- An in-editor control panel with codec controls (HEVC, bitrate, GOP, B-frames, rate-control presets), capture mode, gamma, color-format and stereo-layout selectors, live preview toggles, fallback warnings, and buffer health indicators.
- Preflight diagnostics that validate NVENC availability, ffmpeg presence, and disk space before recording, automatically falling back to safe PNG output when needed.

### NV12 / P010 零拷贝的实现条件

目前的零拷贝只覆盖 BGRA8 纹理，要想让 NV12 或 P010 也走零拷贝，需要在渲染器和编码器之间完成更多 GPU 端准备工作：

1. **GPU 平面纹理生成**：RDG Pass 需要额外输出 NV12/P010 所需的平面纹理（Y 与交错 UV，或 10-bit Y/UV），并保证纹理尺寸满足 NVENC 对齐规则。
2. **RHI 资源兼容性**：这些平面纹理必须创建为 NVENC 可识别的 `ID3D11Texture2D`/`ID3D12Resource`，同时禁用自动 mip/tiling；否则注册时会失败。
3. **NVENC 会话协商**：初始化时要把 `NV_ENC_BUFFER_FORMAT` 调整为 NV12 或 P010，启用 `enableOutputInVidmem`，并根据 `NvEncGetEncodeCaps` 查询 GPU 是否支持对应格式的 DirectX intake。
4. **PNG / 预览共存**：PNG 序列和预览依赖线性 RGBA 数据，因此仍需保留当前的高精度缓冲，或者在编码后再转换，避免破坏无损输出。

上述步骤尚未实装，因此 NV12/P010 依旧会回退到 CPU 转换路径。但只要补齐 GPU 平面写入和会话协商逻辑，这两种颜色格式理论上也可以纳入零拷贝管线。更详细的分阶段实施规划可参考 [`Docs/NV12_P010_ZeroCopyPlan.md`](Plugins/PanoramaCapture/Docs/NV12_P010_ZeroCopyPlan.md)。

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

## Using the Plugin

1. Drag an actor with a `Panorama Capture Component` into the level.
2. Configure video and audio settings in the details panel (resolution, bitrate, codec, gamma, capture mode).
3. Use the **Panorama Capture** tab (Window → Panorama Capture) to start/stop recording, toggle previews, and monitor buffer usage.
4. Recorded output (PNG/NVENC elementary streams and WAV) is written to `Saved/PanoramaCaptures` by default.

The capture manager coordinates rendering, audio, encoding, and muxing, while the editor UI exposes live statistics and controls tailored for VR panoramic production workflows.
