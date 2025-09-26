#include "PanoramaCaptureFFmpeg.h"
#include "PanoramaCaptureFrame.h"
#include "PanoramaCaptureLog.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "HAL/PlatformProcess.h"

namespace
{
    const TCHAR* GetFFmpegPixelFormat(EPanoramaColorFormat Format)
    {
        switch (Format)
        {
        case EPanoramaColorFormat::NV12:
            return TEXT("nv12");
        case EPanoramaColorFormat::P010:
            return TEXT("p010le");
        case EPanoramaColorFormat::BGRA8:
            return TEXT("bgra");
        default:
            break;
        }
        return TEXT("nv12");
    }
}

FPanoramaFFmpegMuxer::FPanoramaFFmpegMuxer()
    : bInitialized(false)
    , bHasFFmpegExecutable(false)
    , CapturedFrameCount(0)
    , CachedAudioDurationSeconds(0.0)
    , NVENCResolution(FIntPoint::ZeroValue)
    , NVENCFrameCount(0)
    , bHasNVENCSource(false)
    , bNVENCIsHEVC(false)
    , bNVENCStereo(false)
    , bNVENCIsCompressedStream(false)
{
}

FPanoramaFFmpegMuxer::~FPanoramaFFmpegMuxer()
{
    Shutdown();
}

void FPanoramaFFmpegMuxer::Initialize(const FString& OutputDirectory)
{
    TargetDirectory = OutputDirectory;
    IFileManager::Get().MakeDirectory(*TargetDirectory, true);
    OutputFilePath = FPaths::Combine(TargetDirectory, TEXT("PanoramaCapture.mp4"));
    FramesDirectory = FPaths::Combine(TargetDirectory, TEXT("Frames"));
    FrameFilePattern = FPaths::Combine(FramesDirectory, TEXT("Frame_%06d.png"));
    CapturedFrameTimestamps.Reset();
    CapturedFrameCount = 0;
    CachedAudioDurationSeconds = 0.0;
    NVENCRawVideoPath.Reset();
    NVENCResolution = FIntPoint::ZeroValue;
    NVENCFrameCount = 0;
    bHasNVENCSource = false;
    bNVENCIsHEVC = false;
    bNVENCStereo = false;
    bNVENCIsCompressedStream = false;

    if (TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("PanoramaCapture")))
    {
        const FString ThirdPartyDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("ThirdParty/Win64"));
        const FString Candidate = FPaths::Combine(ThirdPartyDir, TEXT("ffmpeg.exe"));
        bHasFFmpegExecutable = FPaths::FileExists(Candidate);
        if (bHasFFmpegExecutable)
        {
            FFmpegExecutablePath = Candidate;
        }
    }

    bInitialized = true;

    UE_LOG(LogPanoramaCapture, Log, TEXT("Muxer initialized output %s"), *OutputFilePath);
}

void FPanoramaFFmpegMuxer::Shutdown()
{
    bInitialized = false;
    bHasFFmpegExecutable = false;
    OutputFilePath.Reset();
    AudioFilePath.Reset();
    CapturedFrameTimestamps.Reset();
    CapturedFrameCount = 0;
    CachedAudioDurationSeconds = 0.0;
    NVENCRawVideoPath.Reset();
    NVENCResolution = FIntPoint::ZeroValue;
    NVENCFrameCount = 0;
    bHasNVENCSource = false;
}

void FPanoramaFFmpegMuxer::Configure(const FPanoramicVideoSettings& VideoSettings, const FPanoramicAudioSettings& AudioSettings)
{
    CachedVideoSettings = VideoSettings;
    CachedAudioSettings = AudioSettings;
    CapturedFrameTimestamps.Reset();
    CapturedFrameCount = 0;
    CachedAudioDurationSeconds = 0.0;
    NVENCResolution = VideoSettings.Resolution;
    NVENCFrameCount = 0;
    bHasNVENCSource = false;
    bNVENCIsHEVC = VideoSettings.bUseHEVC;
    bNVENCStereo = VideoSettings.CaptureMode == EPanoramaCaptureMode::Stereo;
    bNVENCIsCompressedStream = false;

    const bool bPreferMKV = CachedVideoSettings.bUseHEVC || CachedVideoSettings.CaptureMode == EPanoramaCaptureMode::Stereo;
    const FString ContainerName = bPreferMKV ? TEXT("PanoramaCapture.mkv") : TEXT("PanoramaCapture.mp4");
    OutputFilePath = FPaths::Combine(TargetDirectory, ContainerName);
}

void FPanoramaFFmpegMuxer::AddVideoFrame(const TSharedPtr<FPanoramaFrame, ESPMode::ThreadSafe>& Frame)
{
    if (!bInitialized || !Frame.IsValid())
    {
        return;
    }

    if (!Frame->DiskFilePath.IsEmpty())
    {
        if (!FPaths::FileExists(Frame->DiskFilePath))
        {
            UE_LOG(LogPanoramaCapture, Warning, TEXT("PNG frame missing on disk: %s"), *Frame->DiskFilePath);
            return;
        }

        CapturedFrameTimestamps.Add(Frame->TimestampSeconds);
        ++CapturedFrameCount;
    }
    else if (Frame->EncodedVideo.Num() > 0)
    {
        CapturedFrameTimestamps.Add(Frame->TimestampSeconds);
        ++CapturedFrameCount;
        Frame->EncodedVideo.Reset();
    }
}

void FPanoramaFFmpegMuxer::AddAudioSamples(const FPanoramaAudioPacket& Packet)
{
    if (!bInitialized || Packet.PCMData.Num() == 0)
    {
        return;
    }

    CachedAudioDurationSeconds = FMath::Max(CachedAudioDurationSeconds, Packet.TimestampSeconds + Packet.GetDurationSeconds());
}

void FPanoramaFFmpegMuxer::SetAudioSource(const FString& FilePath, double DurationSeconds)
{
    AudioFilePath = FilePath;
    CachedAudioDurationSeconds = DurationSeconds;
}

void FPanoramaFFmpegMuxer::SetNVENCVideoSource(const FString& RawFilePath, const FIntPoint& Resolution, int64 FrameCount, bool bIsHEVC, bool bStereo, bool bIsEncodedStream)
{
    NVENCRawVideoPath = RawFilePath;
    NVENCResolution = Resolution;
    NVENCFrameCount = FrameCount;
    bNVENCIsHEVC = bIsHEVC;
    bNVENCStereo = bStereo;
    bNVENCIsCompressedStream = bIsEncodedStream;
    bHasNVENCSource = !NVENCRawVideoPath.IsEmpty() && FPaths::FileExists(NVENCRawVideoPath);
}

void FPanoramaFFmpegMuxer::FinalizeContainer()
{
    if (!bInitialized)
    {
        return;
    }

    if (CachedVideoSettings.OutputFormat == EPanoramaOutputFormat::PNGSequence)
    {
        FinalizePNGSequence();
    }
    else
    {
        FinalizeNVENCStream();
    }
}

void FPanoramaFFmpegMuxer::FinalizePNGSequence()
{
    if (CapturedFrameCount == 0)
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("No frames were captured - skipping ffmpeg invocation"));
        return;
    }

    const double FrameRate = ComputeFrameRate();
    FString CommandLine = FString::Printf(TEXT("-y -framerate %.6f -i \"%s\""), FrameRate, *FrameFilePattern);

    if (!AudioFilePath.IsEmpty() && FPaths::FileExists(AudioFilePath))
    {
        CommandLine += FString::Printf(TEXT(" -i \"%s\" -c:a aac -ar %d -ac %d"), *AudioFilePath, CachedAudioSettings.SampleRate, CachedAudioSettings.NumChannels);
    }

    if (CachedVideoSettings.bUseHEVC)
    {
        CommandLine += FString::Printf(TEXT(" -c:v libx265 -x265-params bitrate=%d"), CachedVideoSettings.TargetBitrateMbps * 1000);
    }
    else
    {
        CommandLine += FString::Printf(TEXT(" -c:v libx264 -b:v %dk"), CachedVideoSettings.TargetBitrateMbps * 1000);
    }

    CommandLine += FString::Printf(TEXT(" -g %d"), CachedVideoSettings.GOPLength);
    CommandLine += FString::Printf(TEXT(" -bf %d"), CachedVideoSettings.NumBFrames);
    CommandLine += TEXT(" -pix_fmt yuv420p");

    if (CachedVideoSettings.CaptureMode == EPanoramaCaptureMode::Stereo)
    {
        const bool bSideBySide = CachedVideoSettings.StereoLayout == EPanoramaStereoLayout::SideBySide;
        if (bSideBySide)
        {
            CommandLine += TEXT(" -metadata:s:v:0 stereo=left-right -metadata:s:v:0 stereomode=left_right");
        }
        else
        {
            CommandLine += TEXT(" -metadata:s:v:0 stereo=top-bottom -metadata:s:v:0 stereomode=top_bottom");
        }
    }
    else
    {
        CommandLine += TEXT(" -metadata:s:v:0 stereo=mono");
    }

    CommandLine += TEXT(" -metadata:s:v:0 projection=equirectangular");
    if (CachedVideoSettings.Gamma == EPanoramaGamma::Linear)
    {
        CommandLine += TEXT(" -color_primaries bt2020 -colorspace bt2020nc -color_trc smpte2084");
    }
    else
    {
        CommandLine += TEXT(" -color_primaries bt709 -colorspace bt709 -color_trc bt709");
    }

    CommandLine += TEXT(" -color_range tv");

    const bool bIsMP4 = OutputFilePath.EndsWith(TEXT(".mp4"));
    if (bIsMP4)
    {
        CommandLine += TEXT(" -movflags +faststart");
    }

    CommandLine += FString::Printf(TEXT(" \"%s\""), *OutputFilePath);

    if (!InvokeFFmpeg(CommandLine))
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("Failed to run ffmpeg. Command line: %s"), *CommandLine);
    }
    else
    {
        UE_LOG(LogPanoramaCapture, Log, TEXT("FFmpeg muxing complete -> %s"), *OutputFilePath);
        CleanupPNGFrames();
    }
}

void FPanoramaFFmpegMuxer::FinalizeNVENCStream()
{
    if (!bHasNVENCSource)
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("NVENC finalize requested without a valid raw video source."));
        return;
    }

    if (!FPaths::FileExists(NVENCRawVideoPath))
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("NVENC raw file missing: %s"), *NVENCRawVideoPath);
        return;
    }

    if (NVENCResolution.X <= 0 || NVENCResolution.Y <= 0)
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("Invalid NVENC resolution %dx%d"), NVENCResolution.X, NVENCResolution.Y);
        return;
    }

    if (CapturedFrameCount == 0)
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("No NVENC frames were captured - skipping ffmpeg invocation"));
        return;
    }

    const double FrameRate = ComputeFrameRate();
    FString CommandLine;

    if (bNVENCIsCompressedStream)
    {
        const TCHAR* Demuxer = bNVENCIsHEVC ? TEXT("hevc") : TEXT("h264");
        CommandLine = FString::Printf(TEXT("-y -f %s -i \"%s\""), Demuxer, *NVENCRawVideoPath);
        if (!AudioFilePath.IsEmpty() && FPaths::FileExists(AudioFilePath))
        {
            CommandLine += FString::Printf(TEXT(" -i \"%s\" -c:a aac -ar %d -ac %d"), *AudioFilePath, CachedAudioSettings.SampleRate, CachedAudioSettings.NumChannels);
        }
        CommandLine += TEXT(" -c:v copy");
        CommandLine += FString::Printf(TEXT(" -r %.6f"), FrameRate);
    }
    else
    {
        const TCHAR* PixelFormat = GetFFmpegPixelFormat(CachedVideoSettings.ColorFormat);
        CommandLine = FString::Printf(TEXT("-y -f rawvideo -pix_fmt %s -s %dx%d -r %.6f -i \"%s\""), PixelFormat, NVENCResolution.X, NVENCResolution.Y, FrameRate, *NVENCRawVideoPath);

        if (!AudioFilePath.IsEmpty() && FPaths::FileExists(AudioFilePath))
        {
            CommandLine += FString::Printf(TEXT(" -i \"%s\" -c:a aac -ar %d -ac %d"), *AudioFilePath, CachedAudioSettings.SampleRate, CachedAudioSettings.NumChannels);
        }

        const TCHAR* VideoCodec = bNVENCIsHEVC ? TEXT("hevc_nvenc") : TEXT("h264_nvenc");
        CommandLine += FString::Printf(TEXT(" -c:v %s"), VideoCodec);
        CommandLine += FString::Printf(TEXT(" -b:v %dk"), CachedVideoSettings.TargetBitrateMbps * 1000);
        CommandLine += FString::Printf(TEXT(" -g %d"), CachedVideoSettings.GOPLength);
        CommandLine += FString::Printf(TEXT(" -bf %d"), CachedVideoSettings.NumBFrames);
    }

    if (bNVENCStereo)
    {
        const bool bSideBySide = CachedVideoSettings.StereoLayout == EPanoramaStereoLayout::SideBySide;
        if (bSideBySide)
        {
            CommandLine += TEXT(" -metadata:s:v:0 stereo=left-right -metadata:s:v:0 stereomode=left_right");
        }
        else
        {
            CommandLine += TEXT(" -metadata:s:v:0 stereo=top-bottom -metadata:s:v:0 stereomode=top_bottom");
        }
    }
    else
    {
        CommandLine += TEXT(" -metadata:s:v:0 stereo=mono");
    }

    CommandLine += TEXT(" -metadata:s:v:0 projection=equirectangular");
    if (CachedVideoSettings.Gamma == EPanoramaGamma::Linear)
    {
        CommandLine += TEXT(" -color_primaries bt2020 -colorspace bt2020nc -color_trc smpte2084");
    }
    else
    {
        CommandLine += TEXT(" -color_primaries bt709 -colorspace bt709 -color_trc bt709");
    }

    CommandLine += TEXT(" -color_range tv");

    const bool bIsMP4 = OutputFilePath.EndsWith(TEXT(".mp4"));
    if (bIsMP4)
    {
        CommandLine += TEXT(" -movflags +faststart");
    }

    CommandLine += FString::Printf(TEXT(" \"%s\""), *OutputFilePath);

    if (!InvokeFFmpeg(CommandLine))
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("NVENC ffmpeg invocation failed. Command line: %s"), *CommandLine);
    }
    else
    {
        UE_LOG(LogPanoramaCapture, Log, TEXT("NVENC ffmpeg muxing complete -> %s"), *OutputFilePath);
        IFileManager::Get().Delete(*NVENCRawVideoPath);
    }
}

void FPanoramaFFmpegMuxer::CleanupPNGFrames()
{
    if (FramesDirectory.IsEmpty())
    {
        return;
    }

    IFileManager::Get().DeleteDirectory(*FramesDirectory, false, true);
}

double FPanoramaFFmpegMuxer::ComputeFrameRate() const
{
    if (CapturedFrameTimestamps.Num() <= 1)
    {
        return 30.0;
    }

    const double Duration = CapturedFrameTimestamps.Last() - CapturedFrameTimestamps[0];
    if (Duration <= KINDA_SMALL_NUMBER)
    {
        return 30.0;
    }

    const double Frames = static_cast<double>(CapturedFrameTimestamps.Num() - 1);
    return FMath::Clamp(Frames / Duration, 1.0, 120.0);
}

bool FPanoramaFFmpegMuxer::InvokeFFmpeg(const FString& CommandLine) const
{
    if (FFmpegExecutablePath.IsEmpty() || !FPaths::FileExists(FFmpegExecutablePath))
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("ffmpeg executable not found at %s"), *FFmpegExecutablePath);
        return false;
    }

    UE_LOG(LogPanoramaCapture, Log, TEXT("Invoking ffmpeg %s %s"), *FFmpegExecutablePath, *CommandLine);

    FProcHandle ProcHandle = FPlatformProcess::CreateProc(*FFmpegExecutablePath, *CommandLine, true, false, false, nullptr, 0, nullptr, nullptr);
    if (!ProcHandle.IsValid())
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("Failed to launch ffmpeg process"));
        return false;
    }

    FPlatformProcess::WaitForProc(ProcHandle);
    int32 ReturnCode = 0;
    FPlatformProcess::GetProcReturnCode(ProcHandle, &ReturnCode);
    FPlatformProcess::CloseProc(ProcHandle);

    if (ReturnCode != 0)
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("ffmpeg exited with code %d"), ReturnCode);
        return false;
    }

    return true;
}
