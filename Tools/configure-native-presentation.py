"""Install the shared presentation function through native VPP attribute post-processing.
Only the owned function, two product surface materials and two MegaMaterials are saved.
"""
import unreal
from pathlib import Path
m=unreal.MaterialEditingLibrary;a=unreal.EditorAssetLibrary;t=unreal.AssetToolsHelpers.get_asset_tools()
path='/CatalystLandform/Surfaces/MF_PlanetPresentation'
f=unreal.load_asset(path) or t.create_asset('MF_PlanetPresentation','/CatalystLandform/Surfaces',unreal.MaterialFunction,unreal.MaterialFunctionFactoryNew())
assert f
m.delete_all_material_expressions_in_function(f)
def node(cls,**props):
 n=m.create_material_expression_in_function(f,cls)
 for k,v in props.items():n.set_editor_property(k,v)
 return n
def connect(s,p,d,i):assert m.connect_material_expressions(s,p,d,i),(p,i)
def custom(inputs,code):
 n=node(unreal.MaterialExpressionCustom,code=code,output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3,include_file_paths=['/Plugin/CatalystLandform/Public/CLWorldAnchoredPresentation.ush'])
 arr=[]
 for name in inputs:
  v=unreal.CustomInput();v.set_editor_property('input_name',name);arr.append(v)
 n.set_editor_property('inputs',arr)
 for name,(s,p) in inputs.items():connect(s,p,n,name)
 return n
i=node(unreal.MaterialExpressionFunctionInput,input_name='Canonical',input_type=unreal.FunctionInputType.FUNCTION_INPUT_MATERIAL_ATTRIBUTES)
b=node(unreal.MaterialExpressionBreakMaterialAttributes);connect(i,'',b,'Attr')
world=node(unreal.MaterialExpressionWorldPosition,world_position_shader_offset=unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
z=node(unreal.MaterialExpressionConstant,r=0);one=node(unreal.MaterialExpressionConstant,r=1);previous=node(unreal.MaterialExpressionPreviousFrameSwitch);connect(z,'',previous,'Current Frame');connect(one,'',previous,'Previous Frame')
wpo=custom({'Source':(world,''),'Previous':(previous,''),'Authored':(b,'WorldPositionOffset')},'return Authored+CLWorldAnchoredOffset(GetPrimitiveData(Parameters),WSToDF(GetWorldPosition(Parameters)),Previous>.5);')
normal=custom({'Source':(world,''),'N':(b,'Normal')},'return CLWorldAnchoredSurfaceNormal(GetPrimitiveData(Parameters),WSToDF(Parameters.LWCData.WorldPosition_NoOffsets),N);')
out=node(unreal.MaterialExpressionMakeMaterialAttributes)
for pin in m.get_material_expression_input_names(out):
 if pin=='Normal':connect(normal,'',out,pin)
 elif pin.replace(' ','')=='WorldPositionOffset':connect(wpo,'',out,pin)
 else:connect(b,pin.replace('CustomizedUVs_','CustomizedUV'),out,pin)
output=node(unreal.MaterialExpressionFunctionOutput,output_name='Presented');connect(out,'',output,'');m.update_material_function(f);assert a.save_loaded_asset(f)
# Shade the original native surface before the post-process bends its position
# and completed normal. Preserve native surface selection and all PBR inputs.
for path in ['/CatalystLandform/Surfaces/M_Surface','/CatalystLandform/Measured/M_MarsSurface']:
 material=unreal.load_asset(path);assert material
 base=m.get_material_property_input_node(material,unreal.MaterialProperty.MP_BASE_COLOR)
 code=base.get_editor_property('code').replace('Parameters.AbsoluteWorldPosition','Parameters.WorldPosition_NoOffsets')
 base.set_editor_property('code',code)
 for world_node in unreal.ObjectIterator(unreal.MaterialExpressionWorldPosition):
  if world_node.get_outer()==material:world_node.set_editor_property('world_position_shader_offset',unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
 m.recompile_material(material);assert a.save_loaded_asset(material)
for path in ['/CatalystLandform/Surfaces/MM_LandformSurfaces','/CatalystLandform/Measured/MM_Mars']:
 asset=unreal.load_asset(path);assert asset
 current=asset.get_editor_property('attribute_post_process');assert current is None or current==f,'Preserve an unrelated attribute post-process: '+path
 asset.set_editor_property('attribute_post_process',f);assert a.save_loaded_asset(asset)
unreal.log('CL_NATIVE_PRESENTATION_MATERIALS_READY')
