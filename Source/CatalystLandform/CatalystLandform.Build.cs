using UnrealBuildTool;
using System.IO;
using System.Diagnostics;

public class CatalystLandform : ModuleRules
{
    public CatalystLandform(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "VoxelCore", "VoxelGraph", "Voxel", "CatalystFoundation", "DeveloperSettings" });
        PrivateDependencyModuleNames.AddRange(new[] { "GeometryCore", "Json", "JsonUtilities", "CatalystConnect", "PCG", "CatalystLandformRender", "GeoReferencing", "PROJ", "SQLiteCore" });
        FPSemantics = FPSemanticsMode.Precise;
        bool SpatialSIMD = Target.Platform == UnrealTargetPlatform.Mac && !Target.Architectures.bIsMultiArch && Target.Architecture == UnrealArch.Arm64;
        PrivateDefinitions.Add("CL_SPATIAL_SIMD=" + (SpatialSIMD ? "1" : "0"));
        if (SpatialSIMD)
        {
            string Script = Path.Combine(PluginDirectory, "Tools", "build-spatial-ispc.py");
            ExternalDependencies.Add(Script);
            ExternalDependencies.Add(Path.Combine(ModuleDirectory, "Private", "Model", "CLSpatialMorph.ispc"));
            // Generate the header/archive before UBT resolves compilation inputs.
            // The content-addressed pre-build step also handles cached makefiles.
            var Start = new ProcessStartInfo("python3") { UseShellExecute = false };
            Start.ArgumentList.Add(Script);
            using (var Build = Process.Start(Start))
            {
                if (Build == null) throw new BuildException("Could not start Landform's ISPC build");
                Build.WaitForExit();
                if (Build.ExitCode != 0) throw new BuildException("Landform spatial ISPC build failed");
            }
            string Generated = Path.Combine(PluginDirectory, "Intermediate", "SpatialSIMD", "Mac", "arm64");
            PrivateIncludePaths.Add(Generated);
            PublicAdditionalLibraries.Add(Path.Combine(Generated, "libCLSpatialMorph.a"));
        }
    }
}
