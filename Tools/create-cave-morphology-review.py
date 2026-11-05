"""Create missing morphology review assets; never modify an existing recipe."""
from pathlib import Path
import unreal
root = Path(unreal.Paths.project_plugins_dir()).resolve() / "CatalystLandform"

def require_success(result):
    assert (isinstance(result, tuple) and result[0]) or result is True or result == "", result

for name, sources in (
    ("FissureMorphology", ("ContinentalRift", "TransformBelt")),
    ("KarstMorphology", ("CarbonatePlatform",)),
    ("CaveMorphology", ("CarbonatePlatform", "ContinentalRift", "TransformBelt")),
):
    path = "/Game/LandformDesigner/Caves/DA_" + name
    asset = unreal.load_asset(path)
    created = not asset
    if created:
        folder, asset_name = path.rsplit("/", 1)
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(asset_name, folder, unreal.CLPlanetRecipeAsset, unreal.CLPlanetRecipeFactory())
        assert asset
        require_success(asset.import_recipe_json((root / ("Examples/" + name + ".json")).read_text()))
        for source_name in sources:
            source = unreal.load_asset("/CatalystLandform/TerrainTypes/Provinces/DA_Province_" + source_name)
            assert source
            require_success(asset.add_palette_source(source))
        assert asset.get_editor_property("recipe").get_editor_property("caves").get_editor_property("algorithm_version") == 4
        require_success(asset.save_to_disk_json())
        assert unreal.EditorAssetLibrary.save_loaded_asset(asset)
    unreal.log("CAVE_MORPHOLOGY_REVIEW_READY: " + asset.get_path_name() + "; exported=" + asset.get_saved_json_file())
