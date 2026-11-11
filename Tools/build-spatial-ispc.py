"""Build Landform's own Mac arm64 field kernel; no VPP or engine source edits."""
from pathlib import Path
import fcntl
import hashlib
import json
import os
import platform
import shutil
import subprocess
import tempfile

if platform.system() != 'Darwin' or platform.machine() != 'arm64':
    raise SystemExit(0)
root = Path(__file__).resolve().parents[1]
source = root / 'Source/CatalystLandform/Private/Model/CLSpatialMorph.ispc'
output = root / 'Intermediate/SpatialSIMD/Mac/arm64'
output.mkdir(parents=True, exist_ok=True)
compiler = os.environ.get('LANDFORM_ISPC') or shutil.which('ispc')
if not compiler:
    raise SystemExit('Mac arm64 spatial fields require ISPC on PATH or LANDFORM_ISPC (qualified with ISPC 1.31).')
version = subprocess.check_output([compiler, '--version'], text=True).strip()
flags = ['--target=neon-i32x4', '--arch=aarch64', '--target-os=macos', '--pic', '--opt=disable-fma', '-O2']
voxel_source = root.parent / 'Voxel/Source'
includes = [voxel_source / 'VoxelCore/Public', voxel_source / 'VoxelGraph/Public']
if not all(p.is_dir() for p in includes):
    raise SystemExit('Build inside the physical host beside its licensed VPP plugin; native ISPC headers are required.')
headers = {str(p.relative_to(voxel_source)): hashlib.sha256(p.read_bytes()).hexdigest()
           for folder in includes for p in sorted(folder.rglob('*.isph'))}
flags += [arg for folder in includes for arg in ['-I', str(folder)]]
digest = hashlib.sha256(source.read_bytes() + Path(__file__).read_bytes() + version.encode() + json.dumps(flags).encode() + json.dumps(headers, sort_keys=True).encode()).hexdigest()
with (output / 'build.lock').open('w') as lock:
    fcntl.flock(lock, fcntl.LOCK_EX)
    manifest = output / 'artifact.json'
    artifacts = ['CLSpatialMorph.generated.h', 'libCLSpatialMorph.a']
    if manifest.exists():
        previous = json.loads(manifest.read_text())
        if previous.get('inputSHA256') == digest and all((output / n).is_file() and hashlib.sha256((output / n).read_bytes()).hexdigest() == previous['outputs'].get(n) for n in artifacts):
            raise SystemExit(0)
    with tempfile.TemporaryDirectory(dir=output) as temporary:
        temp = Path(temporary)
        subprocess.run([compiler, str(source), *flags, '-h', str(temp / artifacts[0]), '-o', str(temp / 'kernel.o')], check=True)
        subprocess.run(['/usr/bin/ar', 'rcs', str(temp / artifacts[1]), str(temp / 'kernel.o')], check=True)
        for name in artifacts:
            target = output / name
            if not target.exists() or target.read_bytes() != (temp / name).read_bytes():
                os.replace(temp / name, target)
        record = {'inputSHA256': digest, 'compiler': compiler, 'version': version, 'flags': flags, 'nativeHeaderSHA256': headers, 'outputs': {n: hashlib.sha256((output / n).read_bytes()).hexdigest() for n in artifacts}}
        (temp / 'artifact.json').write_text(json.dumps(record, indent=2) + '\n')
        os.replace(temp / 'artifact.json', manifest)
print('Built Landform spatial SIMD kernel:', output)
