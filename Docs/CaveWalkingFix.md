# Free cave walking without admission pauses

Follow-up: [chamber collision residency](ChamberCollisionResidency.md) replaces the earlier walking qualification after fresh-session pauses were reproduced.

7 November 2026. Follow-up to the [spatial cave prototype](SpatialCavePrototype.md).
The original centreline test missed pauses during broader chamber exploration.
Geometry, geology, edits, the existing UI and VPP source are unchanged.

## Causes and correction

Our movement guard treated a failed floor ray 1.5 m ahead as missing streamed
collision whenever any native world state was busy. That point could lie beyond
a wall, corner or turn that the capsule would not reach during the frame. It
called `StopMovementImmediately` and discarded input until the probe recovered.

The guard now checks the capsule's possible step during the current frame,
including existing momentum during turns. A native capsule sweep and native
slide projection account for blocking walls. Real centre support is required;
accepting capsule perching alone can allow the centre to cross an unpublished
chunk edge. Missing support still stops an unsafe step. Ordinary supported
walking and wall contact no longer inherit the global generation wait.

The native collision readiness check previously used only the capsule centre,
which can occupy an air chunk above its supporting floor. A second native VPP
invoker checks just below the foot. Its 1 m sphere sits wholly inside the main
request sphere; it adds no proxy collider or custom streaming owner.

The original 25 m request radius also left too little publication margin at
6 m/s near chunk boundaries. A corrected-step run retained a 70.8 ms real
coverage wait. The final spatial fixture uses five native collision chunks:
32 m with its 20 cm voxels and 32-voxel chunks. The captured maximum streaming
publication interval is 3.595 s. As a conservative tuning budget, reserving one
6.4 m chunk leaves about 3.10 s of lead from 25 m, versus 4.27 s from 32 m. This
is a measured fixture budget, not a universal native latency guarantee.

VPP still owns chunk demand, generation, meshing, active physics, sculpting and
persistence. Resolution and render quality are unchanged. The broader radius
applies to the spatial prototype; historical spline controls retain their settings.

## Qualification

The new route travels through the passage, around the chamber, close to a wall,
and back. It checks actual grounded contact, input progress and maximum moving
frame duration. Its final gate permits no application admission waits, no input
stop of 100 ms, and no moving frame of 100 ms. It runs with the manual launch's
render/timing settings, without the benchmark's console-variable overrides.

| Case | Travel m | Grounded frames | Admission wait s | Longest stopped-input interval s | Moving frame p95 / max ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Original guard, 25 m | 308.63 | 3,659 / 3,659 | 0.161 | 0.151 | 17.36 / 33.71 |
| Corrected step, 25 m | 308.69 | 3,720 / 3,720 | 0.071 | 0.092 | 17.20 / 43.28 |
| Corrected step, 32 m | 308.62 | 3,727 / 3,727 | 0 | 0 | 17.19 / 36.47 |

The final minimum sampled foot field is +0.0111 m, on the air side of the
foundation. Its passive observer finds two frames where the old distant probe
would reject movement although actual movement remains grounded. Native terrain
and invoker identities remain stable. These are single trials with scheduling
variation, not a controlled throughput speedup comparison.

Seven native gravity, scene, geometry, UI/undo and delivery parity tests pass
with zero warnings/errors. The historical spline entrance still completes
100.06 m with 3,356 / 3,356 frames grounded and no floor penetration. That control
retains 17.23 s of generation-related waits: it is a safety regression check,
not a claim that the old spline provider's generation cost is fixed.

[Retained reports, source hashes and verifier](../conformance/CaveWalkingFix/README.md)
include the baseline, residual coverage wait and final result. Real keyboard
input also retains 2,320 / 2,320 grounded input frames, zero admission waits and
a 45.86 ms maximum moving frame, including forward, sideways and reverse motion.
`Tools/qualify-spatial-cave.py --phase roam --manual-settings` repeats the broader
route with the existing project/output arguments. The manual launcher also keeps
`manual-movement.json` beside its ready report, recording real keyboard-input
frames, grounded frames, maximum moving frame time and admission waits. It adds
no UI or terrain ownership.
