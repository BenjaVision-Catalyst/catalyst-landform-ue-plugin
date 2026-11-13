#!/usr/bin/env python3
"""Build and exercise the installed plugin in a new, independent Unreal project."""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--catalystfoundation', type=Path, required=True)
    parser.add_argument('--voxel', type=Path, required=True)
    args = parser.parse_args()
    root = args.output.resolve()
    if root.exists():
        raise SystemExit('Use a new empty output path; verification never replaces an existing project.')
    root.mkdir(parents=True)
    name = 'LandformInstallCheck'
    project = root/(name+'.uproject')
    project.write_text(json.dumps({'FileVersion': 3, 'EngineAssociation': '5.7',
        'Modules': [{'Name': name, 'Type': 'Runtime', 'LoadingPhase': 'Default'}],
        'Plugins': [{'Name': 'TextureFormatOodle', 'Enabled': True}]}, indent=2)+'\n')
    source = root/'Source'/name
    source.mkdir(parents=True)
    (source/(name+'.Build.cs')).write_text('using UnrealBuildTool;\npublic class '+name+': ModuleRules { public '+name+'(ReadOnlyTargetRules Target): base(Target) { PCHUsage=PCHUsageMode.UseExplicitOrSharedPCHs; PrivateDependencyModuleNames.AddRange(new[]{"Core","CoreUObject","Engine"}); } }\n')
    (root/'Source'/(name+'Editor.Target.cs')).write_text('using UnrealBuildTool;\npublic class '+name+'EditorTarget: TargetRules { public '+name+'EditorTarget(TargetInfo Target): base(Target) { Type=TargetType.Editor; DefaultBuildSettings=BuildSettingsVersion.V6; IncludeOrderVersion=EngineIncludeOrderVersion.Unreal5_7; ExtraModuleNames.Add("'+name+'"); } }\n')
    (source/(name+'.cpp')).write_text('#include "Modules/ModuleManager.h"\n#include "Misc/CoreDelegates.h"\nclass FInstallHost: public FDefaultGameModuleImpl { public: void StartupModule() override { FCoreDelegates::OnPostEngineInit.AddLambda([]{FModuleManager::Get().LoadModuleChecked<IModuleInterface>("CatalystLandformTests");}); } };\nIMPLEMENT_PRIMARY_GAME_MODULE(FInstallHost, '+name+', "'+name+'");\n')
    spec = importlib.util.spec_from_file_location('landform_install', Path(__file__).with_name('install-plugin.py'))
    installer = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(installer)
    installer.install(Path(__file__).resolve().parents[1], project,
        {'CatalystFoundation': args.catalystfoundation,
         'Voxel': args.voxel})
    # CoreRedirects and maps from LandformLab are deliberately absent.
    build = [str(args.engine/'Engine/Build/BatchFiles/Mac/Build.sh'), name+'Editor',
             'Mac', 'Development', '-Project='+str(project), '-architecture=arm64']
    (root/'build-command.json').write_text(json.dumps(build, indent=2)+'\n')
    with (root/'build.log').open('w') as log:
        result = subprocess.run(build, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        raise SystemExit('Fresh-project build failed; see '+str(root/'build.log'))
    command = [str(args.engine/'Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor'),
        str(project), '/Engine/Maps/Entry', '-unattended', '-nosplash', '-nosound',
        '-notracethreading', '-nohighdpi', '-abslog='+str(root/'editor.log'),
        '-ExecCmds=Automation RunTests Catalyst.Landform.Installation.FreshProject+Catalyst.Landform.RadialGuides.PackagedExamples',
        '-TestExit=Automation Test Queue Empty', '-ReportExportPath='+str(root/'automation')]
    (root/'editor-command.json').write_text(json.dumps(command, indent=2)+'\n')
    with (root/'editor-stdout.log').open('w') as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=900)
    report = json.loads((root/'automation/index.json').read_text(encoding='utf-8-sig'))
    tests = report.get('tests', [])
    summary = {'exitCode': result.returncode, 'tests': [
        {'name': test['fullTestPath'], 'state': test['state'], 'entries': test.get('entries', [])}
        for test in tests]}
    (root/'result.json').write_text(json.dumps(summary, indent=2)+'\n')
    print(json.dumps(summary, indent=2))
    if result.returncode or not tests or any(test['state'] != 'Success' for test in tests):
        sys.exit(1)


if __name__ == '__main__':
    main()
