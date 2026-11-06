"""Author the shipped Rift source and Lab-only review with native Unreal serialization.
Existing authored reviews and modified neutral source assets are preserved.
Run in an isolated LandformLab host after staging maintained sources.
"""
from pathlib import Path
import json,hashlib
import unreal
p=Path(unreal.Paths.project_dir()).resolve();root=Path(unreal.Paths.project_plugins_dir()).resolve()/'CatalystLandform';a=unreal.EditorAssetLibrary;t=unreal.AssetToolsHelpers.get_asset_tools()
path='/CatalystLandform/TerrainTypes/Provinces/DA_Province_ContinentalRift';obj=unreal.load_asset(path);assert obj
current=obj.get_editor_property('terrain_type').get_editor_property('rift').get_editor_property('algorithm_version')
if current==0:
    file=root/'Content/TerrainTypes/Provinces/DA_Province_ContinentalRift.uasset'
    assert hashlib.sha256(file.read_bytes()).hexdigest()=='518db6b7c4c17891a8e956363e2fb389726942771ba292b16c2199e57be168c7','Preserve edited neutral province; explicit reconciliation required'
    result=obj.import_terrain_type_json((root/'Examples/TerrainTypes/Provinces/ContinentalRift.json').read_text());assert result is not None,result
    assert a.save_loaded_asset(obj)
else:assert current==1,'Preserve another physical source version'
review_path='/Game/LandformDesigner/Provinces/DA_ContinentalRiftReview';review=unreal.load_asset(review_path)
if not review:
    folder,name=review_path.rsplit('/',1);review=t.create_asset(name,folder,unreal.CLPlanetRecipeAsset,unreal.CLPlanetRecipeFactory());assert review
    result=review.import_recipe_json((root/'Examples/ContinentalRiftProvince.json').read_text());assert result is not None,result
    result=review.add_palette_source(obj);assert result is not None,result
    assert a.save_loaded_asset(review)
unreal.log('CONTINENTAL_RIFT_NATIVE_ASSETS_READY')
