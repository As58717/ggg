# Plan to Enable NV12 / P010 NVENC Zero-Copy

This document outlines the engineering tasks required to extend the existing BGRA8 zero-copy pipeline so that NV12 and P010 color formats can also submit GPU textures directly to NVENC. Each section calls out dependencies, implementation details, and validation steps.

## 1. Render Graph Output Enhancements

### 1.1 Shared render graph data
- Introduce a per-frame render graph payload (e.g., `FPanoramaEncodeResources`) that carries:
  - Existing linear equirect render target for PNG/preview paths.
  - New Y and UV plane UAVs for NV12.
  - New 10-bit Y and UV plane UAVs for P010.
  - Resource transitions/fences required before handing the textures to NVENC.
- Extend `FPanoramaFrame` to store `FTextureRHIRef` handles for each plane alongside the legacy CPU buffers.

### 1.2 NV12 render pass
- Author a compute shader variant that writes:
  - Luma plane as `R8_UNORM` texture sized `(Width, Height)`.
  - Interleaved chroma plane as `RG8_UNORM` with half resolution.
- Ensure thread group size keeps NVENC pitch alignment (multiple of 128 bytes for luma, 64 for chroma).
- Add render graph passes that create UAV-backed textures with `TexCreate_UAV | TexCreate_RenderTargetable | TexCreate_Shared` flags and skip mip generation.

### 1.3 P010 render pass
- Emit luma as `R16_UINT` and chroma as `RG16_UINT` with values left-shifted by 6 bits (NVENC expects 10-bit samples in the high bits).
- Validate that UE RHI exposes these DXGI formats (`DXGI_FORMAT_R16_UNORM` / `DXGI_FORMAT_R16G16_UNORM` on D3D11 and `DXGI_FORMAT_P010` equivalents on D3D12 via create parameters).
- For D3D11, use texture arrays or shared handles because typeless 10-bit formats require explicit resource creation through `FD3D11DynamicRHI::RHICreateTexture2D`.

### 1.4 Synchronization and lifetime
- Transition the planar textures to `ERHIAccess::CopySrc` after compute passes finish.
- Defer releasing the render graph resources until the encoding thread signals completion (introduce a fence or reuse the existing frame recycling mechanism).

## 2. NVENC Session Negotiation

### 2.1 Capability probing
- During encoder initialization, call `NvEncGetEncodeCaps` with:
  - `NV_ENC_CAPS_SUPPORT_DIRECTX_TEXTURE` to confirm DirectX intake.
  - `NV_ENC_CAPS_SUPPORT_YUV444_ENCODE` / `NV_ENC_CAPS_SUPPORT_10BIT_ENCODE` for P010.
- Cache capability results in the capture status so the UI can surface whether GPU planes will be consumed or the CPU fallback will be used.

### 2.2 Session configuration
- When color format is NV12:
  - Set `NV_ENC_BUFFER_FORMAT_NV12` for input.
  - Enable `enableOutputInVidmem` and `enableInputInVidmem` in `NV_ENC_INITIALIZE_PARAMS` to stay on GPU memory.
- When color format is P010:
  - Use `NV_ENC_BUFFER_FORMAT_YUV420_10BIT`.
  - Pick HEVC-only presets (fall back to CPU or disallow combination if user forces H.264).
- Maintain BGRA path unchanged, but refactor encoder code so the input registration logic is shared between formats.

### 2.3 Texture registration
- For each frame:
  - Register luma and chroma textures separately via `NvEncRegisterResource` with `resourceType = NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX` and appropriate `subResourceIndex`.
  - Map each resource using `NvEncMapInputResource` and populate `NV_ENC_PIC_PARAMS` with plane pointers (`inputBuffer`, `inputBufferYuv[2]`).
  - Unmap after submission to avoid leaking registered handles.
- Consider persistent registration (register once, reuse) keyed by render target pointers to reduce per-frame overhead.

## 3. Capture Manager Integration

### 3.1 Frame queue updates
- Extend the frame queue items to reference planar GPU textures.
- Introduce ref-counted wrappers (e.g., `FPanoramaGpuPlaneHandle`) so that when the worker thread finishes encoding, it releases the RHI resources or enqueues them back to the renderer for reuse.

### 3.2 PNG / preview coexistence
- Continue producing linear RGBA render target for PNG readback and preview materials.
- Guard the planar pass execution so it runs only when NVENC is active; PNG-only sessions can skip the extra compute work.
- Ensure that CPU readbacks don't stall the GPU when zero-copy is active by sequencing passes appropriately (NVENC first, PNG readback after fence completion if both outputs are requested).

## 4. Editor & Diagnostics

### 4.1 Status reporting
- Extend the capture status payload with fields:
  - `bGpuPlaneBuilt` (per format).
  - `bGpuPlaneSubmitted` (per frame) with failure reasons.
  - Capability flags exposed by NVENC probing.
- Update the Slate panel to show when NV12/P010 zero-copy is active, and highlight fallback reasons (e.g., "GPU format unsupported", "D3D11 typeless creation failed").

### 4.2 Configuration guards
- If the user selects P010 without HEVC, display an inline warning and automatically disable zero-copy (still allow CPU path).
- Provide tooltips explaining alignment requirements and potential performance implications.

## 5. Validation Plan

1. **Unit tests / automation**: Implement small `AutomationSpec` cases that exercise encoder initialization for NV12 and P010 with mocked capability responses.
2. **PIE testing**: Record short mono/stereo sessions in UE5.4, 5.5, and 5.6 builds using RTX 30- and 40-series GPUs. Verify zero-copy activation in logs/UI.
3. **Bitstream inspection**: Use `ffprobe` to confirm pixel formats (`yuv420p`, `p010le`) and metadata in resulting MP4/MKV files.
4. **Stress tests**: Run 8K stereo capture for at least five minutes, monitoring ring buffer utilization and ensuring no CPU conversions occur.

## 6. Dependencies & Risks

- **Driver requirements**: Document minimum NVIDIA driver versions that expose DirectX intake for NV12/P010 (typically R465+).
- **RHI differences**: UE5.4 ships with legacy D3D12 RHI macros; guard code with `UE_VERSION_OLDER_THAN` where necessary.
- **Resource lifetime**: Improper synchronization may lead to GPU hangs. Test with the D3D debug layer enabled.
- **Fallback parity**: Keep CPU conversion path functional for legacy GPUs or when capability checks fail.

---

By following this plan, the plugin will extend its zero-copy capabilities beyond BGRA8, allowing NV12 and P010 formats to bypass CPU memory entirely while preserving existing PNG workflows and diagnostic tooling.
