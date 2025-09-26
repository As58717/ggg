using UnrealBuildTool;
using System.IO;

public class PanoramaCaptureEditor : ModuleRules
{
    public PanoramaCaptureEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "Slate",
                "SlateCore"
            });

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "PanoramaCapture",
                "UnrealEd",
                "LevelEditor",
                "EditorSubsystem",
                "ToolMenus"
            });

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicDefinitions.Add("PANORAMA_EDITOR_SUPPORTED=1");
        }
        else
        {
            PublicDefinitions.Add("PANORAMA_EDITOR_SUPPORTED=0");
        }
    }
}
