#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "PanoramaCaptureTypes.h"

class UPanoramaCaptureComponent;
class UTextureRenderTarget2D;
struct FPanoramaFrame;

/** Responsible for issuing scene capture updates and dispatching the equirect compute shader. */
class FRDGBuilder;

class FPanoramaCaptureRenderer
{
public:
    FPanoramaCaptureRenderer();
    ~FPanoramaCaptureRenderer();

    void Initialize();
    void Shutdown();

    bool IsInitialized() const { return bInitialized; }

    void CaptureFrame(UPanoramaCaptureComponent* Component, const FPanoramicVideoSettings& VideoSettings, double CaptureStartTimeSeconds, bool bEnableNVENCZeroCopy, TFunction<void(const TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe>&)> OnFrameReady);

    void SetOutputTargets(UTextureRenderTarget2D* LeftTarget, UTextureRenderTarget2D* RightTarget, UTextureRenderTarget2D* PreviewTarget, float PreviewInterval, bool bPreviewEnabled);

private:
    void DispatchRenderCommand(UPanoramaCaptureComponent* Component, const FPanoramicVideoSettings& VideoSettings, double CaptureStartTimeSeconds, bool bEnableNVENCZeroCopy, TFunction<void(const TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe>&)> OnFrameReady);

    bool bInitialized;
    TAtomic<bool> bRenderCommandQueued;

    TWeakObjectPtr<UTextureRenderTarget2D> MonoTarget;
    TWeakObjectPtr<UTextureRenderTarget2D> StereoTarget;
    TWeakObjectPtr<UTextureRenderTarget2D> PreviewTarget;
    float PreviewIntervalSeconds;
    double LastPreviewSubmitSeconds;
    bool bPreviewUpdatesEnabled;
    FCriticalSection PreviewTimingCS;
};

void AddPanoramaEquirectPass(FRDGBuilder& GraphBuilder, const TArray<FRDGTextureRef>& FaceTextures, FRDGTextureRef OutputTexture, const FPanoramicVideoSettings& Settings, int32 EyeIndex);
void AddPanoramaConvertForNVENCPass(FRDGBuilder& GraphBuilder, FRDGTextureRef SourceTexture, FRDGTextureRef DestTexture, const FPanoramicVideoSettings& Settings, int32 EyeIndex, const FIntPoint& DestOffset);
