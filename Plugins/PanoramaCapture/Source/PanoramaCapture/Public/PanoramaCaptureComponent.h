#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "PanoramaCaptureTypes.h"
#include "PanoramaCaptureComponent.generated.h"

class FPanoramaCaptureManager;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class USoundSubmix;

/** Actor component responsible for spawning the six-face capture rig and forwarding capture requests to the manager. */
UCLASS(ClassGroup = (Panorama), meta = (BlueprintSpawnableComponent))
class PANORAMACAPTURE_API UPanoramaCaptureComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPanoramaCaptureComponent();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void OnRegister() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    /** Start capture session. */
    UFUNCTION(BlueprintCallable, Category = "PanoramaCapture")
    void StartCapture();

    /** Stop capture session and flush outstanding frames. */
    UFUNCTION(BlueprintCallable, Category = "PanoramaCapture")
    void StopCapture();

    /** Returns true if capture currently active. */
    UFUNCTION(BlueprintCallable, Category = "PanoramaCapture")
    bool IsCapturing() const;

    /** Toggle preview plane visibility. */
    UFUNCTION(BlueprintCallable, Category = "PanoramaCapture")
    void SetPreviewEnabled(bool bEnabled);

    /** Provide copy of capture status. */
    UFUNCTION(BlueprintCallable, Category = "PanoramaCapture")
    FPanoramicCaptureStatus GetCaptureStatus() const;

    /** Called on UI to update ring buffer size. */
    UFUNCTION(BlueprintCallable, Category = "PanoramaCapture")
    int32 GetRingBufferCapacity() const;

    /** Called on UI to update ring buffer occupancy. */
    UFUNCTION(BlueprintCallable, Category = "PanoramaCapture")
    int32 GetRingBufferOccupancy() const;

    /** Recreate capture rig and render targets to reflect updated settings. */
    UFUNCTION(BlueprintCallable, Category = "PanoramaCapture")
    void ReinitializeRig();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PanoramaCapture")
    FPanoramicVideoSettings VideoSettings;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PanoramaCapture")
    FPanoramicAudioSettings AudioSettings;

    /** Directory root for intermediate outputs. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PanoramaCapture")
    FString OutputDirectory;

    /** Optional preview material instanced onto a plane mesh. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PanoramaCapture|Preview")
    UMaterialInterface* PreviewMaterialTemplate;

    /** Target preview frame rate to avoid saturating the editor viewport. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PanoramaCapture|Preview", meta = (ClampMin = "5.0", ClampMax = "120.0"))
    float PreviewMaxFPS = 30.0f;

    /** Fractional resolution used for the preview render target relative to the capture resolution. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PanoramaCapture|Preview", meta = (ClampMin = "0.1", ClampMax = "1.0"))
    float PreviewResolutionScale = 1.0f;

    /** Optional submix to record instead of the master output. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PanoramaCapture|Audio")
    TObjectPtr<USoundSubmix> SubmixToCapture;

    FORCEINLINE const TArray<TObjectPtr<USceneCaptureComponent2D>>& GetLeftEyeCaptureComponents() const { return LeftEyeCaptures; }
    FORCEINLINE const TArray<TObjectPtr<UTextureRenderTarget2D>>& GetLeftEyeFaceTargets() const { return LeftEyeFaceTargets; }
    FORCEINLINE const TArray<TObjectPtr<USceneCaptureComponent2D>>& GetRightEyeCaptureComponents() const { return RightEyeCaptures; }
    FORCEINLINE const TArray<TObjectPtr<UTextureRenderTarget2D>>& GetRightEyeFaceTargets() const { return RightEyeFaceTargets; }

protected:
    void CreateCaptureRig();
    void DestroyCaptureRig();
    void AllocateRenderTargets();
    void UpdatePreviewMaterial();

    TArray<USceneCaptureComponent2D*> GetActiveCaptureComponents() const;

    void BindDelegates();
    void UnbindDelegates();

private:
    UPROPERTY(Transient)
    TArray<TObjectPtr<USceneCaptureComponent2D>> LeftEyeCaptures;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextureRenderTarget2D>> LeftEyeFaceTargets;

    UPROPERTY(Transient)
    TArray<TObjectPtr<USceneCaptureComponent2D>> RightEyeCaptures;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextureRenderTarget2D>> RightEyeFaceTargets;

    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> PreviewMeshComponent;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> PreviewMID;

    UPROPERTY(Transient)
    TObjectPtr<UTextureRenderTarget2D> MonoEquirectTarget;

    UPROPERTY(Transient)
    TObjectPtr<UTextureRenderTarget2D> RightEquirectTarget;

    UPROPERTY(Transient)
    TObjectPtr<UTextureRenderTarget2D> PreviewEquirectTarget;

    TSharedPtr<FPanoramaCaptureManager> CaptureManager;

    bool bPreviewRequested;

    FPanoramicCaptureStatus CachedStatus;

    void HandleStatusUpdated(const FPanoramicCaptureStatus& Status);

    void UpdatePreviewSettingsOnManager();

    FIntPoint GetPreviewResolution() const;

    float GetPreviewFrameInterval() const;
};
