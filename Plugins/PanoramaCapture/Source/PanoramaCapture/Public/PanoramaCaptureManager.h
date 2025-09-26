#pragma once

#include "CoreMinimal.h"
#include "PanoramaCaptureTypes.h"
#include "PanoramaCaptureFrameQueue.h"
#include "HAL/ThreadSafeBool.h"

class FPanoramaCaptureRenderer;
class FPanoramaAudioRecorder;
class FPanoramaFFmpegMuxer;
class FPanoramaNVENCEncoder;
class UPanoramaCaptureComponent;
class FRunnableThread;
class FEvent;
class USoundSubmix;

struct FPanoramaFrame;

/** High-level orchestrator for the capture pipeline. */
class PANORAMACAPTURE_API FPanoramaCaptureManager : public TSharedFromThis<FPanoramaCaptureManager, ESPMode::ThreadSafe>
{
public:
    FPanoramaCaptureManager();
    ~FPanoramaCaptureManager();

    void Initialize(UPanoramaCaptureComponent* InOwnerComponent, const FPanoramicVideoSettings& VideoSettings, const FPanoramicAudioSettings& AudioSettings, const FString& OutputDirectory);
    void Shutdown();

    bool IsInitialized() const { return bInitialized; }

    void StartCapture();
    void StopCapture();

    void EnqueueFrame_RenderThread(const TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe>& Frame);

    FPanoramicCaptureStatus GetStatus() const;
    int32 GetRingBufferCapacity() const;
    int32 GetRingBufferOccupancy() const;

    void Tick_GameThread(float DeltaTime);

    void SetPreviewTargets_GameThread(UTextureRenderTarget2D* MonoTarget, UTextureRenderTarget2D* RightTarget, UTextureRenderTarget2D* PreviewTarget, float InPreviewInterval, bool bPreviewEnabled);
    void SetAudioSubmix(USoundSubmix* Submix);

    FPanoramaCaptureStarted OnCaptureStarted;
    FPanoramaCaptureStopped OnCaptureStopped;
    FPanoramaCaptureStatusUpdated OnCaptureStatusUpdated;

private:
    class FFrameProcessor;

    void StartWorkers();
    void StopWorkers();

    void ProcessPendingFrames();
    void ProcessPendingAudio();

    void ProcessPendingFrames_Worker();
    bool HandlePNGFrame(const TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe>& Frame);
    bool HandleStereoPNGPair(const TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe>& LeftFrame, const TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe>& RightFrame);
    bool HandleNVENCFrame(const TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe>& Frame);
    TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe> HandleStereoNVENCPair(const TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe>& LeftFrame, const TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe>& RightFrame);
    bool SavePNGToDisk(const FString& Filename, const TArray<FFloat16Color>& Pixels, const FIntPoint& Resolution);
    FString BuildPNGFilePath(int32 FrameIndex) const;

    void NotifyStatus_GameThread();
    void UpdateStatusAfterVideoFrame(const TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe>& Frame);
    void UpdateStatusAfterAudioPacket(const FPanoramaAudioPacket& Packet);
    void ResetStatus();
    bool PerformPreflightChecks();
    bool VerifyDiskCapacity();
    void ApplyFallbackIfNeeded();
    void PushWarningMessage(const FString& Message);

    TUniquePtr<FPanoramaCaptureRenderer> Renderer;
    TUniquePtr<FPanoramaAudioRecorder> AudioRecorder;
    TUniquePtr<FPanoramaNVENCEncoder> VideoEncoder;
    TUniquePtr<FPanoramaFFmpegMuxer> Muxer;

    FPanoramicVideoSettings CurrentVideoSettings;
    FPanoramicAudioSettings CurrentAudioSettings;
    FString TargetOutputDirectory;

    mutable FCriticalSection StatusCriticalSection;
    FPanoramicCaptureStatus CachedStatus;

    bool bInitialized;
    bool bCaptureRequested;
    bool bCaptureActive;
    double CaptureStartTimeSeconds;

    FDelegateHandle TickHandle;

    TPanoramaFrameQueue<FPanoramaFrame> FrameQueue;
    TWeakObjectPtr<UPanoramaCaptureComponent> OwnerComponent;

    TUniquePtr<FFrameProcessor> FrameProcessor;
    TUniquePtr<FRunnableThread> FrameProcessorThread;

    TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe> PendingLeftFrame;
    TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe> PendingNVENCLeftFrame;
    int32 FrameCounter;

    TWeakObjectPtr<UTextureRenderTarget2D> MonoTargetWeak;
    TWeakObjectPtr<UTextureRenderTarget2D> StereoTargetWeak;
    TWeakObjectPtr<UTextureRenderTarget2D> PreviewTargetWeak;
    float PreviewFrameIntervalSeconds;
    double LastPreviewUpdateSeconds;
    bool bPreviewEnabled;
    bool bHasFallenBack;
    FString LastWarningMessage;
};
