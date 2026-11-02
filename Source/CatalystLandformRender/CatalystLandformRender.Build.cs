using UnrealBuildTool;
public class CatalystLandformRender : ModuleRules
{
    public CatalystLandformRender(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.Add("Core");
        PrivateDependencyModuleNames.AddRange(new[] { "Projects", "RenderCore", "RHI" });
    }
}
