#!/usr/bin/env python3
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parent
first = json.loads((root / 'direct-binding-summary.json').read_text())
phases = json.loads((root / 'phase-replay.json').read_text())
counts = json.loads((root / 'candidate-overlap.json').read_text())
assert counts['wholeBoundsMatch'] and counts['cubicIntervalFeatures'] == 1626
assert counts['passages'] == 20 and counts['chambers'] == 4
for a, b, c in zip(first['sites'], phases['sites'], counts['sites']):
    assert a['site'] == b['site'] == c['site']
    rows = [{k: r[k] for k in ['key', 'minFieldCm', 'maxFieldCm', 'minAbsFieldCm', 'finePointsSelected']} for r in b['chunkRecords']]
    digest = hashlib.sha256(json.dumps(rows, sort_keys=True).encode()).hexdigest()
    assert digest == a['fieldAndSelectionSHA256'] == b['fieldAndSelectionSHA256']
    assert a['checksum'] == b['checksum']
    assert b['coarsePoints'] == b['chunks'] * 1331 == c['points']
    assert sum(r['finePointsSelected'] for r in b['chunkRecords']) == b['finePointsSelected']
    assert sum(r['maxFieldCm'] < 0 and r['finePointsSelected'] == 0 for r in b['chunkRecords']) == b['skipRockChunks']
    assert c['skippedRockPoints'] == b['skipRockChunks'] * 1331
    assert abs(c['skippedRockFeaturePointPairs'] / c['skippedRockPoints'] - c['averageCandidatesOnSkippedRock']) < 1.e-10
assert phases['heightPoints'] == sum(s['coarsePoints'] for s in phases['sites'])
print('Verified matched field/selection replays and rejected-rock candidate counts at all three sites.')
