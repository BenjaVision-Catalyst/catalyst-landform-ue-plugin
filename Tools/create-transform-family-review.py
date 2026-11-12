"""Create and open the Planet Transform review; preserve existing assets and edits."""
from pathlib import Path
import hashlib
import unreal
root = Path(unreal.Paths.project_plugins_dir()) / 'CatalystLandform'
tools = unreal.AssetToolsHelpers.get_asset_tools()
def create(path, cls, factory):
    folder, name = path.rsplit('/', 1)
    return tools.create_asset(name, folder, cls, factory)
source = unreal.load_asset('/Game/LandformDesigner/Provinces/DA_TransformShearFamilyCandidate')
if not source:
    source = create('/Game/LandformDesigner/Provinces/DA_TransformShearFamilyCandidate', unreal.CLTerrainTypeAsset, unreal.CLTerrainTypeFactory())
    source.import_terrain_type_json((root / 'Examples/TerrainTypes/TransformShearFamily.json').read_text())
    assert source.terrain_type.transform.algorithm_version == 1
    unreal.EditorAssetLibrary.save_loaded_asset(source, only_if_is_dirty=False)
# Adopt only the byte-identical shipped placeholder. Authored definitions stay intact.
catalog_file = root / 'Content/TerrainTypes/Provinces/DA_Province_TransformBelt.uasset'
if hashlib.sha256(catalog_file.read_bytes()).hexdigest() == '723b542779f340f9561bc6cb266d12074986eafce7fb0d6035359510737e7012':
    catalog = unreal.load_asset('/CatalystLandform/TerrainTypes/Provinces/DA_Province_TransformBelt')
    catalog.terrain_type = source.terrain_type
    unreal.EditorAssetLibrary.save_loaded_asset(catalog, only_if_is_dirty=False)
review = unreal.load_asset('/Game/LandformDesigner/Provinces/DA_TransformShearFamilyReview')
if not review:
    review = create('/Game/LandformDesigner/Provinces/DA_TransformShearFamilyReview', unreal.CLPlanetRecipeAsset, unreal.CLPlanetRecipeFactory())
    review.import_recipe_json((root / 'Examples/TransformShearProvince.json').read_text())
    review.add_palette_source(source)
    unreal.EditorAssetLibrary.save_loaded_asset(review, only_if_is_dirty=False)
unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).open_editor_for_assets([review])
unreal.SystemLibrary.execute_console_command(None, 'Landform.Designer')
unreal.log('TRANSFORM_FAMILY_REVIEW_OPENED_ORIGINAL_AUTHORING_PRESERVED')
