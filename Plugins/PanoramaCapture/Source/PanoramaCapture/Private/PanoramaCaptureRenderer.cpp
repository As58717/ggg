#include "PanoramaCaptureRenderer.h"
#include "PanoramaCaptureComponent.h"
#include "PanoramaCaptureFrame.h"
#include "PanoramaCaptureColorConversion.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "ShaderParameterUtils.h"
#include "ComputeShaderUtils.h"
#include "RHIStaticStates.h"
#include "Misc/App.h"
#include "HAL/PlatformTime.h"

class FPanoramaEquirectCS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FPanoramaEquirectCS);
    SHADER_USE_PARAMETER_STRUCT(FPanoramaEquirectCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FIntPoint, OutputResolution)
        SHADER_PARAMETER(int32, EyeIndex)
        SHADER_PARAMETER(int32, GammaMode)
        SHADER_PARAMETER(float, Padding)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, FacePX)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, FaceNX)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, FacePY)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, FaceNY)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, FacePZ)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, FaceNZ)
        SHADER_PARAMETER_SAMPLER(SamplerState, FaceSampler)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputTexture)
    END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FPanoramaEquirectCS, "/PanoramaCapture/PanoramaEquirectCS.usf", "MainCS", SF_Compute);

class FPanoramaConvertNVENCCS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FPanoramaConvertNVENCCS);
    SHADER_USE_PARAMETER_STRUCT(FPanoramaConvertNVENCCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FIntPoint, NVENCOutputResolution)
        SHADER_PARAMETER(FIntPoint, NVENCSourceResolution)
        SHADER_PARAMETER(int32, NVENCGammaMode)
        SHADER_PARAMETER(FIntPoint, NVENCOffset)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, NVENCSourceTexture)
        SHADER_PARAMETER_UAV(RWTexture2D<uint4>, NVENCOutputTexture)
    END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FPanoramaConvertNVENCCS, "/PanoramaCapture/PanoramaEquirectCS.usf", "ConvertToNVENCBGRA", SF_Compute);

FPanoramaCaptureRenderer::FPanoramaCaptureRenderer()
    : bInitialized(false)
    , PreviewIntervalSeconds(1.0f / 30.0f)
    , LastPreviewSubmitSeconds(0.0)
    , bPreviewUpdatesEnabled(true)
{
    bRenderCommandQueued = false;
}

FPanoramaCaptureRenderer::~FPanoramaCaptureRenderer()
{
    Shutdown();
}

void FPanoramaCaptureRenderer::Initialize()
{
    bInitialized = true;
}

void FPanoramaCaptureRenderer::Shutdown()
{
    bInitialized = false;
    bRenderCommandQueued = false;
    MonoTarget = nullptr;
    StereoTarget = nullptr;
    PreviewTarget = nullptr;
}

void FPanoramaCaptureRenderer::SetOutputTargets(UTextureRenderTarget2D* LeftTarget, UTextureRenderTarget2D* RightTarget, UTextureRenderTarget2D* InPreviewTarget, float PreviewInterval, bool bEnablePreview)
{
    MonoTarget = LeftTarget;
    StereoTarget = RightTarget;
    PreviewTarget = InPreviewTarget;
    {
        FScopeLock Lock(&PreviewTimingCS);
        PreviewIntervalSeconds = (PreviewInterval > 0.f) ? PreviewInterval : 0.f;
        bPreviewUpdatesEnabled = bEnablePreview && InPreviewTarget != nullptr;
        LastPreviewSubmitSeconds = 0.0;
    }
}

void FPanoramaCaptureRenderer::CaptureFrame(UPanoramaCaptureComponent* Component, const FPanoramicVideoSettings& VideoSettings, double CaptureStartTimeSeconds, bool bEnableNVENCZeroCopy, TFunction<void(const TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe>&)> OnFrameReady)
{
    if (!bInitialized || bRenderCommandQueued)
    {
        return;
    }

    if (!Component)
    {
        return;
    }

    bRenderCommandQueued = true;
    DispatchRenderCommand(Component, VideoSettings, CaptureStartTimeSeconds, bEnableNVENCZeroCopy, MoveTemp(OnFrameReady));
}

void FPanoramaCaptureRenderer::DispatchRenderCommand(UPanoramaCaptureComponent* Component, const FPanoramicVideoSettings& VideoSettings, double CaptureStartTimeSeconds, bool bEnableNVENCZeroCopy, TFunction<void(const TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe>&)> OnFrameReady)
{
    UTextureRenderTarget2D* MonoTextureTarget = MonoTarget.Get();
    if (!MonoTextureTarget)
    {
        bRenderCommandQueued = false;
        return;
    }

    FTextureRenderTargetResource* MonoResource = MonoTextureTarget->GameThread_GetRenderTargetResource();
    if (!MonoResource)
    {
        bRenderCommandQueued = false;
        return;
    }

    FTexture2DRHIRef MonoTargetRHI = MonoResource->GetRenderTargetTexture();

    UTextureRenderTarget2D* StereoTextureTarget = StereoTarget.Get();
    FTexture2DRHIRef StereoTargetRHI;
    if (VideoSettings.CaptureMode == EPanoramaCaptureMode::Stereo && StereoTextureTarget)
    {
        StereoTargetRHI = StereoTextureTarget->GameThread_GetRenderTargetResource()->GetRenderTargetTexture();
    }

    UTextureRenderTarget2D* PreviewTextureTarget = PreviewTarget.Get();
    FTexture2DRHIRef PreviewTargetRHI;
    if (PreviewTextureTarget)
    {
        if (FTextureRenderTargetResource* PreviewResource = PreviewTextureTarget->GameThread_GetRenderTargetResource())
        {
            PreviewTargetRHI = PreviewResource->GetRenderTargetTexture();
        }
    }

    // Update all scene capture components before submitting render command.
    for (USceneCaptureComponent2D* CaptureComp : Component->GetLeftEyeCaptureComponents())
    {
        if (CaptureComp)
        {
            CaptureComp->CaptureScene();
        }
    }

    if (VideoSettings.CaptureMode == EPanoramaCaptureMode::Stereo)
    {
        for (USceneCaptureComponent2D* CaptureComp : Component->GetRightEyeCaptureComponents())
        {
            if (CaptureComp)
            {
                CaptureComp->CaptureScene();
            }
        }
    }

    TArray<FTexture2DRHIRef, TInlineAllocator<6>> LeftFaceTextures;
    for (UTextureRenderTarget2D* FaceRT : Component->GetLeftEyeFaceTargets())
    {
        if (FaceRT)
        {
            FTextureRenderTargetResource* FaceResource = FaceRT->GameThread_GetRenderTargetResource();
            if (FaceResource)
            {
                LeftFaceTextures.Add(FaceResource->GetRenderTargetTexture());
            }
        }
    }

    TArray<FTexture2DRHIRef, TInlineAllocator<6>> RightFaceTextures;
    if (VideoSettings.CaptureMode == EPanoramaCaptureMode::Stereo)
    {
        for (UTextureRenderTarget2D* FaceRT : Component->GetRightEyeFaceTargets())
        {
            if (FaceRT)
            {
                FTextureRenderTargetResource* FaceResource = FaceRT->GameThread_GetRenderTargetResource();
                if (FaceResource)
                {
                    RightFaceTextures.Add(FaceResource->GetRenderTargetTexture());
                }
            }
        }
    }

    const double Timestamp = FPlatformTime::Seconds() - CaptureStartTimeSeconds;

    bool bLocalPreviewEnabled = false;
    {
        FScopeLock Lock(&PreviewTimingCS);
        bLocalPreviewEnabled = bPreviewUpdatesEnabled;
    }

    ENQUEUE_RENDER_COMMAND(DispatchPanoramaEquirect)([this, VideoSettings, MonoTargetRHI, StereoTargetRHI, PreviewTargetRHI, LeftFaceTextures, RightFaceTextures, Timestamp, bEnableNVENCZeroCopy, Callback = MoveTemp(OnFrameReady), bLocalPreviewEnabled](FRHICommandListImmediate& RHICmdList) mutable
    {
        if (!MonoTargetRHI.IsValid())
        {
            bRenderCommandQueued = false;
            return;
        }

        FRDGBuilder GraphBuilder(RHICmdList);

        auto RegisterFaceTextures = [&GraphBuilder](const TArray<FTexture2DRHIRef, TInlineAllocator<6>>& FaceSources)
        {
            TArray<FRDGTextureRef, TInlineAllocator<6>> RDGTextures;
            RDGTextures.Reserve(FaceSources.Num());
            for (int32 Index = 0; Index < FaceSources.Num(); ++Index)
            {
                if (FaceSources[Index].IsValid())
                {
                    RDGTextures.Add(GraphBuilder.RegisterExternalTexture(CreateRenderTarget(FaceSources[Index], *FString::Printf(TEXT("PanoramaFace_%d"), Index))));
                }
            }
            return RDGTextures;
        };

        const TArray<FRDGTextureRef, TInlineAllocator<6>> LeftRDG = RegisterFaceTextures(LeftFaceTextures);
        FRDGTextureRef OutputLeft = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(MonoTargetRHI, TEXT("PanoramaEquirectLeft")));
        const bool bWantsZeroCopyBGRA = bEnableNVENCZeroCopy && VideoSettings.OutputFormat == EPanoramaOutputFormat::NVENC && VideoSettings.ColorFormat == EPanoramaColorFormat::BGRA8;
        FRDGTextureRef NVENCCombined = nullptr;
        if (bWantsZeroCopyBGRA && OutputLeft)
        {
            const FIntPoint BaseExtent = OutputLeft->Desc.Extent;
            if (BaseExtent.X > 0 && BaseExtent.Y > 0)
            {
                const bool bStereo = VideoSettings.CaptureMode == EPanoramaCaptureMode::Stereo;
                const bool bSideBySide = bStereo && VideoSettings.StereoLayout == EPanoramaStereoLayout::SideBySide;
                const int32 CombinedWidth = bSideBySide ? BaseExtent.X * 2 : BaseExtent.X;
                const int32 CombinedHeight = bSideBySide ? BaseExtent.Y : BaseExtent.Y * (bStereo ? 2 : 1);
                const FIntPoint CombinedExtent(CombinedWidth, CombinedHeight);
                FRDGTextureDesc NVENCDesc = FRDGTextureDesc::Create2D(CombinedExtent, PF_B8G8R8A8, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
                NVENCCombined = GraphBuilder.CreateTexture(NVENCDesc, TEXT("PanoramaNVENCBGRA"));
            }
        }
        if (LeftRDG.Num() == 6)
        {
            AddPanoramaEquirectPass(GraphBuilder, LeftRDG, OutputLeft, VideoSettings, 0);
            if (NVENCCombined)
            {
                AddPanoramaConvertForNVENCPass(GraphBuilder, OutputLeft, NVENCCombined, VideoSettings, 0, FIntPoint::ZeroValue);
            }
        }

        FRDGTextureRef OutputRight = nullptr;
        if (VideoSettings.CaptureMode == EPanoramaCaptureMode::Stereo && StereoTargetRHI.IsValid())
        {
            const TArray<FRDGTextureRef, TInlineAllocator<6>> RightRDG = RegisterFaceTextures(RightFaceTextures);
            OutputRight = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(StereoTargetRHI, TEXT("PanoramaEquirectRight")));
            if (RightRDG.Num() == 6)
            {
                AddPanoramaEquirectPass(GraphBuilder, RightRDG, OutputRight, VideoSettings, 1);
                if (NVENCCombined && OutputLeft)
                {
                    const bool bSideBySide = VideoSettings.StereoLayout == EPanoramaStereoLayout::SideBySide;
                    const FIntPoint LeftExtent = OutputLeft->Desc.Extent;
                    const FIntPoint DestOffset = bSideBySide ? FIntPoint(LeftExtent.X, 0) : FIntPoint(0, LeftExtent.Y);
                    AddPanoramaConvertForNVENCPass(GraphBuilder, OutputRight, NVENCCombined, VideoSettings, 1, DestOffset);
                }
            }
        }

        FTexture2DRHIRef NVENCCombinedRHI;
        if (NVENCCombined)
        {
            GraphBuilder.QueueTextureExtraction(NVENCCombined, NVENCCombinedRHI);
        }
        GraphBuilder.Execute();

        if (PreviewTargetRHI.IsValid())
        {
            bool bShouldUpdatePreview = false;
            const double CurrentSeconds = FPlatformTime::Seconds();
            {
                FScopeLock Lock(&PreviewTimingCS);
                if (bPreviewUpdatesEnabled && bLocalPreviewEnabled)
                {
                    if (PreviewIntervalSeconds <= 0.f)
                    {
                        bShouldUpdatePreview = true;
                        LastPreviewSubmitSeconds = CurrentSeconds;
                    }
                    else if (CurrentSeconds - LastPreviewSubmitSeconds >= static_cast<double>(PreviewIntervalSeconds))
                    {
                        bShouldUpdatePreview = true;
                        LastPreviewSubmitSeconds = CurrentSeconds;
                    }
                }
            }

            if (bShouldUpdatePreview && MonoTargetRHI.IsValid())
            {
                FRHICopyTextureInfo CopyInfo;
                CopyInfo.Size = FIntVector(PreviewTargetRHI->GetSizeX(), PreviewTargetRHI->GetSizeY(), 1);
                RHICmdList.CopyTexture(MonoTargetRHI.GetReference(), PreviewTargetRHI.GetReference(), CopyInfo);
            }
        }

        auto ReadLinearPixels = [&RHICmdList](const FTexture2DRHIRef& SourceTexture, TArray<FFloat16Color>& OutPixels)
        {
            OutPixels.Reset();
            if (!SourceTexture.IsValid())
            {
                return;
            }

            const FIntPoint Size(SourceTexture->GetSizeX(), SourceTexture->GetSizeY());
            if (Size.X <= 0 || Size.Y <= 0)
            {
                return;
            }

            const FIntRect ReadRect(0, 0, Size.X, Size.Y);
            RHICmdList.ReadSurfaceFloatData(SourceTexture, ReadRect, OutPixels, CubeFace_MAX, 0, 0);
        };

        auto PopulatePlanarPayload = [&VideoSettings](const TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe>& Frame)
        {
            if (!Frame.IsValid())
            {
                return;
            }

            if (VideoSettings.OutputFormat != EPanoramaOutputFormat::NVENC)
            {
                return;
            }

            switch (VideoSettings.ColorFormat)
            {
            case EPanoramaColorFormat::NV12:
            {
                PanoramaCapture::Color::FNV12PlaneBuffers Planes;
                if (PanoramaCapture::Color::ConvertLinearToNV12Planes(Frame->LinearPixels, Frame->Resolution, VideoSettings.Gamma, Planes))
                {
                    PanoramaCapture::Color::CollapsePlanesToNV12(Planes, Frame->PlanarVideo);
                }
                break;
            }
            case EPanoramaColorFormat::P010:
            {
                PanoramaCapture::Color::FP010PlaneBuffers Planes;
                if (PanoramaCapture::Color::ConvertLinearToP010Planes(Frame->LinearPixels, Frame->Resolution, VideoSettings.Gamma, Planes))
                {
                    PanoramaCapture::Color::CollapsePlanesToP010(Planes, Frame->PlanarVideo);
                }
                break;
            }
            default:
                break;
            }

            if (Frame->PlanarVideo.Num() > 0)
            {
                Frame->LinearPixels.Reset();
            }
        };

        if (Callback)
        {
            TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe> LeftFrame = MakeShared<FPanoramaFrame, ESPMode::ThreadSafe>();
            LeftFrame->EyeIndex = 0;
            LeftFrame->TimestampSeconds = Timestamp;
            LeftFrame->Format = MonoTargetRHI->GetFormat();
            LeftFrame->bIsStereo = VideoSettings.CaptureMode == EPanoramaCaptureMode::Stereo;
            LeftFrame->Texture = MonoTargetRHI;
            LeftFrame->Resolution = FIntPoint(MonoTargetRHI->GetSizeX(), MonoTargetRHI->GetSizeY());
            LeftFrame->ColorFormat = VideoSettings.ColorFormat;
            if (VideoSettings.OutputFormat == EPanoramaOutputFormat::PNGSequence || !bWantsZeroCopyBGRA || !NVENCCombinedRHI.IsValid())
            {
                ReadLinearPixels(MonoTargetRHI, LeftFrame->LinearPixels);
            }
            if (!bWantsZeroCopyBGRA)
            {
                PopulatePlanarPayload(LeftFrame);
            }
            LeftFrame->NVENCTexture = (bWantsZeroCopyBGRA && NVENCCombinedRHI.IsValid()) ? NVENCCombinedRHI : nullptr;
            LeftFrame->NVENCResolution = (bWantsZeroCopyBGRA && NVENCCombinedRHI.IsValid()) ? FIntPoint(NVENCCombinedRHI->GetSizeX(), NVENCCombinedRHI->GetSizeY()) : LeftFrame->Resolution;
            Callback(LeftFrame);

            if (VideoSettings.CaptureMode == EPanoramaCaptureMode::Stereo && OutputRight)
            {
                TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe> RightFrame = MakeShared<FPanoramaFrame, ESPMode::ThreadSafe>();
                RightFrame->EyeIndex = 1;
                RightFrame->TimestampSeconds = Timestamp;
                RightFrame->Format = StereoTargetRHI.IsValid() ? StereoTargetRHI->GetFormat() : PF_FloatRGBA;
                RightFrame->bIsStereo = true;
                RightFrame->Texture = StereoTargetRHI;
                RightFrame->Resolution = StereoTargetRHI.IsValid() ? FIntPoint(StereoTargetRHI->GetSizeX(), StereoTargetRHI->GetSizeY()) : LeftFrame->Resolution;
                RightFrame->ColorFormat = VideoSettings.ColorFormat;
                if (VideoSettings.OutputFormat == EPanoramaOutputFormat::PNGSequence || !bWantsZeroCopyBGRA || !NVENCCombinedRHI.IsValid())
                {
                    ReadLinearPixels(StereoTargetRHI, RightFrame->LinearPixels);
                }
                if (!bWantsZeroCopyBGRA)
                {
                    PopulatePlanarPayload(RightFrame);
                }
                Callback(RightFrame);
            }
        }

        bRenderCommandQueued = false;
    });
}

void AddPanoramaEquirectPass(FRDGBuilder& GraphBuilder, const TArray<FRDGTextureRef>& FaceTextures, FRDGTextureRef OutputTexture, const FPanoramicVideoSettings& Settings, int32 EyeIndex)
{
    if (FaceTextures.Num() < 6 || OutputTexture == nullptr)
    {
        return;
    }

    FPanoramaEquirectCS::FParameters* Parameters = GraphBuilder.AllocParameters<FPanoramaEquirectCS::FParameters>();
    Parameters->OutputResolution = OutputTexture->Desc.Extent;
    Parameters->EyeIndex = EyeIndex;
    Parameters->GammaMode = static_cast<int32>(Settings.Gamma);
    const float MaxExtent = static_cast<float>(FMath::Max(OutputTexture->Desc.Extent.X, OutputTexture->Desc.Extent.Y));
    const float SeamFix = (MaxExtent > 0.f) ? FMath::Clamp(Settings.SeamFixTexels / MaxExtent, 0.0f, 0.25f) : 0.0f;
    Parameters->Padding = SeamFix;
    Parameters->FacePX = FaceTextures[0];
    Parameters->FaceNX = FaceTextures[1];
    Parameters->FacePY = FaceTextures[2];
    Parameters->FaceNY = FaceTextures[3];
    Parameters->FacePZ = FaceTextures[4];
    Parameters->FaceNZ = FaceTextures[5];
    Parameters->FaceSampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
    Parameters->OutputTexture = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(OutputTexture));

    TShaderMapRef<FPanoramaEquirectCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

    FIntVector GroupSize;
    GroupSize.X = FMath::DivideAndRoundUp(OutputTexture->Desc.Extent.X, 8);
    GroupSize.Y = FMath::DivideAndRoundUp(OutputTexture->Desc.Extent.Y, 8);
    GroupSize.Z = 1;

    FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("PanoramaEquirect_Eye%d", EyeIndex), ComputeShader, Parameters, GroupSize);
}

void AddPanoramaConvertForNVENCPass(FRDGBuilder& GraphBuilder, FRDGTextureRef SourceTexture, FRDGTextureRef DestTexture, const FPanoramicVideoSettings& Settings, int32 EyeIndex, const FIntPoint& DestOffset)
{
    if (SourceTexture == nullptr || DestTexture == nullptr)
    {
        return;
    }

    FPanoramaConvertNVENCCS::FParameters* Parameters = GraphBuilder.AllocParameters<FPanoramaConvertNVENCCS::FParameters>();
    Parameters->NVENCOutputResolution = DestTexture->Desc.Extent;
    Parameters->NVENCSourceResolution = SourceTexture->Desc.Extent;
    Parameters->NVENCGammaMode = static_cast<int32>(Settings.Gamma);
    Parameters->NVENCOffset = DestOffset;
    Parameters->NVENCSourceTexture = SourceTexture;
    Parameters->NVENCOutputTexture = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(DestTexture));

    const FIntPoint SourceExtent = SourceTexture->Desc.Extent;
    FIntVector GroupSize;
    GroupSize.X = FMath::DivideAndRoundUp(SourceExtent.X, 8);
    GroupSize.Y = FMath::DivideAndRoundUp(SourceExtent.Y, 8);
    GroupSize.Z = 1;

    TShaderMapRef<FPanoramaConvertNVENCCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
    FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("PanoramaNVENCConvert_Eye%d", EyeIndex), ComputeShader, Parameters, GroupSize);
}
