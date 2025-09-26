#include "PanoramaCaptureComponent.h"
#include "PanoramaCaptureManager.h"
#include "PanoramaCaptureRenderer.h"
#include "PanoramaCaptureTypes.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "GameFramework/Actor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Scene.h"
#include "Sound/SoundSubmix.h"

namespace PanoramaCapture
{
    static const FName PreviewMeshName(TEXT("PanoramaPreviewMesh"));
    static const TArray<FVector> Directions = {
        FVector::ForwardVector,
        FVector::BackwardVector,
        FVector::RightVector,
        FVector::LeftVector,
        FVector::UpVector,
        FVector::DownVector
    };

    static FRotator DirectionToRotation(const FVector& Direction)
    {
        return Direction.Rotation();
    }
}

UPanoramaCaptureComponent::UPanoramaCaptureComponent()
    : OutputDirectory(TEXT(""))
    , PreviewMaterialTemplate(nullptr)
    , bPreviewRequested(true)
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
    VideoSettings.Resolution = FIntPoint(7680, 3840);
    CachedStatus = FPanoramicCaptureStatus();
}

void UPanoramaCaptureComponent::BeginPlay()
{
    Super::BeginPlay();

    if (!CaptureManager.IsValid())
    {
        CaptureManager = MakeShared<FPanoramaCaptureManager>();
        CaptureManager->Initialize(this, VideoSettings, AudioSettings, OutputDirectory);
        UpdatePreviewSettingsOnManager();
        CaptureManager->SetAudioSubmix(SubmixToCapture);
    }

    BindDelegates();
}

void UPanoramaCaptureComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    StopCapture();

    if (CaptureManager.IsValid())
    {
        CaptureManager->Shutdown();
        CaptureManager.Reset();
    }

    UnbindDelegates();
    DestroyCaptureRig();
    Super::EndPlay(EndPlayReason);
}

void UPanoramaCaptureComponent::OnRegister()
{
    Super::OnRegister();
    CreateCaptureRig();
    AllocateRenderTargets();
    UpdatePreviewMaterial();
}

void UPanoramaCaptureComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (CaptureManager.IsValid())
    {
        CaptureManager->Tick_GameThread(DeltaTime);
    }
}

void UPanoramaCaptureComponent::StartCapture()
{
    if (!CaptureManager.IsValid())
    {
        CaptureManager = MakeShared<FPanoramaCaptureManager>();
        CaptureManager->Initialize(this, VideoSettings, AudioSettings, OutputDirectory);
        CaptureManager->SetAudioSubmix(SubmixToCapture);
    }

    UpdatePreviewSettingsOnManager();
    CaptureManager->StartCapture();
}

void UPanoramaCaptureComponent::StopCapture()
{
    if (CaptureManager.IsValid())
    {
        CaptureManager->StopCapture();
    }
}

bool UPanoramaCaptureComponent::IsCapturing() const
{
    return CachedStatus.bIsCapturing;
}

void UPanoramaCaptureComponent::SetPreviewEnabled(bool bEnabled)
{
    bPreviewRequested = bEnabled;
    if (PreviewMeshComponent)
    {
        PreviewMeshComponent->SetVisibility(bPreviewRequested);
    }
    UpdatePreviewSettingsOnManager();
}

FPanoramicCaptureStatus UPanoramaCaptureComponent::GetCaptureStatus() const
{
    return CachedStatus;
}

int32 UPanoramaCaptureComponent::GetRingBufferCapacity() const
{
    return CaptureManager.IsValid() ? CaptureManager->GetRingBufferCapacity() : 0;
}

int32 UPanoramaCaptureComponent::GetRingBufferOccupancy() const
{
    return CaptureManager.IsValid() ? CaptureManager->GetRingBufferOccupancy() : 0;
}

void UPanoramaCaptureComponent::ReinitializeRig()
{
    DestroyCaptureRig();
    CreateCaptureRig();
    AllocateRenderTargets();
    UpdatePreviewMaterial();
    if (CaptureManager.IsValid())
    {
        UpdatePreviewSettingsOnManager();
        CaptureManager->SetAudioSubmix(SubmixToCapture);
    }
}

void UPanoramaCaptureComponent::CreateCaptureRig()
{
    DestroyCaptureRig();

    if (AActor* Owner = GetOwner())
    {
        LeftEyeFaceTargets.Reset();
        const int32 FaceResolution = FMath::Max(256, VideoSettings.Resolution.X / 4);
        for (const FVector& Direction : PanoramaCapture::Directions)
        {
            USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(Owner, USceneCaptureComponent2D::StaticClass(), NAME_None, RF_Transactional);
            Capture->AttachToComponent(this, FAttachmentTransformRules::SnapToTargetIncludingScale);
            Capture->FOVAngle = 90.f;
            Capture->ProjectionType = ECameraProjectionMode::Perspective;
            Capture->bCaptureEveryFrame = false;
            Capture->bCaptureOnMovement = false;
            Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorHDR;
            Capture->RegisterComponent();
            Capture->SetRelativeRotation(PanoramaCapture::DirectionToRotation(Direction));
            UTextureRenderTarget2D* FaceRenderTarget = NewObject<UTextureRenderTarget2D>(this);
            FaceRenderTarget->RenderTargetFormat = RTF_RGBA16f;
            FaceRenderTarget->InitAutoFormat(FaceResolution, FaceResolution);
            FaceRenderTarget->UpdateResourceImmediate(true);
            Capture->TextureTarget = FaceRenderTarget;
            LeftEyeCaptures.Add(Capture);
            LeftEyeFaceTargets.Add(FaceRenderTarget);
        }

        if (VideoSettings.CaptureMode == EPanoramaCaptureMode::Stereo)
        {
            RightEyeFaceTargets.Reset();
            for (const FVector& Direction : PanoramaCapture::Directions)
            {
                USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(Owner, USceneCaptureComponent2D::StaticClass(), NAME_None, RF_Transactional);
                Capture->AttachToComponent(this, FAttachmentTransformRules::SnapToTargetIncludingScale);
                Capture->FOVAngle = 90.f;
                Capture->ProjectionType = ECameraProjectionMode::Perspective;
                Capture->bCaptureEveryFrame = false;
                Capture->bCaptureOnMovement = false;
                Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorHDR;
                Capture->RegisterComponent();
                Capture->SetRelativeRotation(PanoramaCapture::DirectionToRotation(Direction));
                UTextureRenderTarget2D* FaceRenderTarget = NewObject<UTextureRenderTarget2D>(this);
                FaceRenderTarget->RenderTargetFormat = RTF_RGBA16f;
                FaceRenderTarget->InitAutoFormat(FaceResolution, FaceResolution);
                FaceRenderTarget->UpdateResourceImmediate(true);
                Capture->TextureTarget = FaceRenderTarget;
                RightEyeCaptures.Add(Capture);
                RightEyeFaceTargets.Add(FaceRenderTarget);
            }
        }

        if (!PreviewMeshComponent)
        {
            PreviewMeshComponent = NewObject<UStaticMeshComponent>(Owner, PanoramaCapture::PreviewMeshName);
            PreviewMeshComponent->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
            PreviewMeshComponent->RegisterComponent();
            if (UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")))
            {
                PreviewMeshComponent->SetStaticMesh(PlaneMesh);
                PreviewMeshComponent->SetRelativeScale3D(FVector(2.0f, 2.0f, 2.0f));
            }
        }
    }
}

void UPanoramaCaptureComponent::DestroyCaptureRig()
{
    for (USceneCaptureComponent2D* Capture : LeftEyeCaptures)
    {
        if (Capture)
        {
            Capture->DestroyComponent();
        }
    }
    for (USceneCaptureComponent2D* Capture : RightEyeCaptures)
    {
        if (Capture)
        {
            Capture->DestroyComponent();
        }
    }
    LeftEyeCaptures.Empty();
    LeftEyeFaceTargets.Empty();
    RightEyeCaptures.Empty();
    RightEyeFaceTargets.Empty();

    if (PreviewMeshComponent)
    {
        PreviewMeshComponent->DestroyComponent();
        PreviewMeshComponent = nullptr;
    }

    if (PreviewEquirectTarget)
    {
        PreviewEquirectTarget->ConditionalBeginDestroy();
        PreviewEquirectTarget = nullptr;
    }
}

void UPanoramaCaptureComponent::AllocateRenderTargets()
{
    if (!MonoEquirectTarget)
    {
        MonoEquirectTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("PanoramaMonoEquirect"));
    }

    MonoEquirectTarget->RenderTargetFormat = RTF_RGBA16f;
    MonoEquirectTarget->bAutoGenerateMips = false;
    MonoEquirectTarget->OverrideFormat = PF_FloatRGBA;
    MonoEquirectTarget->ClearColor = FLinearColor::Transparent;
    MonoEquirectTarget->InitAutoFormat(VideoSettings.Resolution.X, VideoSettings.Resolution.Y);
    MonoEquirectTarget->UpdateResourceImmediate(true);

    const FIntPoint PreviewResolution = GetPreviewResolution();
    if (!PreviewEquirectTarget)
    {
        PreviewEquirectTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("PanoramaPreviewEquirect"));
    }

    PreviewEquirectTarget->RenderTargetFormat = RTF_RGBA16f;
    PreviewEquirectTarget->bAutoGenerateMips = false;
    PreviewEquirectTarget->OverrideFormat = PF_FloatRGBA;
    PreviewEquirectTarget->ClearColor = FLinearColor::Transparent;
    PreviewEquirectTarget->InitAutoFormat(PreviewResolution.X, PreviewResolution.Y);
    PreviewEquirectTarget->UpdateResourceImmediate(true);

    if (VideoSettings.CaptureMode == EPanoramaCaptureMode::Stereo)
    {
        if (!RightEquirectTarget)
        {
            RightEquirectTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("PanoramaRightEquirect"));
        }

        RightEquirectTarget->RenderTargetFormat = RTF_RGBA16f;
        RightEquirectTarget->bAutoGenerateMips = false;
        RightEquirectTarget->OverrideFormat = PF_FloatRGBA;
        RightEquirectTarget->ClearColor = FLinearColor::Transparent;
        RightEquirectTarget->InitAutoFormat(VideoSettings.Resolution.X, VideoSettings.Resolution.Y);
        RightEquirectTarget->UpdateResourceImmediate(true);
    }
    else if (RightEquirectTarget)
    {
        RightEquirectTarget->ConditionalBeginDestroy();
        RightEquirectTarget = nullptr;
    }
}

void UPanoramaCaptureComponent::UpdatePreviewMaterial()
{
    if (!PreviewMeshComponent)
    {
        return;
    }

    if (PreviewMaterialTemplate)
    {
        PreviewMID = UMaterialInstanceDynamic::Create(PreviewMaterialTemplate, this);
        PreviewMeshComponent->SetMaterial(0, PreviewMID);
    }

    if (PreviewMID)
    {
        PreviewMID->SetTextureParameterValue(TEXT("PanoramaTexture"), PreviewEquirectTarget ? PreviewEquirectTarget : MonoEquirectTarget);
    }

    PreviewMeshComponent->SetVisibility(bPreviewRequested);
}

TArray<USceneCaptureComponent2D*> UPanoramaCaptureComponent::GetActiveCaptureComponents() const
{
    TArray<USceneCaptureComponent2D*> Result = LeftEyeCaptures;
    if (VideoSettings.CaptureMode == EPanoramaCaptureMode::Stereo)
    {
        Result.Append(RightEyeCaptures);
    }
    return Result;
}

void UPanoramaCaptureComponent::BindDelegates()
{
    if (!CaptureManager.IsValid())
    {
        return;
    }

    CaptureManager->OnCaptureStatusUpdated.BindUObject(this, &UPanoramaCaptureComponent::HandleStatusUpdated);
}

void UPanoramaCaptureComponent::UnbindDelegates()
{
    if (CaptureManager.IsValid())
    {
        CaptureManager->OnCaptureStatusUpdated.Unbind();
    }
}

void UPanoramaCaptureComponent::HandleStatusUpdated(const FPanoramicCaptureStatus& Status)
{
    CachedStatus = Status;
}

void UPanoramaCaptureComponent::UpdatePreviewSettingsOnManager()
{
    if (!CaptureManager.IsValid())
    {
        return;
    }

    CaptureManager->SetPreviewTargets_GameThread(MonoEquirectTarget, RightEquirectTarget, PreviewEquirectTarget, GetPreviewFrameInterval(), bPreviewRequested);
}

FIntPoint UPanoramaCaptureComponent::GetPreviewResolution() const
{
    const float Scale = FMath::Clamp(PreviewResolutionScale, 0.1f, 1.0f);
    const int32 Width = FMath::Max(8, FMath::RoundToInt(static_cast<float>(VideoSettings.Resolution.X) * Scale));
    const int32 Height = FMath::Max(4, FMath::RoundToInt(static_cast<float>(VideoSettings.Resolution.Y) * Scale));
    return FIntPoint(Width, Height);
}

float UPanoramaCaptureComponent::GetPreviewFrameInterval() const
{
    const float ClampedFPS = FMath::Clamp(PreviewMaxFPS, 5.0f, 120.0f);
    return (ClampedFPS > 0.f) ? (1.0f / ClampedFPS) : 0.f;
}
