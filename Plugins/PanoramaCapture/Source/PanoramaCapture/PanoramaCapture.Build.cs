using UnrealBuildTool;
using System.IO;

public class PanoramaCapture : ModuleRules
{
    public PanoramaCapture(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "RenderCore",
                "RHI",
                "Projects",
                "AudioMixer",
                "SlateCore"
            });

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "ApplicationCore",
                "Json",
                "JsonUtilities",
                "ImageWrapper"
            });

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            string ThirdPartyDir = Path.Combine(ModuleDirectory, "..", "..", "ThirdParty", "Win64");
            PublicDefinitions.Add("PANORAMA_WITH_NVENC=1");
            PublicAdditionalLibraries.AddRange(new string[]
            {
                // NVENC and FFmpeg libraries are expected to live under ThirdParty
                Path.Combine(ThirdPartyDir, "nvEncodeAPI64.lib"),
                Path.Combine(ThirdPartyDir, "avcodec.lib"),
                Path.Combine(ThirdPartyDir, "avformat.lib"),
                Path.Combine(ThirdPartyDir, "avutil.lib")
            });
            PublicIncludePaths.Add(ThirdPartyDir);
        }
        else
        {
            PublicDefinitions.Add("PANORAMA_WITH_NVENC=0");
        }

        PublicIncludePaths.AddRange(
            new string[]
            {
                Path.Combine(ModuleDirectory, "Public")
            });

        PrivateIncludePaths.AddRange(
            new string[]
            {
                Path.Combine(ModuleDirectory, "Private")
            });

        RuntimeDependencies.Add(Path.Combine(ModuleDirectory, "..", "..", "Shaders", "PanoramaEquirectCS.usf"));
    }
}
