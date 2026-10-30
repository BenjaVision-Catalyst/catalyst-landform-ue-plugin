using UnrealBuildTool;

public class CatalystLandform : ModuleRules
{
    public CatalystLandform(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "VoxelCore", "VoxelGraph", "Voxel", "CatalystFoundation" });
        PrivateDependencyModuleNames.AddRange(new[] { "Json", "JsonUtilities", "CatalystConnect" });
        FPSemantics = FPSemanticsMode.Precise;
    }
}
