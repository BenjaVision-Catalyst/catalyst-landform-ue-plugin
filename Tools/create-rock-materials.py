"""Build the plugin's solid-rock artwork bindings; never save planet/test assets.

Run in an empty editor/commandlet map with this plugin installed. Only the named
product materials, Surface Types, surface library and MegaMaterial are saved.
Existing artwork entries remain byte-for-byte equivalent as model values.
"""
import unreal

assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
materials = unreal.MaterialEditingLibrary
folder = '/CatalystLandform/Surfaces'
library = unreal.load_asset(folder + '/DA_SurfaceLibrary')
mega = unreal.load_asset(folder + '/MM_LandformSurfaces')
assert library and mega
data = library.get_editor_property('library')
entries = list(data.get_editor_property('entries'))
original = {str(e.id): e.export_text() for e in entries}
rock = next(e for e in entries if str(e.id) == 'Rock')
native_types = list(mega.get_editor_property('surface_types'))
profiles = [
    ('StrongRock', (.91, .88, .82), .94, .20, .25, .65, 2.7),
    ('MixedRock', (.73, .77, .81), .98, .10, .20, .50, 1.8),
    ('WeakRock', (.45, .47, .49), 1.0, .045, .15, .35, 1.2),
]
saved = []
for name, colour, roughness, grain, strength, bump, tile in profiles:
    mi = unreal.load_asset(folder + '/MI_' + name)
    if mi is None:
        mi = tools.duplicate_asset('MI_' + name, folder, rock.surface_type.material)
    assert mi
    appearance = unreal.CLSurfaceAppearance()
    appearance.set_editor_property('color', unreal.LinearColor(*colour, 1))
    for key, value in [('roughness', roughness), ('grain_meters', grain),
                       ('grain_strength', strength), ('normal_strength', bump)]:
        appearance.set_editor_property(key, value)
    textures = unreal.CLSurfaceTextures()
    for key in ('base_color', 'normal', 'roughness'):
        tex = rock.textures.get_editor_property(key)
        textures.set_editor_property(key, tex)
    textures.set_editor_property('tile_meters', tile)
    materials.set_material_instance_vector_parameter_value(mi, 'CLTint', unreal.LinearColor(*colour, roughness))
    materials.set_material_instance_vector_parameter_value(mi, 'CLDetail', unreal.LinearColor(grain * 100, strength, bump, 0))
    materials.set_material_instance_vector_parameter_value(mi, 'CLTextureDetail', unreal.LinearColor(tile * 100, 1, 0, 0))
    for parameter, key in [('CLBaseColor', 'base_color'), ('CLNormal', 'normal'), ('CLRoughness', 'roughness')]:
        materials.set_material_instance_texture_parameter_value(mi, parameter, textures.get_editor_property(key))
    materials.update_material_instance(mi)
    st = unreal.load_asset(folder + '/ST_' + name)
    if st is None:
        st = tools.create_asset('ST_' + name, folder, unreal.VoxelSurfaceTypeAsset, None)
    assert st
    st.set_editor_property('material', mi)
    st.set_editor_property('blend_smoothness', .3)
    entry = unreal.CLSurfaceLibraryEntry()
    for key, value in [('id', name), ('revision', 1), ('appearance', appearance),
                       ('textures', textures), ('surface_type', st)]:
        entry.set_editor_property(key, value)
    entries = [e for e in entries if str(e.id) != name] + [entry]
    if st not in native_types:
        native_types.append(st)
    for asset in (mi, st):
        assert assets.save_loaded_asset(asset, only_if_is_dirty=False)
        saved.append(asset.get_path_name())

assert all(next(e for e in entries if str(e.id) == key).export_text() == value
           for key, value in original.items() if key not in {p[0] for p in profiles})
data.set_editor_property('entries', entries)
library.set_editor_property('library', data)
library.update_version_metadata()
mega.set_editor_property('surface_types', native_types)
for asset in (library, mega):
    assert assets.save_loaded_asset(asset, only_if_is_dirty=False)
    saved.append(asset.get_path_name())
unreal.log('LANDFORM_ROCK_PRODUCT_ASSETS ' + str(saved))
