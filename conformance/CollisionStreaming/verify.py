#!/usr/bin/env python3
"""Check the retained single-run collision publication witness."""
import json
from pathlib import Path

root = Path(__file__).resolve().parent
result = json.loads((root / "native-result.json").read_text())
records = json.loads((root / "witness.json").read_text())["records"]
assert result["passed"] and result["nativeOwnerStable"]
assert result["routeDistanceMeters"] >= 100
assert result["routeFrames"] == result["routeGroundedFrames"] == 3135
held = [r for r in records if r["held"]]
assert len(records) == 25 and len(held) == 20
for r in held:
    assert r["currentPhysicsHit"]
    assert r["generatedFloorCrossingInShortTrace"]
    assert r["crossingComputed"] and r["crossingBelowComputed"]
    assert r["nextFootComputed"] and not r["nextPhysicsHit"]
    assert r["stateBusy"] and not r["newReadyToRender"]
transitions = [r for r in records if r["transition"]]
assert len(transitions) == 10
for start, end in zip(transitions[::2], transitions[1::2]):
    assert start["held"] and not end["held"]
    assert start["newState"] == end["oldState"]
    assert end["nextPhysicsHit"] and end["nextPhysicsWalkable"]
assert transitions[-1]["guardWaitSeconds"] == result["nativeCollisionWaitSeconds"]
print("Verified: five publication-correlated holds; 20 computed-floor/no-physics witnesses; 100% grounded native walk.")
