using UnrealBuildTool;
public class CatalystLandformEditor : ModuleRules
{
    public CatalystLandformEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "UnrealEd", "AssetDefinition", "CatalystFoundationEditor", "CatalystLandform" });
        PrivateDependencyModuleNames.AddRange(new[] { "CatalystLandformRender", "Voxel", "VoxelCore", "VoxelGraph", "EditorFramework", "CatalystFoundation", "Slate", "SlateCore", "PropertyEditor", "ToolMenus", "AdvancedPreviewScene", "ProceduralMeshComponent", "GeometryFramework", "GeometryCore", "RenderCore", "InputCore", "AssetRegistry", "AssetTools", "Projects", "Json" });
        PrivateIncludePaths.Add(System.IO.Path.Combine(GetModuleDirectory("Voxel"), "Private"));
    }
}
