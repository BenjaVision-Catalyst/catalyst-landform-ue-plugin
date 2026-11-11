# Procedural carbonate cave family

**Experimental checkpoint: geometry tested; streaming acceptance fails.** Normal
review launchers retain profile 8. No final family headroom, persistence, full-route
or visual acceptance is claimed.

Profile 9 extends the accepted spatial passage/chamber study into seeded,
geologically placed underground networks. The existing UI and native VPP terrain,
materials, LOD, collision, sculpting and persistence remain the supported path.
Profile 8 remains a frozen comparison composition; published profiles 1–7 retain
their saved behavior.

## Shape, topology and floors

Each level has an irregular gallery loop with differently sized, asymmetric
chambers. Branching controls gallery complexity and one, two or three levels.
Adjacent levels have two different ramp passages, providing alternative routes
and a return path. Seeded joint footprints vary passage widths, plan bends,
chamber elongation, orientation and wall detail. Independent roof fields provide
pockets, shoulders, undercuts and retained overhead rock. Mammoth passages and
Carlsbad chambers remain the anatomy/scale references; this is a procedural prior,
not chemical dissolution, collapse or speleothem simulation.

The route graph supplies connectivity and inspection/traversal metadata. It does
not sweep sections, generate a tube, or produce a mesh. Native polygon distance
queries, canonical bed contacts and spatial noise define the three-dimensional
void field. Rooms and galleries on a level share one rolling floor function.
Both ramp alternatives share the same monotonic floor transition between levels.
Flat transition zones keep the descent outside each room, with continuous first
derivatives at the joins. Rock separation between levels and conservative support
bounds keep unrelated floors distinct. Length must admit a walkable grade before
additional levels are added; a short network remains one level.

## Existing controls and preservation

The spatial recipe uses the existing Caves Character, Variation seed and Advanced
rows. Explicit Character edits in profiles 8/9 select profile 9. Existing profiles
1–7 retain their prior Character mapping, including the frozen profile-7 struct
and factory defaults. No new family selector or replacement editor is introduced.

| Existing value | Profile-9 meaning |
| --- | --- |
| Length | Characteristic network footprint; room/gallery and ramp space |
| Radius | Physical gallery width and roof/chamber scales |
| Depth | Minimum upper-level burial, increased where required by roof and cover |
| Branching | Gallery count, loops and up to three levels when ramps fit |
| Density | Complete networks per eligible square kilometre, with existing enabled minimum |
| Variation | Stable site order, joint layout, sizes and spatial noise |

Physical rock beds, unit/stratum identities, lithology, mining/erosion properties
and the original Rock/Scree/Soil appearance bindings are preserved. All cave
surfaces use the canonical rock coordinate; native excavation exposes the same
geology. The artwork names do not reclassify carbonate as loose scree or topsoil.
Saved worlds are not silently migrated to different geometry.

This milestone is underground connectivity. `bSurfaceEntrances=true` explicitly
refuses profile 9 until natural, walkable surface connections are qualified.
Existing entrance-capable profiles remain available. This option is never ignored.

## Placement and execution

Native-area candidate sampling admits only included, eligible carbonate provinces,
known positive-dissolution carbonate Rock and terrestrial watershed context.
Complete footprint/roof support is checked against the real composed terrain.
Bed alignment and finite cover are compiled before a network is accepted.
Nonoverlapping support domains retain independent inspection selection. Requested
and fitted network counts remain explicit; an entire network is accepted or
rejected. The existing 16,384-candidate area budget and a 64-network family budget
refuse oversize work instead of quietly coarsening or truncating it.

Immutable per-network bedding charts retain exact fallback; the spherical surface
inputs are compiled once for the whole family. Native AABB indices select
query-coherent cell candidates. One owned ISPC union evaluates the connected
field without per-cell C++ point maps. Profile 9 uses native float arithmetic
inside its bounded local coordinates; body/bedding coordinates and profile 8
retain double precision. A bounded immutable address table selects
the bedding polynomial leaf; deeper or uncertain cells retain their original
refinement/exact fallback. Positions outside the chart lie outside all compiled
cell supports and use conservative native world-support distances instead of
expensive tectonic fallback. These are recipe coefficients and leaf addresses, not
precomputed voxel geometry. Sparse and worker queries retain the same field
within the declared native floating-point budget. No per-point
locks, whole-cave collision preload, private mesh, substitute floor or new streaming
owner is used. The procedural density is a conservative signed field, not a claim
of globally exact Euclidean distance.

## Qualification and review

`Examples/ProceduralCaves.json` is the reference fixture. The existing native
qualifier accepts `--phase family-geometry` and `--phase family-walk --recipe
<absolute fixture>`. `review` and `reader` retain rendered, native sculpt/save and
cold reload checks. Current launchers leave the native scheduler unchanged.
Historical worker/radius/consumer trials were rejected and are not active
configuration. VPP changes require measured benefit against a stock control.
The existing 20 cm resolution and review invoker settings are project settings,
not a fully stock-default VPP control.
`Tools/create-procedural-cave-review.py` creates a separate
`DA_ProceduralCaves` asset only if missing.

The [qualification record](../conformance/ProceduralCaves/README.md) records the
actual seed/Character/selection tests, native traversal, performance and rendered
views. The high-speed collision integration and final rendered qualification remain
under investigation until the native zero-hold and floor-contact gates pass.
Mac arm64 qualification does not establish other platforms or arbitrary
Advanced settings. Real ecosystem assets and their rendering budget remain
separate work.

## Documentation re-audit

11 November 2026. The current provider is still too costly to claim a freeze fix.
The official [generation-performance guide](https://docs.voxelplugin.com/technical-information/profiling-and-debugging/generation-performance)
recommends Insights with the Voxel channel. Our traces distinguish a smooth-frame
movement admission hold from a game-thread hitch. During the original longest
2.236 s hold, density callbacks took 3.301 aggregate seconds across two background
workers, approximately 74% of their capacity. The game thread spent 1.972 s idle.
The movement guard itself consumed negligible CPU. In the residual trace, density
still consumed about 52% of worker capacity during its longest hold.

The [bounds guide](https://docs.voxelplugin.com/knowledgebase/working-with-stamps/bounds-ranges-and-voids)
requires finite, conservative support and distinguishes Void from known solid.
Our aggregate-network fallback gave near-zero values in unrelated rock. A
certified cave-free audit selected 236,736 extra fine points at LOD 1, 227,648 at
LOD 2 and 406,656 at LOD 3. The retained compiled primitive-bound index removes
all excess at LODs 0–2 and leaves 128 extra points at LOD 3. It stores primitive
box candidate masks, not sampled density, voxel geometry or collision.

The [custom-node guide](https://docs.voxelplugin.com/technical-information/adding-custom-nodes-using-c%2B%2B)
and [buffer guide](https://docs.voxelplugin.com/technical-information/buffers-and-uniforms)
support bulk math and shared inputs. The retained profile-9 path now evaluates
prepared bedding, coordinate conversion and geometry in SIMD. Coherent lanes
share chart coefficients. Uncertain leaves retain exact geology. Native VPP's
own public Perlin implementation is included directly; no vendor code is copied
or edited. Independent scalar comparisons retain wall/floor/roof signs and the
existing precision budgets. Prepared surface classification preserves exact
strata weights, with uncertain contacts evaluated canonically.

The installed VPP source also confirms that computed chunk availability does not
prove active physics publication. Our guard had a separate perching bug: a missing
centre-floor trace could be treated as permission to continue from a cached floor.
The retained guard checks the complete proposed step and preserves admission while
that native floor is missing during pending work. This provides no synthetic floor
and does not change VPP's publication, scheduler, collision cooking or tick code.

The test host previously forced game LOD quality 2, against VPP's default 1.
That override was not justified by a measured benefit and is removed. The existing
20 cm resolution remains for the geometry comparison; VPP's default is 100 cm.
The 32 m moving invoker and 1 m foot anchor are also application settings, not
stock component defaults. These tests must not be called an entirely default
VPP-world control. Visibility collision is disabled when collision invokers are
used, as the [collision guide](https://docs.voxelplugin.com/knowledgebase/blueprints/collision-navmesh-and-invokers)
requires to avoid overlapping colliders.

At default game quality 1 and unchanged 20 cm resolution, the earlier retained
unprofiled 400 m control completes 397.92 m with all 1,386 movement frames grounded.
It still fails: total admission holds are 0.949 s and the maximum stop is 0.522 s.
Streaming density work is 170,086,478 point visits / 20.072 aggregate callback
seconds; process CPU is 56.026 s over 15.694 wall seconds. Compared with the prior
prepared-input control at quality 1, bulk preparation reduces callback time from
22.448 s, but this is a partial improvement, not successful acceptance.

Rejected prototypes are retained as failures. Per-point C++ support-tree traversal
removed false refinement but doubled callback cost and recorded a floor failure.
A local nearest-box index did not improve live work. Forty independent native
cutter stamps multiplied point visits to 2.637 billion and caused 94.4 s of holds.
A public child-bounds prototype used two stamps and native nonoverlapping query
partitions; its geometry and no-duplicate-query checks passed, but base-plus-cave
sampling still rose to 330.675 million point visits, 26.588 callback seconds and
3.388 s of holds. Both stamp experiments and the local-index trial were removed.
Native graph delivery remains available around the canonical composed field;
this investigation does not establish a faster graph backend for profile 9.

The remaining gap is measured delivery/publication latency at the requested 30 m/s,
with no demonstrated ecosystem budget yet. Full traversal, final native sculpt/save/
cold reload, manual visual review and moving ecosystem-headroom checks remain
open. The old profile-8 acceptance is not evidence that profile 9 passes them.
Raw numeric reports, exact commands and filtered trace summaries are retained in
[conformance](../conformance/ProceduralCaves/README.md). Large trace files remain
local with SHA-256 provenance; they are not committed to Git.

The final exact-source repeat (`retained-source-walk.json`) covers 398.80 m with
1,398/1,398 grounded frames, 19.928 aggregate density seconds, 0.897 s total holds
and a 0.427 s maximum stop. It also fails the unchanged streaming gate. All 11
retained-source geometry, determinism, legacy, native delivery and UI regression
tests pass in `retained-automation.json`; this does not convert a failed walk into
a passing family milestone.

## Measured collision gate

A subsequent read-only correlation of the existing `residual-critical-path` trace
identifies the active gate; this is not another performance trial or a code fix.
See `conformance/ProceduralCaves/collision-gate-audit.json` for exact exports,
clipped intervals, call-stack attribution, all eight holds and trace SHA-256.

The longest traced hold, `CL_CaveCollisionHold_7`, spans trace time
63.496558–63.746384 s (249.826 ms) during Voxel State 47. Its 365 collision-generation
calls had all ended by 63.181665 s, 314.893 ms before the hold. There are **zero**
`CreateColliderRef` calls during this hold. Collision generation for that state
was not waiting to start.

State 47 instead remains busy with render generation. Within the hold, 1,301
Landform density callbacks are descendants of render meshing, with 259.496 ms of
aggregate elapsed callback time. Base render meshing accounts for 1,151 callbacks,
1,875,074 point visits and 253.611 ms; relative LODs 1–3 account for only 5.885 ms.
Thus repeated coarse-LOD evaluation is not the dominant measured cost here.

Those callbacks enter `FCLDeliveryBinding::Distances` (`CLDeliveryPrototype.cpp:32`),
which sends each packet to `FCLSpatialCave::SamplePlanet` (`CLSpatialCave.cpp:202`).
That invokes the family preparation and `CLFamilyUnion` (`CLSpatialCaveFamily.cpp:127`,
`:137`), including the cell union and three native Perlin bands, then the bounded
planet-field composition. During the hold the background cave union accounts
for approximately 142.7 ms, preparation 33.2 ms and planet composition 63.7 ms
of aggregate elapsed intervals. These are stages inside the density callbacks;
they must not be added to the inclusive callback total.

The final render density callback ends at 63.665790 s. Native render-data/Nanite
construction continues afterward; the last background render-data task ends at
63.736462 s. State 47's game-thread `Render` starts at 63.753869 s and its first
`SetCollider` occurs at 63.753924 s. State 48 begins background work at 63.757998 s
and its first collider-generation call starts at 63.758164 s.

The source gate is `FVoxelRuntime::Tick`: `!NewState->IsReadyToRender()` prevents
publication and returns before the next `FVoxelState` is constructed.
`FVoxelState::IsReadyToRender` returns `TaskContext->IsComplete()`. Publication
then calls each subsystem's `Render`, including collision-component setup.
This explains how render-generation work delays both normal collision publication
and the next collision batch without a collision-generation deadlock. Our density
provider is measured work in that render batch; the final native render-data tail
is also real and must not be attributed entirely to Landform callbacks.

Across the four holds longer than 100 ms, render meshing invokes 5,076 density
callbacks (653.308 ms aggregate) versus 79 collision-meshing callbacks (9.206 ms).
Two other holds, about 8 ms each, contain collision generation. The capture does
not support describing every held frame as the same phase. Nor does `HasCollision`
prove a specific private collider reference is non-null; the gate finding uses
native task ancestry and publication events rather than that assumption.

VPP source, scheduler, task limits, collision settings and render settings were
not changed for this correlation. No new claim of a passed walkthrough or
available ecosystem headroom follows from identifying the gate.
