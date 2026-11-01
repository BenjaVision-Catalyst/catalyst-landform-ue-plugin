"""Raise the canyon bed into long upstream/downstream approaches (Canyons v2).
Operates on quantitative uint16 source data, preserving v1 and its natural banks.
"""
from pathlib import Path
import hashlib,json,numpy as np
from PIL import Image
from scipy import ndimage as nd
ROOT=Path(__file__).resolve().parents[1]
def sha(f):return hashlib.sha256(f.read_bytes()).hexdigest()
def main():
 parent=ROOT/'SourceArt/Stamps/MotionForgeCanyonV1';meta=json.loads((parent/'sources.json').read_text());old=parent/'MFC_RiverCanyonDepth.png'
 assert sha(old)==meta['sources'][0]['png_sha256']
 a=np.asarray(Image.open(old),dtype=np.uint16);river=np.asarray(Image.open(parent/'MFC_RiverMask.png'),dtype=np.float64)/65535;h,w=a.shape
 mass=river.sum(axis=0);valid=np.nonzero(mass>0)[0];ys=np.arange(h)[:,None];centres=np.zeros(w);centres[valid]=(river*ys).sum(axis=0)[valid]/mass[valid]
 centres=nd.gaussian_filter1d(np.interp(np.arange(w),valid,centres[valid]),3)
 # Canonical angular footprint 156 x 48 degrees; source is not georeferenced.
 step_x=156/(w-3.0001);step_y=48/(h-3.0001)
 distance=np.r_[0,np.cumsum(np.hypot(step_x,np.diff(centres)*step_y))]
 _,xx=np.nonzero(a);lo,hi=int(xx.min()),int(xx.max());length=distance[hi]-distance[lo];ramp=.32*length
 t=np.clip(np.minimum(distance-distance[lo],distance[hi]-distance)/ramp,0,1);profile=t**3*(t*(6*t-15)+10)
 # A rising bed intersects the existing walls. It does not attenuate every bank
 # height or merely feather the outside rectangle. Central relief is untouched.
 depth=np.minimum(a/65535.,profile[None,:]);values=np.rint(depth*65535).astype(np.uint16)
 labels,n=nd.label(values>0);values*=labels==labels[np.unravel_index(values.argmax(),values.shape)]
 assert nd.label(values>0)[1]==1 and values.max()==65535
 middle=(distance>distance[lo]+ramp)&(distance<distance[hi]-ramp);assert np.array_equal(values[:,middle],a[:,middle])
 assert not np.any(values>a) and not values[:,0].any() and not values[:,-1].any() and not values[0].any() and not values[-1].any()
 boundary=(values>0)&nd.binary_dilation(values==0);assert values[boundary].max()/65535<.002
 out=ROOT/'SourceArt/Stamps/MotionForgeCanyonV2';out.mkdir(parents=True,exist_ok=True)
 png=out/'MFC_RiverCanyonRampDepth.png';Image.fromarray(values).save(png)
 mask=out/'MFC_RiverMask.png';Image.fromarray(np.rint(river*(values>0)*65535).astype(np.uint16)).save(mask)
 samples=[]
 for x in np.linspace(lo,hi,25,dtype=int):
  y=int(round(centres[x]));samples.append(dict(x=int(x),y=y,value=int(values[y,x])))
 for region in [(values==0),boundary,values>60000]:
  yy,xx=np.nonzero(region)
  for i in np.linspace(0,len(xx)-1,3,dtype=int):samples.append(dict(x=int(xx[i]),y=int(yy[i]),value=int(values[yy[i],xx[i]])))
 result=dict(schemaVersion=1,author=meta['author'],license=meta['license'],source_page=meta['source_page'],parent_manifest='../MotionForgeCanyonV1/sources.json',parent_manifest_sha256=sha(parent/'sources.json'),parent_depth_sha256=sha(old),
  preparation='River-mask centreline, smoothed 3px; cumulative arc length in the canonical 156 x 48 degree footprint. Raise the bed to a quintic depth ceiling over 32% of river length at each end. Existing banks remain where shallower than the raised bed; central elevations remain byte-identical. Linear uint16. No water generation.',
  ramp_length_fraction_each_end=.32,canonical_half_extents_degrees=[78,24],profile_arc_degrees=float(length),ramp_arc_degrees=float(ramp),profile_max_slope_at_470m_102m_depth_degrees=float(np.degrees(np.arctan(102*1.875/(470*np.radians(ramp))))),
  output_resolution=[w,h],connected_structures=1,boundary_depth_max=float(values[boundary].max()/65535),central_pixels_preserved=int(middle.sum()*h),river_output_sha256=sha(mask),zero_moat_pixels=meta['zero_moat_pixels'],
  sources=[dict(id='MFC_RiverCanyonRampDepth',name='River canyon with rising end beds',png_sha256=sha(png),scale_xy_cm=25,scale_z_cm=10000,reference_relief_m=100,samples_16bit=samples)])
 (out/'sources.json').write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps({k:v for k,v in result.items() if k.startswith(('ramp','profile','central'))}))
if __name__=='__main__':main()
