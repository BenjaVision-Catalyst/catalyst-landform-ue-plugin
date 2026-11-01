using UnrealBuildTool;
public class CatalystLandformEditor : ModuleRules
{
    public CatalystLandformEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "UnrealEd", "AssetDefinition", "CatalystFoundationEditor", "CatalystLandform" });
        PrivateDependencyModuleNames.AddRange(new[] { "EditorFramework", "CatalystFoundation", "Slate", "SlateCore", "PropertyEditor", "ToolMenus", "AdvancedPreviewScene", "ProceduralMeshComponent", "RenderCore", "InputCore", "AssetRegistry", "AssetTools", "Projects" });
    }
}
