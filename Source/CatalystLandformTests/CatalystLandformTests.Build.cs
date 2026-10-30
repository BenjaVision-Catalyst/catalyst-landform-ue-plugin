using UnrealBuildTool;
public class CatalystLandformTests : ModuleRules
{
    public CatalystLandformTests(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "UnrealEd", "CatalystLandform", "CatalystLandformEditor", "CatalystFoundation", "Voxel", "VoxelCore", "VoxelGraph" });
    }
}
