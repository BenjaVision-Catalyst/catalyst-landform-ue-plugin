import unreal
from pathlib import Path
root=Path(unreal.Paths.project_dir()).resolve();assert (root/'LandformWalkBuild.uproject').is_file(),'Use only the independent LandformWalkBuild fixture';out=root/'Saved/RecoveryProbe';out.mkdir(parents=True,exist_ok=True)
p='/Game/Developers/RecoveryQualification/M_RecoveryProbe'
assert not unreal.EditorAssetLibrary.does_asset_exist(p)
m=unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_RecoveryProbe',p.rsplit('/',1)[0],unreal.Material,unreal.MaterialFactoryNew());assert m
assert unreal.EditorAssetLibrary.save_loaded_asset(m,False)
file=root/'Content/Developers/RecoveryQualification/M_RecoveryProbe.uasset';(out/'original.bin').write_bytes(file.read_bytes())
unreal.EditorAssetLibrary.set_metadata_tag(m,'RecoveryQualification','x'*1048576);assert unreal.EditorAssetLibrary.save_loaded_asset(m,False)
assert file.stat().st_size>len((out/'original.bin').read_bytes())+1000000
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
def close(dt):
 unreal.unregister_slate_pre_tick_callback(handle);unreal.SystemLibrary.quit_editor()
handle=unreal.register_slate_pre_tick_callback(close)
