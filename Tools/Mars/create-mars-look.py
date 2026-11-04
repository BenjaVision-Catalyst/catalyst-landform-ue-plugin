"""Author Mars artwork using native UE materials and native VPP surface/mega-material assets."""
import unreal,json,argparse,sys,shutil
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('--prepared',type=Path,required=True);args=parser.parse_args(sys.argv[1:]);p=args.prepared.resolve()
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(['/CatalystLandform/Surfaces','/CatalystLandform/Measured'],True)
a=unreal.EditorAssetLibrary;t=unreal.AssetToolsHelpers.get_asset_tools();m=unreal.MaterialEditingLibrary;folder='/CatalystLandform/Measured'
def asset(name,cls,factory=None):return unreal.load_asset(folder+'/'+name) or t.create_asset(name,folder,cls,factory)
tex=unreal.load_asset(folder+'/T_MarsColor')
if tex is None:
 task=unreal.AssetImportTask();task.set_editor_property('filename',str(p/'MarsColor8k.png'));task.set_editor_property('destination_path',folder);task.set_editor_property('destination_name','T_MarsColor');task.set_editor_property('automated',True);task.set_editor_property('save',True);t.import_asset_tasks([task]);tex=unreal.load_asset(folder+'/T_MarsColor')
assert tex;tex.set_editor_property('address_x',unreal.TextureAddress.TA_WRAP);tex.set_editor_property('address_y',unreal.TextureAddress.TA_CLAMP);tex.set_editor_property('max_texture_size',8192);assert a.save_loaded_asset(tex)
mat=unreal.load_asset(folder+'/M_MarsSurface') or a.duplicate_asset('/CatalystLandform/Surfaces/M_Surface',folder+'/M_MarsSurface');assert mat
n=m.get_material_property_input_node(mat,unreal.MaterialProperty.MP_BASE_COLOR);assert isinstance(n,unreal.MaterialExpressionCustom)
ins=list(n.get_editor_property('inputs'))
if 'CLMarsColor' not in m.get_material_expression_input_names(n):
 i=unreal.CustomInput();i.set_editor_property('input_name','CLMarsColor');ins.append(i);n.set_editor_property('inputs',ins)
 obj=m.create_material_expression(mat,unreal.MaterialExpressionTextureObjectParameter);obj.set_editor_property('parameter_name','CLMarsColor');obj.set_editor_property('texture',tex);obj.set_editor_property('sampler_type',unreal.MaterialSamplerType.SAMPLERTYPE_COLOR);assert m.connect_material_expressions(obj,'',n,'CLMarsColor')
host=Path(unreal.Paths.project_dir()).resolve();component=host.parents[1]/'catalyst-landform-ue-plugin'
shader=(component/'SourceArt/Surfaces/Appearance.hlsl').read_text()+'\n'+(component/'SourceArt/Surfaces/TexturedAppearance.hlsl').read_text()
# Body-relative DF arithmetic and normalized-preview metadata reuse the qualified adapter.
code='''
CLTexturedAppearanceShader S;float3 C,NN;float R;
FDFVector3 Body=DFSubtract(Parameters.AbsoluteWorldPosition,GetPrimitiveData(Parameters).ActorWorldPosition);if(MappingHigh.a>.5)Body=CLPreviewBodyPosition(Parameters.AbsoluteWorldPosition,MappingHigh,MappingLow);
S.ShadeCanonical(Body,N,Geometry,CLTint,CLDetail,CLTextureDetail,CLBaseColor,CLNormal,CLRoughness,C,R,NN);
float3 B=normalize(DFDemote(Body));float2 UV=float2(frac(atan2(-B.y,B.x)/6.28318530718+1),acos(clamp(B.z,-1,1))/3.14159265359);
float2 Dx=ddx(UV),Dy=ddy(UV);Dx.x-=round(Dx.x);Dy.x-=round(Dy.x);
float3 Context=Texture2DSampleGrad(CLMarsColor,CLMarsColorSampler,UV,Dx,Dy).rgb;
// Viking colour is orbital context, not fine albedo. Nearby scanned detail is authored.
float Near=MappingHigh.a>.5?0:1-smoothstep(2000000,20000000,distance(P,Eye));
C=lerp(Context,C,Near);
Roughness=R;Normal=NN;Emissive=0;return C;
'''
n.set_editor_property('code',shader+'\n'+code);m.recompile_material(mat);assert a.save_loaded_asset(mat)
base=unreal.load_asset('/CatalystLandform/Surfaces/DA_SurfaceLibrary');data=base.get_editor_property('library');entries=list(data.get_editor_property('entries'));types=[]
for e in entries:
 name=str(e.get_editor_property('id'));mi=asset('MI_Mars'+name,unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew());m.set_material_instance_parent(mi,mat);scan=e.get_editor_property('textures');look=e.get_editor_property('appearance')
 for key,field in [('CLBaseColor','base_color'),('CLNormal','normal'),('CLRoughness','roughness')]:m.set_material_instance_texture_parameter_value(mi,key,scan.get_editor_property(field))
 tints={'Rock':(.95,.76,.56),'Scree':(.98,.79,.58),'Soil':(1,.8,.58)};look.set_editor_property('color',unreal.LinearColor(*tints[name],1));e.set_editor_property('appearance',look)
 color=look.get_editor_property('color');color.a=look.get_editor_property('roughness');m.set_material_instance_vector_parameter_value(mi,'CLTint',color);m.set_material_instance_vector_parameter_value(mi,'CLDetail',unreal.LinearColor(look.get_editor_property('grain_meters')*100,look.get_editor_property('grain_strength'),look.get_editor_property('normal_strength'),0));m.set_material_instance_vector_parameter_value(mi,'CLTextureDetail',unreal.LinearColor(scan.get_editor_property('tile_meters')*100,1,0,0));m.set_material_instance_texture_parameter_value(mi,'CLMarsColor',tex);m.update_material_instance(mi);assert a.save_loaded_asset(mi)
 st=asset('ST_Mars'+name,unreal.VoxelSurfaceTypeAsset);st.set_editor_property('material',mi);st.set_editor_property('blend_smoothness',.5);assert a.save_loaded_asset(st);types.append(st);e.set_editor_property('surface_type',st)
data.set_editor_property('entries',entries);lib=asset('DA_MarsSurfaces',unreal.CLSurfaceLibraryAsset);lib.set_editor_property('library',data);lib.update_version_metadata();assert a.save_loaded_asset(lib)
mega=asset('MM_Mars',unreal.VoxelMegaMaterial);mega.set_editor_property('surface_types',types);mega.set_editor_property('detect_new_surfaces',False);mega.set_editor_property('enable_smooth_blends',True);mega.set_editor_property('non_nanite_material_type',unreal.VoxelMegaMaterialGenerationType.GENERATED);assert a.save_loaded_asset(mega)
measured=unreal.load_asset(folder+'/DA_MeasuredTerrain');entries=list(measured.get_editor_property('entries'))
for e in entries:e.set_editor_property('surface_library',lib);e.set_editor_property('material',mega)
measured.set_editor_property('entries',entries);before=[e.get_editor_property('definition_digest') for e in entries];assert measured.bake_digests() is not None;assert before==[e.get_editor_property('definition_digest') for e in measured.get_editor_property('entries')];assert a.save_loaded_asset(measured)
# An owned cook root closes dynamically selected native resources explicitly.
label=asset('PAL_Mars',unreal.PrimaryAssetLabel);label.set_editor_property('is_runtime_label',True);label.set_editor_property('label_assets_in_my_directory',False);label.set_editor_property('explicit_assets',[unreal.load_asset(x) for x in ['/CatalystLandform/Measured/DA_Mars','/CatalystLandform/Measured/DA_MeasuredTerrain','/CatalystLandform/Measured/MM_Mars','/Voxel/Default/VoxelWorldGridMaterial','/Voxel/Default/DefaultStack','/Voxel/Default/DefaultVolumeLayer']]);rules=label.get_editor_property('rules');rules.set_editor_property('cook_rule',unreal.PrimaryAssetCookRule.ALWAYS_COOK);rules.set_editor_property('apply_recursively',True);label.set_editor_property('rules',rules);assert a.save_loaded_asset(label)
shutil.copytree(host/'Plugins/CatalystLandform/Content/Measured',component/'Content/Measured',dirs_exist_ok=True)
unreal.log('MARS_NATIVE_LOOK_READY')
