#!/usr/bin/env python3
"""Generate an isolated UE 5.8.4 Nanite shader overlay. Engine/vendor files are read-only."""
import argparse,hashlib,json,shutil
from pathlib import Path

CLUSTER='NaniteClusterCulling.usf'
INSTANCE='NaniteInstanceCulling.usf'
# Filled from the qualified installed UE 5.8.4 source; no engine source is distributed.
QUALIFIED={'NaniteClusterCulling.usf': 'f0b346d438bdb6341dbf7c2c1f22eea128ec317906ac1cef1a0abf52cac1d4e3', 'NaniteInstanceCulling.usf': 'f214ee73b1270693035a1d03f1ea4d15c39fc20e228ca718f42c0e80c8d4654a'}

def digest(data):return hashlib.sha256(data).hexdigest()
def patched(name,data):
    s=data.decode();newline='\r\n' if '\r\n' in s else '\n';s=s.replace('\r\n','\n')
    if name==CLUSTER:
        anchor='#define NANITE_DEPTH_BUCKETING\tNANITE_EXTENDED_VISIBLE_CLUSTERS'
        assert s.count(anchor)==1
        s=s.replace(anchor,'#include "/Plugin/CatalystLandform/Private/CLNaniteProjection.ush"\n\n'+anchor)
        anchor='\t// Extend the bounds for WPO or displacement'
        assert s.count(anchor)==1
        s=s.replace(anchor,'\tCLNaniteProjectNode(PrimitiveData, InstanceData, Bounds);\n\n'+anchor)
    elif name==INSTANCE:
        anchor='\t\tconst float3 LocalBoundsCenter = InstanceData.LocalBoundsCenter;\n\t\tconst float3 LocalBoundsExtent = InstanceData.LocalBoundsExtent;'
        assert s.count(anchor)==1
        s=s.replace(anchor,'\t\tfloat3 LocalBoundsCenter = InstanceData.LocalBoundsCenter;\n\t\tfloat3 LocalBoundsExtent = InstanceData.LocalBoundsExtent;\n\t\tif (PrimitiveData.CustomPrimitiveData[2].w == 9137.0f) { LocalBoundsCenter = PrimitiveData.CustomPrimitiveData[3].xyz; LocalBoundsExtent = PrimitiveData.CustomPrimitiveData[4].xyz; }\n\t\tCLNaniteProjectBox(PrimitiveData, InstanceData, LocalBoundsCenter, LocalBoundsExtent);')
        # FNodeCullingBounds is defined by NaniteCullingCommon before this include.
        anchor='#include "NaniteCullingCommon.ush"'
        assert s.count(anchor)==1
        s=s.replace(anchor,anchor+'\n#include "/Plugin/CatalystLandform/Private/CLNaniteProjection.ush"')
    return s.replace('\n',newline).encode()

def stage(project,engine):
    version=json.loads((engine/'Engine/Build/Build.version').read_text())
    if tuple(version[k] for k in ['MajorVersion','MinorVersion','PatchVersion'])!=(5,8,4):raise ValueError('Only UE 5.8.4 is qualified')
    source=engine/'Engine/Shaders/Private/Nanite'
    for name,sha in QUALIFIED.items():
        if digest((source/name).read_bytes())!=sha:raise ValueError('Unknown engine shader: '+name)
    # Validate every modification before creating any output.
    changes={name:patched(name,(source/name).read_bytes()) for name in [CLUSTER,INSTANCE]}
    target=project/'Saved/NaniteProjection/EngineShaders/Nanite'
    manifest_path=target.parent.parent/'manifest.json'
    if target.exists():
        if not manifest_path.is_file():raise ValueError('Refusing to replace an unmanaged shader overlay')
        previous=json.loads(manifest_path.read_text())
        expected=previous.get('output_sha256')
        if expected is None:
            expected={p.relative_to(source).as_posix():digest(p.read_bytes()) for p in source.rglob('*') if p.is_file()}
            expected.update(previous['patched_sha256'])
            expected['CLProjectionOverlayMarker.ush']=digest(b'#define CL_NANITE_PROJECTION_OVERLAY_VERSION 1\n')
        actual={p.relative_to(target).as_posix():digest(p.read_bytes()) for p in target.rglob('*') if p.is_file()}
        if actual!=expected:raise ValueError('Unrelated edits in generated overlay; no files written')
        shutil.rmtree(target)
    shutil.copytree(source,target)
    for name,data in changes.items():(target/name).write_bytes(data)
    (target/'CLProjectionOverlayMarker.ush').write_text('#define CL_NANITE_PROJECTION_OVERLAY_VERSION 1\n')
    manifest=dict(overlay_version=1,engine_version='5.8.4',original_sha256=QUALIFIED,patched_sha256={n:digest(d) for n,d in changes.items()})
    manifest['output_sha256']={p.relative_to(target).as_posix():digest(p.read_bytes()) for p in sorted(target.rglob('*')) if p.is_file()}
    (target.parent.parent/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    return manifest
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--project',type=Path,required=True);parser.add_argument('--engine',type=Path,required=True);args=parser.parse_args()
    result=stage(args.project,args.engine);print(json.dumps({k:v for k,v in result.items() if k!="output_sha256"},indent=2))
