#!/usr/bin/env python3
"""Offline PDS/PROJ conversion; native VPP owns runtime height storage and sampling."""
from pathlib import Path
import json,hashlib,math
import numpy as np
import rasterio
from rasterio.windows import Window
from rasterio.transform import from_origin
from rasterio.warp import reproject, Resampling
from pyproj import Transformer
import argparse,urllib.request
parser=argparse.ArgumentParser(description="Prepare pinned Mars rasters for native VPP asset import; no runtime downloader.")
parser.add_argument('--source',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--fetch',action='store_true',help='Download missing pinned PDS sources')
args=parser.parse_args();p=args.source.resolve();out=args.output.resolve();p.mkdir(parents=True,exist_ok=True);out.mkdir(parents=True,exist_ok=True)
sources=json.loads(Path(__file__).with_name('sources.json').read_text())
def sha(f):
 d=hashlib.sha256()
 with f.open('rb') as s:
  for b in iter(lambda:s.read(1024*1024),b''):d.update(b)
 return d.hexdigest()
for name,record in sources.items():
 file=p/name
 if not file.exists() and args.fetch:
  temporary=file.with_suffix(file.suffix+'.download')
  urllib.request.urlretrieve(record['url'],temporary)
  assert temporary.stat().st_size==record['bytes'] and sha(temporary)==record['sha256'],name
  temporary.replace(file)
 assert file.is_file() and file.stat().st_size==record['bytes'] and sha(file)==record['sha256'],name
def native(name,data):
 # Native VPP's planar sampler owns interpolation; explicit halos bind periodic/global coverage.
 padded=np.pad(data,((2,2),(2,2)),mode='edge')
 if name=='MOLA32':
  padded[:,0:2]=padded[:,data.shape[1]:data.shape[1]+2]
  padded[:,-2:]=padded[:,2:4]
 file=out/(name+'.f32');padded.astype('<f4').tofile(file)
 return {'name':name,'width':data.shape[1],'height':data.shape[0],'file':file.name,'sha256':sha(file)}
global_data=np.memmap(p/'megr90n000fb.img',dtype='>i2',mode='r',shape=(5760,11520))
g=native('MOLA32',global_data)
g.update(westCenterDegrees=1/64,northCenterDegrees=90-1/64,longitudeStepDegrees=1/32,latitudeStepDegrees=1/32,radiusOffsetMeters=3396000,bGlobal=True,northPoleOffsetMeters=float(np.mean(global_data[0],dtype=np.float64)),southPoleOffsetMeters=float(np.mean(global_data[-1],dtype=np.float64)))
areoid=np.memmap(p/'mega90n000eb.img',dtype='>i2',mode='r',shape=(2880,5760))
def resample_areoid(west,north,dx,dy,width,height):
 # Both lattices are already planetocentric/east-positive angles after the
 # native PROJ inverse below. Native GDAL owns the raster resampling; no
 # ellipsoid/datum transformation is requested between identical angular CRSs.
 crs='+proj=longlat +R=3396000 +type=crs'
 result=np.empty((height,width),dtype=np.float64)
 reproject(source=areoid.astype(np.float64),destination=result,
  src_transform=from_origin(0,90,1/16,1/16),src_crs=crs,
  dst_transform=from_origin(west-dx/2,north+dy/2,dx,dy),dst_crs=crs,
  resampling=Resampling.bilinear,dst_nodata=float('nan'))
 assert np.isfinite(result).all(),'Required native areoid coverage is missing'
 return result
with rasterio.open(p/'Jezero-DTM.IMG') as d:
 W=4096;H=4096;cx=d.width//2;cy=d.height//2;window=Window(cx-W//2,cy-H//2,W,H);e=d.read(1,window=window)
 affine=d.window_transform(window);projection=Transformer.from_crs(d.crs,d.crs.geodetic_crs,always_xy=True)
 west,north=projection.transform(*(affine*(.5,.5)));east,lower=projection.transform(*(affine*(1.5,1.5)));dx=east-west;dy=north-lower
 lon=west+np.arange(W)*dx;lat=north-np.arange(H)*dy;valid=np.isfinite(e)&(e>-3e38)
 areoid_grid=resample_areoid(west,north,dx,dy,W,H)
 radius_offsets=np.where(valid,areoid_grid+e,0).astype(np.float32)
 j=native('Jezero1m',radius_offsets);mask=native('JezeroCoverage',valid.astype(np.float32));j.update(westCenterDegrees=west,northCenterDegrees=north,longitudeStepDegrees=dx,latitudeStepDegrees=dy,radiusOffsetMeters=3396000,bGlobal=False,coverage=mask)
 # Independent points retain original PDS elevations and PROJ inverse coordinates.
 oracles=[]
 for y,x in [(H//2,W//2),(H//3,W//3),(H//4,W//2),(3*H//4,W//2),(H//2,3*W//4)]:
  if valid[y,x]:oracles.append({'latitude':float(lat[y]),'longitude':float(lon[x]),'areoidHeightMeters':float(e[y,x]),'areoidRadiusMeters':float(3396000+areoid_grid[y,x]),'planetaryRadiusMeters':float(3396000+areoid_grid[y,x]+float(e[y,x]))})
 # The reference was independently evaluated from original PDS samples and
 # coarse-grid cell coordinates before this GDAL implementation was adopted.
 pinned=json.loads((Path(__file__).parents[2]/'conformance/Mars/source-oracle-v1.json').read_text())['regionalOracle']
 assert len(pinned)==len(oracles)
 for expected,actual in zip(pinned,oracles):
  assert max(abs(expected[k]-actual[k]) for k in expected)<1e-6,(expected,actual)
 oracles=pinned
 source_crs=d.crs.to_wkt();source_affine=list(affine)
minimum=3396000+min(float(global_data.min()),float(radius_offsets[valid].min()));maximum=3396000+max(float(global_data.max()),float(radius_offsets[valid].max()))
landmarks=[]
for label,lat,lon in [('Olympus Mons',18.65,226.2),('Valles Marineris',-10,290),('Hellas basin',-42.4,70.5),('Jezero',18.46,77.52),('North',89.9,0),('Seam',0,359.99)]:
 x=int(lon*32)%11520;y=min(5759,max(0,int((90-lat)*32)));landmarks.append({'name':label,'pixelX':x,'pixelY':y,'latitude':90-(y+.5)/32,'longitude':(x+.5)/32,'radiusMeters':3396000+int(global_data[y,x])})
files=['megr90n000fb.img','megr90n000fb.lbl','megr90n000fb.xml','mega90n000eb.img','mega90n000eb.lbl','Jezero-DTM.IMG']
record={'schemaVersion':1,'body':'Mars','frame':'MOLA IAU2000 body-fixed, planetocentric/east-positive','nativeParity':'canonical (X,Y,Z) -> native (X,-Y,Z)','shapeQuantity':'planetary radius in metres; HiRISE areoid height + MOLA Mars2000 areoid radius','global':g,'region':j,'sourceCRS':source_crs,'regionAffine':source_affine,'regionValidFraction':float(valid.mean()),'minimumRadiusMeters':minimum,'maximumRadiusMeters':maximum,'outerRadiusMeters':3425000,'sources':sources,'globalOracle':landmarks,'regionalOracle':oracles,'uncertainty':'Original MOLA metre-quantized/interpolated grid and HiRISE stereo precision/artifacts retained; 1 m/pixel is not a claim of 1 m absolute accuracy','coveragePolicy':'Regional invalid/border samples use declared global MOLA context; regional feather 100 m; poles converge to original first/last-row mean'}
(out/'manifest.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps({k:record[k] for k in ['minimumRadiusMeters','maximumRadiusMeters','regionValidFraction','globalOracle','regionalOracle']},indent=2))
