"""Run one native spatial-cave qualification phase in the existing Landform Lab."""
from pathlib import Path
import argparse
import json
import os
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--project', type=Path, required=True)
parser.add_argument('--engine', type=Path, default=Path(os.environ.get('UNREAL_ENGINE_ROOT', '/Users/jupiter/UnrealEngine/UE_5.7')))
parser.add_argument('--phase', choices=['geometry', 'review', 'reader', 'headroom'], required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--delivery', choices=['legacy', 'cpp', 'graph'], default='cpp')
parser.add_argument('--walk-speed', type=float, default=600, help='Review speed in cm/s, 100..600')
parser.add_argument('--slot', default='LandformSpatialCaveQualification')
args = parser.parse_args()
project, output = args.project.resolve(), args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
editor = args.engine / 'Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor'
assert editor.is_file() and project.is_file(), 'Use the qualified UE5.7 physical Lab host'
command = [str(editor), str(project), '-unattended', '-nosplash', '-nosound', '-notracethreading', '-nohighdpi', '-CLNaniteProjectionShaders', '-stdout', '-FullStdOutLogOutput', '-abslog=' + str(output / 'native.log')]
if args.phase == 'geometry':
    command.insert(2, '/Engine/Maps/Entry')
    command += ['-ExecCmds=Automation RunTests Catalyst.Landform.Caves.SpatialPrototype', '-TestExit=Automation Test Queue Empty', '-ReportExportPath=' + str(output / 'automation'), '-CLSpatialReport=' + str(output / 'geometry.json'), '-CLSpatialWorkReport=' + str(output / 'work.json')]
else:
    command.insert(2, '/Engine/Maps/Entry?game=/Script/LandformLab.CLGeneratedCaveProofGameMode')
    command += ['-game', '-CLCaveReviewWorkers=8', '-ResX=1280', '-ResY=800', '-windowed', '-CLDelivery=' + args.delivery, '-CLCaveRecipe=' + str(project.parent / 'Plugins/CatalystLandform/Examples/SpatialCave.json'), '-CLReport=' + str(output / 'result.json'), '-CLCaveSlot=' + args.slot, '-CLCaveWalkSpeed=' + str(args.walk_speed), '-ExecCmds=r.VSync 0,t.MaxFPS 0,t.IdleWhenNotForeground 0,r.ScreenPercentage 100,r.DynamicRes.OperationMode 0,sg.ResolutionQuality,sg.ShadowQuality,sg.GlobalIlluminationQuality,sg.ViewDistanceQuality']
    command += {'review': ['-CLCaveReferenceReview'], 'reader': ['-CLCaveLoad', '-CLCaveWalk'], 'headroom': ['-CLHeadroom']}[args.phase]
for name in ['result.json', 'geometry.json', 'work.json']:
    (output / name).unlink(missing_ok=True)
(output / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
with (output / 'stdout.log').open('w') as log:
    result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=1100)
assert result.returncode == 0, 'Native process failed; inspect native.log'
if args.phase == 'geometry':
    report = json.loads((output / 'automation/index.json').read_text(encoding='utf-8-sig'))
    assert report['tests'] and all(test['state'] == 'Success' for test in report['tests']), 'Native tests failed'
else:
    report = json.loads((output / 'result.json').read_text())
    assert report['passed'] and report['nativeOwnerStable'], report['message']
    if args.phase == 'review':
        assert report['routeGroundedFrames'] == report['routeFrames'] > 0
        assert report['nativeCollisionWaitSeconds'] <= .25
    if args.phase == 'reader':
        assert report['comparedProbes'] > 0 and report['maximumReloadErrorCm'] == 0
    if args.phase == 'headroom':
        assert len(report['headroom']) == 4
        assert all(row['peakNativePendingTasks'] == row['nativeStateBusyFrames'] == 0 and row['gpuMs']['samples'] > 0 for row in report['headroom'])
print('Native spatial cave', args.phase, 'passed:', output)
