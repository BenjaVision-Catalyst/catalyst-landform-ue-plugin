from pathlib import Path
import argparse,json,hashlib
p=argparse.ArgumentParser();p.add_argument('--host',type=Path);a=p.parse_args();root=Path(__file__).resolve().parent
read=lambda n:json.loads((root/n).read_text())
b=read('baseline.json');assert b['nativeCollisionWaitSeconds']>0 and b['maximumStoppedUnderInputSeconds']>.1
s=read('step-only.json');assert s['routeFrames']==s['routeGroundedFrames'] and s['nativeCollisionWaitFrames']>0
f=read('fixed.json');assert f['passed'] and f['roamProof'] and f['nativeOwnerStable'] and f['nativeFootAnchorEnabled']
assert f['routeDistanceMeters']>=300 and f['routeFrames']==f['routeGroundedFrames']>0
assert f['nativeCollisionWaitFrames']==0 and f['maximumStoppedUnderInputSeconds']<.1 and f['referenceMovingFrameMs']['max']<100
assert f['minimumFootFieldMeters']>0 and f['legacyProbeRejectedWhileMoving']>0 and f['collisionInvokerRadiusCm']==3200
r=read('real-input.json');assert r['movingInputFrames']==r['groundedInputFrames']>=2300 and r['admissionWaitSeconds']==0 and r['maximumMovingFrameMs']<100
old=read('legacy-entrance.json');assert old['passed'] and old['routeDistanceMeters']>=100 and old['routeGroundedFrames']==old['routeFrames'] and old['minimumFootFieldMeters']>0
t=read('native-tests.json');assert len(t)==7 and all(x['state']=='Success' and x['errors']==x['warnings']==0 for x in t)
if a.host:
 for path,h in read('native-source-sha256.json').items():assert hashlib.sha256((a.host/path).read_bytes()).hexdigest()==h,path
print('Verified native loop, real keyboard input, legacy entrance safety and seven native regressions.')
