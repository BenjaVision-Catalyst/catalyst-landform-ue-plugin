"""Create the missing carbonate karst review asset; never overwrite an existing asset."""
from pathlib import Path
import unreal
root = Path(unreal.Paths.project_plugins_dir()).resolve() / "CatalystLandform"
path = "/Game/LandformDesigner/Caves/DA_KarstCaves"
asset = unreal.load_asset(path)
if not asset:
    folder, name = path.rsplit("/", 1)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.CLPlanetRecipeAsset, unreal.CLPlanetRecipeFactory())
    assert asset
    asset.import_recipe_json((root / "Examples/KarstCaves.json").read_text())
    source = unreal.load_asset("/CatalystLandform/TerrainTypes/Provinces/DA_Province_CarbonatePlatform")
    assert source
    asset.add_palette_source(source)
    recipe = asset.get_editor_property("recipe")
    assert recipe.get_editor_property("schema_version") == 23
    assert recipe.get_editor_property("caves").get_editor_property("b_enabled")
    assert unreal.EditorAssetLibrary.save_loaded_asset(asset)
assert asset.get_editor_property("recipe").get_editor_property("schema_version") == 23
unreal.log("KARST_REVIEW_READY: " + asset.get_path_name())
