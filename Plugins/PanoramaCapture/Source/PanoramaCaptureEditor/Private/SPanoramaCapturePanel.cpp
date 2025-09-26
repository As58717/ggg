#include "SPanoramaCapturePanel.h"
#include "PanoramaCaptureComponent.h"
#include "PanoramaCaptureTypes.h"
#include "PanoramaCaptureLog.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Styling/AppStyle.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Internationalization/Internationalization.h"

void SPanoramaCapturePanel::Construct(const FArguments& InArgs)
{
    bRequestPreviewToggle = true;

    OutputFormatOptions = {
        MakeShared<EPanoramaOutputFormat>(EPanoramaOutputFormat::PNGSequence),
        MakeShared<EPanoramaOutputFormat>(EPanoramaOutputFormat::NVENC)
    };
    CaptureModeOptions = {
        MakeShared<EPanoramaCaptureMode>(EPanoramaCaptureMode::Mono),
        MakeShared<EPanoramaCaptureMode>(EPanoramaCaptureMode::Stereo)
    };
    GammaOptions = {
        MakeShared<EPanoramaGamma>(EPanoramaGamma::SRGB),
        MakeShared<EPanoramaGamma>(EPanoramaGamma::Linear)
    };
    ColorFormatOptions = {
        MakeShared<EPanoramaColorFormat>(EPanoramaColorFormat::NV12),
        MakeShared<EPanoramaColorFormat>(EPanoramaColorFormat::P010),
        MakeShared<EPanoramaColorFormat>(EPanoramaColorFormat::BGRA8)
    };
    StereoLayoutOptions = {
        MakeShared<EPanoramaStereoLayout>(EPanoramaStereoLayout::TopBottom),
        MakeShared<EPanoramaStereoLayout>(EPanoramaStereoLayout::SideBySide)
    };
    RateControlOptions = {
        MakeShared<EPanoramaRateControlPreset>(EPanoramaRateControlPreset::Default),
        MakeShared<EPanoramaRateControlPreset>(EPanoramaRateControlPreset::LowLatency),
        MakeShared<EPanoramaRateControlPreset>(EPanoramaRateControlPreset::HighQuality)
    };

    RefreshComponentFromSelection();

    ChildSlot
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(4.f)
        [
            SAssignNew(ComponentCombo, SComboBox<TSharedPtr<FPanoramaComponentEntry>>)
            .OptionsSource(&ComponentItems)
            .OnGenerateWidget_Lambda([](TSharedPtr<FPanoramaComponentEntry> Item)
            {
                return SNew(STextBlock).Text(Item.IsValid() ? FText::FromString(Item->DisplayName) : FText::FromString(TEXT("None")));
            })
            .OnSelectionChanged_Lambda([this](TSharedPtr<FPanoramaComponentEntry> InItem, ESelectInfo::Type InType)
            {
                ActiveItem = InItem;
                if (ActiveItem.IsValid())
                {
                    SelectedComponent = ActiveItem->Component.Get();
                }
            })
            .InitiallySelectedItem(ActiveItem)
            [
                SNew(STextBlock)
                .Text_Lambda([this]() -> FText
                {
                    if (ActiveItem.IsValid())
                    {
                        return FText::FromString(ActiveItem->DisplayName);
                    }
                    return FText::FromString(TEXT("Select Panorama Component"));
                })
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(4.f)
        [
            SNew(SSeparator)
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(4.f)
        [
            SNew(SButton)
            .Text_Lambda([this]() { return GetStartStopButtonText(); })
            .OnClicked(this, &SPanoramaCapturePanel::HandleStartStopButton)
            .IsEnabled_Lambda([this]() { return SelectedComponent.IsValid(); })
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(4.f)
        [
            SNew(SCheckBox)
            .OnCheckStateChanged(this, &SPanoramaCapturePanel::HandlePreviewToggled)
            .IsChecked_Lambda([this]() { return GetPreviewCheckState(); })
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("Preview Enabled")))
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(4.f)
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("Video Settings")))
            .Font(FAppStyle::Get().GetFontStyle("HeadingExtraSmall"))
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(4.f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("Output")))
            ]
            + SHorizontalBox::Slot()
            .Padding(8.f, 0.f)
            .AutoWidth()
            [
                SAssignNew(OutputFormatCombo, SComboBox<TSharedPtr<EPanoramaOutputFormat>>)
                .OptionsSource(&OutputFormatOptions)
                .OnGenerateWidget_Lambda([](TSharedPtr<EPanoramaOutputFormat> Item)
                {
                    const FString Label = Item.IsValid() && *Item == EPanoramaOutputFormat::NVENC ? TEXT("NVENC Hardware") : TEXT("PNG Sequence");
                    return SNew(STextBlock).Text(FText::FromString(Label));
                })
                .OnSelectionChanged(this, &SPanoramaCapturePanel::HandleOutputFormatChanged)
                .InitiallySelectedItem(SelectedOutputFormat)
                [
                    SNew(STextBlock)
                    .Text_Lambda([this]() { return GetFormatSummaryText(); })
                ]
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(16.f, 0.f, 0.f, 0.f)
            .VAlign(VAlign_Center)
            [
                SNew(SCheckBox)
                .OnCheckStateChanged(this, &SPanoramaCapturePanel::HandleHEVCToggled)
                .IsChecked_Lambda([this]()
                {
                    return (SelectedComponent.IsValid() && SelectedComponent->VideoSettings.bUseHEVC) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                })
                .IsEnabled_Lambda([this]() { return SelectedComponent.IsValid() && !SelectedComponent->IsCapturing(); })
                [
                    SNew(STextBlock).Text(FText::FromString(TEXT("Use HEVC")))
                ]
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(4.f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock).Text(FText::FromString(TEXT("Mode")))
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(8.f, 0.f)
            [
                SAssignNew(CaptureModeCombo, SComboBox<TSharedPtr<EPanoramaCaptureMode>>)
                .OptionsSource(&CaptureModeOptions)
                .OnGenerateWidget_Lambda([](TSharedPtr<EPanoramaCaptureMode> Item)
                {
                    const FString Label = (Item.IsValid() && *Item == EPanoramaCaptureMode::Stereo) ? TEXT("Stereo") : TEXT("Mono");
                    return SNew(STextBlock).Text(FText::FromString(Label));
                })
                .OnSelectionChanged(this, &SPanoramaCapturePanel::HandleCaptureModeChanged)
                .InitiallySelectedItem(SelectedCaptureMode)
                [
                    SNew(STextBlock).Text_Lambda([this]()
                    {
                        if (!SelectedComponent.IsValid())
                        {
                            return FText::FromString(TEXT("Mode"));
                        }
                        return SelectedComponent->VideoSettings.CaptureMode == EPanoramaCaptureMode::Stereo ? FText::FromString(TEXT("Stereo")) : FText::FromString(TEXT("Mono"));
                    })
                ]
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(16.f, 0.f, 0.f, 0.f)
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock).Text(FText::FromString(TEXT("Stereo Layout")))
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(8.f, 0.f)
            [
                SAssignNew(StereoLayoutCombo, SComboBox<TSharedPtr<EPanoramaStereoLayout>>)
                .OptionsSource(&StereoLayoutOptions)
                .OnGenerateWidget_Lambda([](TSharedPtr<EPanoramaStereoLayout> Item)
                {
                    const FString Label = (Item.IsValid() && *Item == EPanoramaStereoLayout::SideBySide) ? TEXT("Side-by-Side") : TEXT("Top-Bottom");
                    return SNew(STextBlock).Text(FText::FromString(Label));
                })
                .OnSelectionChanged(this, &SPanoramaCapturePanel::HandleStereoLayoutChanged)
                .InitiallySelectedItem(SelectedStereoLayout)
                .IsEnabled_Lambda([this]()
                {
                    return SelectedComponent.IsValid() && SelectedComponent->VideoSettings.CaptureMode == EPanoramaCaptureMode::Stereo && !SelectedComponent->IsCapturing();
                })
                [
                    SNew(STextBlock).Text_Lambda([this]()
                    {
                        if (!SelectedComponent.IsValid())
                        {
                            return FText::FromString(TEXT("Layout"));
                        }
                        return (SelectedComponent->VideoSettings.StereoLayout == EPanoramaStereoLayout::SideBySide)
                            ? FText::FromString(TEXT("Side-by-Side"))
                            : FText::FromString(TEXT("Top-Bottom"));
                    })
                ]
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(16.f, 0.f, 0.f, 0.f)
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock).Text(FText::FromString(TEXT("Gamma")))
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(8.f, 0.f)
            [
                SAssignNew(GammaCombo, SComboBox<TSharedPtr<EPanoramaGamma>>)
                .OptionsSource(&GammaOptions)
                .OnGenerateWidget_Lambda([](TSharedPtr<EPanoramaGamma> Item)
                {
                    const FString Label = (Item.IsValid() && *Item == EPanoramaGamma::Linear) ? TEXT("Linear") : TEXT("sRGB");
                    return SNew(STextBlock).Text(FText::FromString(Label));
                })
                .OnSelectionChanged(this, &SPanoramaCapturePanel::HandleGammaChanged)
                .InitiallySelectedItem(SelectedGamma)
                [
                    SNew(STextBlock).Text_Lambda([this]()
                    {
                        if (!SelectedComponent.IsValid())
                        {
                            return FText::FromString(TEXT("Gamma"));
                        }
                        return SelectedComponent->VideoSettings.Gamma == EPanoramaGamma::Linear ? FText::FromString(TEXT("Linear")) : FText::FromString(TEXT("sRGB"));
                    })
                ]
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(16.f, 0.f, 0.f, 0.f)
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock).Text(FText::FromString(TEXT("Color Format")))
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(8.f, 0.f)
            [
                SAssignNew(ColorFormatCombo, SComboBox<TSharedPtr<EPanoramaColorFormat>>)
                .OptionsSource(&ColorFormatOptions)
                .OnGenerateWidget_Lambda([](TSharedPtr<EPanoramaColorFormat> Item)
                {
                    FString Label = TEXT("NV12 8-bit");
                    if (Item.IsValid())
                    {
                        switch (*Item)
                        {
                        case EPanoramaColorFormat::NV12:
                            Label = TEXT("NV12 8-bit");
                            break;
                        case EPanoramaColorFormat::P010:
                            Label = TEXT("P010 10-bit");
                            break;
                        case EPanoramaColorFormat::BGRA8:
                            Label = TEXT("BGRA 8-bit");
                            break;
                        default:
                            break;
                        }
                    }
                    return SNew(STextBlock).Text(FText::FromString(Label));
                })
                .OnSelectionChanged(this, &SPanoramaCapturePanel::HandleColorFormatChanged)
                .InitiallySelectedItem(SelectedColorFormat)
                .IsEnabled_Lambda([this]() { return SelectedComponent.IsValid() && !SelectedComponent->IsCapturing(); })
                [
                    SNew(STextBlock).Text_Lambda([this]()
                    {
                        return GetColorFormatSummaryText();
                    })
                ]
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(4.f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock).Text(FText::FromString(TEXT("Bitrate (Mbps)")))
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(8.f, 0.f)
            [
                SNew(SSpinBox<float>)
                .MinValue(1.f)
                .MaxValue(500.f)
                .Delta(1.f)
                .Value_Lambda([this]()
                {
                    return SelectedComponent.IsValid() ? static_cast<float>(SelectedComponent->VideoSettings.TargetBitrateMbps) : 0.f;
                })
                .OnValueCommitted(this, &SPanoramaCapturePanel::HandleBitrateCommitted)
                .IsEnabled_Lambda([this]() { return SelectedComponent.IsValid() && !SelectedComponent->IsCapturing(); })
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(16.f, 0.f)
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock).Text(FText::FromString(TEXT("GOP")))
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(8.f, 0.f)
            [
                SNew(SSpinBox<float>)
                .MinValue(1.f)
                .MaxValue(300.f)
                .Delta(1.f)
                .Value_Lambda([this]()
                {
                    return SelectedComponent.IsValid() ? static_cast<float>(SelectedComponent->VideoSettings.GOPLength) : 0.f;
                })
                .OnValueCommitted(this, &SPanoramaCapturePanel::HandleGOPCommitted)
                .IsEnabled_Lambda([this]() { return SelectedComponent.IsValid() && !SelectedComponent->IsCapturing(); })
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(16.f, 0.f)
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock).Text(FText::FromString(TEXT("B-Frames")))
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(8.f, 0.f)
            [
                SNew(SSpinBox<float>)
                .MinValue(0.f)
                .MaxValue(6.f)
                .Delta(1.f)
                .Value_Lambda([this]()
                {
                    return SelectedComponent.IsValid() ? static_cast<float>(SelectedComponent->VideoSettings.NumBFrames) : 0.f;
                })
                .OnValueCommitted(this, &SPanoramaCapturePanel::HandleBFramesCommitted)
                .IsEnabled_Lambda([this]() { return SelectedComponent.IsValid() && !SelectedComponent->IsCapturing(); })
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(16.f, 0.f)
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock).Text(FText::FromString(TEXT("Rate Control")))
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(8.f, 0.f)
            [
                SAssignNew(RateControlCombo, SComboBox<TSharedPtr<EPanoramaRateControlPreset>>)
                .OptionsSource(&RateControlOptions)
                .OnGenerateWidget_Lambda([](TSharedPtr<EPanoramaRateControlPreset> Item)
                {
                    FString Label = TEXT("Default");
                    if (Item.IsValid())
                    {
                        switch (*Item)
                        {
                        case EPanoramaRateControlPreset::LowLatency:
                            Label = TEXT("Low Latency");
                            break;
                        case EPanoramaRateControlPreset::HighQuality:
                            Label = TEXT("High Quality");
                            break;
                        default:
                            Label = TEXT("Default");
                            break;
                        }
                    }
                    return SNew(STextBlock).Text(FText::FromString(Label));
                })
                .OnSelectionChanged(this, &SPanoramaCapturePanel::HandleRateControlChanged)
                .InitiallySelectedItem(SelectedRateControl)
                .IsEnabled_Lambda([this]()
                {
                    return SelectedComponent.IsValid() && SelectedComponent->VideoSettings.OutputFormat == EPanoramaOutputFormat::NVENC && !SelectedComponent->IsCapturing();
                })
                [
                    SNew(STextBlock).Text_Lambda([this]()
                    {
                        if (!SelectedComponent.IsValid())
                        {
                            return FText::FromString(TEXT("RC"));
                        }
                        switch (SelectedComponent->VideoSettings.RateControlPreset)
                        {
                        case EPanoramaRateControlPreset::LowLatency:
                            return FText::FromString(TEXT("Low Latency"));
                        case EPanoramaRateControlPreset::HighQuality:
                            return FText::FromString(TEXT("High Quality"));
                        default:
                            return FText::FromString(TEXT("Default"));
                        }
                    })
                ]
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(4.f)
        [
            SNew(STextBlock)
            .Text_Lambda([this]() { return GetStatusText(); })
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(4.f)
        [
            SNew(STextBlock)
            .ColorAndOpacity(this, &SPanoramaCapturePanel::GetBufferWarningColor)
            .Text_Lambda([this]() { return GetBufferStatusText(); })
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(4.f)
        [
            SNew(SProgressBar)
            .Percent_Lambda([this]() -> TOptional<float>
            {
                if (!SelectedComponent.IsValid())
                {
                    return 0.f;
                }
                return FMath::Clamp(SelectedComponent->GetCaptureStatus().RingBufferFill, 0.f, 1.f);
            })
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(4.f)
        [
            SNew(STextBlock)
            .Text(this, &SPanoramaCapturePanel::GetPTSStatusText)
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(4.f)
        [
            SNew(STextBlock)
            .Text(this, &SPanoramaCapturePanel::GetNVENCStatusText)
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(4.f)
        [
            SNew(STextBlock)
            .Text_Lambda([this]() { return GetWarningText(); })
            .ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.6f, 0.0f)))
            .WrapTextAt(480.f)
        ]
    ];
}

void SPanoramaCapturePanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
    RefreshComponentFromSelection();
}

FReply SPanoramaCapturePanel::HandleStartStopButton()
{
    if (!SelectedComponent.IsValid())
    {
        return FReply::Handled();
    }

    if (SelectedComponent->IsCapturing())
    {
        SelectedComponent->StopCapture();
    }
    else
    {
        SelectedComponent->StartCapture();
    }

    return FReply::Handled();
}

FText SPanoramaCapturePanel::GetStartStopButtonText() const
{
    if (SelectedComponent.IsValid() && SelectedComponent->IsCapturing())
    {
        return FText::FromString(TEXT("Stop Capture"));
    }
    return FText::FromString(TEXT("Start Capture"));
}

FText SPanoramaCapturePanel::GetStatusText() const
{
    if (!SelectedComponent.IsValid())
    {
        return FText::FromString(TEXT("No component selected"));
    }

    const FPanoramicCaptureStatus Status = SelectedComponent->GetCaptureStatus();
    FString Mode = SelectedComponent->VideoSettings.CaptureMode == EPanoramaCaptureMode::Stereo ? TEXT("Stereo") : TEXT("Mono");
    if (Status.bUsingFallback)
    {
        Mode += TEXT(" (Fallback)");
    }
    FNumberFormattingOptions TimeFormat;
    TimeFormat.SetMinimumFractionalDigits(1);
    TimeFormat.SetMaximumFractionalDigits(1);

    const FString StatusLabel = Status.bIsCapturing ? TEXT("Capturing") : TEXT("Idle");

    return FText::Format(NSLOCTEXT("PanoramaCapture", "StatusText", "Status: {0} | Mode: {1} | Dropped: {2} | Time: {3} s"),
        FText::FromString(StatusLabel),
        FText::FromString(Mode),
        FText::AsNumber(Status.DroppedFrames),
        FText::AsNumber(Status.CurrentCaptureTimeSeconds, &TimeFormat));
}

FText SPanoramaCapturePanel::GetBufferStatusText() const
{
    if (!SelectedComponent.IsValid())
    {
        return FText::GetEmpty();
    }

    const int32 Capacity = SelectedComponent->GetRingBufferCapacity();
    const FPanoramicCaptureStatus Status = SelectedComponent->GetCaptureStatus();
    const int32 Occupancy = Status.PendingFrameCount;
    const float FillPercent = FMath::Clamp(Status.RingBufferFill * 100.0f, 0.0f, 100.0f);

    FNumberFormattingOptions PercentFormat;
    PercentFormat.SetMinimumFractionalDigits(0);
    PercentFormat.SetMaximumFractionalDigits(0);

    return FText::Format(NSLOCTEXT("PanoramaCapture", "BufferStatus", "Buffer: {0}/{1} ({2}% used)"),
        FText::AsNumber(Occupancy),
        FText::AsNumber(Capacity),
        FText::AsNumber(FillPercent, &PercentFormat));
}

FText SPanoramaCapturePanel::GetPTSStatusText() const
{
    if (!SelectedComponent.IsValid())
    {
        return FText::GetEmpty();
    }

    const FPanoramicCaptureStatus Status = SelectedComponent->GetCaptureStatus();
    FNumberFormattingOptions Format;
    Format.SetMinimumFractionalDigits(2);
    Format.SetMaximumFractionalDigits(2);
    const double Delta = FMath::Abs(Status.LastVideoPTS - Status.LastAudioPTS);

    return FText::Format(NSLOCTEXT("PanoramaCapture", "PTSStatus", "Video PTS: {0}s | Audio PTS: {1}s | Δ: {2}s"),
        FText::AsNumber(Status.LastVideoPTS, &Format),
        FText::AsNumber(Status.LastAudioPTS, &Format),
        FText::AsNumber(Delta, &Format));
}

FText SPanoramaCapturePanel::GetNVENCStatusText() const
{
    if (!SelectedComponent.IsValid())
    {
        return FText::GetEmpty();
    }

    const FPanoramicCaptureStatus Status = SelectedComponent->GetCaptureStatus();
    const FString EncoderLabel = Status.bUsingNVENC ? TEXT("NVENC Hardware") : TEXT("PNG Sequence");
    return FText::Format(NSLOCTEXT("PanoramaCapture", "NVENCStatus", "Video Encoder: {0}"), FText::FromString(EncoderLabel));
}

FText SPanoramaCapturePanel::GetWarningText() const
{
    if (!SelectedComponent.IsValid())
    {
        return FText::GetEmpty();
    }

    const FString& Warning = SelectedComponent->GetCaptureStatus().LastWarning;
    return Warning.IsEmpty() ? FText::GetEmpty() : FText::FromString(Warning);
}

FSlateColor SPanoramaCapturePanel::GetBufferWarningColor() const
{
    if (!SelectedComponent.IsValid())
    {
        return FSlateColor::UseForeground();
    }

    const float Fill = SelectedComponent->GetCaptureStatus().RingBufferFill;
    if (Fill >= 0.9f)
    {
        return FSlateColor(FLinearColor::Red);
    }
    if (Fill >= 0.7f)
    {
        return FSlateColor(FLinearColor::Yellow);
    }
    return FSlateColor::UseForeground();
}

ECheckBoxState SPanoramaCapturePanel::GetPreviewCheckState() const
{
    return bRequestPreviewToggle ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

FText SPanoramaCapturePanel::GetFormatSummaryText() const
{
    if (!SelectedComponent.IsValid())
    {
        return FText::FromString(TEXT("Output"));
    }

    return SelectedComponent->VideoSettings.OutputFormat == EPanoramaOutputFormat::NVENC ? FText::FromString(TEXT("NVENC Hardware")) : FText::FromString(TEXT("PNG Sequence"));
}

FText SPanoramaCapturePanel::GetColorFormatSummaryText() const
{
    if (!SelectedComponent.IsValid())
    {
        return FText::FromString(TEXT("Format"));
    }

    switch (SelectedComponent->VideoSettings.ColorFormat)
    {
    case EPanoramaColorFormat::NV12:
        return FText::FromString(TEXT("NV12 8-bit"));
    case EPanoramaColorFormat::P010:
        return FText::FromString(TEXT("P010 10-bit"));
    case EPanoramaColorFormat::BGRA8:
        return FText::FromString(TEXT("BGRA 8-bit"));
    default:
        break;
    }
    return FText::FromString(TEXT("Format"));
}

void SPanoramaCapturePanel::RefreshComponentFromSelection()
{
    ComponentItems.Reset();
    ActiveItem.Reset();

#if WITH_EDITOR
    if (GEditor)
    {
        if (UWorld* EditorWorld = GEditor->GetEditorWorldContext().World())
        {
            for (TActorIterator<AActor> It(EditorWorld); It; ++It)
            {
                if (UPanoramaCaptureComponent* Component = It->FindComponentByClass<UPanoramaCaptureComponent>())
                {
                    TSharedPtr<FPanoramaComponentEntry> Entry = MakeShared<FPanoramaComponentEntry>();
                    Entry->Component = Component;
                    Entry->DisplayName = It->GetActorLabel();
                    ComponentItems.Add(Entry);
                    if (Component == SelectedComponent.Get())
                    {
                        ActiveItem = Entry;
                    }
                }
            }
        }
    }
#endif

    if (!ActiveItem.IsValid() && ComponentItems.Num() > 0)
    {
        ActiveItem = ComponentItems[0];
        SelectedComponent = ActiveItem->Component.Get();
    }

    if (SelectedComponent.IsValid())
    {
        auto SelectOption = [](const auto& Options, auto Value)
        {
            for (const auto& Option : Options)
            {
                if (Option.IsValid() && *Option == Value)
                {
                    return Option;
                }
            }
            return Options.Num() > 0 ? Options[0] : nullptr;
        };

        const FPanoramicCaptureStatus StatusSnapshot = SelectedComponent->GetCaptureStatus();
        const FPanoramicVideoSettings& SourceSettings = (SelectedComponent->IsCapturing() || StatusSnapshot.bUsingFallback)
            ? StatusSnapshot.EffectiveVideoSettings
            : SelectedComponent->VideoSettings;

        SelectedOutputFormat = SelectOption(OutputFormatOptions, SourceSettings.OutputFormat);
        SelectedCaptureMode = SelectOption(CaptureModeOptions, SourceSettings.CaptureMode);
        SelectedGamma = SelectOption(GammaOptions, SourceSettings.Gamma);
        SelectedColorFormat = SelectOption(ColorFormatOptions, SourceSettings.ColorFormat);
        SelectedStereoLayout = SelectOption(StereoLayoutOptions, SourceSettings.StereoLayout);
        SelectedRateControl = SelectOption(RateControlOptions, SourceSettings.RateControlPreset);

        if (OutputFormatCombo.IsValid())
        {
            OutputFormatCombo->SetSelectedItem(SelectedOutputFormat);
        }
        if (CaptureModeCombo.IsValid())
        {
            CaptureModeCombo->SetSelectedItem(SelectedCaptureMode);
        }
        if (GammaCombo.IsValid())
        {
            GammaCombo->SetSelectedItem(SelectedGamma);
        }
        if (ColorFormatCombo.IsValid())
        {
            ColorFormatCombo->SetSelectedItem(SelectedColorFormat);
        }
        if (StereoLayoutCombo.IsValid())
        {
            StereoLayoutCombo->SetSelectedItem(SelectedStereoLayout);
        }
        if (RateControlCombo.IsValid())
        {
            RateControlCombo->SetSelectedItem(SelectedRateControl);
        }
    }

    if (ComponentCombo.IsValid())
    {
        ComponentCombo->RefreshOptions();
        ComponentCombo->SetSelectedItem(ActiveItem);
    }
}

void SPanoramaCapturePanel::ApplyVideoSettings(TFunctionRef<void(FPanoramicVideoSettings&)> Mutator)
{
    if (!SelectedComponent.IsValid())
    {
        return;
    }

    FPanoramicVideoSettings Updated = SelectedComponent->VideoSettings;
    Mutator(Updated);
    SelectedComponent->VideoSettings = Updated;
}

void SPanoramaCapturePanel::ApplyAudioSettings(TFunctionRef<void(FPanoramicAudioSettings&)> Mutator)
{
    if (!SelectedComponent.IsValid())
    {
        return;
    }

    FPanoramicAudioSettings Updated = SelectedComponent->AudioSettings;
    Mutator(Updated);
    SelectedComponent->AudioSettings = Updated;
}

void SPanoramaCapturePanel::HandleHEVCToggled(ECheckBoxState NewState)
{
    if (!SelectedComponent.IsValid() || SelectedComponent->IsCapturing())
    {
        return;
    }

    const bool bNewValue = (NewState == ECheckBoxState::Checked);
    ApplyVideoSettings([bNewValue](FPanoramicVideoSettings& Settings)
    {
        Settings.bUseHEVC = bNewValue;
    });
}

void SPanoramaCapturePanel::HandleOutputFormatChanged(TSharedPtr<EPanoramaOutputFormat> InFormat, ESelectInfo::Type)
{
    if (!SelectedComponent.IsValid() || !InFormat.IsValid() || SelectedComponent->IsCapturing())
    {
        return;
    }

    SelectedOutputFormat = InFormat;
    ApplyVideoSettings([InFormat](FPanoramicVideoSettings& Settings)
    {
        Settings.OutputFormat = *InFormat;
    });
}

void SPanoramaCapturePanel::HandleCaptureModeChanged(TSharedPtr<EPanoramaCaptureMode> InMode, ESelectInfo::Type)
{
    if (!SelectedComponent.IsValid() || !InMode.IsValid() || SelectedComponent->IsCapturing())
    {
        return;
    }

    SelectedCaptureMode = InMode;
    ApplyVideoSettings([InMode](FPanoramicVideoSettings& Settings)
    {
        Settings.CaptureMode = *InMode;
    });
    SelectedComponent->ReinitializeRig();
}

void SPanoramaCapturePanel::HandleGammaChanged(TSharedPtr<EPanoramaGamma> InGamma, ESelectInfo::Type)
{
    if (!SelectedComponent.IsValid() || !InGamma.IsValid() || SelectedComponent->IsCapturing())
    {
        return;
    }

    SelectedGamma = InGamma;
    ApplyVideoSettings([InGamma](FPanoramicVideoSettings& Settings)
    {
        Settings.Gamma = *InGamma;
    });
}

void SPanoramaCapturePanel::HandleColorFormatChanged(TSharedPtr<EPanoramaColorFormat> InFormat, ESelectInfo::Type)
{
    if (!SelectedComponent.IsValid() || !InFormat.IsValid() || SelectedComponent->IsCapturing())
    {
        return;
    }

    SelectedColorFormat = InFormat;
    ApplyVideoSettings([InFormat](FPanoramicVideoSettings& Settings)
    {
        Settings.ColorFormat = *InFormat;
    });
}

void SPanoramaCapturePanel::HandleStereoLayoutChanged(TSharedPtr<EPanoramaStereoLayout> InLayout, ESelectInfo::Type)
{
    if (!SelectedComponent.IsValid() || !InLayout.IsValid() || SelectedComponent->IsCapturing())
    {
        return;
    }

    SelectedStereoLayout = InLayout;
    ApplyVideoSettings([InLayout](FPanoramicVideoSettings& Settings)
    {
        Settings.StereoLayout = *InLayout;
    });
}

void SPanoramaCapturePanel::HandleRateControlChanged(TSharedPtr<EPanoramaRateControlPreset> InPreset, ESelectInfo::Type)
{
    if (!SelectedComponent.IsValid() || !InPreset.IsValid() || SelectedComponent->IsCapturing())
    {
        return;
    }

    SelectedRateControl = InPreset;
    ApplyVideoSettings([InPreset](FPanoramicVideoSettings& Settings)
    {
        Settings.RateControlPreset = *InPreset;
    });
}

void SPanoramaCapturePanel::HandlePreviewToggled(ECheckBoxState NewState)
{
    bRequestPreviewToggle = (NewState == ECheckBoxState::Checked);
    if (SelectedComponent.IsValid())
    {
        SelectedComponent->SetPreviewEnabled(bRequestPreviewToggle);
    }
}

void SPanoramaCapturePanel::HandleBitrateCommitted(float NewValue, ETextCommit::Type CommitType)
{
    if (CommitType == ETextCommit::Default || !SelectedComponent.IsValid() || SelectedComponent->IsCapturing())
    {
        return;
    }

    const int32 Rounded = FMath::Max(1, FMath::RoundToInt(NewValue));
    ApplyVideoSettings([Rounded](FPanoramicVideoSettings& Settings)
    {
        Settings.TargetBitrateMbps = Rounded;
    });
}

void SPanoramaCapturePanel::HandleGOPCommitted(float NewValue, ETextCommit::Type CommitType)
{
    if (CommitType == ETextCommit::Default || !SelectedComponent.IsValid() || SelectedComponent->IsCapturing())
    {
        return;
    }

    const int32 Rounded = FMath::Clamp(FMath::RoundToInt(NewValue), 1, 300);
    ApplyVideoSettings([Rounded](FPanoramicVideoSettings& Settings)
    {
        Settings.GOPLength = Rounded;
    });
}

void SPanoramaCapturePanel::HandleBFramesCommitted(float NewValue, ETextCommit::Type CommitType)
{
    if (CommitType == ETextCommit::Default || !SelectedComponent.IsValid() || SelectedComponent->IsCapturing())
    {
        return;
    }

    const int32 Rounded = FMath::Clamp(FMath::RoundToInt(NewValue), 0, 6);
    ApplyVideoSettings([Rounded](FPanoramicVideoSettings& Settings)
    {
        Settings.NumBFrames = Rounded;
    });
}
