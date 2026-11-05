"""Create only the owned province placeholders and review recipe through native Unreal asset APIs."""
import json
from pathlib import Path
import unreal
root=Path(unreal.Paths.project_plugins_dir()).resolve()/'CatalystLandform'
catalog=json.loads((root/'Examples/ProvinceLandforms.json').read_text())
assets=unreal.EditorAssetLibrary;tools=unreal.AssetToolsHelpers.get_asset_tools();sources=[]
for entry in catalog['profiles']:
    path=entry['asset'];obj=unreal.load_asset(path)
    if obj:
        current_id=obj.get_editor_property('terrain_type').get_editor_property('id').to_string().replace('-','').replace('{','').replace('}','').upper()
        assert current_id==entry['id'],(path,current_id,entry['id'])
        sources.append(obj)
        continue  # Existing definitions may contain user edits.
    else:
        folder,name=path.rsplit('/',1);obj=tools.create_asset(name,folder,unreal.CLTerrainTypeAsset,unreal.CLTerrainTypeFactory())
    assert obj,'Unable to create '+path
    result=obj.import_terrain_type_json((root/entry['source']).read_text());assert result is not None,(path,result)
    assert obj.get_editor_property('terrain_type').get_editor_property('schema_version')==9
    assert assets.save_loaded_asset(obj),path
    sources.append(obj)
review_path='/Game/LandformDesigner/Provinces/DA_ProvincePlacement';review=unreal.load_asset(review_path)
if not review:
    folder,name=review_path.rsplit('/',1);review=tools.create_asset(name,folder,unreal.CLPlanetRecipeAsset,unreal.CLPlanetRecipeFactory())
    assert review
    result=review.import_recipe_json((root/'Examples/ProvincePlacement.json').read_text());assert result is not None,result
    for source in sources:
        result=review.add_palette_source(source);assert result is not None,result
    assert len(review.get_editor_property('palette_sources'))==17
    assert assets.save_loaded_asset(review)
assert len(review.get_editor_property('palette_sources'))==17
unreal.log('PROVINCE_ASSETS_READY: '+str(len(sources))+' native Landform assets and '+review_path)
