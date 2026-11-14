import unreal,json,hashlib
from pathlib import Path
root=Path(unreal.Paths.project_dir()).resolve();assert (root/'LandformWalkBuild.uproject').is_file(),'Use only the independent LandformWalkBuild fixture';out=root/'Saved/RecoveryProbe';p='/Game/Developers/RecoveryQualification/M_RecoveryProbe';m=unreal.load_asset(p);assert m
original=(out/'original.bin').read_bytes();file=root/'Content/Developers/RecoveryQualification/M_RecoveryProbe.uasset';loadedSize=file.stat().st_size
assert loadedSize>len(original)+1000000
m.modify();dirty={p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
assert not unreal.CLInspectionRecoveryLibrary.detach_recovery_packages([])
assert not unreal.CLInspectionRecoveryLibrary.detach_recovery_packages(['/Game/NotLoadedRecoveryProbe'])
result=unreal.CLInspectionRecoveryLibrary.detach_recovery_packages([p]);assert result is not None,result
assert dirty.issubset({p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()})
file.write_bytes(original)
target=out/'after-restoration.uasset';unreal.SystemLibrary.execute_console_command(None,f'OBJ SAVEPACKAGE PACKAGE="{p}" FILE="{target}" SILENT=true AUTOSAVING=true KEEPDIRTY=true')
assert target.is_file() and target.stat().st_size>1000000
assert hashlib.sha256(file.read_bytes()).digest()==hashlib.sha256(original).digest()
assert len(unreal.EditorAssetLibrary.get_metadata_tag(m,'RecoveryQualification'))==1048576
(out/'verification.json').write_text(json.dumps({'loadedCheckpointBytes':loadedSize,'restoredOriginalBytes':len(original),'privateSaveAfterRestoreSucceeded':True,'inMemoryMetadataPreserved':True,'originalHashUnchanged':True,'dirtyPackagesPreserved':True,'emptyAndUnloadedRefused':True},indent=2))
