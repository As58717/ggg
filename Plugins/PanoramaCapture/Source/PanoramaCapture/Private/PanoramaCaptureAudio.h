#pragma once

#include "CoreMinimal.h"
#include "PanoramaCaptureTypes.h"
#include "AudioDeviceHandle.h"
#include "HAL/CriticalSection.h"

class USoundSubmix;
class UWorld;

namespace Audio
{
    class ISubmixBufferListener;
}

/** Handles AudioMixer submix recording and WAV output. */
class FPanoramaAudioRecorder
{
public:
    FPanoramaAudioRecorder();
    ~FPanoramaAudioRecorder();

    void Initialize(const FPanoramicAudioSettings& Settings, const FString& OutputDirectory, UWorld* InWorld);
    void Shutdown();

    void StartRecording();
    void StopRecording();

    void Tick(float DeltaSeconds);

    /** Retrieve PCM packets captured since last call. */
    void ConsumeAudioPackets(TArray<FPanoramaAudioPacket>& OutPackets);

    FString GetWaveFilePath() const { return WaveFilePath; }
    double GetRecordingDurationSeconds() const { return RecordingDurationSeconds; }
    void SetSubmixToRecord(USoundSubmix* InSubmix) { SubmixToRecord = InSubmix; }
    void SetCaptureStartTime(double InCaptureStartSeconds)
    {
        CaptureClockStartSeconds = InCaptureStartSeconds;
        SmoothedDriftSeconds = 0.0;
    }
    double GetLastPacketPTS() const { return LastPacketPTS; }

    bool IsRecording() const { return bIsRecording; }

    /** Writes the accumulated PCM buffer to disk as a WAV file. */
    void FinalizeWaveFile();

private:
    class FSubmixCaptureListener;

    void RegisterListener();
    void UnregisterListener();
    void ResetCaptureData();
    void AppendPCMData(const FPanoramaAudioPacket& Packet);
    void HandleSubmixBuffer(float* AudioData, int32 NumSamples, int32 NumChannels, int32 InSampleRate);

    FPanoramicAudioSettings CurrentSettings;
    FString TargetDirectory;
    FString WaveFilePath;

    bool bIsRecording;

    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<USoundSubmix> SubmixToRecord;
    TWeakObjectPtr<USoundSubmix> ActiveRecordingSubmix;

    FAudioDeviceHandle AudioDeviceHandle;
    TSharedPtr<FSubmixCaptureListener, ESPMode::ThreadSafe> SubmixListener;

    FCriticalSection AudioDataCriticalSection;
    TArray<FPanoramaAudioPacket> PendingPackets;
    TArray<uint8> AccumulatedPCMData;

    double RecordingDurationSeconds;
    int32 CapturedSampleRate;
    int32 CapturedNumChannels;
    int64 TotalFramesCaptured;
    double CaptureClockStartSeconds;
    double RecordingStartSeconds;
    double LastPacketPTS;
    double SmoothedDriftSeconds;
};
