"""Create separate Rift family review assets; preserve existing authoring assets.

Run with Unreal's Python console: py <absolute path to this file>.
The staged CatalystLandform plugin must contain ContinentalRiftFamily.json.
Existing candidate assets are reopened without replacing their authoring edits.
"""
from pathlib import Path
import unreal

assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
source_path = '/Game/LandformDesigner/Provinces/DA_ContinentalRiftFamilyCandidate'
review_path = '/Game/LandformDesigner/Provinces/DA_ContinentalRiftFamilyReview'
source = unreal.load_asset(source_path)
if not source:
    folder, name = source_path.rsplit('/', 1)
    source = tools.create_asset(name, folder, unreal.CLTerrainTypeAsset, unreal.CLTerrainTypeFactory())
    recipe = Path(unreal.Paths.project_plugins_dir()) / 'CatalystLandform/Examples/TerrainTypes/ContinentalRiftFamily.json'
    source.import_terrain_type_json(recipe.read_text())
    assert source.terrain_type.rift.algorithm_version == 2
    assert len(source.terrain_type.noise_layers) == 3
    assets.save_loaded_asset(source)
review = unreal.load_asset(review_path)
if not review:
    original = unreal.load_asset('/Game/LandformDesigner/Provinces/DA_ContinentalRiftReview')
    assert original, 'Create the original Continental Rift review first'
    folder, name = review_path.rsplit('/', 1)
    review = tools.create_asset(name, folder, unreal.CLPlanetRecipeAsset, unreal.CLPlanetRecipeFactory())
    review.recipe = original.recipe
    character = review.recipe.caves.character
    review.add_palette_source(source)
    assert review.recipe.region_generation.palette[0].rift.algorithm_version == 2
    assert len(review.recipe.region_generation.palette[0].noise_layers) == 3
    assert review.recipe.caves.character == character
    assets.save_loaded_asset(review)
unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).open_editor_for_assets([review])
unreal.log('RIFT_FAMILY_REVIEW_OPENED_ORIGINAL_AUTHORING_PRESERVED')
