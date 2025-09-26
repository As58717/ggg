#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "PanoramaCaptureTypes.h"
#include "HAL/FileManager.h"

#if PANORAMA_WITH_NVENC
#include "Windows/AllowWindowsPlatformTypes.h"
#include "nvEncodeAPI.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif

struct FPanoramaFrame;

/** Lightweight wrapper around NVENC hardware encoder. */
class FPanoramaNVENCEncoder
{
public:
    FPanoramaNVENCEncoder();
    ~FPanoramaNVENCEncoder();

    void Initialize(const FPanoramicVideoSettings& Settings, const FString& OutputDirectory);
    void Shutdown();

    /**
     * Converts a mono frame into a raw payload for the configured color format. Returns false when conversion fails.
     */
    bool EncodeFrame(const TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe>& Frame);

    /**
     * Produces a top/bottom stereo payload (NV12/P010/BGRA) using a left/right pair. Returns the encoded (left) frame on success.
     */
    TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe> EncodeStereoPair(const TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe>& LeftFrame, const TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe>& RightFrame);

    void Flush();

    bool IsInitialized() const { return bInitialized; }
    bool SupportsZeroCopy() const { return bInitialized && bSupportsZeroCopy; }
    bool HasHardware() const;

    FString GetRawVideoPath() const { return RawVideoPath; }
    FIntPoint GetEncodedResolution() const { return EncodedResolution; }
    int64 GetEncodedFrameCount() const { return EncodedFrameCount; }
    double GetLastVideoPTS() const { return LastVideoPTS; }
    bool IsUsingHEVC() const { return CachedSettings.bUseHEVC; }

private:
    void InitializeEncoderResources(const FPanoramicVideoSettings& Settings);
    void EnsureRawFile();
    bool ConvertFrameToRawPayload(const TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe>& Frame, TArray<uint8>& OutData, FIntPoint& OutResolution) const;
    bool ConvertStereoToRawPayload(const TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe>& LeftFrame, const TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe>& RightFrame, TArray<uint8>& OutData, FIntPoint& OutResolution) const;
    void WritePacketToDisk(const TArray<uint8>& PacketData);
    bool EncodeFrameZeroCopy(const TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe>& Frame);

    bool bInitialized;
    bool bSupportsZeroCopy = false;
    FCriticalSection CriticalSection;

    FString CodecName;
    int32 Bitrate;
    FPanoramicVideoSettings CachedSettings;
    FString TargetDirectory;
    FString RawVideoPath;
    TUniquePtr<IFileHandle> RawVideoHandle;
    int64 EncodedFrameCount;
    FIntPoint EncodedResolution;
    double LastVideoPTS;

#if PANORAMA_WITH_NVENC
    struct FNVENCAPI
    {
        FNVENCAPI();
        NV_ENCODE_API_FUNCTION_LIST FunctionList;
        bool bLoaded;
    };

    TUniquePtr<FNVENCAPI> NVENCAPI;
    void* EncoderInstance = nullptr;
    NV_ENC_CONFIG EncoderConfig;
    NV_ENC_INITIALIZE_PARAMS InitializeParams;
#endif
};
