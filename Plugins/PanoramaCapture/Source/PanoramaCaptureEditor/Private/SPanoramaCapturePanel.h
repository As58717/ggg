#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "PanoramaCaptureTypes.h"
#include "Templates/Function.h"

class UPanoramaCaptureComponent;
template <typename OptionType> class SComboBox;

struct FPanoramaComponentEntry
{
    TWeakObjectPtr<UPanoramaCaptureComponent> Component;
    FString DisplayName;
};

class SPanoramaCapturePanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPanoramaCapturePanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
    FReply HandleStartStopButton();

    FText GetStartStopButtonText() const;
    FText GetStatusText() const;
    FText GetBufferStatusText() const;
    FText GetPTSStatusText() const;
    FText GetNVENCStatusText() const;
    FSlateColor GetBufferWarningColor() const;
    ECheckBoxState GetPreviewCheckState() const;
    FText GetFormatSummaryText() const;
    FText GetColorFormatSummaryText() const;

    void RefreshComponentFromSelection();
    void ApplyVideoSettings(TFunctionRef<void(FPanoramicVideoSettings&)> Mutator);
    void ApplyAudioSettings(TFunctionRef<void(FPanoramicAudioSettings&)> Mutator);

    void HandleHEVCToggled(ECheckBoxState NewState);
    void HandleOutputFormatChanged(TSharedPtr<EPanoramaOutputFormat> InFormat, ESelectInfo::Type SelectInfo);
    void HandleCaptureModeChanged(TSharedPtr<EPanoramaCaptureMode> InMode, ESelectInfo::Type SelectInfo);
    void HandleGammaChanged(TSharedPtr<EPanoramaGamma> InGamma, ESelectInfo::Type SelectInfo);
    void HandleColorFormatChanged(TSharedPtr<EPanoramaColorFormat> InFormat, ESelectInfo::Type SelectInfo);
    void HandleStereoLayoutChanged(TSharedPtr<EPanoramaStereoLayout> InLayout, ESelectInfo::Type SelectInfo);
    void HandleRateControlChanged(TSharedPtr<EPanoramaRateControlPreset> InPreset, ESelectInfo::Type SelectInfo);
    void HandlePreviewToggled(ECheckBoxState NewState);
    void HandleBitrateCommitted(float NewValue, ETextCommit::Type CommitType);
    void HandleGOPCommitted(float NewValue, ETextCommit::Type CommitType);
    void HandleBFramesCommitted(float NewValue, ETextCommit::Type CommitType);

    FText GetWarningText() const;

    TWeakObjectPtr<UPanoramaCaptureComponent> SelectedComponent;
    bool bRequestPreviewToggle;
    TArray<TSharedPtr<struct FPanoramaComponentEntry>> ComponentItems;
    TSharedPtr<FPanoramaComponentEntry> ActiveItem;
    TSharedPtr<SComboBox<TSharedPtr<FPanoramaComponentEntry>>> ComponentCombo;

    TArray<TSharedPtr<EPanoramaOutputFormat>> OutputFormatOptions;
    TSharedPtr<EPanoramaOutputFormat> SelectedOutputFormat;
    TSharedPtr<SComboBox<TSharedPtr<EPanoramaOutputFormat>>> OutputFormatCombo;

    TArray<TSharedPtr<EPanoramaCaptureMode>> CaptureModeOptions;
    TSharedPtr<EPanoramaCaptureMode> SelectedCaptureMode;
    TSharedPtr<SComboBox<TSharedPtr<EPanoramaCaptureMode>>> CaptureModeCombo;

    TArray<TSharedPtr<EPanoramaGamma>> GammaOptions;
    TSharedPtr<EPanoramaGamma> SelectedGamma;
    TSharedPtr<SComboBox<TSharedPtr<EPanoramaGamma>>> GammaCombo;

    TArray<TSharedPtr<EPanoramaColorFormat>> ColorFormatOptions;
    TSharedPtr<EPanoramaColorFormat> SelectedColorFormat;
    TSharedPtr<SComboBox<TSharedPtr<EPanoramaColorFormat>>> ColorFormatCombo;

    TArray<TSharedPtr<EPanoramaStereoLayout>> StereoLayoutOptions;
    TSharedPtr<EPanoramaStereoLayout> SelectedStereoLayout;
    TSharedPtr<SComboBox<TSharedPtr<EPanoramaStereoLayout>>> StereoLayoutCombo;

    TArray<TSharedPtr<EPanoramaRateControlPreset>> RateControlOptions;
    TSharedPtr<EPanoramaRateControlPreset> SelectedRateControl;
    TSharedPtr<SComboBox<TSharedPtr<EPanoramaRateControlPreset>>> RateControlCombo;
};
