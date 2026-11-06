using UnrealBuildTool;
public class CatalystLandformTests : ModuleRules
{
    public CatalystLandformTests(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] { "Json", "JsonUtilities", "Core", "CoreUObject", "Engine", "UnrealEd", "CatalystLandform", "CatalystLandformEditor", "CatalystLandformRender", "CatalystFoundation", "Voxel", "VoxelCore", "VoxelGraph", "Projects", "GeoReferencing" });
    }
}
