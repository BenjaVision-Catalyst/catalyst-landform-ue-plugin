#!/usr/bin/env python3
"""Install the current plugin into an Unreal project without overwriting local edits."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

IGNORED = {'.git', 'Binaries', 'Intermediate', 'Saved', 'DerivedDataCache',
           '__pycache__', '.DS_Store'}
PRODUCT_PATHS = ('CatalystLandform.uplugin', 'Config', 'Content', 'Shaders',
                 'Source', 'Tools', 'LICENSE', 'README.md', 'Docs', 'Examples')
FOUNDATION_REVISION = '9610a33966a23fd1a863b0e2256c6255c39a7b8a'


def acquire_foundation(destination):
    """Resolve the documented dependency from its published, pinned source."""
    checkout = destination/'foundation-source'
    subprocess.run(['git', 'init', '-q', str(checkout)], check=True)
    subprocess.run(['git', '-C', str(checkout), 'remote', 'add', 'origin',
                    'https://github.com/BenjaVisionPro/CatalystDevKit.git'], check=True)
    subprocess.run(['git', '-C', str(checkout), 'config', 'core.sparseCheckout', 'true'], check=True)
    (checkout/'.git/info/sparse-checkout').write_text('/CatalystPlugins/Plugins/CatalystFoundation/\n')
    subprocess.run(['git', '-C', str(checkout), 'fetch', '-q', '--depth=1', '--filter=blob:none',
                    'origin', FOUNDATION_REVISION], check=True)
    subprocess.run(['git', '-C', str(checkout), 'checkout', '-q', '--detach', 'FETCH_HEAD'], check=True)
    return checkout/'CatalystPlugins/Plugins/CatalystFoundation'


def signature(root):
    digest = hashlib.sha256()
    for path in sorted(root.rglob('*')):
        relative = path.relative_to(root)
        if path.is_file() and not any(part in IGNORED for part in relative.parts):
            digest.update(str(relative).encode())
            digest.update(path.read_bytes())
    return digest.hexdigest()


def copy_plugin(source, target, product=False):
    if target.exists():
        raise SystemExit(f'Destination already exists: {target}')
    target.mkdir(parents=True)
    for path in source.iterdir():
        if path.name in IGNORED or (product and path.name not in PRODUCT_PATHS):
            continue
        if path.is_dir():
            shutil.copytree(path, target/path.name, ignore=shutil.ignore_patterns(*IGNORED))
        else:
            shutil.copy2(path, target/path.name)


def copy_foundation(source, destination):
    """Install Foundation's model/editor modules, without unrelated integrations."""
    descriptor = json.loads((source/'CatalystFoundation.uplugin').read_text())
    modules = {'CatalystFoundation', 'CatalystFoundationEditor'}
    descriptor['Modules'] = [module for module in descriptor['Modules'] if module['Name'] in modules]
    destination.mkdir(parents=True)
    (destination/'CatalystFoundation.uplugin').write_text(json.dumps(descriptor, indent=2)+'\n')
    for name in modules:
        shutil.copytree(source/'Source'/name, destination/'Source'/name)
    if (source/'Resources').exists():
        shutil.copytree(source/'Resources', destination/'Resources')


def install(source, project, dependencies):
    project = project.resolve()
    if project.suffix != '.uproject' or not project.is_file():
        raise SystemExit('--project must name an existing .uproject file')
    # Replacing modules loaded by an editor is unsafe even when source is clean.
    if shutil.which('ps'):
        processes = subprocess.check_output(['ps', '-axo', 'command='], text=True)
        if any('UnrealEditor' in line and str(project) in line for line in processes.splitlines()):
            raise SystemExit('Close this project editor before installing the plugin.')
    plugins = project.parent/'Plugins'
    plugins.mkdir(exist_ok=True)
    state_path = project.parent/'Saved/CatalystLandformInstall.json'
    previous = json.loads(state_path.read_text()) if state_path.exists() else {}
    for name, path in dependencies.items():
        destination = plugins/name
        if destination.exists():
            if path and path.resolve() != destination.resolve() and signature(path) != signature(destination):
                matches = False
                if name == 'CatalystFoundation':
                    with tempfile.TemporaryDirectory(prefix='landform-foundation-check-') as temporary:
                        filtered = Path(temporary)/name
                        copy_foundation(path, filtered)
                        matches = signature(filtered) == signature(destination)
                if not matches:
                    raise SystemExit(f'Existing {name} differs from the supplied dependency; preserve it first.')
        elif path:
            if not (path/(name+'.uplugin')).is_file():
                raise SystemExit(f'Invalid {name} plugin: {path}')
            if name == 'CatalystFoundation':
                copy_foundation(path, destination)
            else:
                copy_plugin(path, destination)
        else:
            raise SystemExit(f'Install {name} under {plugins}, or supply --{name.lower()}.')
    destination = plugins/'CatalystLandform'
    if destination.exists() and signature(destination) != previous.get('installedSHA256'):
        raise SystemExit(f'Existing unmanaged or edited plugin at {destination}; preserve it before installation.')
    with tempfile.TemporaryDirectory(prefix='landform-install-') as temporary:
        staged = Path(temporary)/'CatalystLandform'
        copy_plugin(source, staged, product=True)
        digest = signature(staged)
        if destination.exists():
            shutil.rmtree(destination)
        shutil.move(str(staged), destination)
    descriptor = json.loads(project.read_text())
    enabled = descriptor.setdefault('Plugins', [])
    for name in ('CatalystFoundation', 'Voxel', 'CatalystLandform'):
        entry = next((item for item in enabled if item['Name'] == name), None)
        if entry is None:
            enabled.append({'Name': name, 'Enabled': True})
        else:
            entry['Enabled'] = True
    project.write_text(json.dumps(descriptor, indent=2)+'\n')
    revision = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip() if (source/'.git').exists() else 'source-package'
    state = {'sourceRevision': revision, 'installedSHA256': digest,
             'dependencies': {name: signature(plugins/name) for name in dependencies}}
    state_path.parent.mkdir(exist_ok=True)
    state_path.write_text(json.dumps(state, indent=2)+'\n')
    return state


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project', type=Path, required=True)
    parser.add_argument('--catalystfoundation', type=Path)
    parser.add_argument('--voxel', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='landform-dependencies-') as temporary:
        foundation = args.catalystfoundation
        if foundation is None and not (args.project.parent/'Plugins/CatalystFoundation').exists():
            foundation = acquire_foundation(Path(temporary))
        print(json.dumps(install(Path(__file__).resolve().parents[1], args.project,
                                {'CatalystFoundation': foundation, 'Voxel': args.voxel}), indent=2))


if __name__ == '__main__':
    main()
