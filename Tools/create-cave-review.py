"""Create only the missing native cave review recipe; leave all existing assets intact."""
from pathlib import Path
import json
import unreal
root=Path(unreal.Paths.project_plugins_dir()).resolve()/'CatalystLandform'
path='/Game/LandformDesigner/Caves/DA_FissureCaves'
asset=unreal.load_asset(path)
if not asset:
    folder,name=path.rsplit('/',1)
    asset=unreal.AssetToolsHelpers.get_asset_tools().create_asset(name,folder,unreal.CLPlanetRecipeAsset,unreal.CLPlanetRecipeFactory())
    assert asset
    result=asset.import_recipe_json((root/'Examples/Caves.json').read_text());assert result is not None,result
    for entry in json.loads((root/'Examples/ProvinceLandforms.json').read_text())['profiles']:
        source=unreal.load_asset(entry['asset']);assert source,entry['asset']
        result=asset.add_palette_source(source);assert result is not None,result
    assert unreal.EditorAssetLibrary.save_loaded_asset(asset)
assert asset.get_editor_property('recipe').get_editor_property('schema_version')==23
unreal.log('CAVE_REVIEW_READY: '+path)
