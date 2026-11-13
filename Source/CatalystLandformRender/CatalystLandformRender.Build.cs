using UnrealBuildTool;
using System.IO;
using System.Diagnostics;
public class CatalystLandformRender : ModuleRules
{
    public CatalystLandformRender(ReadOnlyTargetRules Target) : base(Target)
    {
        if(Target.ProjectFile != null)
        {
            string Script=Path.Combine(PluginDirectory,"Tools","stage-nanite-projection.py");
            ExternalDependencies.Add(Script);
            var Start=new ProcessStartInfo("python3"){UseShellExecute=false};
            Start.ArgumentList.Add(Script);
            Start.ArgumentList.Add("--project");Start.ArgumentList.Add(Target.ProjectFile.Directory.FullName);
            Start.ArgumentList.Add("--engine");Start.ArgumentList.Add(Directory.GetParent(EngineDirectory).FullName);
            using(var Process=System.Diagnostics.Process.Start(Start))
            {if(Process==null) throw new BuildException("Cannot stage Landform projection shaders"); Process.WaitForExit();
             if(Process.ExitCode!=0) throw new BuildException("Landform projection shader source/version check failed");}
        }
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.Add("Core");
        PrivateDependencyModuleNames.AddRange(new[] { "Projects", "RenderCore", "RHI" });
    }
}
