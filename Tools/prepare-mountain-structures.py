"""Extract independent mountain structures from the original Motion Forge elevations.
Run with --archives <folder>. Requires motion-forge-requirements.txt.
"""
import argparse
import hashlib
import io
import json
import tempfile
import zipfile
from pathlib import Path
import numpy as np
from PIL import Image
import OpenEXR
from scipy import ndimage as nd
from importlib.util import spec_from_file_location, module_from_spec

ROOT = Path(__file__).resolve().parents[1]
# Hand-curated seeds and contour floors in the parent preparation's coordinates.
# Rocky Peaks' southern peak is deliberately excluded: its contour hits the source edge.
STRUCTURES = [
    ('GrandSpine', 'Grand mountain spine', 'GrandMountain', .25, (.50, .51), 3),
    ('CurvedRange', 'Curved mountain range', 'MountainRange', .65, (.45, .43), 3),
    ('RockyMassif', 'Rocky massif', 'RockyPeaks', .66, (.26, .50), 2),
    ('WestRidge', 'Western ridge', 'TerrainRidge', .60, (.27, .25), 2),
    ('EastRidge', 'Eastern ridge', 'TerrainRidge', .60, (.71, .64), 2),
]

def sha(data):
    return hashlib.sha256(data).hexdigest()

def component_at(mask, seed):
    labels, _ = nd.label(mask)
    key = labels[seed]
    assert key, 'Curated seed must be inside its contour'
    return labels == key

def load_parent(spec, archive_dir, provenance):
    id, _, archive, _, member, crop, resolution, _ = spec
    path = archive_dir/archive
    assert sha(path.read_bytes()) == provenance['archive_sha256']
    with zipfile.ZipFile(path) as z:
        raw = z.read(member)
    assert sha(raw) == provenance['member_sha256']
    if member.endswith('.exr'):
        with tempfile.NamedTemporaryFile(suffix='.exr') as tmp:
            tmp.write(raw); tmp.flush()
            with OpenEXR.File(tmp.name, separate_channels=True) as f:
                a = f.channels()['Y'].pixels.astype(np.float32)
    else:
        a = np.asarray(Image.open(io.BytesIO(raw)), dtype=np.float32)
    height, width = a.shape
    box = tuple(round(v*(width if i%2 == 0 else height)) for i,v in enumerate(crop))
    a = a[box[1]:box[3],box[0]:box[2]]
    shape = (resolution, round(resolution*a.shape[0]/a.shape[1]))
    a = np.asarray(Image.fromarray(a).resize(shape, Image.Resampling.LANCZOS))
    floor = float(np.quantile(a,.03)); peak = float(a.max())
    # No rectangular feather is inherited from the earlier stamp preparation.
    return np.clip((a-floor)/(peak-floor),0,1)

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--archives',type=Path,required=True)
    args=parser.parse_args()
    spec=spec_from_file_location('parent_preparation',Path(__file__).with_name('prepare-motion-forge-stamps.py'))
    parent=module_from_spec(spec);spec.loader.exec_module(parent)
    manifest=json.loads((ROOT/'SourceArt/Stamps/MotionForgeV1/sources.json').read_text())
    output=ROOT/'SourceArt/Stamps/MotionForgeStructuresV1';output.mkdir(parents=True,exist_ok=True)
    parents={s[0]:load_parent(s,args.archives,next(e for e in manifest['sources'] if e['id']=='MFP_'+s[0])) for s in parent.SOURCES}
    entries=[]
    for id,name,parent_id,base,seed,weight in STRUCTURES:
        raw=parents[parent_id];point=(round(seed[1]*(raw.shape[0]-1)),round(seed[0]*(raw.shape[1]-1)))
        footprint=component_at(nd.gaussian_filter(raw,3)>base,point)
        # Discard thin contour whiskers and disconnected neighbours, keep the main form.
        footprint=component_at(nd.binary_opening(footprint,iterations=4),point)
        footprint=nd.binary_fill_holes(footprint)
        yy,xx=np.nonzero(footprint);y0,y1=yy.min(),yy.max()+1;x0,x1=xx.min(),xx.max()+1
        clearance=min(x0/raw.shape[1],1-x1/raw.shape[1],y0/raw.shape[0],1-y1/raw.shape[0])
        assert clearance>.10, 'Contour touches source margin: choose another structure/floor'
        positive=component_at((raw>base)&footprint,point)
        distance=nd.distance_transform_edt(positive);toe=max(16.,.16*np.sqrt(footprint.sum()))
        t=np.clip(distance/toe,0,1);envelope=t*t*t*(t*(6*t-15)+10)
        h=np.maximum(raw-base,0)*envelope
        h*=component_at(h>0,point)
        peak=float(h.max());h/=peak
        core=(distance>=toe)&(h>0)
        core_error=float(np.max(np.abs(h[core]-(raw[core]-base)/peak)))
        assert core_error<1e-6 and core.sum()>100
        pad=round(max(x1-x0,y1-y0)*.12)
        h=np.pad(h[y0:y1,x0:x1],pad)
        h=np.pad(h,((0,1-h.shape[0]%2),(0,1-h.shape[1]%2)))
        values=np.rint(h*65535).astype(np.uint16)
        # Quantization can separate tiny toe specks; retain only the peak-connected form.
        values *= component_at(values>0,np.unravel_index(np.argmax(values),values.shape))
        assert nd.label(values>0)[1]==1
        assert not np.any(values[:pad]) and not np.any(values[-pad:]) and not np.any(values[:,:pad]) and not np.any(values[:,-pad:])
        assert values.max()==65535
        file=output/('MFS_'+id+'.png');Image.fromarray(values).save(file)
        assert np.array_equal(np.asarray(Image.open(file)),values)
        # Explicit base, toe and peak samples support an independent native import oracle.
        boundary=(values>0)&nd.binary_dilation(values==0)
        assert values[boundary].max()/65535 < .001, (id,'Toe boundary is too steep',values[boundary].max()/65535)
        samples=[]
        ys,xs=np.indices(values.shape)
        inset=.09*max(values.shape)+3
        full_weight=(xs>inset)&(xs<values.shape[1]-inset)&(ys>inset)&(ys<values.shape[0]-inset)
        for mask in [(values==0)&full_weight,boundary,(values>3000)&(values<15000),values>50000]:
            ys,xs=np.nonzero(mask)
            for i in np.linspace(0,len(xs)-1,3,dtype=int):samples.append(dict(x=int(xs[i]),y=int(ys[i]),value=int(values[ys[i],xs[i]])))
        entries.append(dict(id='MFS_'+id,name=name,parent_id='MFP_'+parent_id,weight=weight,
            source_archive=next(s[2] for s in parent.SOURCES if s[0]==parent_id),
            contour_floor=base,seed=list(seed),footprint_box=[int(x0),int(y0),int(x1),int(y1)],source_clearance_fraction=float(clearance),
            toe_width_parent_pixels=float(toe),zero_padding_pixels=pad,output_resolution=[values.shape[1],values.shape[0]],
            png_sha256=sha(file.read_bytes()),peak_residual=peak,core_error=core_error,core_pixels=int(core.sum()),
            zero_samples='Inside the full-weight native rectangle, outside the structural relief',
            connected_structures=1,boundary_height_max=float(values[boundary].max()/65535),
            scale_xy_cm=25,scale_z_cm=10000,reference_relief_m=100,samples_16bit=samples))
    record=dict(schemaVersion=1,author=manifest['author'],license=manifest['license'],source_page=manifest['source_page'],
        parent_manifest='../MotionForgeV1/sources.json',parent_manifest_sha256=sha((ROOT/'SourceArt/Stamps/MotionForgeV1/sources.json').read_bytes()),
        preparation='Original elevations; same parent crop/Lanczos/normalization, without rectangular fade. Curated connected elevation contour; 4px opening; remove neighbours; subtract contour floor; quintic distance-to-contour toe; tight crop plus 12% zero padding; linear uint16. Interior elevations preserved above the subtracted floor.',
        excluded='Rocky southern summit: its natural contour intersects the source boundary. No artificial cut or image-edge skirt retained.',sources=entries)
    (output/'sources.json').write_text(json.dumps(record,indent=2)+'\n')
    print('Prepared five independent structures:',[(e['id'],e['output_resolution']) for e in entries])

if __name__=='__main__':main()
