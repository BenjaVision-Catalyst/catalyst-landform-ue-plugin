"""Run in LandformLab editor Python; writes only owned /CatalystLandform/Measured assets."""
import unreal,json,shutil,sys,argparse,hashlib
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('--prepared',type=Path,required=True);args=parser.parse_args(sys.argv[1:]);p=args.prepared.resolve();d=json.loads((p/'manifest.json').read_text());a=unreal.EditorAssetLibrary;t=unreal.AssetToolsHelpers.get_asset_tools();root='/CatalystLandform/Measured'
def asset(name,cls):
 path=root+'/'+name
 return unreal.load_asset(path) or t.create_asset(name,root,cls,None)
def raster(r):
 assert hashlib.sha256((p/r['file']).read_bytes()).hexdigest()==r['sha256']
 h=asset('HM_'+r['name'],unreal.VoxelHeightmap)
 tex=asset('T_'+r['name'],unreal.Texture2D)
 result=unreal.CLMeasuredTerrainTools.import_native_heights(h,tex,str(p/r['file']),r['width']+4,r['height']+4)
 assert result is not None,result
 assert a.save_loaded_asset(tex);assert a.save_loaded_asset(h)
 v=unreal.CLMeasuredRaster()
 for k in ['width','height','westCenterDegrees','northCenterDegrees','longitudeStepDegrees','latitudeStepDegrees','radiusOffsetMeters','bGlobal','northPoleOffsetMeters','southPoleOffsetMeters']:
  if k in r:
   import re
   key=re.sub(r'(?<!^)(?=[A-Z])','_',k).lower()
   if key=='b_global':key='bGlobal'
   v.set_editor_property(key,r[k])
 v.set_editor_property('heightmap',h)
 if 'coverage' in r:
  mask=r['coverage'];assert hashlib.sha256((p/mask['file']).read_bytes()).hexdigest()==mask['sha256'];hm=asset('HM_'+mask['name'],unreal.VoxelHeightmap)
  mt=asset('T_'+mask['name'],unreal.Texture2D)
  assert unreal.CLMeasuredTerrainTools.import_native_heights(hm,mt,str(p/mask['file']),mask['width']+4,mask['height']+4) is not None
  assert a.save_loaded_asset(mt);assert a.save_loaded_asset(hm);v.set_editor_property('coverage',hm)
 return v
entry=unreal.CLMeasuredTerrainEntry();entry.set_editor_property('id','Mars-MOLA32-Jezero1m');entry.set_editor_property('body_id','Mars');entry.set_editor_property('frame_id',d['frame']);entry.set_editor_property('datum',d['shapeQuantity']);entry.set_editor_property('provenance',json.dumps(d,separators=(',',':')));entry.set_editor_property('minimum_radius_meters',d['minimumRadiusMeters']);entry.set_editor_property('maximum_radius_meters',d['maximumRadiusMeters']);entry.set_editor_property('global',raster(d['global']));entry.set_editor_property('regions',[raster(d['region'])]);lib=asset('DA_MeasuredTerrain',unreal.CLMeasuredTerrainLibrary);lib.set_editor_property('entries',[entry]);assert lib.bake_digests() is not None;assert a.save_loaded_asset(lib)
entry=lib.get_editor_property('entries')[0]
# Schema 19 retains measured physical radii; guides annotate the source bounds only.
recipe=asset('DA_Mars',unreal.CLPlanetRecipeAsset);r=recipe.get_editor_property('recipe');r.set_editor_property('schema_version',19);r.set_editor_property('name','Mars — measured MOLA + Jezero');r.set_editor_property('coordinates',unreal.CLTerrainCoordinates.SPHERICAL_SURFACE);r.set_editor_property('radius_meters',d['outerRadiusMeters']);r.set_editor_property('noise_layers',[]);r.set_editor_property('base_surface_normalized',3396000/d['outerRadiusMeters'])
r.set_editor_property('algorithm_version',2)
guides=unreal.CLRadialGuideSet();guides.set_editor_property('name','Mars measured radius bounds');items=[]
for name,radius in [('SurfaceMin',d['minimumRadiusMeters']),('SurfaceMax',d['maximumRadiusMeters']),('ReferenceFigure',3396000),('AtmosphereMax',d['outerRadiusMeters'])]:
 g=unreal.CLRadialGuide();g.set_editor_property('name',name);g.set_editor_property('normalized_height',radius/d['outerRadiusMeters']);items.append(g)
guides.set_editor_property('guides',items);r.set_editor_property('radial_guides',guides)
stack=unreal.CLSurfaceStack();stack.set_editor_property('default_surface_id','Rock');stack.set_editor_property('default_surface_revision',2);layers=[]
for name,surface,low,high,feather in [('Loose regolith','Scree',18,52,10),('Settled fines','Soil',0,24,8)]:
 layer=unreal.CLSurfaceLayer();layer.set_editor_property('name',name);layer.set_editor_property('surface_id',surface);layer.set_editor_property('surface_revision',2);layer.set_editor_property('slope_degrees',unreal.Vector2D(low,high));layer.set_editor_property('slope_feather_degrees',feather);layers.append(layer)
stack.set_editor_property('layers',layers);r.set_editor_property('surface_stack',stack)
ref=unreal.CLMeasuredTerrainReference();ref.set_editor_property('id',entry.get_editor_property('id'));ref.set_editor_property('digest',entry.get_editor_property('definition_digest'));r.set_editor_property('measured_terrain',ref);recipe.set_editor_property('recipe',r)
assert recipe.save_to_disk_json() is not None
assert a.save_loaded_asset(recipe)
(p/'mars-recipe.json').write_text(Path(recipe.get_saved_json_file()).read_text(encoding='utf-16'))
# Copy only the newly owned assets from this host's staged plugin to the component.
host=Path(unreal.Paths.project_dir()).resolve();src=host/'Plugins/CatalystLandform/Content/Measured';dst=host.parents[1]/'catalyst-landform-ue-plugin/Content/Measured';shutil.copytree(src,dst,dirs_exist_ok=True)
fixture=host/'Content/RuntimeRecipes/Mars';fixture.mkdir(parents=True,exist_ok=True);shutil.copyfile(p/'mars-recipe.json',fixture/'planet.json')
unreal.log('MARS_NATIVE_ASSETS_READY '+str(entry.get_editor_property('definition_digest')))
