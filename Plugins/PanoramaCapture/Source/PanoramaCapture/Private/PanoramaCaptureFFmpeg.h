#pragma once

#include "CoreMinimal.h"
#include "PanoramaCaptureTypes.h"

struct FPanoramaFrame;

/** Simple wrapper for ffmpeg muxing of audio/video outputs. */
class FPanoramaFFmpegMuxer
{
public:
    FPanoramaFFmpegMuxer();
    ~FPanoramaFFmpegMuxer();

    void Initialize(const FString& OutputDirectory);
    void Shutdown();

    void Configure(const FPanoramicVideoSettings& VideoSettings, const FPanoramicAudioSettings& AudioSettings);
    void AddVideoFrame(const TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe>& Frame);
    void AddAudioSamples(const FPanoramaAudioPacket& Packet);
    void SetAudioSource(const FString& FilePath, double DurationSeconds);
    void SetNVENCVideoSource(const FString& RawFilePath, const FIntPoint& Resolution, int64 FrameCount, bool bIsHEVC, bool bStereo, bool bIsEncodedStream);

    void FinalizeContainer();

    bool IsFFmpegAvailable() const { return bHasFFmpegExecutable; }

private:
    void FinalizePNGSequence();
    void FinalizeNVENCStream();
    void CleanupPNGFrames();
    double ComputeFrameRate() const;
    bool InvokeFFmpeg(const FString& CommandLine) const;

    FString TargetDirectory;
    FString OutputFilePath;
    FString FramesDirectory;
    FString FrameFilePattern;
    FString FFmpegExecutablePath;
    FString AudioFilePath;
    FString NVENCRawVideoPath;
    bool bInitialized;
    bool bHasFFmpegExecutable;
    FPanoramicVideoSettings CachedVideoSettings;
    FPanoramicAudioSettings CachedAudioSettings;
    TArray<double> CapturedFrameTimestamps;
    int32 CapturedFrameCount;
    double CachedAudioDurationSeconds;
    FIntPoint NVENCResolution;
    int64 NVENCFrameCount;
    bool bHasNVENCSource;
    bool bNVENCIsHEVC;
    bool bNVENCStereo;
    bool bNVENCIsCompressedStream;
};
