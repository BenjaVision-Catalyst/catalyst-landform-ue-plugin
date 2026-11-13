using UnrealBuildTool;
public class CatalystLandformEditor : ModuleRules
{
    public CatalystLandformEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        // Translation units own file-local helpers; correctness must not depend on the adaptive Git working set.
        bUseUnity = false;
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "UnrealEd", "AssetDefinition", "CatalystFoundationEditor", "CatalystLandform" });
        PrivateDependencyModuleNames.AddRange(new[] { "CatalystLandformRender", "Voxel", "VoxelCore", "VoxelGraph", "EditorFramework", "CatalystFoundation", "Slate", "SlateCore", "PropertyEditor", "ToolMenus", "AdvancedPreviewScene", "ProceduralMeshComponent", "GeometryFramework", "GeometryCore", "RenderCore", "InputCore", "AssetRegistry", "AssetTools", "Projects", "Json", "JsonUtilities" });
        PrivateIncludePaths.Add(System.IO.Path.Combine(GetModuleDirectory("Voxel"), "Private"));
    }
}
