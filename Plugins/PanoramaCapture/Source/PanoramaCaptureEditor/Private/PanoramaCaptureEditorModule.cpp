#include "PanoramaCaptureEditorModule.h"
#include "SPanoramaCapturePanel.h"
#include "PanoramaCaptureLog.h"
#include "ToolMenus.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

static const FName PanoramaCaptureTabName(TEXT("PanoramaCapturePanel"));

void FPanoramaCaptureEditorModule::StartupModule()
{
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(PanoramaCaptureTabName, FOnSpawnTab::CreateRaw(this, &FPanoramaCaptureEditorModule::SpawnPanoramaTab))
        .SetDisplayName(NSLOCTEXT("PanoramaCapture", "TabTitle", "Panorama Capture"))
        .SetTooltipText(NSLOCTEXT("PanoramaCapture", "TabTooltip", "Panorama capture control panel"))
        .SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory());

    if (UToolMenus::IsToolMenuUIEnabled())
    {
        MenuExtenderHandle = UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FPanoramaCaptureEditorModule::RegisterMenus));
    }
}

void FPanoramaCaptureEditorModule::ShutdownModule()
{
    if (UToolMenus::Get())
    {
        UToolMenus::UnRegisterStartupCallback(MenuExtenderHandle);
        UToolMenus::UnregisterOwner(this);
    }

    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(PanoramaCaptureTabName);
}

TSharedRef<SDockTab> FPanoramaCaptureEditorModule::SpawnPanoramaTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        [
            SNew(SPanoramaCapturePanel)
        ];
}

void FPanoramaCaptureEditorModule::RegisterMenus()
{
    FToolMenuOwnerScoped OwnerScoped(this);
    if (UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window"))
    {
        FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
        Section.AddMenuEntry(
            TEXT("OpenPanoramaCapture"),
            NSLOCTEXT("PanoramaCapture", "OpenPanel", "Panorama Capture"),
            NSLOCTEXT("PanoramaCapture", "OpenPanelTooltip", "Open the panorama capture control panel."),
            FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([]() { FGlobalTabmanager::Get()->TryInvokeTab(PanoramaCaptureTabName); }))
        );
    }

    if (UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar"))
    {
        FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("Settings");
        Section.AddEntry(FToolMenuEntry::InitToolBarButton(
            TEXT("PanoramaCaptureToolbarButton"),
            FUIAction(FExecuteAction::CreateLambda([]() { FGlobalTabmanager::Get()->TryInvokeTab(PanoramaCaptureTabName); })),
            NSLOCTEXT("PanoramaCapture", "ToolbarButton", "Panorama"),
            NSLOCTEXT("PanoramaCapture", "ToolbarButtonTooltip", "Open the panorama capture panel"),
            FSlateIcon()
        ));
    }
}

IMPLEMENT_MODULE(FPanoramaCaptureEditorModule, PanoramaCaptureEditor)
