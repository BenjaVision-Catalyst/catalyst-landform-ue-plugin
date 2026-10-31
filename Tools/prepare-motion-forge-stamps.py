"""Prepare quantitative elevation stamps from the four linked CC0 source archives.
Usage: python prepare-motion-forge-stamps.py --archives /path/to/downloads
Requires numpy 2.5.4, Pillow 13.0.0, OpenEXR 3.5.3. No diffuse/bump images are used.
"""
import argparse, hashlib, io, json, zipfile
from pathlib import Path
import numpy as np
from PIL import Image
import OpenEXR

ROOT = Path(__file__).resolve().parents[1] / 'SourceArt/Stamps/MotionForgeV1'
SOURCES = [
    ('GrandMountain', 'Grand Mountain', 'grand-mountain.zip', '1s134YDk2aGJrpDAa4LNfWCiFQGXf93DQ', 'Grand Mountain Height Map Shared/Height Map TIF.tif', [.05,.10,.95,.95], 2049, 3),
    ('MountainRange', 'Mountain Range', 'mountain-range.zip', '1y_vAI4KOrhxB5Ntt0plDdoBOYggR4UKn', 'Mountain Range 8k Height Map/Mountain Range Height Map EXR.exr', [.10,.20,.95,.95], 2049, 3),
    ('RockyPeaks', 'Rocky Peaks', 'rocky-peaks.zip', '1GpKgzQez_BrAxjDp-jfgR5H9i5UdtVe3', 'Rugged Terrain with Rocky Peaks/Rugged Terrain with Rocky Peaks Height Map EXR.exr', [.12,.42,.98,.98], 2049, 2),
    ('TerrainRidge', 'Terrain Ridge', 'terrain-ridge.zip', '1mXiDuOvmDjC-KGTk2FmQUzRkPO_pgzpY', 'Ridge Through Terrain Height Map.png', [0,.25,1,.88], 1025, 1),
]

def sha(data): return hashlib.sha256(data).hexdigest()
def smooth(t):
    t = np.clip(t, 0, 1)
    return t*t*(3-2*t)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--archives', type=Path, required=True)
    args = parser.parse_args()
    ROOT.mkdir(parents=True, exist_ok=True)
    entries = []
    for id, name, archive, drive_id, member, crop, resolution, weight in SOURCES:
        path = args.archives / archive
        with zipfile.ZipFile(path) as z:
            raw = z.read(member)
        if member.endswith('.exr'):
            # OpenEXR needs a path. The temporary file contains only the selected
            # elevation member, never an executable from the download.
            import tempfile
            with tempfile.NamedTemporaryFile(suffix='.exr') as tmp:
                tmp.write(raw); tmp.flush()
                with OpenEXR.File(tmp.name, separate_channels=True) as f:
                    a = f.channels()['Y'].pixels.astype(np.float32)
        else:
            a = np.asarray(Image.open(io.BytesIO(raw)), dtype=np.float32)
        assert a.ndim == 2 and np.isfinite(a).all()
        height, width = a.shape
        box = tuple(round(v * (width if i % 2 == 0 else height)) for i,v in enumerate(crop))
        a = a[box[1]:box[3], box[0]:box[2]]
        shape = (resolution, max(5, round(resolution*a.shape[0]/a.shape[1])))
        a = np.asarray(Image.fromarray(a).resize(shape, Image.Resampling.LANCZOS)).copy()
        floor = float(np.quantile(a, .03)); peak = float(a.max())
        h = np.clip((a-floor)/(peak-floor), 0, 1)
        # Feather only the outer 7.5% of the crop. Interior ridges and gullies
        # retain source relative elevations; no procedural peaks/noise are added.
        y,x = np.indices(h.shape, dtype=np.float32)
        edge = smooth(np.minimum(x, h.shape[1]-1-x)/(h.shape[1]*.075)) * smooth(np.minimum(y, h.shape[0]-1-y)/(h.shape[0]*.075))
        h *= edge
        h /= h.max()
        values = np.rint(h*65535).astype(np.uint16)
        assert values.max() == 65535 and values.min() == 0
        assert not values[0].any() and not values[-1].any() and not values[:,0].any() and not values[:,-1].any()
        output = ROOT / ('MFP_' + id + '.png')
        Image.fromarray(values).save(output)
        decoded = np.asarray(Image.open(output)); assert np.array_equal(decoded, values)
        entries.append(dict(id='MFP_' + id, name=name, weight=weight, source_archive=archive,
            download_url=f'https://drive.usercontent.google.com/download?id={drive_id}&export=download&confirm=t',
            archive_sha256=sha(path.read_bytes()), member=member, member_sha256=sha(raw), original_resolution=[width,height],
            crop_pixels=list(box), source_floor=floor, source_peak=peak, output_resolution=list(shape),
            png_sha256=sha(output.read_bytes()), source_bit_depth='float32' if member.endswith(('.tif','.exr')) else 'uint16',
            scale_xy_cm=25, scale_z_cm=10000, offset_z_cm=0, reference_relief_m=100, blend='Max',
            samples_16bit=[dict(x=int(x),y=int(y),value=int(values[y,x])) for y in [values.shape[0]//4,values.shape[0]//2,3*values.shape[0]//4] for x in [values.shape[1]//4,values.shape[1]//2,3*values.shape[1]//4]]))
    manifest = dict(schemaVersion=1, author='Chris J Mitchell / Motion Forge Pictures',
        source_page='https://www.motionforgepictures.com/height-maps/', license='CC0-1.0',
        license_url='https://creativecommons.org/publicdomain/zero/1.0/',
        license_evidence='Heightmaps page explicitly states CC0; Grand Mountain and Rocky Peaks archive readmes confirm free Creative Commons / CC0 no-attribution use.',
        preparation='Crop; Lanczos resample; subtract 3rd-percentile floor; linear normalize; outer 7.5% smoothstep feather; re-normalize; linear uint16 encode. No diffuse, gamma correction, noise or invented erosion.',
        scale_note='Sources are not georeferenced and supply no surveyed horizontal/elevation scale. 0.25m source texels and 100m relief are authored normalization units; placement width/relief define final physical size.',
        sources=entries)
    (ROOT/'sources.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print('Prepared four detailed native stamp sources')

if __name__ == '__main__': main()
