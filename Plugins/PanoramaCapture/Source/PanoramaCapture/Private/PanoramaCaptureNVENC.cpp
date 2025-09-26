#include "PanoramaCaptureNVENC.h"
#include "PanoramaCaptureFrame.h"
#include "PanoramaCaptureLog.h"
#include "PanoramaCaptureColorConversion.h"

#include "HAL/PlatformFilemanager.h"
#include "Misc/Paths.h"
#include "RHI.h"

#if PANORAMA_WITH_NVENC
#include "Windows/AllowWindowsPlatformTypes.h"
#include <d3d11.h>
#include <d3d12.h>
#include "nvEncodeAPI.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif

using namespace PanoramaCapture::Color;

FPanoramaNVENCEncoder::FNVENCAPI::FNVENCAPI()
    : bLoaded(false)
{
    FMemory::Memzero(FunctionList);
    FunctionList.version = NV_ENCODE_API_FUNCTION_LIST_VER;
    const NVENCSTATUS Status = NvEncodeAPICreateInstance(&FunctionList);
    bLoaded = (Status == NV_ENC_SUCCESS);
    if (!bLoaded)
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("NvEncodeAPICreateInstance failed with status %d"), static_cast<int32>(Status));
    }
}
#endif

FPanoramaNVENCEncoder::FPanoramaNVENCEncoder()
    : bInitialized(false)
    , Bitrate(0)
    , EncodedFrameCount(0)
    , EncodedResolution(FIntPoint::ZeroValue)
    , LastVideoPTS(0.0)
{
#if PANORAMA_WITH_NVENC
    NVENCAPI = MakeUnique<FNVENCAPI>();
#endif
}

FPanoramaNVENCEncoder::~FPanoramaNVENCEncoder()
{
    Shutdown();
}

void FPanoramaNVENCEncoder::Initialize(const FPanoramicVideoSettings& Settings, const FString& OutputDirectory)
{
    FScopeLock Lock(&CriticalSection);

    CachedSettings = Settings;
    TargetDirectory = OutputDirectory;
    EncodedFrameCount = 0;
    EncodedResolution = Settings.Resolution;
    LastVideoPTS = 0.0;
    bSupportsZeroCopy = false;
    RawVideoHandle.Reset();

    if (!TargetDirectory.IsEmpty())
    {
        IFileManager::Get().MakeDirectory(*TargetDirectory, true);
    }

    InitializeEncoderResources(Settings);

    FString RawFileName;
    if (bSupportsZeroCopy && CachedSettings.bUseHEVC)
    {
        RawFileName = TEXT("PanoramaCapture.hevc");
    }
    else if (bSupportsZeroCopy)
    {
        RawFileName = TEXT("PanoramaCapture.h264");
    }
    else
    {
        switch (CachedSettings.ColorFormat)
        {
        case EPanoramaColorFormat::NV12:
            RawFileName = TEXT("PanoramaCapture_NV12.raw");
            break;
        case EPanoramaColorFormat::P010:
            RawFileName = TEXT("PanoramaCapture_P010.raw");
            break;
        case EPanoramaColorFormat::BGRA8:
            RawFileName = TEXT("PanoramaCapture_BGRA.raw");
            break;
        default:
            RawFileName = TEXT("PanoramaCapture.raw");
            break;
        }
    }

    RawVideoPath = FPaths::Combine(TargetDirectory, RawFileName);

    if (IFileManager::Get().FileExists(*RawVideoPath))
    {
        IFileManager::Get().Delete(*RawVideoPath);
    }

    bInitialized = true;
}

void FPanoramaNVENCEncoder::Shutdown()
{
    FScopeLock Lock(&CriticalSection);
#if PANORAMA_WITH_NVENC
    if (EncoderInstance && NVENCAPI.IsValid() && NVENCAPI->bLoaded)
    {
        NVENCAPI->FunctionList.nvEncDestroyEncoder(EncoderInstance);
        EncoderInstance = nullptr;
    }
#endif
    bInitialized = false;
    CodecName.Reset();
    Bitrate = 0;
    CachedSettings = FPanoramicVideoSettings();
    TargetDirectory.Reset();
    RawVideoPath.Reset();
    RawVideoHandle.Reset();
    EncodedFrameCount = 0;
    EncodedResolution = FIntPoint::ZeroValue;
    LastVideoPTS = 0.0;
    bSupportsZeroCopy = false;
}

void FPanoramaNVENCEncoder::InitializeEncoderResources(const FPanoramicVideoSettings& Settings)
{
#if PANORAMA_WITH_NVENC
    CodecName = Settings.bUseHEVC ? TEXT("HEVC") : TEXT("H264");
    Bitrate = Settings.TargetBitrateMbps;
    UE_LOG(LogPanoramaCapture, Log, TEXT("Initializing NVENC pipeline (codec=%s bitrate=%dMbps res=%dx%d)"), *CodecName, Bitrate, Settings.Resolution.X, Settings.Resolution.Y);

    if (Settings.ColorFormat == EPanoramaColorFormat::P010 && !Settings.bUseHEVC)
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("P010 output selected without HEVC - NVENC hardware path will fall back to CPU encoding."));
    }

    if (!NVENCAPI.IsValid() || !NVENCAPI->bLoaded)
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("NVENC API not available - falling back to CPU color conversion."));
        return;
    }

    if (!GDynamicRHI)
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("DynamicRHI is null - NVENC zero-copy unavailable."));
        return;
    }

    void* NativeDevice = GDynamicRHI->RHIGetNativeDevice();
    if (!NativeDevice)
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("Failed to retrieve native RHI device for NVENC."));
        return;
    }

    const ERHIInterfaceType InterfaceType = GDynamicRHI->GetInterfaceType();

    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS OpenParams = {};
    OpenParams.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
    OpenParams.device = NativeDevice;
    OpenParams.deviceType = (InterfaceType == ERHIInterfaceType::D3D12) ? NV_ENC_DEVICE_TYPE_DIRECTX12 : NV_ENC_DEVICE_TYPE_DIRECTX;

    NVENCSTATUS Status = NVENCAPI->FunctionList.nvEncOpenEncodeSessionEx(&OpenParams, &EncoderInstance);
    if (Status != NV_ENC_SUCCESS)
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("nvEncOpenEncodeSessionEx failed (status=%d) - reverting to CPU path."), static_cast<int32>(Status));
        EncoderInstance = nullptr;
        return;
    }

    FMemory::Memzero(InitializeParams);
    FMemory::Memzero(EncoderConfig);
    InitializeParams.version = NV_ENC_INITIALIZE_PARAMS_VER;
    EncoderConfig.version = NV_ENC_CONFIG_VER;
    InitializeParams.encodeGUID = Settings.bUseHEVC ? NV_ENC_CODEC_HEVC_GUID : NV_ENC_CODEC_H264_GUID;
    NV_ENC_PRESET_GUID ChosenPreset = NV_ENC_PRESET_P4_GUID; // balanced quality
    uint32 RateControlMode = NV_ENC_PARAMS_RC_VBR;
    uint32 MultiPass = NV_ENC_TWO_PASS_QUARTER_RESOLUTION;
    uint32 EnableAQ = 1;
    uint32 EnableTemporalAQ = 0;

    switch (Settings.RateControlPreset)
    {
    case EPanoramaRateControlPreset::LowLatency:
        ChosenPreset = NV_ENC_PRESET_LOW_LATENCY_HQ_GUID;
        RateControlMode = NV_ENC_PARAMS_RC_CBR_LOWDELAY_HQ;
        MultiPass = NV_ENC_TWO_PASS_QUARTER_RESOLUTION;
        EnableAQ = 0;
        EnableTemporalAQ = 0;
        break;
    case EPanoramaRateControlPreset::HighQuality:
        ChosenPreset = Settings.bUseHEVC ? NV_ENC_PRESET_P7_GUID : NV_ENC_PRESET_P5_GUID;
        RateControlMode = NV_ENC_PARAMS_RC_2_PASS_QUALITY;
        MultiPass = NV_ENC_TWO_PASS_FULL_RESOLUTION;
        EnableAQ = 1;
        EnableTemporalAQ = 1;
        break;
    default:
        ChosenPreset = NV_ENC_PRESET_P4_GUID;
        RateControlMode = NV_ENC_PARAMS_RC_VBR;
        MultiPass = NV_ENC_TWO_PASS_QUARTER_RESOLUTION;
        EnableAQ = 1;
        EnableTemporalAQ = Settings.bUseHEVC ? 1 : 0;
        break;
    }

    InitializeParams.presetGUID = ChosenPreset;
    InitializeParams.encodeWidth = Settings.Resolution.X;
    InitializeParams.encodeHeight = Settings.Resolution.Y;
    InitializeParams.darWidth = Settings.Resolution.X;
    InitializeParams.darHeight = Settings.Resolution.Y;
    InitializeParams.frameRateNum = 60000;
    InitializeParams.frameRateDen = 1000;
    InitializeParams.enablePTD = 1;
    InitializeParams.maxEncodeWidth = Settings.Resolution.X;
    InitializeParams.maxEncodeHeight = Settings.Resolution.Y;
    InitializeParams.encodeConfig = &EncoderConfig;

    EncoderConfig.profileGUID = Settings.bUseHEVC ? NV_ENC_HEVC_PROFILE_MAIN_GUID : NV_ENC_H264_HIGH_GUID;
    EncoderConfig.gopLength = Settings.GOPLength;
    EncoderConfig.frameIntervalP = FMath::Max(1, Settings.NumBFrames + 1);
    EncoderConfig.rcParams.rateControlMode = RateControlMode;
    EncoderConfig.rcParams.multiPass = MultiPass;
    const uint32 TargetBitrate = static_cast<uint32>(Bitrate * 1000000);
    EncoderConfig.rcParams.averageBitRate = TargetBitrate;
    EncoderConfig.rcParams.maxBitRate = TargetBitrate;
    EncoderConfig.rcParams.vbvBufferSize = TargetBitrate / 8;
    EncoderConfig.rcParams.vbvInitialDelay = EncoderConfig.rcParams.vbvBufferSize;
    EncoderConfig.rcParams.enableAQ = EnableAQ;
    EncoderConfig.rcParams.enableTemporalAQ = EnableTemporalAQ;

    if (Settings.bUseHEVC)
    {
        EncoderConfig.encodeCodecConfig.hevcConfig.pixelBitDepthMinus8 = (Settings.ColorFormat == EPanoramaColorFormat::P010) ? 2 : 0;
        EncoderConfig.encodeCodecConfig.hevcConfig.chromaFormatIDC = 1; // 4:2:0
    }
    else
    {
        EncoderConfig.encodeCodecConfig.h264Config.idrPeriod = Settings.GOPLength;
        EncoderConfig.encodeCodecConfig.h264Config.enableFillerDataInsertion = 1;
    }

    Status = NVENCAPI->FunctionList.nvEncInitializeEncoder(EncoderInstance, &InitializeParams);
    if (Status != NV_ENC_SUCCESS)
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("nvEncInitializeEncoder failed (status=%d) - reverting to CPU path."), static_cast<int32>(Status));
        NVENCAPI->FunctionList.nvEncDestroyEncoder(EncoderInstance);
        EncoderInstance = nullptr;
        return;
    }

    bSupportsZeroCopy = true;
    if (Settings.ColorFormat != EPanoramaColorFormat::BGRA8)
    {
        bSupportsZeroCopy = false;
        UE_LOG(LogPanoramaCapture, Log, TEXT("NVENC zero-copy disabled for color format %d; CPU path will be used."), static_cast<int32>(Settings.ColorFormat));
    }
#else
    UE_LOG(LogPanoramaCapture, Warning, TEXT("InitializeEncoderResources called without NVENC support."));
#endif
}

bool FPanoramaNVENCEncoder::EncodeFrame(const TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe>& Frame)
{
    if (!Frame.IsValid())
    {
        return false;
    }

    FScopeLock Lock(&CriticalSection);
    if (!bInitialized)
    {
        return false;
    }

    if (bSupportsZeroCopy && Frame->NVENCTexture.IsValid())
    {
        const bool bResult = EncodeFrameZeroCopy(Frame);
        Frame->LinearPixels.Reset();
        Frame->PlanarVideo.Reset();
        return bResult;
    }

    FIntPoint OutputResolution = Frame->Resolution;
    TArray<uint8> EncodedPayload;
    if (!ConvertFrameToRawPayload(Frame, EncodedPayload, OutputResolution))
    {
        return false;
    }

    Frame->EncodedVideo = MoveTemp(EncodedPayload);
    Frame->LinearPixels.Reset();
    Frame->PlanarVideo.Reset();
    Frame->bIsStereo = false;
    Frame->Resolution = OutputResolution;
    Frame->ColorFormat = CachedSettings.ColorFormat;
    WritePacketToDisk(Frame->EncodedVideo);
    ++EncodedFrameCount;
    EncodedResolution = Frame->Resolution;
    LastVideoPTS = Frame->TimestampSeconds;
    return true;
}

TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe> FPanoramaNVENCEncoder::EncodeStereoPair(const TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe>& LeftFrame, const TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe>& RightFrame)
{
    if (!LeftFrame.IsValid() || !RightFrame.IsValid())
    {
        return nullptr;
    }

    FScopeLock Lock(&CriticalSection);
    if (!bInitialized)
    {
        return nullptr;
    }

    if (bSupportsZeroCopy && LeftFrame->NVENCTexture.IsValid())
    {
        if (EncodeFrameZeroCopy(LeftFrame))
        {
            LeftFrame->LinearPixels.Reset();
            LeftFrame->PlanarVideo.Reset();
            RightFrame->LinearPixels.Reset();
            RightFrame->PlanarVideo.Reset();
            return LeftFrame;
        }
        return nullptr;
    }

    TArray<uint8> EncodedPayload;
    FIntPoint CombinedResolution = FIntPoint::ZeroValue;
    if (!ConvertStereoToRawPayload(LeftFrame, RightFrame, EncodedPayload, CombinedResolution))
    {
        return nullptr;
    }

    LeftFrame->EncodedVideo = MoveTemp(EncodedPayload);
    LeftFrame->LinearPixels.Reset();
    LeftFrame->PlanarVideo.Reset();
    RightFrame->LinearPixels.Reset();
    RightFrame->PlanarVideo.Reset();
    LeftFrame->bIsStereo = true;
    LeftFrame->Resolution = CombinedResolution;
    LeftFrame->ColorFormat = CachedSettings.ColorFormat;
    RightFrame->ColorFormat = CachedSettings.ColorFormat;
    RightFrame->Resolution = CombinedResolution;
    LeftFrame->TimestampSeconds = FMath::Min(LeftFrame->TimestampSeconds, RightFrame->TimestampSeconds);
    WritePacketToDisk(LeftFrame->EncodedVideo);
    ++EncodedFrameCount;
    EncodedResolution = CombinedResolution;
    LastVideoPTS = LeftFrame->TimestampSeconds;
    return LeftFrame;
}

void FPanoramaNVENCEncoder::Flush()
{
    FScopeLock Lock(&CriticalSection);
#if PANORAMA_WITH_NVENC
    if (bSupportsZeroCopy && EncoderInstance && NVENCAPI.IsValid() && NVENCAPI->bLoaded)
    {
        NV_ENC_PIC_PARAMS PicParams = {};
        PicParams.version = NV_ENC_PIC_PARAMS_VER;
        PicParams.encodePicFlags = NV_ENC_PIC_FLAG_EOS;
        NVENCAPI->FunctionList.nvEncEncodePicture(EncoderInstance, &PicParams);
    }
#endif
    if (RawVideoHandle.IsValid())
    {
        RawVideoHandle->Flush();
        RawVideoHandle.Reset();
    }
}

void FPanoramaNVENCEncoder::EnsureRawFile()
{
    if (RawVideoHandle.IsValid())
    {
        return;
    }

    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    RawVideoHandle.Reset(PlatformFile.OpenWrite(*RawVideoPath, true));
    if (!RawVideoHandle.IsValid())
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("Failed to open NVENC raw output file %s"), *RawVideoPath);
    }
}

bool FPanoramaNVENCEncoder::EncodeFrameZeroCopy(const TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe>& Frame)
{
#if PANORAMA_WITH_NVENC
    if (!Frame.IsValid() || !EncoderInstance || !NVENCAPI.IsValid() || !NVENCAPI->bLoaded)
    {
        return false;
    }

    if (!Frame->NVENCTexture.IsValid())
    {
        return false;
    }

    if (CachedSettings.ColorFormat != EPanoramaColorFormat::BGRA8)
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("NVENC zero-copy requested with unsupported color format %d"), static_cast<int32>(CachedSettings.ColorFormat));
        return false;
    }

    EnsureRawFile();

    void* NativeResource = Frame->NVENCTexture->GetNativeResource();
    if (!NativeResource)
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("NVENC zero-copy submission failed: native texture resource missing."));
        return false;
    }

    const FIntPoint InputResolution = Frame->NVENCResolution;
    if (InputResolution.X <= 0 || InputResolution.Y <= 0)
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("NVENC zero-copy submission failed: invalid resolution %dx%d."), InputResolution.X, InputResolution.Y);
        return false;
    }

    const ERHIInterfaceType InterfaceType = GDynamicRHI ? GDynamicRHI->GetInterfaceType() : ERHIInterfaceType::Null;

    NV_ENC_REGISTER_RESOURCE RegisterParams = {};
    RegisterParams.version = NV_ENC_REGISTER_RESOURCE_VER;
    RegisterParams.resourceType = (InterfaceType == ERHIInterfaceType::D3D12) ? NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX12 : NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
    RegisterParams.resourceToRegister = NativeResource;
    RegisterParams.width = InputResolution.X;
    RegisterParams.height = InputResolution.Y;
    RegisterParams.pitch = 0;
    RegisterParams.bufferFormat = NV_ENC_BUFFER_FORMAT_ARGB;

    NVENCSTATUS Status = NVENCAPI->FunctionList.nvEncRegisterResource(EncoderInstance, &RegisterParams);
    if (Status != NV_ENC_SUCCESS)
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("nvEncRegisterResource failed (status=%d)"), static_cast<int32>(Status));
        return false;
    }

    NV_ENC_MAP_INPUT_RESOURCE MapParams = {};
    MapParams.version = NV_ENC_MAP_INPUT_RESOURCE_VER;
    MapParams.registeredResource = RegisterParams.registeredResource;
    Status = NVENCAPI->FunctionList.nvEncMapInputResource(EncoderInstance, &MapParams);
    if (Status != NV_ENC_SUCCESS)
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("nvEncMapInputResource failed (status=%d)"), static_cast<int32>(Status));
        NVENCAPI->FunctionList.nvEncUnregisterResource(EncoderInstance, RegisterParams.registeredResource);
        return false;
    }

    NV_ENC_CREATE_BITSTREAM_BUFFER CreateParams = {};
    CreateParams.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
    Status = NVENCAPI->FunctionList.nvEncCreateBitstreamBuffer(EncoderInstance, &CreateParams);
    if (Status != NV_ENC_SUCCESS)
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("nvEncCreateBitstreamBuffer failed (status=%d)"), static_cast<int32>(Status));
        NVENCAPI->FunctionList.nvEncUnmapInputResource(EncoderInstance, MapParams.mappedResource);
        NVENCAPI->FunctionList.nvEncUnregisterResource(EncoderInstance, RegisterParams.registeredResource);
        return false;
    }

    NV_ENC_PIC_PARAMS PicParams = {};
    PicParams.version = NV_ENC_PIC_PARAMS_VER;
    PicParams.inputBuffer = MapParams.mappedResource;
    PicParams.bufferFmt = NV_ENC_BUFFER_FORMAT_ARGB;
    PicParams.inputWidth = InputResolution.X;
    PicParams.inputHeight = InputResolution.Y;
    PicParams.outputBitstream = CreateParams.bitstreamBuffer;
    PicParams.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
    PicParams.inputTimeStamp = static_cast<uint64>(Frame->TimestampSeconds * 1000.0);

    Status = NVENCAPI->FunctionList.nvEncEncodePicture(EncoderInstance, &PicParams);
    if (Status != NV_ENC_SUCCESS && Status != NV_ENC_ERR_NEED_MORE_INPUT)
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("nvEncEncodePicture failed (status=%d)"), static_cast<int32>(Status));
        NVENCAPI->FunctionList.nvEncDestroyBitstreamBuffer(EncoderInstance, CreateParams.bitstreamBuffer);
        NVENCAPI->FunctionList.nvEncUnmapInputResource(EncoderInstance, MapParams.mappedResource);
        NVENCAPI->FunctionList.nvEncUnregisterResource(EncoderInstance, RegisterParams.registeredResource);
        return false;
    }

    NV_ENC_LOCK_BITSTREAM LockParams = {};
    LockParams.version = NV_ENC_LOCK_BITSTREAM_VER;
    LockParams.outputBitstream = CreateParams.bitstreamBuffer;
    LockParams.doNotWait = false;

    Status = NVENCAPI->FunctionList.nvEncLockBitstream(EncoderInstance, &LockParams);
    if (Status != NV_ENC_SUCCESS)
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("nvEncLockBitstream failed (status=%d)"), static_cast<int32>(Status));
        NVENCAPI->FunctionList.nvEncDestroyBitstreamBuffer(EncoderInstance, CreateParams.bitstreamBuffer);
        NVENCAPI->FunctionList.nvEncUnmapInputResource(EncoderInstance, MapParams.mappedResource);
        NVENCAPI->FunctionList.nvEncUnregisterResource(EncoderInstance, RegisterParams.registeredResource);
        return false;
    }

    Frame->EncodedVideo.SetNumUninitialized(LockParams.bitstreamSizeInBytes);
    if (LockParams.bitstreamSizeInBytes > 0 && LockParams.bitstreamBufferPtr)
    {
        FMemory::Memcpy(Frame->EncodedVideo.GetData(), LockParams.bitstreamBufferPtr, LockParams.bitstreamSizeInBytes);
    }

    NVENCAPI->FunctionList.nvEncUnlockBitstream(EncoderInstance, CreateParams.bitstreamBuffer);
    NVENCAPI->FunctionList.nvEncDestroyBitstreamBuffer(EncoderInstance, CreateParams.bitstreamBuffer);
    NVENCAPI->FunctionList.nvEncUnmapInputResource(EncoderInstance, MapParams.mappedResource);
    NVENCAPI->FunctionList.nvEncUnregisterResource(EncoderInstance, RegisterParams.registeredResource);

    WritePacketToDisk(Frame->EncodedVideo);
    ++EncodedFrameCount;
    EncodedResolution = InputResolution;
    LastVideoPTS = Frame->TimestampSeconds;
    Frame->bIsStereo = CachedSettings.CaptureMode == EPanoramaCaptureMode::Stereo;
    Frame->Resolution = InputResolution;
    Frame->ColorFormat = CachedSettings.ColorFormat;
    return Frame->EncodedVideo.Num() > 0;
#else
    UE_UNUSED(Frame);
    return false;
#endif
}

bool FPanoramaNVENCEncoder::ConvertFrameToRawPayload(const TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe>& Frame, TArray<uint8>& OutData, FIntPoint& OutResolution) const
{
    if (!Frame.IsValid())
    {
        return false;
    }

    OutResolution = Frame->Resolution;

    switch (CachedSettings.ColorFormat)
    {
    case EPanoramaColorFormat::NV12:
    {
        const int32 Width = Frame->Resolution.X;
        const int32 Height = Frame->Resolution.Y;
        const int32 ExpectedBytes = Width * Height + (Width * Height) / 2;
        if (Frame->PlanarVideo.Num() == ExpectedBytes)
        {
            OutData = Frame->PlanarVideo;
            return true;
        }
        FNV12PlaneBuffers Planes;
        if (!ConvertLinearToNV12Planes(Frame->LinearPixels, Frame->Resolution, CachedSettings.Gamma, Planes))
        {
            UE_LOG(LogPanoramaCapture, Warning, TEXT("Failed to convert frame to NV12 (resolution %dx%d)"), Frame->Resolution.X, Frame->Resolution.Y);
            return false;
        }
        CollapsePlanesToNV12(Planes, OutData);
        return true;
    }
    case EPanoramaColorFormat::P010:
    {
        const int32 Width = Frame->Resolution.X;
        const int32 Height = Frame->Resolution.Y;
        const int32 ExpectedSamples = Width * Height + (Width * Height) / 2;
        const int32 ExpectedBytes = ExpectedSamples * sizeof(uint16);
        if (Frame->PlanarVideo.Num() == ExpectedBytes)
        {
            OutData = Frame->PlanarVideo;
            return true;
        }
        FP010PlaneBuffers Planes;
        if (!ConvertLinearToP010Planes(Frame->LinearPixels, Frame->Resolution, CachedSettings.Gamma, Planes))
        {
            UE_LOG(LogPanoramaCapture, Warning, TEXT("Failed to convert frame to P010 (resolution %dx%d)"), Frame->Resolution.X, Frame->Resolution.Y);
            return false;
        }
        CollapsePlanesToP010(Planes, OutData);
        return true;
    }
    case EPanoramaColorFormat::BGRA8:
    {
        if (!ConvertLinearToBGRAPayload(Frame->LinearPixels, Frame->Resolution, CachedSettings.Gamma, OutData))
        {
            UE_LOG(LogPanoramaCapture, Warning, TEXT("Failed to convert frame to BGRA payload (resolution %dx%d)"), Frame->Resolution.X, Frame->Resolution.Y);
            return false;
        }
        return true;
    }
    default:
        break;
    }

    return false;
}

bool FPanoramaNVENCEncoder::ConvertStereoToRawPayload(const TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe>& LeftFrame, const TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe>& RightFrame, TArray<uint8>& OutData, FIntPoint& OutResolution) const
{
    if (!LeftFrame.IsValid() || !RightFrame.IsValid())
    {
        return false;
    }

    if (LeftFrame->Resolution != RightFrame->Resolution)
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("Stereo frames have mismatched resolution (%dx%d vs %dx%d)"), LeftFrame->Resolution.X, LeftFrame->Resolution.Y, RightFrame->Resolution.X, RightFrame->Resolution.Y);
        return false;
    }

    const FIntPoint BaseResolution = LeftFrame->Resolution;
    const bool bSideBySide = CachedSettings.StereoLayout == EPanoramaStereoLayout::SideBySide;
    OutResolution = bSideBySide ? FIntPoint(BaseResolution.X * 2, BaseResolution.Y) : FIntPoint(BaseResolution.X, BaseResolution.Y * 2);

    switch (CachedSettings.ColorFormat)
    {
    case EPanoramaColorFormat::NV12:
    {
        const int32 Width = BaseResolution.X;
        const int32 Height = BaseResolution.Y;
        const int32 PlaneYBytes = Width * Height;
        const int32 PlaneUVBytes = (Width * Height) / 2;

        const bool bHasPlanar = LeftFrame->PlanarVideo.Num() == (PlaneYBytes + PlaneUVBytes) && RightFrame->PlanarVideo.Num() == (PlaneYBytes + PlaneUVBytes);
        if (bHasPlanar)
        {
            const uint8* LeftY = LeftFrame->PlanarVideo.GetData();
            const uint8* LeftUV = LeftY + PlaneYBytes;
            const uint8* RightY = RightFrame->PlanarVideo.GetData();
            const uint8* RightUV = RightY + PlaneYBytes;

            if (bSideBySide)
            {
                const int32 CombinedWidth = Width * 2;
                const int32 CombinedYBytes = CombinedWidth * Height;
                const int32 CombinedUVBytes = CombinedWidth * (Height / 2);
                OutData.SetNumUninitialized(CombinedYBytes + CombinedUVBytes);

                uint8* DestY = OutData.GetData();
                for (int32 Row = 0; Row < Height; ++Row)
                {
                    uint8* DestRow = DestY + Row * CombinedWidth;
                    const uint8* LeftRow = LeftY + Row * Width;
                    const uint8* RightRow = RightY + Row * Width;
                    FMemory::Memcpy(DestRow, LeftRow, Width);
                    FMemory::Memcpy(DestRow + Width, RightRow, Width);
                }

                uint8* DestUV = DestY + CombinedYBytes;
                for (int32 Row = 0; Row < Height / 2; ++Row)
                {
                    uint8* DestRow = DestUV + Row * CombinedWidth;
                    const uint8* LeftRow = LeftUV + Row * Width;
                    const uint8* RightRow = RightUV + Row * Width;
                    FMemory::Memcpy(DestRow, LeftRow, Width);
                    FMemory::Memcpy(DestRow + Width, RightRow, Width);
                }
            }
            else
            {
                const int32 CombinedYBytes = PlaneYBytes * 2;
                const int32 CombinedUVBytes = PlaneUVBytes * 2;
                OutData.SetNumUninitialized(CombinedYBytes + CombinedUVBytes);
                uint8* DestPtr = OutData.GetData();
                FMemory::Memcpy(DestPtr, LeftY, PlaneYBytes);
                FMemory::Memcpy(DestPtr + PlaneYBytes, RightY, PlaneYBytes);
                uint8* DestUV = DestPtr + CombinedYBytes;
                FMemory::Memcpy(DestUV, LeftUV, PlaneUVBytes);
                FMemory::Memcpy(DestUV + PlaneUVBytes, RightUV, PlaneUVBytes);
            }
            return true;
        }

        FNV12PlaneBuffers LeftPlanes;
        FNV12PlaneBuffers RightPlanes;
        if (!ConvertLinearToNV12Planes(LeftFrame->LinearPixels, BaseResolution, CachedSettings.Gamma, LeftPlanes))
        {
            return false;
        }
        if (!ConvertLinearToNV12Planes(RightFrame->LinearPixels, BaseResolution, CachedSettings.Gamma, RightPlanes))
        {
            return false;
        }

        if (bSideBySide)
        {
            const int32 CombinedWidth = Width * 2;
            const int32 CombinedYBytes = CombinedWidth * Height;
            const int32 CombinedUVBytes = CombinedWidth * (Height / 2);
            OutData.SetNumUninitialized(CombinedYBytes + CombinedUVBytes);

            uint8* DestY = OutData.GetData();
            for (int32 Row = 0; Row < Height; ++Row)
            {
                uint8* DestRow = DestY + Row * CombinedWidth;
                const uint8* LeftRow = LeftPlanes.YPlane.GetData() + Row * Width;
                const uint8* RightRow = RightPlanes.YPlane.GetData() + Row * Width;
                FMemory::Memcpy(DestRow, LeftRow, Width);
                FMemory::Memcpy(DestRow + Width, RightRow, Width);
            }

            uint8* DestUV = DestY + CombinedYBytes;
            for (int32 Row = 0; Row < Height / 2; ++Row)
            {
                uint8* DestRow = DestUV + Row * CombinedWidth;
                const uint8* LeftRow = LeftPlanes.UVPlane.GetData() + Row * Width;
                const uint8* RightRow = RightPlanes.UVPlane.GetData() + Row * Width;
                FMemory::Memcpy(DestRow, LeftRow, Width);
                FMemory::Memcpy(DestRow + Width, RightRow, Width);
            }
            return true;
        }

        const int32 CombinedYBytes = (LeftPlanes.YPlane.Num() + RightPlanes.YPlane.Num());
        const int32 CombinedUVBytes = (LeftPlanes.UVPlane.Num() + RightPlanes.UVPlane.Num());
        OutData.SetNumUninitialized(CombinedYBytes + CombinedUVBytes);

        uint8* DestPtr = OutData.GetData();
        FMemory::Memcpy(DestPtr, LeftPlanes.YPlane.GetData(), LeftPlanes.YPlane.Num());
        FMemory::Memcpy(DestPtr + LeftPlanes.YPlane.Num(), RightPlanes.YPlane.GetData(), RightPlanes.YPlane.Num());

        uint8* DestUV = DestPtr + CombinedYBytes;
        FMemory::Memcpy(DestUV, LeftPlanes.UVPlane.GetData(), LeftPlanes.UVPlane.Num());
        FMemory::Memcpy(DestUV + LeftPlanes.UVPlane.Num(), RightPlanes.UVPlane.GetData(), RightPlanes.UVPlane.Num());
        return true;
    }
    case EPanoramaColorFormat::P010:
    {
        const int32 Width = BaseResolution.X;
        const int32 Height = BaseResolution.Y;
        const int32 PlaneYBytes = Width * Height * sizeof(uint16);
        const int32 PlaneUVBytes = (Width * Height / 2) * sizeof(uint16);
        const bool bHasPlanar = LeftFrame->PlanarVideo.Num() == (PlaneYBytes + PlaneUVBytes) && RightFrame->PlanarVideo.Num() == (PlaneYBytes + PlaneUVBytes);
        if (bHasPlanar)
        {
            const uint8* LeftY = LeftFrame->PlanarVideo.GetData();
            const uint8* LeftUV = LeftY + PlaneYBytes;
            const uint8* RightY = RightFrame->PlanarVideo.GetData();
            const uint8* RightUV = RightY + PlaneYBytes;

            if (bSideBySide)
            {
                const int32 CombinedWidth = Width * 2;
                const int32 CombinedYBytes = CombinedWidth * Height * sizeof(uint16);
                const int32 CombinedUVBytes = CombinedWidth * (Height / 2) * sizeof(uint16);
                OutData.SetNumUninitialized(CombinedYBytes + CombinedUVBytes);

                uint8* DestY = OutData.GetData();
                for (int32 Row = 0; Row < Height; ++Row)
                {
                    uint8* DestRow = DestY + Row * CombinedWidth * sizeof(uint16);
                    const uint8* LeftRow = LeftY + Row * Width * sizeof(uint16);
                    const uint8* RightRow = RightY + Row * Width * sizeof(uint16);
                    FMemory::Memcpy(DestRow, LeftRow, Width * sizeof(uint16));
                    FMemory::Memcpy(DestRow + Width * sizeof(uint16), RightRow, Width * sizeof(uint16));
                }

                uint8* DestUV = DestY + CombinedYBytes;
                for (int32 Row = 0; Row < Height / 2; ++Row)
                {
                    uint8* DestRow = DestUV + Row * CombinedWidth * sizeof(uint16);
                    const uint8* LeftRow = LeftUV + Row * Width * sizeof(uint16);
                    const uint8* RightRow = RightUV + Row * Width * sizeof(uint16);
                    FMemory::Memcpy(DestRow, LeftRow, Width * sizeof(uint16));
                    FMemory::Memcpy(DestRow + Width * sizeof(uint16), RightRow, Width * sizeof(uint16));
                }
            }
            else
            {
                const int32 CombinedYBytes = PlaneYBytes * 2;
                const int32 CombinedUVBytes = PlaneUVBytes * 2;
                OutData.SetNumUninitialized(CombinedYBytes + CombinedUVBytes);
                uint8* DestPtr = OutData.GetData();
                FMemory::Memcpy(DestPtr, LeftY, PlaneYBytes);
                FMemory::Memcpy(DestPtr + PlaneYBytes, RightY, PlaneYBytes);
                uint8* DestUV = DestPtr + CombinedYBytes;
                FMemory::Memcpy(DestUV, LeftUV, PlaneUVBytes);
                FMemory::Memcpy(DestUV + PlaneUVBytes, RightUV, PlaneUVBytes);
            }
            return true;
        }

        FP010PlaneBuffers LeftPlanes;
        FP010PlaneBuffers RightPlanes;
        if (!ConvertLinearToP010Planes(LeftFrame->LinearPixels, BaseResolution, CachedSettings.Gamma, LeftPlanes))
        {
            return false;
        }
        if (!ConvertLinearToP010Planes(RightFrame->LinearPixels, BaseResolution, CachedSettings.Gamma, RightPlanes))
        {
            return false;
        }

        if (bSideBySide)
        {
            const int32 CombinedWidth = Width * 2;
            const int32 CombinedYBytes = CombinedWidth * Height * sizeof(uint16);
            const int32 CombinedUVBytes = CombinedWidth * (Height / 2) * sizeof(uint16);
            OutData.SetNumUninitialized(CombinedYBytes + CombinedUVBytes);

            uint8* DestY = OutData.GetData();
            for (int32 Row = 0; Row < Height; ++Row)
            {
                uint8* DestRow = DestY + Row * CombinedWidth * sizeof(uint16);
                const uint16* LeftRow = LeftPlanes.YPlane.GetData() + Row * Width;
                const uint16* RightRow = RightPlanes.YPlane.GetData() + Row * Width;
                FMemory::Memcpy(DestRow, LeftRow, Width * sizeof(uint16));
                FMemory::Memcpy(DestRow + Width * sizeof(uint16), RightRow, Width * sizeof(uint16));
            }

            uint8* DestUV = DestY + CombinedYBytes;
            for (int32 Row = 0; Row < Height / 2; ++Row)
            {
                uint8* DestRow = DestUV + Row * CombinedWidth * sizeof(uint16);
                const uint16* LeftRow = LeftPlanes.UVPlane.GetData() + Row * Width;
                const uint16* RightRow = RightPlanes.UVPlane.GetData() + Row * Width;
                FMemory::Memcpy(DestRow, LeftRow, Width * sizeof(uint16));
                FMemory::Memcpy(DestRow + Width * sizeof(uint16), RightRow, Width * sizeof(uint16));
            }
            return true;
        }

        const int32 CombinedYElements = LeftPlanes.YPlane.Num() + RightPlanes.YPlane.Num();
        const int32 CombinedUVElements = LeftPlanes.UVPlane.Num() + RightPlanes.UVPlane.Num();
        const int32 TotalBytes = (CombinedYElements + CombinedUVElements) * sizeof(uint16);
        OutData.SetNumUninitialized(TotalBytes);

        uint8* DestPtr = OutData.GetData();
        const int32 LeftYBytes = LeftPlanes.YPlane.Num() * sizeof(uint16);
        const int32 RightYBytes = RightPlanes.YPlane.Num() * sizeof(uint16);
        FMemory::Memcpy(DestPtr, LeftPlanes.YPlane.GetData(), LeftYBytes);
        FMemory::Memcpy(DestPtr + LeftYBytes, RightPlanes.YPlane.GetData(), RightYBytes);

        uint8* DestUV = DestPtr + (LeftYBytes + RightYBytes);
        const int32 LeftUVBytes = LeftPlanes.UVPlane.Num() * sizeof(uint16);
        const int32 RightUVBytes = RightPlanes.UVPlane.Num() * sizeof(uint16);
        FMemory::Memcpy(DestUV, LeftPlanes.UVPlane.GetData(), LeftUVBytes);
        FMemory::Memcpy(DestUV + LeftUVBytes, RightPlanes.UVPlane.GetData(), RightUVBytes);
        return true;
    }
    case EPanoramaColorFormat::BGRA8:
    {
        TArray<uint8> LeftPixels;
        TArray<uint8> RightPixels;
        if (!ConvertLinearToBGRAPayload(LeftFrame->LinearPixels, BaseResolution, CachedSettings.Gamma, LeftPixels))
        {
            return false;
        }
        if (!ConvertLinearToBGRAPayload(RightFrame->LinearPixels, BaseResolution, CachedSettings.Gamma, RightPixels))
        {
            return false;
        }

        const int32 Width = BaseResolution.X;
        const int32 Height = BaseResolution.Y;
        const int32 BytesPerPixel = 4;
        if (bSideBySide)
        {
            const int32 CombinedWidth = Width * 2;
            OutData.SetNumUninitialized(CombinedWidth * Height * BytesPerPixel);
            uint8* DestPtr = OutData.GetData();
            for (int32 Row = 0; Row < Height; ++Row)
            {
                const uint8* LeftRow = LeftPixels.GetData() + Row * Width * BytesPerPixel;
                const uint8* RightRow = RightPixels.GetData() + Row * Width * BytesPerPixel;
                uint8* DestRow = DestPtr + Row * CombinedWidth * BytesPerPixel;
                FMemory::Memcpy(DestRow, LeftRow, Width * BytesPerPixel);
                FMemory::Memcpy(DestRow + Width * BytesPerPixel, RightRow, Width * BytesPerPixel);
            }
        }
        else
        {
            OutData.SetNumUninitialized(LeftPixels.Num() + RightPixels.Num());
            FMemory::Memcpy(OutData.GetData(), LeftPixels.GetData(), LeftPixels.Num());
            FMemory::Memcpy(OutData.GetData() + LeftPixels.Num(), RightPixels.GetData(), RightPixels.Num());
        }
        return true;
    }
    default:
        break;
    }

    return false;
}

void FPanoramaNVENCEncoder::WritePacketToDisk(const TArray<uint8>& PacketData)
{
    if (PacketData.Num() == 0)
    {
        return;
    }

    EnsureRawFile();
    if (RawVideoHandle.IsValid())
    {
        RawVideoHandle->Write(PacketData.GetData(), PacketData.Num());
    }
}
