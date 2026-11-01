"""Prepare Motion Forge's CC0 canyon as one connected negative-relief feature.
Numerical elevation processing only. No generated detail or rectangular edge skirt.
"""
import argparse, hashlib, io, json, zipfile
from pathlib import Path
import numpy as np
from PIL import Image
from scipy import ndimage as nd
ROOT=Path(__file__).resolve().parents[1]
def sha(b):return hashlib.sha256(b).hexdigest()
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--archive',type=Path,required=True);args=ap.parse_args()
    with zipfile.ZipFile(args.archive) as z:
        raw=z.read('Canyon Height Maps/Canyon Height Map.png')
        river_raw=z.read('Canyon Height Maps/Water River Mask.png')
    a=np.asarray(Image.open(io.BytesIO(raw)),dtype=np.float64)
    river=np.asarray(Image.open(io.BytesIO(river_raw)))>0
    # Main incision below a contour, including its tributaries; keep clear of source ends.
    datum=16700.;mask=nd.gaussian_filter(a,3)<datum
    mask[:,:246]=False;mask[:,3850:]=False
    labels,_=nd.label(nd.binary_opening(mask,iterations=3))
    ids,counts=np.unique(labels[river & mask],return_counts=True);counts[ids==0]=0
    mask=nd.binary_fill_holes(labels==ids[np.argmax(counts)])
    assert not mask[0].any() and not mask[-1].any()
    distance=nd.distance_transform_edt(mask);toe=150.
    t=np.clip(distance/toe,0,1);envelope=t**3*(t*(6*t-15)+10)
    depth=np.maximum(datum-a,0)*envelope;depth[~mask]=0
    core=distance>=toe;peak=depth.max();depth/=peak
    error=float(np.max(np.abs(depth[core]-np.maximum(datum-a[core],0)/peak)))
    assert error<1e-12 and core.sum()>1000
    yy,xx=np.nonzero(mask);box=[int(xx.min()),int(yy.min()),int(xx.max()+1),int(yy.max()+1)]
    x0,y0,x1,y1=box;pad=round(max(x1-x0,y1-y0)*.12)
    h=np.pad(depth[y0:y1,x0:x1],pad);m=np.pad(river[y0:y1,x0:x1].astype(np.float32)*mask[y0:y1,x0:x1],pad)
    size=(2049,round(2049*h.shape[0]/h.shape[1])|1)
    h=np.clip(np.asarray(Image.fromarray(h.astype(np.float32)).resize(size,Image.Resampling.BILINEAR)),0,1);h/=h.max()
    values=np.rint(h*65535).astype(np.uint16)
    labels,n=nd.label(values>0);values*=labels==labels[np.unravel_index(values.argmax(),values.shape)]
    assert nd.label(values>0)[1]==1
    moat=min(np.nonzero(values)[0].min(),np.nonzero(values)[1].min(),values.shape[0]-1-np.nonzero(values)[0].max(),values.shape[1]-1-np.nonzero(values)[1].max())
    assert moat>100 and not values[0].any() and not values[-1].any() and not values[:,0].any() and not values[:,-1].any()
    boundary=(values>0)&nd.binary_dilation(values==0);assert values[boundary].max()/65535<.002
    out=ROOT/'SourceArt/Stamps/MotionForgeCanyonV1';out.mkdir(parents=True,exist_ok=True)
    png=out/'MFC_RiverCanyonDepth.png';Image.fromarray(values).save(png)
    riverfile=out/'MFC_RiverMask.png';Image.fromarray(np.rint(np.asarray(Image.fromarray(m).resize(size,Image.Resampling.BILINEAR))*65535).astype(np.uint16)).save(riverfile)
    samples=[]
    for mask in [values==0,boundary,(values>10000)&(values<30000),values>60000]:
        ys,xs=np.nonzero(mask)
        for i in np.linspace(0,len(xs)-1,3,dtype=int):samples.append(dict(x=int(xs[i]),y=int(ys[i]),value=int(values[ys[i],xs[i]])))
    record=dict(schemaVersion=1,author='Chris J Mitchell / Motion Forge Pictures',license='CC0-1.0',source_page='https://www.motionforgepictures.com/height-maps/',
        download_id=10152,archive_sha256=sha(args.archive.read_bytes()),height_member='Canyon Height Maps/Canyon Height Map.png',height_sha256=sha(raw),river_member='Canyon Height Maps/Water River Mask.png',river_sha256=sha(river_raw),
        preparation='Positive depth below elevation contour 16700; river-connected incision and tributaries; ends inset 246px; 3px opening; quintic 150px distance-to-contour toe closes the canyon ends and banks; tight crop and 12% zero moat; bilinear 2049px resample; linear uint16. Interior source relief preserved. No georeferenced metre scale is claimed. River mask retained for future use; no water generation.',
        datum=datum,crop=box,padding_pixels=pad,output_resolution=list(size),connected_structures=1,zero_moat_pixels=int(moat),boundary_depth_max=float(values[boundary].max()/65535),interior_error=error,
        sources=[dict(id='MFC_RiverCanyonDepth',name='River canyon incision',png_sha256=sha(png.read_bytes()),scale_xy_cm=25,scale_z_cm=10000,reference_relief_m=100,samples_16bit=samples)],river_output_sha256=sha(riverfile.read_bytes()))
    (out/'sources.json').write_text(json.dumps(record,indent=2)+'\n')
    Image.fromarray(np.uint8(values/257)).resize((1024,round(1024*size[1]/size[0]))).save('/private/tmp/canyon-review/processed-depth.png')
    print(json.dumps({k:v for k,v in record.items() if k in ['output_resolution','zero_moat_pixels','boundary_depth_max','interior_error']}))
if __name__=='__main__':main()
