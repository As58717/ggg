#include "PanoramaCaptureAudio.h"
#include "PanoramaCaptureLog.h"
#include "AudioDevice.h"
#if WITH_AUDIOMIXER
#include "AudioMixerSubmix.h"
#endif
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Sound/SoundSubmix.h"
#include "Engine/World.h"
#include "Serialization/BufferArchive.h"
#include "HAL/PlatformTime.h"

namespace
{
    static constexpr int32 BitsPerSample = 16;
}

#if WITH_AUDIOMIXER
class FPanoramaAudioRecorder::FSubmixCaptureListener : public Audio::ISubmixBufferListener
{
public:
    explicit FSubmixCaptureListener(FPanoramaAudioRecorder& InOwner)
        : Owner(InOwner)
    {
    }

    virtual void OnNewSubmixBuffer(USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate, double /*AudioClock*/, bool /*bIsPaused*/) override
    {
        Owner.HandleSubmixBuffer(AudioData, NumSamples, NumChannels, SampleRate);
    }

    virtual bool IsSubmixListenerEnabled() const override
    {
        return true;
    }

private:
    FPanoramaAudioRecorder& Owner;
};
#endif // WITH_AUDIOMIXER

FPanoramaAudioRecorder::FPanoramaAudioRecorder()
    : bIsRecording(false)
    , RecordingDurationSeconds(0.0)
    , CapturedSampleRate(0)
    , CapturedNumChannels(0)
    , TotalFramesCaptured(0)
    , CaptureClockStartSeconds(0.0)
    , RecordingStartSeconds(0.0)
    , LastPacketPTS(0.0)
    , SmoothedDriftSeconds(0.0)
{
}

FPanoramaAudioRecorder::~FPanoramaAudioRecorder()
{
    Shutdown();
}

void FPanoramaAudioRecorder::Initialize(const FPanoramicAudioSettings& Settings, const FString& OutputDirectory, UWorld* InWorld)
{
    CurrentSettings = Settings;
    TargetDirectory = OutputDirectory;
    IFileManager::Get().MakeDirectory(*TargetDirectory, true);
    WaveFilePath = FPaths::Combine(TargetDirectory, TEXT("PanoramaAudio.wav"));
    World = InWorld;
    SubmixToRecord = nullptr;
    ActiveRecordingSubmix.Reset();
    ResetCaptureData();
}

void FPanoramaAudioRecorder::Shutdown()
{
    StopRecording();
    FinalizeWaveFile();
    ResetCaptureData();
    WaveFilePath.Reset();
    World.Reset();
    SubmixToRecord.Reset();
    ActiveRecordingSubmix.Reset();
    SubmixListener.Reset();
}

void FPanoramaAudioRecorder::StartRecording()
{
    if (bIsRecording)
    {
        return;
    }

    if (!CurrentSettings.bCaptureAudio)
    {
        UE_LOG(LogPanoramaCapture, Log, TEXT("Audio capture disabled - skipping start"));
        return;
    }

#if WITH_AUDIOMIXER
    RecordingStartSeconds = FPlatformTime::Seconds();
    ResetCaptureData();
    RegisterListener();
    if (bIsRecording)
    {
        UE_LOG(LogPanoramaCapture, Log, TEXT("Audio recording started at %d Hz (%d channels)"), CapturedSampleRate, CapturedNumChannels);
    }
    else
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("Failed to start audio capture - no valid submix or audio device."));
    }
#else
    UE_LOG(LogPanoramaCapture, Warning, TEXT("AudioMixer not available - audio will not be captured"));
#endif
}

void FPanoramaAudioRecorder::StopRecording()
{
    if (!bIsRecording)
    {
        return;
    }

    UE_LOG(LogPanoramaCapture, Log, TEXT("Stopping audio recording"));
    UnregisterListener();
    RecordingDurationSeconds = LastPacketPTS;
    bIsRecording = false;
}

void FPanoramaAudioRecorder::Tick(float DeltaSeconds)
{
    UE_UNUSED(DeltaSeconds);
    // Duration is derived from captured sample counts; no work required per tick.
}

void FPanoramaAudioRecorder::ConsumeAudioPackets(TArray<FPanoramaAudioPacket>& OutPackets)
{
    FScopeLock Lock(&AudioDataCriticalSection);
    OutPackets = MoveTemp(PendingPackets);
    PendingPackets.Reset();
}

void FPanoramaAudioRecorder::FinalizeWaveFile()
{
    TArray<uint8> PCMBuffer;
    int32 NumChannels = CurrentSettings.NumChannels;
    int32 SampleRate = CurrentSettings.SampleRate;

    {
        FScopeLock Lock(&AudioDataCriticalSection);
        if (AccumulatedPCMData.Num() == 0)
        {
            return;
        }

        PCMBuffer = AccumulatedPCMData;
        NumChannels = (CapturedNumChannels > 0) ? CapturedNumChannels : NumChannels;
        SampleRate = (CapturedSampleRate > 0) ? CapturedSampleRate : SampleRate;
    }

    const int32 BytesPerSample = BitsPerSample / 8;
    const int32 DataSize = PCMBuffer.Num();
    const int32 ByteRate = SampleRate * NumChannels * BytesPerSample;
    const int16 BlockAlign = NumChannels * BytesPerSample;
    const int32 ChunkSize = 36 + DataSize;

    FBufferArchive WaveData;
    WaveData.Reserve(PCMBuffer.Num() + 44);

    auto WriteUInt32 = [&WaveData](uint32 Value)
    {
        WaveData << Value;
    };

    auto WriteUInt16 = [&WaveData](uint16 Value)
    {
        WaveData << Value;
    };

    const uint32 RIFF = 0x46464952; // 'RIFF'
    const uint32 WAVE = 0x45564157; // 'WAVE'
    const uint32 FMT  = 0x20746D66; // 'fmt '
    const uint32 DATA = 0x61746164; // 'data'

    WriteUInt32(RIFF);
    WriteUInt32(static_cast<uint32>(ChunkSize));
    WriteUInt32(WAVE);
    WriteUInt32(FMT);
    WriteUInt32(16); // PCM chunk size
    WriteUInt16(1);  // PCM format
    WriteUInt16(static_cast<uint16>(NumChannels));
    WriteUInt32(static_cast<uint32>(SampleRate));
    WriteUInt32(static_cast<uint32>(ByteRate));
    WriteUInt16(static_cast<uint16>(BlockAlign));
    WriteUInt16(static_cast<uint16>(BitsPerSample));
    WriteUInt32(DATA);
    WriteUInt32(static_cast<uint32>(DataSize));

    if (PCMBuffer.Num() > 0)
    {
        WaveData.Append(PCMBuffer.GetData(), PCMBuffer.Num());
    }

    if (!FFileHelper::SaveArrayToFile(WaveData, *WaveFilePath))
    {
        UE_LOG(LogPanoramaCapture, Warning, TEXT("Failed to write WAV file to %s"), *WaveFilePath);
    }

    WaveData.FlushCache();
    WaveData.Empty();

    {
        FScopeLock Lock(&AudioDataCriticalSection);
        AccumulatedPCMData.Reset();
        PendingPackets.Reset();
    }
}

void FPanoramaAudioRecorder::RegisterListener()
{
#if WITH_AUDIOMIXER
    if (!World.IsValid())
    {
        return;
    }

    AudioDeviceHandle = World->GetAudioDevice();
    FAudioDevice* AudioDevice = AudioDeviceHandle.GetAudioDevice();
    if (!AudioDevice)
    {
        return;
    }

    USoundSubmix* TargetSubmix = SubmixToRecord.Get();
    if (!TargetSubmix)
    {
        TargetSubmix = AudioDevice->GetMainSubmixObject();
    }

    if (!TargetSubmix)
    {
        return;
    }

    if (!SubmixListener.IsValid())
    {
        SubmixListener = MakeShared<FSubmixCaptureListener, ESPMode::ThreadSafe>(*this);
    }

    AudioDevice->RegisterSubmixBufferListener(SubmixListener.Get(), TargetSubmix);
    ActiveRecordingSubmix = TargetSubmix;
    bIsRecording = true;
#endif
}

void FPanoramaAudioRecorder::UnregisterListener()
{
#if WITH_AUDIOMIXER
    if (FAudioDevice* AudioDevice = AudioDeviceHandle.GetAudioDevice())
    {
        if (ActiveRecordingSubmix.IsValid() && SubmixListener.IsValid())
        {
            AudioDevice->UnregisterSubmixBufferListener(SubmixListener.Get(), ActiveRecordingSubmix.Get());
        }
    }

    AudioDeviceHandle = FAudioDeviceHandle();
    ActiveRecordingSubmix.Reset();
#endif
}

void FPanoramaAudioRecorder::ResetCaptureData()
{
    FScopeLock Lock(&AudioDataCriticalSection);
    PendingPackets.Reset();
    AccumulatedPCMData.Reset();
    RecordingDurationSeconds = 0.0;
    TotalFramesCaptured = 0;
    CapturedSampleRate = CurrentSettings.SampleRate;
    CapturedNumChannels = CurrentSettings.NumChannels;
    LastPacketPTS = 0.0;
    SmoothedDriftSeconds = 0.0;
}

void FPanoramaAudioRecorder::AppendPCMData(const FPanoramaAudioPacket& Packet)
{
    AccumulatedPCMData.Append(Packet.PCMData);
}

void FPanoramaAudioRecorder::HandleSubmixBuffer(float* AudioData, int32 NumSamples, int32 NumChannels, int32 InSampleRate)
{
#if WITH_AUDIOMIXER
    if (!bIsRecording || NumSamples <= 0 || NumChannels <= 0)
    {
        return;
    }

    const int32 BytesPerSample = BitsPerSample / 8;
    const int32 NumFrames = NumSamples / NumChannels;
    FPanoramaAudioPacket Packet;
    Packet.NumChannels = NumChannels;
    Packet.SampleRate = InSampleRate;
    const double FrameOffsetSeconds = (InSampleRate > 0) ? static_cast<double>(TotalFramesCaptured) / static_cast<double>(InSampleRate) : 0.0;
    const double BaseOffsetSeconds = RecordingStartSeconds - CaptureClockStartSeconds;
    Packet.TimestampSeconds = FMath::Max(0.0, BaseOffsetSeconds) + FrameOffsetSeconds;
    const double PacketDurationSeconds = (InSampleRate > 0) ? static_cast<double>(NumFrames) / static_cast<double>(InSampleRate) : 0.0;
    const double RealClockSeconds = FPlatformTime::Seconds() - CaptureClockStartSeconds;
    const double ExpectedEnd = Packet.TimestampSeconds + PacketDurationSeconds;
    const double Drift = RealClockSeconds - ExpectedEnd;
    SmoothedDriftSeconds = FMath::Clamp(FMath::Lerp(SmoothedDriftSeconds, Drift, 0.05), -0.25, 0.25);
    Packet.TimestampSeconds = FMath::Max(0.0, Packet.TimestampSeconds + SmoothedDriftSeconds);
    Packet.PCMData.SetNumUninitialized(NumSamples * BytesPerSample);

    int16* DestBuffer = reinterpret_cast<int16*>(Packet.PCMData.GetData());
    for (int32 SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
    {
        const float Clamped = FMath::Clamp(AudioData[SampleIndex], -1.0f, 1.0f);
        DestBuffer[SampleIndex] = static_cast<int16>(Clamped * 32767.0f);
    }

    {
        FScopeLock Lock(&AudioDataCriticalSection);
        const double PacketDuration = Packet.GetDurationSeconds();
        LastPacketPTS = Packet.TimestampSeconds + PacketDuration;
        RecordingDurationSeconds = FMath::Max(RecordingDurationSeconds, LastPacketPTS);
        FPanoramaAudioPacket& StoredPacket = PendingPackets.Emplace_GetRef(MoveTemp(Packet));
        AppendPCMData(StoredPacket);
    }

    TotalFramesCaptured += NumFrames;
    CapturedSampleRate = InSampleRate;
    CapturedNumChannels = NumChannels;
#endif
}
