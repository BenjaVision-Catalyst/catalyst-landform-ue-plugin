#!/usr/bin/env python3
"""Build a qualified Landform compatibility pass from read-only local VPP source."""
import argparse,hashlib,json
from pathlib import Path
IMPL='Source/Voxel/Private/Nanite/VoxelNaniteMaterialRendererImpl.cpp'
# Qualification checks the exact local source before generating any build output.
QUALIFIED={'Source/Voxel/Private/Nanite/VoxelNaniteMaterialRendererImpl.cpp': '6a94418c11558d449565160e4a56648e054f0d80d03dc1830888673d04cb7e9a', 'Source/Voxel/Private/Nanite/VoxelMaterialSelectionCS.h': '6204740fbf83cf075d9feb7627b693ecde3a24b60a4c609cc3a30998bed6a92b', 'Source/Voxel/Private/Nanite/VoxelNaniteComponent.cpp': '4eea298b14c7732995ec929e7f9c9360e8a21aa057cd456afe46b65603a3ec55', 'Source/Voxel/Public/VoxelConfig.h': '9b44e80a36ef4acfbd5fcdf555a9bb5b15bce927254c93116de5f535bc9c18c9'}
def extract(text,signature):
 start=text.index(signature);brace=text.index('{',start);depth=1;i=brace+1
 while depth:
  depth+=(text[i]=='{')-(text[i]=='}');i+=1
 return text[start:i]
def stage(plugin,voxel):
 source=(voxel/IMPL).read_bytes();sha=hashlib.sha256(source).hexdigest()
 for rel,expected in QUALIFIED.items():
  if hashlib.sha256((voxel/rel).read_bytes()).hexdigest()!=expected:raise ValueError('Unknown VPP material adapter input: '+rel+'; no generated output written')
 s=source.decode().replace('\r\n','\n')
 a=s.index('BEGIN_SHADER_PARAMETER_STRUCT(FVoxelNaniteMaterialSelectionPassParameters, )');b=s.index('DECLARE_GPU_STAT',a)
 params=s[a:b].replace('FVoxelNaniteMaterialSelectionPassParameters','FCLVPPSelectionPassParameters')
 pre=extract(s,'void FVoxelNaniteMaterialRendererImpl::PreRenderBasePass_RenderThread(')
 a=pre.index('\tif (QueuedData_RenderThread.IsValid())');b=pre.index('\tif (UsedSurfaceTypes.Num()',a);pre=pre[:a]+pre[b:]
 pre=pre.replace('UsedSurfaceTypes.Num() == 0 ||\n\t\tGVoxelDisableMaterialSelection','UsedSurfaceTypes.Num() == 0')
 compute=extract(s,'void FVoxelNaniteMaterialRendererImpl::ComputeMaterialSelection(')
 compute=compute.replace('MaterialShaderMap->GetShader<FVoxelMaterialSelectionCS>()','TShaderRef<FMaterialShader>::Cast(MaterialShaderMap->GetShader(FindShaderTypeByName(FHashedName(TEXT("FVoxelMaterialSelectionCS")))))')
 compute=compute.replace('Instance->GetInstance()','Instance->GetMaterial()').replace('TSharedPtr<FVoxelMaterialInstanceRef>','TSharedPtr<FVoxelMaterialRef>')
 anchor='\t\t\t\tfor (const auto& It : ShadingPipelineMap)'
 assert compute.count(anchor)==1
 code='\t\t\t\tuint CLExcluded=0,CLExcludedVariants=0;TMap<const FMaterialRenderProxy*,FRHIComputeShader*> CLSeen;\n\t\t\t\tTSet<const FMaterialRenderProxy*> RequiredProxies;\n\t\t\t\tfor(const auto& Entry:Bindings) if(auto* M=Entry.Value->GetMaterial()) RequiredProxies.Add(M->GetRenderProxy());\n'
 compute=compute.replace(anchor,code+anchor)
 anchor='\t\t\t\t\tFNaniteShadingPipeline& ShadingPipeline = *It.Value.ShadingPipeline;'
 assert compute.count(anchor)==1
 compute=compute.replace(anchor,anchor+'\n\t\t\t\t\tif(!RequiredProxies.Contains(ShadingPipeline.MaterialProxy)){++CLExcluded;if(auto* Old=CLSeen.Find(ShadingPipeline.MaterialProxy);Old&&*Old!=ShadingPipeline.ComputeShader)++CLExcludedVariants;CLSeen.Add(ShadingPipeline.MaterialProxy,ShadingPipeline.ComputeShader);continue;}')
 compute=compute.replace('\t\t\t}\n\n\t\t\tfor (const FVoxelSurfaceType SurfaceType', '\t\t\t\tif(!bLoggedBindings){UE_LOG(LogTemp,Display,TEXT("CL_VPP_BOUND_SELECTION: world=%u required=%d excluded=%u excludedShaderVariants=%u"),UniqueId,RequiredProxies.Num(),CLExcluded,CLExcludedVariants);bLoggedBindings=true;}\n\t\t\t}\n\n\t\t\tfor (const FVoxelSurfaceType SurfaceType')
 result=(params+pre+'\n'+compute).replace('FVoxelNaniteMaterialRendererImpl::','FCLVPPSelectionContext::').replace('FVoxelNaniteMaterialSelectionPassParameters','FCLVPPSelectionPassParameters').replace('VoxelNaniteMaterialSelection','CLVPPMaterialSelection')
 target=plugin/'Intermediate/VPPMaterialBridge';output=target/'CLVPPSelection.inc';manifest=target/'manifest.json'
 if output.exists():
  if not manifest.exists() or hashlib.sha256(output.read_bytes()).hexdigest()!=json.loads(manifest.read_text())['outputSHA256']:raise ValueError('Edited or unmanaged generated material adapter; no files written')
 data='// Generated locally from qualified licensed source; do not distribute.\n'+result+'\n'
 target.mkdir(parents=True,exist_ok=True)
 if not output.exists() or output.read_text()!=data:output.write_text(data)
 manifest.write_text(json.dumps({'sourceSHA256':sha,'qualifiedInputs':QUALIFIED,'outputSHA256':hashlib.sha256(data.encode()).hexdigest()},indent=2)+'\n')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--plugin',type=Path,required=True);p.add_argument('--voxel',type=Path,required=True);a=p.parse_args();stage(a.plugin,a.voxel)
