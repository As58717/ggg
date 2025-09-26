#pragma once

#include "CoreMinimal.h"
#include "PanoramaCaptureTypes.h"
#include "Math/Float16Color.h"

namespace PanoramaCapture
{
namespace Color
{
    struct FNV12PlaneBuffers
    {
        FIntPoint Resolution = FIntPoint::ZeroValue;
        TArray<uint8> YPlane;
        TArray<uint8> UVPlane;
    };

    struct FP010PlaneBuffers
    {
        FIntPoint Resolution = FIntPoint::ZeroValue;
        TArray<uint16> YPlane;
        TArray<uint16> UVPlane;
    };

    /** Converts linear HDR pixels to NV12 planes with optional gamma processing. */
    bool ConvertLinearToNV12Planes(const TArray<FFloat16Color>& SourcePixels, const FIntPoint& Resolution, EPanoramaGamma GammaMode, FNV12PlaneBuffers& OutPlanes);

    /** Flattens NV12 planes into a contiguous Y + UV byte payload. */
    void CollapsePlanesToNV12(const FNV12PlaneBuffers& Planes, TArray<uint8>& OutData);

    /** Converts linear HDR pixels to P010 planes with optional gamma processing. */
    bool ConvertLinearToP010Planes(const TArray<FFloat16Color>& SourcePixels, const FIntPoint& Resolution, EPanoramaGamma GammaMode, FP010PlaneBuffers& OutPlanes);

    /** Flattens P010 planes into a contiguous Y + UV payload (16-bit per sample). */
    void CollapsePlanesToP010(const FP010PlaneBuffers& Planes, TArray<uint8>& OutData);

    /** Converts linear HDR pixels directly into a BGRA8 byte payload. */
    bool ConvertLinearToBGRAPayload(const TArray<FFloat16Color>& SourcePixels, const FIntPoint& Resolution, EPanoramaGamma GammaMode, TArray<uint8>& OutData);
}
}

