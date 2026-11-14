import unreal,time
from pathlib import Path
assert (Path(unreal.Paths.project_dir())/'LandformWalkBuild.uproject').is_file()
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
unreal.SystemLibrary.execute_console_command(None,'Module Load CatalystLandformTests')
unreal.SystemLibrary.execute_console_command(None,'landform.VerifyLateSlateNotification')
start=time.monotonic()
def quit_tick(dt):
 if time.monotonic()-start<2:return
 unreal.unregister_slate_pre_tick_callback(handle)
 unreal.SystemLibrary.quit_editor()
handle=unreal.register_slate_pre_tick_callback(quit_tick)
