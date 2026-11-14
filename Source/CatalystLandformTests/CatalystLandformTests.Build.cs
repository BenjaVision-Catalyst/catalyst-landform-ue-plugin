using UnrealBuildTool;
public class CatalystLandformTests : ModuleRules
{
    public CatalystLandformTests(ReadOnlyTargetRules Target) : base(Target)
    {
        // Translation units own file-local helpers; correctness must not depend on the adaptive Git working set.
        bUseUnity = false;
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] { "AssetRegistry", "Json", "JsonUtilities", "Core", "CoreUObject", "Engine", "UnrealEd", "CatalystLandform", "CatalystLandformEditor", "CatalystLandformRender", "CatalystFoundation", "Voxel", "VoxelCore", "VoxelGraph", "Projects", "GeoReferencing", "PCG" });
    }
}
