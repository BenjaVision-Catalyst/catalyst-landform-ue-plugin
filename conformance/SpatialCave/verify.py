"""Check captured native spatial-cave qualification evidence."""
from pathlib import Path
import argparse
import hashlib
import json
import math

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--host', type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parent

def read(name):
    return json.loads((root / name).read_text())

tests = read('automation-summary.json')
assert len(tests) == 17 and all(t['state'] == 'Success' and t['errors'] == 0 for t in tests)
assert sum(t['warnings'] for t in tests) == 1
assert [m for t in tests for m in t.get('warningEvents', [])] == ['LogHttp: HTTP request timed out after 3.00 seconds URL=https://www.google.com/generate_204']
g = read('geometry.json')
assert g['floorProbes'] >= 250 and g['floorReliefMeters'] > 1
assert g['maximumRouteGrade'] < math.tan(math.radians(30))
assert g['minimumHeadClearanceFieldMeters'] > .35
assert g['heightPoints'] == g['pruningMaxErrorMeters'] == g['deliveryMaxErrorCm'] == 0
assert g['openingWidthsMeters'][-1] > 2 * g['openingWidthsMeters'][0]
assert g['ceilingHeightsMeters'][-1] > 1.7 * g['ceilingHeightsMeters'][0]
work = read('coarse-work.json')
assert len(work['sites']) == 2 and work['heightPoints'] == 0
for row in work['sites']:
    assert row['coarsePoints'] == row['chunks'] * 1331
    assert 0 < row['skipRockChunks'] < row['chunks']
    assert row['finePointsSelected'] < row['chunks'] * 36**3
    assert sum(c['finePointsSelected'] for c in row['chunkRecords']) == row['finePointsSelected']
for name in ['walk-250.json', 'walk-600.json']:
    row = read(name)
    assert row['passed'] and row['nativeOwnerStable'] and row['referenceWalkComplete']
    assert row['routeDistanceMeters'] >= 75
    assert row['routeFrames'] == row['routeGroundedFrames'] > 0
    assert row['nativeCollisionWaitSeconds'] <= .25 and row['minimumFootFieldMeters'] > -.35
    assert row['beforeMineCm'] < 0 < row['afterMineCm'] and row['afterFillCm'] < 0
    assert row['controlIdentityUnchanged'] and row['saveBytes'] > 0
fast = read('walk-600.json')
assert fast['referenceWalkSpeedCmPerSecond'] == 600 and fast['peakSpeedCmPerSecond'] >= 599
assert fast['referenceMovingFrameMs']['samples'] == fast['routeFrames']
reload = read('reload.json')
assert reload['passed'] and reload['nativeOwnerStable'] and reload['comparedProbes'] == 14
assert reload['maximumReloadErrorCm'] == 0 and reload['walkGroundedFrames'] > 0
head = read('headroom-100.json')
assert head['passed'] and head['nativeOwnerStable']
assert [r['instances'] for r in head['headroom']] == [0, 1024, 4096, 16384]
for row in head['headroom']:
    assert row['peakNativePendingTasks'] == row['nativeStateBusyFrames'] == 0
    assert row['measuredSeconds'] >= 15 and row['gpuMs']['samples'] > 0
for name, digest in read('capture-sha256.json').items():
    assert hashlib.sha256((root / name).read_bytes()).hexdigest() == digest
if args.host:
    for name, digest in read('native-source-sha256.json').items():
        assert hashlib.sha256((args.host / name).read_bytes()).hexdigest() == digest, name
print('Verified 17 native tests, both grounded walks, reload, chunk work and settled rendering windows.')
