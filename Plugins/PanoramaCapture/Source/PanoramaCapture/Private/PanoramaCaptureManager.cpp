#include "PanoramaCaptureManager.h"
#include "PanoramaCaptureComponent.h"
#include "PanoramaCaptureRenderer.h"
#include "PanoramaCaptureAudio.h"
#include "PanoramaCaptureFFmpeg.h"
#include "PanoramaCaptureNVENC.h"
#include "PanoramaCaptureFrame.h"
#include "PanoramaCaptureLog.h"
#include "Async/Async.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/ThreadSafeCounter.h"
#include "Modules/ModuleManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"

namespace
{
    static constexpr TCHAR const* GFrameSubdirectory = TEXT("Frames");
}

class FPanoramaCaptureManager::FFrameProcessor : public FRunnable
{
public:
    explicit FFrameProcessor(FPanoramaCaptureManager& InOwner)
        : Owner(InOwner)
        , WorkEvent(FPlatformProcess::GetSynchEventFromPool(false))
    {
    }

    virtual ~FFrameProcessor()
    {
        if (WorkEvent)
        {
            FPlatformProcess::ReturnSynchEventToPool(WorkEvent);
            WorkEvent = nullptr;
        }
    }

    virtual uint32 Run() override
    {
        while (!StopTaskCounter.GetValue())
        {
            WorkEvent->Wait();
            Owner.ProcessPendingFrames_Worker();
        }
        return 0;
    }

    virtual void Stop() override
    {
        StopTaskCounter.Increment();
        if (WorkEvent)
        {
            WorkEvent->Trigger();
        }
    }

    void SignalWork()
    {
        if (WorkEvent)
        {
            WorkEvent->Trigger();
        }
    }

private:
    FPanoramaCaptureManager& Owner;
    FEvent* WorkEvent;
    FThreadSafeCounter StopTaskCounter;
};

FPanoramaCaptureManager::FPanoramaCaptureManager()
    : bInitialized(false)
    , bCaptureRequested(false)
    , bCaptureActive(false)
    , CaptureStartTimeSeconds(0.0)
    , FrameCounter(0)
    , PreviewFrameIntervalSeconds(1.0f / 30.0f)
    , LastPreviewUpdateSeconds(0.0)
    , bPreviewEnabled(true)
    , bHasFallenBack(false)
{
    ResetStatus();
}

FPanoramaCaptureManager::~FPanoramaCaptureManager()
{
    Shutdown();
}

void FPanoramaCaptureManager::Initialize(UPanoramaCaptureComponent* InOwnerComponent, const FPanoramicVideoSettings& VideoSettings, const FPanoramicAudioSettings& AudioSettings, const FString& OutputDirectory)
{
    if (bInitialized)
    {
        return;
    }

    CurrentVideoSettings = VideoSettings;
    CurrentAudioSettings = AudioSettings;
    TargetOutputDirectory = OutputDirectory.IsEmpty() ? FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PanoramaCaptures")) : OutputDirectory;
    OwnerComponent = InOwnerComponent;

    Renderer = MakeUnique<FPanoramaCaptureRenderer>();
    Renderer->Initialize();

    AudioRecorder = MakeUnique<FPanoramaAudioRecorder>();
    AudioRecorder->Initialize(CurrentAudioSettings, TargetOutputDirectory, InOwnerComponent ? InOwnerComponent->GetWorld() : nullptr);

    VideoEncoder = MakeUnique<FPanoramaNVENCEncoder>();
    VideoEncoder->Initialize(CurrentVideoSettings, TargetOutputDirectory);

    Muxer = MakeUnique<FPanoramaFFmpegMuxer>();
    Muxer->Initialize(TargetOutputDirectory);
    Muxer->Configure(CurrentVideoSettings, CurrentAudioSettings);

    ResetStatus();
    bInitialized = true;
}

void FPanoramaCaptureManager::Shutdown()
{
    StopWorkers();

    if (AudioRecorder)
    {
        AudioRecorder->Shutdown();
        AudioRecorder.Reset();
    }

    if (VideoEncoder)
    {
        VideoEncoder->Shutdown();
        VideoEncoder.Reset();
    }

    if (Muxer)
    {
        Muxer->Shutdown();
        Muxer.Reset();
    }

    if (Renderer)
    {
        Renderer->Shutdown();
        Renderer.Reset();
    }

    FrameQueue.Reset();
    bInitialized = false;
}

void FPanoramaCaptureManager::StartCapture()
{
    if (!bInitialized)
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("StartCapture called before Initialize"));
        return;
    }

    if (bCaptureActive)
    {
        return;
    }

    LastWarningMessage.Reset();
    bHasFallenBack = false;
    PerformPreflightChecks();

    bCaptureRequested = true;
    bCaptureActive = true;
    FrameCounter = 0;
    PendingLeftFrame.Reset();
    PendingNVENCLeftFrame.Reset();
    FrameQueue.Reset();
    CaptureStartTimeSeconds = FPlatformTime::Seconds();
    ResetStatus();
    if (Muxer)
    {
        Muxer->Configure(CurrentVideoSettings, CurrentAudioSettings);
    }
    StartWorkers();
    {
        FScopeLock Lock(&StatusCriticalSection);
        CachedStatus.bIsCapturing = true;
        CachedStatus.CurrentCaptureTimeSeconds = 0.f;
        CachedStatus.bUsingNVENC = CurrentVideoSettings.OutputFormat == EPanoramaOutputFormat::NVENC;
        CachedStatus.EffectiveVideoSettings = CurrentVideoSettings;
    }

    if (AudioRecorder)
    {
        AudioRecorder->SetCaptureStartTime(CaptureStartTimeSeconds);
        AudioRecorder->StartRecording();
    }

    {
        FScopeLock Lock(&StatusCriticalSection);
        CachedStatus.PendingFrameCount = 0;
        CachedStatus.DroppedFrames = 0;
    }
}

void FPanoramaCaptureManager::StopCapture()
{
    if (!bCaptureActive)
    {
        return;
    }

    bCaptureRequested = false;
    bCaptureActive = false;

    if (AudioRecorder)
    {
        AudioRecorder->StopRecording();
        if (Muxer)
        {
            TArray<FPanoramaAudioPacket> FinalPackets;
            AudioRecorder->ConsumeAudioPackets(FinalPackets);
            for (const FPanoramaAudioPacket& Packet : FinalPackets)
            {
                if (Packet.PCMData.Num() > 0)
                {
                    Muxer->AddAudioSamples(Packet);
                    UpdateStatusAfterAudioPacket(Packet);
                }
            }
        }
        AudioRecorder->FinalizeWaveFile();
        const FString AudioPath = AudioRecorder->GetWaveFilePath();
        if (Muxer && !AudioPath.IsEmpty() && FPaths::FileExists(AudioPath))
        {
            Muxer->SetAudioSource(AudioPath, AudioRecorder->GetRecordingDurationSeconds());
        }
    }

    StopWorkers();

    PendingLeftFrame.Reset();
    PendingNVENCLeftFrame.Reset();

    if (VideoEncoder)
    {
        VideoEncoder->Flush();
        if (CurrentVideoSettings.OutputFormat == EPanoramaOutputFormat::NVENC && Muxer)
        {
            const bool bStereo = CurrentVideoSettings.CaptureMode == EPanoramaCaptureMode::Stereo;
            const bool bEncodedStream = VideoEncoder->SupportsZeroCopy();
            Muxer->SetNVENCVideoSource(VideoEncoder->GetRawVideoPath(), VideoEncoder->GetEncodedResolution(), VideoEncoder->GetEncodedFrameCount(), VideoEncoder->IsUsingHEVC(), bStereo, bEncodedStream);
        }
    }

    if (Muxer)
    {
        Muxer->FinalizeContainer();
    }

    {
        FScopeLock Lock(&StatusCriticalSection);
        CachedStatus.bIsCapturing = false;
    }
    NotifyStatus_GameThread();
}

void FPanoramaCaptureManager::EnqueueFrame_RenderThread(const TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe>& Frame)
{
    if (!Frame.IsValid())
    {
        return;
    }

    if (!FrameQueue.Enqueue(Frame))
    {
        FScopeLock Lock(&StatusCriticalSection);
        CachedStatus.DroppedFrames++;
    }
    else if (FrameProcessor.IsValid())
    {
        FrameProcessor->SignalWork();
    }
}

FPanoramicCaptureStatus FPanoramaCaptureManager::GetStatus() const
{
    FScopeLock Lock(&StatusCriticalSection);
    return CachedStatus;
}

int32 FPanoramaCaptureManager::GetRingBufferCapacity() const
{
    return FrameQueue.GetCapacity();
}

int32 FPanoramaCaptureManager::GetRingBufferOccupancy() const
{
    return FrameQueue.Num();
}

void FPanoramaCaptureManager::Tick_GameThread(float DeltaTime)
{
    if (!bCaptureActive)
    {
        return;
    }

    {
        FScopeLock Lock(&StatusCriticalSection);
        CachedStatus.CurrentCaptureTimeSeconds = static_cast<float>(FPlatformTime::Seconds() - CaptureStartTimeSeconds);
    }

    if (AudioRecorder)
    {
        AudioRecorder->Tick(DeltaTime);
        TArray<FPanoramaAudioPacket> CapturedPackets;
        AudioRecorder->ConsumeAudioPackets(CapturedPackets);
        if (Muxer)
        {
            for (const FPanoramaAudioPacket& Packet : CapturedPackets)
            {
                if (Packet.PCMData.Num() > 0)
                {
                    Muxer->AddAudioSamples(Packet);
                    UpdateStatusAfterAudioPacket(Packet);
                }
            }
        }
    }

    if (Renderer && OwnerComponent.IsValid())
    {
        const bool bZeroCopy = VideoEncoder && VideoEncoder->SupportsZeroCopy();
        Renderer->CaptureFrame(OwnerComponent.Get(), CurrentVideoSettings, CaptureStartTimeSeconds, bZeroCopy, [this](const TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe>& Frame)
        {
            EnqueueFrame_RenderThread(Frame);
        });
    }

    if (!FrameProcessor.IsValid())
    {
        ProcessPendingFrames();
    }
    NotifyStatus_GameThread();

    {
        FScopeLock Lock(&StatusCriticalSection);
        CachedStatus.CurrentCaptureTimeSeconds += DeltaTime;
    }
}

void FPanoramaCaptureManager::SetPreviewTargets_GameThread(UTextureRenderTarget2D* MonoTarget, UTextureRenderTarget2D* RightTarget, UTextureRenderTarget2D* PreviewTarget, float InPreviewInterval, bool bInPreviewEnabled)
{
    MonoTargetWeak = MonoTarget;
    StereoTargetWeak = RightTarget;
    PreviewTargetWeak = PreviewTarget;
    PreviewFrameIntervalSeconds = (InPreviewInterval > 0.f) ? InPreviewInterval : 0.f;
    bPreviewEnabled = bInPreviewEnabled;
    LastPreviewUpdateSeconds = 0.0;

    if (Renderer)
    {
        Renderer->SetOutputTargets(MonoTarget, RightTarget, PreviewTarget, PreviewFrameIntervalSeconds, bPreviewEnabled);
    }
}

void FPanoramaCaptureManager::SetAudioSubmix(USoundSubmix* Submix)
{
    if (AudioRecorder)
    {
        AudioRecorder->SetSubmixToRecord(Submix);
    }
}

void FPanoramaCaptureManager::StartWorkers()
{
    if (FrameProcessor.IsValid())
    {
        return;
    }

    FrameProcessor = MakeUnique<FFrameProcessor>(*this);
    FrameProcessorThread.Reset(FRunnableThread::Create(FrameProcessor.Get(), TEXT("PanoramaFrameProcessor"), 0, TPri_AboveNormal));
    if (!FrameProcessorThread.IsValid())
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("Failed to spawn frame processor thread - falling back to game thread processing"));
        FrameProcessor.Reset();
    }
}

void FPanoramaCaptureManager::StopWorkers()
{
    if (!FrameProcessor.IsValid())
    {
        return;
    }

    FrameProcessor->Stop();
    if (FrameProcessorThread.IsValid())
    {
        FrameProcessorThread->WaitForCompletion();
        FrameProcessorThread.Reset();
    }
    FrameProcessor.Reset();
}

void FPanoramaCaptureManager::ProcessPendingFrames()
{
    if (!VideoEncoder || !Muxer)
    {
        return;
    }

    TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe> Frame;
    while ((Frame = FrameQueue.Dequeue()).IsValid())
    {
        if (CurrentVideoSettings.OutputFormat == EPanoramaOutputFormat::PNGSequence)
        {
            HandlePNGFrame(Frame);
        }
        else
        {
            HandleNVENCFrame(Frame);
        }
        {
            FScopeLock Lock(&StatusCriticalSection);
            CachedStatus.PendingFrameCount = FrameQueue.Num();
        }
    }
}

void FPanoramaCaptureManager::ProcessPendingFrames_Worker()
{
    if (!VideoEncoder || !Muxer)
    {
        return;
    }

    TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe> Frame;
    while ((Frame = FrameQueue.Dequeue()).IsValid())
    {
        if (CurrentVideoSettings.OutputFormat == EPanoramaOutputFormat::PNGSequence)
        {
            HandlePNGFrame(Frame);
        }
        else
        {
            HandleNVENCFrame(Frame);
        }

        UpdateStatusAfterVideoFrame(nullptr);
    }
}

bool FPanoramaCaptureManager::HandlePNGFrame(const TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe>& Frame)
{
    if (!Frame.IsValid())
    {
        return false;
    }

    if (CurrentVideoSettings.CaptureMode == EPanoramaCaptureMode::Stereo && Frame->EyeIndex == 1)
    {
        if (PendingLeftFrame.IsValid())
        {
            const bool bResult = HandleStereoPNGPair(PendingLeftFrame, Frame);
            PendingLeftFrame.Reset();
            return bResult;
        }
        return false;
    }

    if (CurrentVideoSettings.CaptureMode == EPanoramaCaptureMode::Stereo)
    {
        PendingLeftFrame = Frame;
        return true;
    }

    const FString FilePath = BuildPNGFilePath(FrameCounter++);
    const bool bSuccess = SavePNGToDisk(FilePath, Frame->LinearPixels, Frame->Resolution);
    if (bSuccess)
    {
        Frame->DiskFilePath = FilePath;
        Frame->LinearPixels.Reset();
        Muxer->AddVideoFrame(Frame);
        UpdateStatusAfterVideoFrame(Frame);
    }
    return bSuccess;
}

bool FPanoramaCaptureManager::HandleStereoPNGPair(const TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe>& LeftFrame, const TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe>& RightFrame)
{
    if (!LeftFrame.IsValid() || !RightFrame.IsValid())
    {
        return false;
    }

    const FIntPoint LeftRes = LeftFrame->Resolution;
    const FIntPoint RightRes = RightFrame->Resolution;
    if (LeftRes != RightRes || LeftFrame->LinearPixels.Num() == 0 || RightFrame->LinearPixels.Num() == 0)
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("Stereo frame mismatch - skipping pair"));
        return false;
    }

    const bool bSideBySide = CurrentVideoSettings.StereoLayout == EPanoramaStereoLayout::SideBySide;
    TArray<FFloat16Color> Combined;
    FIntPoint CombinedRes;
    if (bSideBySide)
    {
        CombinedRes = FIntPoint(LeftRes.X * 2, LeftRes.Y);
        Combined.SetNumUninitialized(CombinedRes.X * CombinedRes.Y);
        for (int32 Row = 0; Row < LeftRes.Y; ++Row)
        {
            const int32 LeftRowIndex = Row * LeftRes.X;
            const int32 RightRowIndex = Row * RightRes.X;
            FFloat16Color* DestRow = Combined.GetData() + Row * CombinedRes.X;
            FMemory::Memcpy(DestRow, LeftFrame->LinearPixels.GetData() + LeftRowIndex, sizeof(FFloat16Color) * LeftRes.X);
            FMemory::Memcpy(DestRow + LeftRes.X, RightFrame->LinearPixels.GetData() + RightRowIndex, sizeof(FFloat16Color) * RightRes.X);
        }
    }
    else
    {
        CombinedRes = FIntPoint(LeftRes.X, LeftRes.Y * 2);
        const int32 TotalPixels = LeftFrame->LinearPixels.Num() + RightFrame->LinearPixels.Num();
        Combined.Reserve(TotalPixels);
        Combined.Append(LeftFrame->LinearPixels);
        Combined.Append(RightFrame->LinearPixels);
    }

    const FString FilePath = BuildPNGFilePath(FrameCounter++);
    const bool bSuccess = SavePNGToDisk(FilePath, Combined, CombinedRes);
    if (bSuccess)
    {
        LeftFrame->DiskFilePath = FilePath;
        LeftFrame->LinearPixels.Reset();
        LeftFrame->Resolution = CombinedRes;
        LeftFrame->bIsStereo = true;
        Muxer->AddVideoFrame(LeftFrame);
        UpdateStatusAfterVideoFrame(LeftFrame);
    }
    RightFrame->LinearPixels.Reset();
    return bSuccess;
}

bool FPanoramaCaptureManager::SavePNGToDisk(const FString& Filename, const TArray<FFloat16Color>& Pixels, const FIntPoint& Resolution)
{
    if (Pixels.Num() == 0 || Resolution.X <= 0 || Resolution.Y <= 0)
    {
        return false;
    }

    const int32 ExpectedPixels = Resolution.X * Resolution.Y;
    if (ExpectedPixels != Pixels.Num())
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("PNG save aborted due to mismatched pixel count %d vs expected %d"), Pixels.Num(), ExpectedPixels);
        return false;
    }

    IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    TSharedPtr<IImageWrapper> ImageWriter = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
    if (!ImageWriter.IsValid())
    {
        return false;
    }

    TArray<uint16> RawBuffer;
    RawBuffer.SetNum(ExpectedPixels * 4);

    for (int32 PixelIndex = 0; PixelIndex < ExpectedPixels; ++PixelIndex)
    {
        const FFloat16Color& Source = Pixels[PixelIndex];
        const int32 BaseIndex = PixelIndex * 4;
        RawBuffer[BaseIndex + 0] = static_cast<uint16>(FMath::Clamp(Source.R.GetFloat() * 65535.0f, 0.0f, 65535.0f));
        RawBuffer[BaseIndex + 1] = static_cast<uint16>(FMath::Clamp(Source.G.GetFloat() * 65535.0f, 0.0f, 65535.0f));
        RawBuffer[BaseIndex + 2] = static_cast<uint16>(FMath::Clamp(Source.B.GetFloat() * 65535.0f, 0.0f, 65535.0f));
        RawBuffer[BaseIndex + 3] = static_cast<uint16>(FMath::Clamp(Source.A.GetFloat() * 65535.0f, 0.0f, 65535.0f));
    }

    if (!ImageWriter->SetRaw(RawBuffer.GetData(), RawBuffer.Num() * sizeof(uint16), Resolution.X, Resolution.Y, ERGBFormat::RGBA, 16))
    {
        return false;
    }

    const TArray64<uint8>& Compressed = ImageWriter->GetCompressed(0);
    if (Compressed.Num() == 0)
    {
        return false;
    }

    const FString Directory = FPaths::GetPath(Filename);
    IFileManager::Get().MakeDirectory(*Directory, true);
    return FFileHelper::SaveArrayToFile(Compressed, *Filename);
}

FString FPanoramaCaptureManager::BuildPNGFilePath(int32 FrameIndex) const
{
    const FString FramesDir = FPaths::Combine(TargetOutputDirectory, GFrameSubdirectory);
    return FPaths::Combine(FramesDir, FString::Printf(TEXT("Frame_%06d.png"), FrameIndex));
}

void FPanoramaCaptureManager::ProcessPendingAudio()
{
    // Audio is handled during Tick_GameThread via ConsumeCapturedAudio.
}

void FPanoramaCaptureManager::NotifyStatus_GameThread()
{
    FScopeLock Lock(&StatusCriticalSection);
    const int32 Occupancy = FrameQueue.Num();
    CachedStatus.PendingFrameCount = Occupancy;
    CachedStatus.DroppedFrames = FrameQueue.GetDroppedCount();
    const int32 Capacity = FrameQueue.GetCapacity();
    CachedStatus.RingBufferFill = (Capacity > 0) ? static_cast<float>(Occupancy) / static_cast<float>(Capacity) : 0.f;
    CachedStatus.bUsingFallback = bHasFallenBack;
    CachedStatus.LastWarning = LastWarningMessage;
    if (OnCaptureStatusUpdated.IsBound())
    {
        OnCaptureStatusUpdated.Execute(CachedStatus);
    }
}

bool FPanoramaCaptureManager::HandleNVENCFrame(const TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe>& Frame)
{
    if (!Frame.IsValid() || !VideoEncoder || !Muxer)
    {
        return false;
    }

    if (VideoEncoder->SupportsZeroCopy())
    {
        if (CurrentVideoSettings.CaptureMode == EPanoramaCaptureMode::Stereo && Frame->EyeIndex == 1)
        {
            return true;
        }

        if (!Frame->NVENCTexture.IsValid())
        {
            UE_LOG(LogPanoramaCapture, Warning, TEXT("NVENC zero-copy frame missing GPU texture."));
            return false;
        }

        if (VideoEncoder->EncodeFrame(Frame))
        {
            Muxer->AddVideoFrame(Frame);
            UpdateStatusAfterVideoFrame(Frame);
            return true;
        }

        return false;
    }

    if (CurrentVideoSettings.CaptureMode == EPanoramaCaptureMode::Stereo)
    {
        if (Frame->EyeIndex == 0)
        {
            PendingNVENCLeftFrame = Frame;
            return true;
        }

        if (Frame->EyeIndex == 1 && PendingNVENCLeftFrame.IsValid())
        {
            TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe> Encoded = HandleStereoNVENCPair(PendingNVENCLeftFrame, Frame);
            PendingNVENCLeftFrame.Reset();
            if (Encoded.IsValid())
            {
                Muxer->AddVideoFrame(Encoded);
                UpdateStatusAfterVideoFrame(Encoded);
                return true;
            }
            UE_LOG(LogPanoramaCapture, Warning, TEXT("NVENC failed to encode stereo pair."));
        }
        return false;
    }

    if (VideoEncoder->EncodeFrame(Frame))
    {
        Muxer->AddVideoFrame(Frame);
        UpdateStatusAfterVideoFrame(Frame);
        return true;
    }

    UE_LOG(LogPanoramaCapture, Warning, TEXT("NVENC failed to encode mono frame (eye=%d)."), Frame->EyeIndex);

    return false;
}

TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe> FPanoramaCaptureManager::HandleStereoNVENCPair(const TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe>& LeftFrame, const TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe>& RightFrame)
{
    if (!VideoEncoder)
    {
        return nullptr;
    }

    return VideoEncoder->EncodeStereoPair(LeftFrame, RightFrame);
}

void FPanoramaCaptureManager::UpdateStatusAfterVideoFrame(const TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe>& Frame)
{
    FScopeLock Lock(&StatusCriticalSection);
    const int32 Occupancy = FrameQueue.Num();
    CachedStatus.PendingFrameCount = Occupancy;
    CachedStatus.DroppedFrames = FrameQueue.GetDroppedCount();
    const int32 Capacity = FrameQueue.GetCapacity();
    CachedStatus.RingBufferFill = (Capacity > 0) ? static_cast<float>(Occupancy) / static_cast<float>(Capacity) : 0.f;
    if (Frame.IsValid())
    {
        CachedStatus.LastVideoPTS = Frame->TimestampSeconds;
    }
}

void FPanoramaCaptureManager::UpdateStatusAfterAudioPacket(const FPanoramaAudioPacket& Packet)
{
    if (Packet.PCMData.Num() == 0)
    {
        return;
    }

    const double PacketEnd = Packet.TimestampSeconds + Packet.GetDurationSeconds();
    FScopeLock Lock(&StatusCriticalSection);
    CachedStatus.LastAudioPTS = FMath::Max(CachedStatus.LastAudioPTS, PacketEnd);
}

void FPanoramaCaptureManager::ResetStatus()
{
    FScopeLock Lock(&StatusCriticalSection);
    CachedStatus = FPanoramicCaptureStatus();
    CachedStatus.bUsingNVENC = CurrentVideoSettings.OutputFormat == EPanoramaOutputFormat::NVENC;
    CachedStatus.bUsingFallback = bHasFallenBack;
    CachedStatus.LastWarning = LastWarningMessage;
    CachedStatus.EffectiveVideoSettings = CurrentVideoSettings;
    const int32 Occupancy = FrameQueue.Num();
    CachedStatus.PendingFrameCount = Occupancy;
    CachedStatus.DroppedFrames = FrameQueue.GetDroppedCount();
    const int32 Capacity = FrameQueue.GetCapacity();
    CachedStatus.RingBufferFill = (Capacity > 0) ? static_cast<float>(Occupancy) / static_cast<float>(Capacity) : 0.f;
}

bool FPanoramaCaptureManager::PerformPreflightChecks()
{
    bool bAllGood = true;

    if (CurrentVideoSettings.OutputFormat == EPanoramaOutputFormat::NVENC)
    {
        const bool bHardwareReady = VideoEncoder.IsValid() && VideoEncoder->IsInitialized() && VideoEncoder->HasHardware();
        if (!bHardwareReady)
        {
            PushWarningMessage(TEXT("NVENC hardware unavailable - reverting to PNG sequence."));
            CurrentVideoSettings.OutputFormat = EPanoramaOutputFormat::PNGSequence;
            bHasFallenBack = true;
            bAllGood = false;
        }
    }

    if (Muxer.IsValid() && !Muxer->IsFFmpegAvailable())
    {
        PushWarningMessage(TEXT("ffmpeg executable missing - automatic muxing will be skipped."));
        bAllGood = false;
    }

    if (!VerifyDiskCapacity())
    {
        bAllGood = false;
    }

    ApplyFallbackIfNeeded();
    return bAllGood;
}

bool FPanoramaCaptureManager::VerifyDiskCapacity()
{
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    const uint64 FreeSpace = PlatformFile.GetDiskFreeSpace(*TargetOutputDirectory);
    if (FreeSpace == 0)
    {
        PushWarningMessage(TEXT("Unable to query disk free space; proceeding with caution."));
        return true;
    }

    const uint64 Threshold = 2ull * 1024ull * 1024ull * 1024ull; // 2 GB safety margin
    if (FreeSpace < Threshold)
    {
        const double FreeGB = static_cast<double>(FreeSpace) / (1024.0 * 1024.0 * 1024.0);
        PushWarningMessage(FString::Printf(TEXT("Low disk space (%.2f GB remaining)"), FreeGB));
        return false;
    }
    return true;
}

void FPanoramaCaptureManager::ApplyFallbackIfNeeded()
{
    if (VideoEncoder.IsValid())
    {
        VideoEncoder->Shutdown();
        VideoEncoder->Initialize(CurrentVideoSettings, TargetOutputDirectory);
    }

    if (Muxer.IsValid())
    {
        Muxer->Configure(CurrentVideoSettings, CurrentAudioSettings);
    }

    FScopeLock Lock(&StatusCriticalSection);
    CachedStatus.bUsingNVENC = CurrentVideoSettings.OutputFormat == EPanoramaOutputFormat::NVENC;
    CachedStatus.bUsingFallback = bHasFallenBack;
    CachedStatus.LastWarning = LastWarningMessage;
    CachedStatus.EffectiveVideoSettings = CurrentVideoSettings;
}

void FPanoramaCaptureManager::PushWarningMessage(const FString& Message)
{
    if (Message.IsEmpty())
    {
        return;
    }

    if (!LastWarningMessage.IsEmpty())
    {
        LastWarningMessage += TEXT("\n");
    }
    LastWarningMessage += Message;

    FScopeLock Lock(&StatusCriticalSection);
    CachedStatus.LastWarning = LastWarningMessage;
}
