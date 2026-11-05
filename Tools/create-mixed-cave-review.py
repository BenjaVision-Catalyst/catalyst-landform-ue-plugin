"""Create only the missing mixed-family review recipe through native Unreal APIs."""
from pathlib import Path
import unreal
root = Path(unreal.Paths.project_plugins_dir()).resolve() / "CatalystLandform"
path = "/Game/LandformDesigner/Caves/DA_MixedCaves"
def require_success(result):
    # UE may expose a bool/out pair, or the out error string (None on failure).
    assert (isinstance(result, tuple) and result[0]) or result is True or result == "", result

asset = unreal.load_asset(path)
created = not asset
if created:
    folder, name = path.rsplit("/", 1)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.CLPlanetRecipeAsset, unreal.CLPlanetRecipeFactory())
    assert asset
    require_success(asset.import_recipe_json((root / "Examples/MixedCaves.json").read_text()))
    for name in ("CarbonatePlatform", "ContinentalRift", "TransformBelt"):
        source = unreal.load_asset("/CatalystLandform/TerrainTypes/Provinces/DA_Province_" + name)
        assert source
        require_success(asset.add_palette_source(source))
    recipe = asset.get_editor_property("recipe")
    assert recipe.get_editor_property("schema_version") == 23
    assert recipe.get_editor_property("caves").get_editor_property("algorithm_version") == 3
recipe = asset.get_editor_property("recipe")
assert recipe.get_editor_property("radius_meters") == 800
caves = recipe.get_editor_property("caves")
assert caves.get_editor_property("enabled") and caves.get_editor_property("seed") == 176
assert caves.get_editor_property("algorithm_version") == 3
# Export invokes the native compiler after all source links are installed.
require_success(asset.save_to_disk_json())
if created:
    assert unreal.EditorAssetLibrary.save_loaded_asset(asset)
unreal.log("MIXED_CAVE_REVIEW_READY: " + asset.get_path_name() + "; exported=" + asset.get_saved_json_file())
