using UnrealBuildTool;
using System.IO;
using System.Diagnostics;

public class CatalystLandform : ModuleRules
{
    public CatalystLandform(ReadOnlyTargetRules Target) : base(Target)
    {
        // Translation units own file-local helpers; correctness must not depend on the adaptive Git working set.
        bUseUnity = false;
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "VoxelCore", "VoxelGraph", "Voxel", "CatalystFoundation", "DeveloperSettings", "PCG" });
        PrivateDependencyModuleNames.AddRange(new[] { "GeometryCore", "Json", "JsonUtilities", "InputCore", "PCG", "CatalystLandformRender", "GeoReferencing", "PROJ", "SQLiteCore" });
        PrivateDependencyModuleNames.AddRange(new[] { "Renderer", "RenderCore", "RHI" });
        PrivateIncludePaths.Add(Path.Combine(EngineDirectory,"Source/Runtime/Renderer/Private"));
        PrivateIncludePaths.Add(Path.Combine(EngineDirectory,"Source/Runtime/Renderer/Internal"));
        string VoxelPlugin=Path.Combine(Directory.GetParent(PluginDirectory).FullName,"Voxel");
        PrivateIncludePaths.Add(Path.Combine(VoxelPlugin,"Source/Voxel/Private"));
        string BridgeScript=Path.Combine(PluginDirectory,"Tools/stage-vpp-material-bridge.py");
        ExternalDependencies.Add(BridgeScript);
        ExternalDependencies.Add(Path.Combine(VoxelPlugin,"Source/Voxel/Private/Nanite/VoxelNaniteMaterialRendererImpl.cpp"));
        var BridgeStart=new ProcessStartInfo("python3"){UseShellExecute=false};
        BridgeStart.ArgumentList.Add(BridgeScript);BridgeStart.ArgumentList.Add("--plugin");BridgeStart.ArgumentList.Add(PluginDirectory);
        BridgeStart.ArgumentList.Add("--voxel");BridgeStart.ArgumentList.Add(VoxelPlugin);
        using(var Bridge=Process.Start(BridgeStart))
        {if(Bridge==null)throw new BuildException("Cannot stage Landform VPP material adapter");Bridge.WaitForExit();if(Bridge.ExitCode!=0)throw new BuildException("VPP material adapter source qualification failed");}
        PrivateIncludePaths.Add(Path.Combine(PluginDirectory,"Intermediate/VPPMaterialBridge"));
        FPSemantics = FPSemanticsMode.Precise;
        bool SpatialSIMD = Target.Platform == UnrealTargetPlatform.Mac && !Target.Architectures.bIsMultiArch && Target.Architecture == UnrealArch.Arm64;
        PrivateDefinitions.Add("CL_SPATIAL_SIMD=" + (SpatialSIMD ? "1" : "0"));
        if (SpatialSIMD)
        {
            string Script = Path.Combine(PluginDirectory, "Tools", "build-spatial-ispc.py");
            ExternalDependencies.Add(Script);
            ExternalDependencies.Add(Path.Combine(ModuleDirectory, "Private", "Model", "CLSpatialMorph.ispc"));
            string VoxelSource = Path.Combine(Directory.GetParent(PluginDirectory).FullName, "Voxel", "Source");
            foreach (string Module in new[] { "VoxelCore", "VoxelGraph" })
                ExternalDependencies.AddRange(Directory.GetFiles(Path.Combine(VoxelSource, Module, "Public"), "*.isph", SearchOption.AllDirectories));
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
