> Withdrawn as a streaming fix on 11 November 2026. These runs preloaded the complete cave floor before walking. The implementation is removed. See [the moving-demand delivery fix](CaveStreamingDeliveryFix.md). The original experiment is retained below as historical evidence.

# Chamber walking without publication waits

11 November 2026. Supersedes the walking claim in [CaveWalkingFix.md](CaveWalkingFix.md)
for the opt-in spatial cave. The user reproduced long chamber pauses after that
checkpoint, including on a fresh launch. Its short walkthrough was insufficient.

## Exact stop and cause

The fresh session recorded 54.11 s of application admission waits. The expanded
three-lap chamber reproduction at 6 m/s recorded 69.43 s, including a 9.93 s stop.
Every captured rejection was a missing destination physics hit, not an unwalkable
normal or a penetrating sweep. The guard called `StopMovementImmediately`,
consumed input and returned before native CharacterMovement could run.

A second witness sampled the generated volume along the rejected floor trace.
It found a real air-to-rock crossing beyond the active native collider bounds.
VPP's newest state reported the foot chunk computed, but was not ready to render.
A computed key is not an active physics surface, and surface-net vertices can
extend across a chunk-key boundary. The witness retains state indices, native
component bounds, the physics result and the generated crossing.

The long reproduction measured a 13.75 s maximum publication interval. The former
32 m sphere has only 5.33 s nominal reach at 6 m/s, before pipeline/chunk margins.
Its earlier 3.60 s tuning sample was not a sufficient end-to-end latency budget.
At the requested 30 m/s, nominal reach would be only 1.07 s. Waiting for a whole
native state after reaching this frontier is the underlying integration error.
The safety guard exposed that missing floor; removing it would allow a fall.

Worker sampling also found our cave field repeatedly evaluating canonical rock
restoration/tectonics inside render meshing. The cache experiment below reduces
that repeated work, but does not independently remove publication waits.

A later real-input run exposed a separate short false stop at a steep passage
wall: the trace had active physics and a computed key, but `IsWalkable` was false.
The guard now distinguishes presence of blocking physics from walkability.
Native CharacterMovement owns wall/slope response. Missing-physics admission
checks remain; a dedicated native wall-contact/reversal test covers this case.

## Correction

This is one finite active passage/chamber composition. Its activation now requests
its whole floor region before releasing the walker, using ordinary native VPP
collision invokers. The compiled footprint, including warp/undercut/join padding,
produces 91 overlapping demand spheres. Their positions follow canonical curved
bedding and the existing floor field. They do not use the walkthrough route,
player heading or anticipated input. A coverage test checks 1,821 generated floor
sites, including feet, capsule centres and heads.

VPP still owns demand merging, generation, meshing, physics publication, live
sculpting and persistence. There is no substitute floor, collider installation,
manual partial state render, vendor-source change or new terrain/streaming owner.
The native demand components belong to this activation's planet actor and retire
with it. The current and foot safety invokers remain. Inside the resident region,
the current invoker requests a local 2 m sphere; outside it, ordinary 32 m moving
demand remains available for flight or excavation. The existing UI is unchanged.

An eight MiB, snapshot-owned cache reuses exact-position base cave-field samples
between native collision/render workers. It performs no interpolation or rounding,
retains no meshes or sculpt results, and uses bounded direct-map eviction. Read
and write leases cover lookup/copy only, not geology/noise evaluation. Recompiling
a composition creates a separate cache; native edits remain composed after the
base field. Mixed, reordered and concurrent queries preserve exact field values.

The manual launcher and qualifier now use 3,000 cm/s, five times the previous cap.
Acceleration/braking scale with speed to preserve response time. Automated routes
slow near waypoints through normal CharacterMovement to turn; retained peak speed
must demonstrate that long crossings actually reach the requested cap.

## Evidence

| Captured experiment | Speed cap | Travel | Admission waits | Longest stopped-input interval |
| --- | ---: | ---: | ---: | ---: |
| Previous moving demand, no sample cache | 6 m/s | 1,045 m | 69.43 s | 9.93 s |
| Same demand, exact sample cache only | 6 m/s | 1,045 m | 14.11 s | 7.50 s |
| Native floor residency, initial trial | 30 m/s | 1,042 m | 0 | 0.018 s |

The initial residency trial retained 2,417/2,417 grounded frames, reached
3,000 cm/s, and had an 80.73 ms worst moving frame. These single trials locate
the cause and validate the policy; they are not a controlled throughput ratio.
The final build completes 1,041.75 m at the requested 30 m/s cap with
2,418/2,418 grounded route frames and zero admission waits. Moving-frame p95 is
26.09 ms, max 89.01 ms. Native collider storage is 64.61 MiB across 598 native
collision components, plus the bounded eight MiB sample allocation. These are
collider/cache sizes, not total process memory. The dedicated wall test retains 609/609 grounded frames, 477 native blocking
contact frames, zero admission holds and a 41.31 ms worst frame. Native excavation/backfill keeps
the control unchanged, writes a 150,082-byte save, and a separate process reloads
14 probes with zero field error and a short grounded traversal.

The stationary render-only proxy check has zero native pending work at every
level. GPU p95 is 10.79 ms with no proxy instances and 16.48 ms with 16,384 sphere
instances (960 potential triangles each). This measures that fixed render proxy,
not populated ecology, animation, object collision or moving ecosystem behavior.
Real keyboard input retains 1,571 input frames, 1,478 grounded input frames,
zero admission waits and a 37.11 ms worst moving frame. These free obstacle inputs
include brief airborne movement; they are not labelled 100% grounded. The normal
kilometre route and dedicated wall fixture retain exact grounded continuity.

[Qualification reports and verifier](../conformance/ChamberCollisionResidency/README.md)
retain the failed baseline and cache-only experiment as well as the correction.
The manual telemetry now includes a bounded `manual-waits.jsonl`: hold/release,
rejected step, active physics, computed key, native state indices and pending work.
It records the cause directly if another hold occurs, without adding UI.

This floor residency policy is scoped to the finite spatial prototype activation.
It does not preload every cave on a planet or claim that historical spline-family
generation latency is fixed. Cold activation work and resident memory are explicit
costs; the desired steady walking path has no collision generation deadline to race.
