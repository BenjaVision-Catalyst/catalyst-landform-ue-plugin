"""Create the profile-5 dry-karst review asset; preserve existing recipes."""
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_plugins_dir()).resolve() / "CatalystLandform"
path = "/Game/LandformDesigner/Caves/DA_NaturalKarst"
asset = unreal.load_asset(path)

def require_success(result):
    assert (isinstance(result, tuple) and result[0]) or result is True or result == "", result

if not asset:
    folder, name = path.rsplit("/", 1)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, folder, unreal.CLPlanetRecipeAsset, unreal.CLPlanetRecipeFactory())
    assert asset
    require_success(asset.import_recipe_json((root / "Examples/NaturalKarst.json").read_text()))
    source = unreal.load_asset("/CatalystLandform/TerrainTypes/Provinces/DA_Province_CarbonatePlatform")
    assert source
    require_success(asset.add_palette_source(source))
    assert asset.get_editor_property("recipe").get_editor_property("caves").get_editor_property("algorithm_version") == 5
    require_success(asset.save_to_disk_json())
    assert unreal.EditorAssetLibrary.save_loaded_asset(asset)
unreal.log("NATURAL_KARST_REVIEW_READY: " + asset.get_path_name() + "; exported=" + asset.get_saved_json_file())
