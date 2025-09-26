#pragma once

#include "CoreMinimal.h"
#include "RHIResources.h"
#include "Math/Float16Color.h"
#include "PanoramaCaptureTypes.h"

/** Representation of a frame captured from the render thread. */
struct FPanoramaFrame
{
    FPanoramaFrame()
        : TimestampSeconds(0.0)
        , EyeIndex(0)
        , bIsStereo(false)
        , Format(PF_FloatRGBA)
        , Resolution(FIntPoint::ZeroValue)
        , ColorFormat(EPanoramaColorFormat::NV12)
    {
    }

    double TimestampSeconds;
    int32 EyeIndex;
    bool bIsStereo;
    EPixelFormat Format;

    FTextureRHIRef Texture;
    FIntPoint Resolution;

    /** Raw half float pixels captured from the render thread for PNG output. */
    TArray<FFloat16Color> LinearPixels;

    /** GPU-resident texture prepared for NVENC zero-copy submission (BGRA8). */
    FTextureRHIRef NVENCTexture;

    /** Resolution of the NVENC-ready texture. May differ from the float equirect target in stereo mode. */
    FIntPoint NVENCResolution = FIntPoint::ZeroValue;

    /** Location of an intermediate file written by the worker (PNG sequence). */
    FString DiskFilePath;

    /** Encoded elementary stream payload for hardware encoder output. */
    TArray<uint8> EncodedVideo;

    /** Color format used when producing EncodedVideo or NVENCTexture. */
    EPanoramaColorFormat ColorFormat;

    /** Optional planar payload generated on the GPU (NV12/P010) before NVENC submission. */
    TArray<uint8> PlanarVideo;
};
