# Why cave walking pauses

7 November 2026. Research against workspace `9f88550`, Landform `712ddd4`,
UE 5.7.4 / installed VPP 2.0p8. This follows the
[delivery comparison](VPPDeliveryPrototype.md). Geometry, geology, edits, the
existing UI and native VPP ownership remain unchanged. No movement, generator,
engine or VPP fix is included in this research change.

## Finding

The root failure is in our integration: we do not keep active collision ahead of
the character. We feed VPP expensive field work, configure a fixed spatial lead
without a publication-latency budget, and stop the character when coverage runs
out. The captured mechanism is the normal distinction between computed data and
physics publication of a completed native state. It is not evidence that VPP's
collision system needs to be replaced or patched.

Retained profiling identifies our height and cave evaluation as the leading
measured generation cost. We need to make our workload and advance requests fit
VPP's native state contract.
This is a movement hold while the application continues ticking, distinct from
a game-thread hitch. It is not evidence that Chaos is spending the whole pause
cooking collision. The previously reported “collision holds” are the guard's
accumulated stopped movement time, not a collision cooker timer.

Normal streaming should request collision sufficiently early that it is active
before the character arrives. A deliberate stop is our fallback for missing
support, not a necessary walking phase in the VPP collision design.

## Direct witness

A disposable Lab build added a read-only observer to the existing 100 m entrance
walk, using the optimized C++ delivery path. It changed no field or collision
settings. The observer checked the same forward floor trace as the guard,
queried VPP's newest collision subsystem, sampled the unedited generated field
along that trace, and recorded state indices. It observed transitions plus one
sample per second during a hold.

The run completed 100.04 m with 3,135 / 3,135 route frames grounded. It recorded
16.038 s of guard time. This instrumented run establishes causality; its timing
is not a new performance comparison with the uninstrumented trials.

All **20 held snapshots** had:

- A generated air-to-solid floor crossing within the short trace range.
- Computed collision keys at the crossing and below it, according to VPP.
- No hit from the forward physics line trace.
- A pending native state whose task context was not complete.

All five releases acquired a walkable physics floor when the formerly pending
state became the rendered state. State processing could already have begun the
next state at release, so being globally busy alone does not require a stop.

| Rendered state during hold | Pending state | Rendered state at release | Observed hold, wall seconds |
| ---: | ---: | ---: | ---: |
| 14 | 15 | 15 | 2.24 |
| 15 | 16 | 16 | 3.25 |
| 16 | 17 | 17 | 3.12 |
| 17 | 18 | 18 | 4.25 |
| 18 | 19 | 19 | 3.22 |

Several held samples had only one native task left. The local floor's computed
key was already present throughout. The witness does not identify that last
task's type; it establishes the state-publication dependency.

[Captured records, summary, source hashes and observer](../conformance/CollisionStreaming/README.md)
retain the evidence. `HasCollision` checks computed keys, which can represent
empty chunks; the separate generated-floor crossing is therefore necessary.
Nearby component counts alone are not treated as proof of an active floor.

## Our integration mistakes

- We optimized callback throughput without enforcing a deadline for upcoming
  collision to become active in physics. A faster sample function can still
  miss that deadline when too much work shares one state.
- We kept expensive body and cave evaluation inside a broad planet callback.
  The optimized prototype still evaluates terrain height eagerly and leaves
  the global execution problem intact.
- We selected a 25 m sphere around the current character position without
  qualifying its lead against worst-case state latency, speed and turns.
- We treated a stop-walking safety fallback as acceptable traversal. The native
  entrance proof checks grounded frames, distance and peak speed, and records
  hold time, but has no maximum hold or minimum average-progress requirement.
  A route with seconds-long stalls can therefore pass. Its success establishes
  safe waiting; it does not establish successful continuous streaming.

The guard protected against the earlier fall-through, but it did not solve the
integration error that made physics arrive late. The next acceptance gate must
fail ordinary movement holds, alongside retaining native grounded contact.

## The exact stop in our code

[`UCLRadialGravityMovement`](../Source/CatalystLandform/Private/CLRadialGravityMovement.cpp)
checks before its normal movement tick:

1. The character is grounded on the bound Voxel World, movement is requested,
   and that world reports a new state in progress.
2. A radial line trace finds the current walkable floor.
3. A second radial trace looks 1.5–3 m ahead for a walkable floor.
4. If the second trace fails, we increment the wait counter, call
   `StopMovementImmediately`, consume input and return before the normal
   `CharacterMovement` tick.

At 600 cm/s with the current capsule and normal frame durations, the look-ahead
clamps to 150 cm: about 0.25 s of travel. This guard was introduced to prevent the
observed fall-through. Removing it now would remove the stop without making the
missing physics floor active.

The predicate also has limits. It uses a global state-busy flag and a floor trace,
not a classification of local streaming readiness. A real drop, steep floor or
wall can also fail the probe. None of the 20 held snapshots in this run was a
steep physics hit: all were missing hits with a generated floor crossing. That
does not prove the guard is free of false positives elsewhere.

## What native VPP does

The installed source separates data computation from physics publication:

| Stage | Native implementation | Meaning |
| --- | --- | --- |
| Request | `FVoxelCollisionInvokerView::Tick` | Computes the chunk set around enabled invoker spheres and invalidates demand when that set changes. |
| Compute | `FVoxelInvokerCollisionSubsystem::CreateColliderRef` | Samples the final volume through `FVoxelMesher`, creates a mesh, then creates its collider. |
| Computed check | `FVoxelInvokerCollisionSubsystem::HasCollision` | Checks current/previous computed-key maps; it does not perform a physics trace or require a published component. |
| State barrier | `FVoxelState::IsReadyToRender` | Requires the state's task context to complete. |
| Publish | `FVoxelRuntime::Tick` → `FVoxelState::Render` | Publishes the completed state and invokes subsystem rendering, including collision component setup. |
| Active collision | `FVoxelInvokerCollisionSubsystem::Render` / `SetupComponent` | Creates/reuses native collision components and installs their colliders into physics. Old supported components are retained through the transition. |

The `Render` name includes collision publication, not just graphics work. A
ready local collider does not cause an early partial publication of the state.
The runtime also serializes state transitions: it finishes and publishes the
pending state before constructing the next one. Invoker demand may change while
that work is in progress.

The character already has native `bWaitForVoxelWorld = true`. In
`FVoxelCollisionInvokerManager::CheckVoxelWorlds`, that fallback calls
`ComputeInline` only when `HasCollision` is false at the invoker position.
The component documentation explicitly says this check ignores its radius and
may cause hitches. `ComputeInline` itself skips keys already present in the
computed maps. It is therefore not a “publish this computed floor now” API.
The captured ahead-of-player publication gap is not repaired by merely enabling
that already-enabled flag.

VPP's [collision documentation](https://docs.voxelplugin.com/2.0p8%20-%20v2/design-docs/collision-mesh-navmesh)
describes invoker-driven collision and separate camera-driven visibility
collision. Our Lab correctly disables visibility collision while using the
moving collision invoker, avoiding duplicate overlapping colliders.

## Why we exhaust the lead

The character's invoker radius is 25 m. At 6 m/s, that provides a nominal
4.17 s forward reach from its sampled centre, before allowing for chunk
boundaries, asynchronous demand updates, state publication and safety margin.
Collision chunks are 32 voxels at 20 cm spacing, or 6.4 m on a side. Collision
uses LOD0 independently of distant visual mesh LOD.

The usable lead is less than simply dividing radius by speed: a pending state
can be based on an earlier invoker position, and its computed floor is unusable
until publication. The captured player repeatedly reaches the old active
coverage while the next state is still running. A radius is a spatial request,
not a guarantee of a time deadline.

Successive state publications observed in this capture were 5.92, 5.70, 6.71 and
5.59 seconds apart. Those intervals exceed the nominal 4.17 s forward reach.
They include both moving and held time; they are not cooker timings. This is
why the character can catch the active collision frontier despite a 25 m radius.

The earlier retained worker sample found our stamp-distance path in 97.32% of
sampled worker stacks, terrain height in 51.19%, caves in 41.27%, and Chaos cooking
in 0.03%. These are overlapping inclusive samples from the earlier control
build, not exclusive percentages or current optimized wall times. They locate
the expensive work inside collider generation: evaluating our procedural field,
rather than only building Chaos's acceleration structure.
[Retained sampling summary](../conformance/VPPDataDelivery/walking-sample-summary.json)

The final optimized prototype improved grid query time by roughly 8–11%, while
its two-trial median movement hold remained 26.77 s over 100 m. It still computes
terrain height eagerly and exposes broad planet bounds. The witness explains
why that residual generation cost becomes a visible movement hold: shared-state
publication delays the already-computed local floor.

PCG and navigation are not the gate in this Lab run: `ACLProofPlanet` disables
both navigation and `bWaitForPCG`. Other hosts should preserve an explicit PCG
publication policy when the ecosystem is integrated; the native runtime can
otherwise wait for generating PCG components as well.

## What should change next

The next experiment should target **request-to-active-physics latency** at the
upcoming character footprint, alongside total generation throughput.

1. Reduce work that holds a state open. Implement the bounded field plan and
   conservative distance-query filtering identified in the delivery report.
   Deep cave collision should avoid unrelated expensive surface work where the
   query contract permits it. Shared bulk and attribute work remains relevant.
2. Request upcoming collision earlier through supported native invokers, using
   a measured speed/turn/latency envelope and retaining support at the player.
   Test prediction and the current 25 m policy before increasing volume. A
   larger spherical radius increases requested volume approximately with its
   cube, so more radius can worsen generation latency.
3. Evaluate the existing native render/state work controls as a separate test.
   `voxel.render.MaxChunksToProcessPerPass` exists, but is not a hard deadline
   for all work and is not a collision-priority switch. No setting here has yet
   been shown to fix the pause at unchanged quality.
4. Refine our guard to distinguish absent published collision from ordinary
   terrain discontinuities. Retain a safety fallback until native physics
   coverage is proven; the desired normal path has no movement hold.

Do not manually invoke a partial collision `Render`, install substitute floors,
or remove the guard and call the resulting fall-through a performance gain.
The implementation work belongs in our field plan, request policy, readiness
logic and acceptance tests. Preserve the installed VPP publication contract; no
vendor patch or external issue submission is part of this research.

Acceptance should keep the frozen cave family, geology, native edits, UI and
collision resolution; retain 100% grounded traversal; record computed versus
active collision at the footprint; and demonstrate no ordinary movement holds
under the agreed maximum movement speed. Then test turns, reversals, edits and
teleports separately, plus the ecosystem workload.

VPP recommends [Unreal Insights](https://docs.voxelplugin.com/2.0p8%20-%20v2/technical-information/profiling-and-debugging/generation-performance)
for tracing generation tasks and distinguishing worker work from game/render
thread hitches. The existing callback benchmark, movement guard counter and GPU
fixture measure different parts of this system and should remain separate.
