from pathlib import Path
import argparse,hashlib,json
p=argparse.ArgumentParser();p.add_argument('--host',type=Path);a=p.parse_args();root=Path(__file__).resolve().parent
read=lambda n:json.loads((root/n).read_text())
b=read('baseline.json');assert b['nativeCollisionWaitSeconds']>60 and b['maximumStoppedUnderInputSeconds']>9
c=read('cache-only.json');assert c['nativeCollisionWaitSeconds']>0 and c['maximumStoppedUnderInputSeconds']>5
wall=read('wall.json');assert wall['passed'] and wall['nativeCollisionWaitFrames']==0 and wall['wallContactFrames']>10 and wall['minimumFootFieldMeters']>-.35
f=read('chamber.json');assert f['passed'] and f['nativeOwnerStable'] and f['nativeFloorDemandStable']
assert f['routeDistanceMeters']>=1000 and f['routeFrames']==f['routeGroundedFrames']>0
assert f['nativeCollisionWaitFrames']==0 and f['maximumStoppedUnderInputSeconds']<.1 and f['referenceMovingFrameMs']['max']<100
assert f['peakSpeedCmPerSecond']>=2970 and f['referenceWalkSpeedCmPerSecond']==3000 and f['minimumFootFieldMeters']>0
assert f['nativeFloorDemandInvokers']==91 and f['nativeColliderBytes']>0 and f['nativeCollisionComponents']>0
r=read('review.json');assert r['passed'] and r['saveBytes']>0 and r['controlIdentityUnchanged']
reload=read('reader.json');assert reload['passed'] and reload['comparedProbes']>0 and reload['maximumReloadErrorCm']==0
cache=read('cache-parity.json');assert cache['maximumFieldErrorMeters']==0 and cache['bytes']<=8388608 and cache['floorSites']>=1800
native=read('native-tests.json');assert len(native)==8 and all(t['state']=='Success' and t['errors']==t['warnings']==0 for t in native)
h=read('headroom.json');assert h['passed'] and len(h['headroom'])==4
assert all(v['peakNativePendingTasks']==v['nativeStateBusyFrames']==0 and v['gpuMs']['samples']>0 for v in h['headroom'])
manual=read('real-input.json');assert manual['movingInputFrames']>=1000 and 0<manual['groundedInputFrames']<=manual['movingInputFrames'] and manual['admissionWaitSeconds']==0 and manual['walkSpeedCmPerSecond']==3000
assert manual['maximumMovingFrameMs']<100
if a.host:
 for rel,digest in read('native-source-sha256.json').items():assert hashlib.sha256((a.host/rel).read_bytes()).hexdigest()==digest,rel
print('Verified kilometre chamber walk at 30 m/s, real input, native edits/reload, eight regressions and four headroom levels.')
