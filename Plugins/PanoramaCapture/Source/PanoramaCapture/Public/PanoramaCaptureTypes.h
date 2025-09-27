#pragma once

#include "CoreMinimal.h"
#include "PanoramaCaptureTypes.generated.h"

UENUM(BlueprintType)
enum class EPanoramaCaptureMode : uint8
{
    Mono,
    Stereo
};

UENUM(BlueprintType)
enum class EPanoramaOutputFormat : uint8
{
    PNGSequence,
    NVENC
};

UENUM(BlueprintType)
enum class EPanoramaGamma : uint8
{
    SRGB,
    Linear
};

UENUM(BlueprintType)
enum class EPanoramaStereoLayout : uint8
{
    TopBottom,
    SideBySide
};

UENUM(BlueprintType)
enum class EPanoramaRateControlPreset : uint8
{
    Default,
    LowLatency,
    HighQuality
};

UENUM(BlueprintType)
enum class EPanoramaColorFormat : uint8
{
    NV12,
    P010,
    BGRA8
};

USTRUCT(BlueprintType)
struct FPanoramicVideoSettings
{
    GENERATED_BODY()

    FPanoramicVideoSettings()
        : Resolution(FIntPoint(4096, 2048))
        , TargetBitrateMbps(80)
        , GOPLength(30)
        , NumBFrames(2)
        , bUseHEVC(true)
        , OutputFormat(EPanoramaOutputFormat::NVENC)
        , CaptureMode(EPanoramaCaptureMode::Mono)
        , Gamma(EPanoramaGamma::SRGB)
        , ColorFormat(EPanoramaColorFormat::NV12)
        , StereoLayout(EPanoramaStereoLayout::TopBottom)
        , SeamFixTexels(1.0f)
        , RateControlPreset(EPanoramaRateControlPreset::Default)
    {
    }

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video")
    FIntPoint Resolution;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video", meta = (ClampMin = "1"))
    int32 TargetBitrateMbps;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video", meta = (ClampMin = "1"))
    int32 GOPLength;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video", meta = (ClampMin = "0"))
    int32 NumBFrames;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video")
    bool bUseHEVC;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video")
    EPanoramaOutputFormat OutputFormat;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video")
    EPanoramaCaptureMode CaptureMode;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video")
    EPanoramaGamma Gamma;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video")
    EPanoramaColorFormat ColorFormat;

    /** Layout for stereo output when CaptureMode is Stereo. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video")
    EPanoramaStereoLayout StereoLayout;

    /** Number of texels to shrink cubemap sampling to hide seams. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video", meta = (ClampMin = "0.0", ClampMax = "8.0"))
    float SeamFixTexels;

    /** NVENC rate control preset exposed in the UI. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video")
    EPanoramaRateControlPreset RateControlPreset;
};

USTRUCT(BlueprintType)
struct FPanoramicAudioSettings
{
    GENERATED_BODY()

    FPanoramicAudioSettings()
        : SampleRate(48000)
        , NumChannels(2)
        , bCaptureAudio(true)
    {
    }

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio", meta = (ClampMin = "8000", ClampMax = "192000"))
    int32 SampleRate;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio", meta = (ClampMin = "1", ClampMax = "8"))
    int32 NumChannels;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    bool bCaptureAudio;
};

USTRUCT(BlueprintType)
struct FPanoramicCaptureStatus
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Status")
    bool bIsCapturing = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Status")
    int32 PendingFrameCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Status")
    int32 DroppedFrames = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Status")
    float CurrentCaptureTimeSeconds = 0.f;

    /** Last video presentation timestamp relative to capture start (seconds). */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Status")
    double LastVideoPTS = 0.0;

    /** Last audio presentation timestamp relative to capture start (seconds). */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Status")
    double LastAudioPTS = 0.0;

    /** Ring buffer fill ratio (0-1). */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Status")
    float RingBufferFill = 0.f;

    /** True when NVENC hardware encoding is active. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Status")
    bool bUsingNVENC = false;

    /** True when capture fell back to a safer configuration after preflight. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Status")
    bool bUsingFallback = false;

    /** True when the current session requested zero-copy submission to NVENC. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Status")
    bool bZeroCopyRequested = false;

    /** True when NVENC zero-copy submission is active. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Status")
    bool bZeroCopyActive = false;

    /** Optional warning or diagnostic string surfaced to the UI. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Status")
    FString LastWarning;

    /** Additional diagnostic string describing the zero-copy decision. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Status")
    FString ZeroCopyDiagnostic;

    /** Effective video settings after preflight/fallback adjustments. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Status")
    FPanoramicVideoSettings EffectiveVideoSettings;
};

/** Streaming audio packet produced by the submix recorder. */
struct FPanoramaAudioPacket
{
    FPanoramaAudioPacket()
        : TimestampSeconds(0.0)
        , NumChannels(0)
        , SampleRate(0)
    {
    }

    /** Presentation timestamp anchored to the start of the capture session. */
    double TimestampSeconds;

    /** Number of interleaved channels in this packet. */
    int32 NumChannels;

    /** Sample rate in Hertz for the PCM payload. */
    int32 SampleRate;

    /** Interleaved little-endian PCM16 audio samples. */
    TArray<uint8> PCMData;

    /** Utility accessor that converts payload length into seconds. */
    double GetDurationSeconds() const
    {
        const int32 BytesPerFrame = NumChannels * sizeof(int16);
        if (BytesPerFrame <= 0 || SampleRate <= 0 || PCMData.Num() <= 0)
        {
            return 0.0;
        }

        const int32 FrameCount = PCMData.Num() / BytesPerFrame;
        return static_cast<double>(FrameCount) / static_cast<double>(SampleRate);
    }
};

DECLARE_DELEGATE(FPanoramaCaptureStarted);
DECLARE_DELEGATE_OneParam(FPanoramaCaptureStopped, bool /*bSuccess*/);
DECLARE_DELEGATE_OneParam(FPanoramaCaptureStatusUpdated, const FPanoramicCaptureStatus& /*Status*/);
