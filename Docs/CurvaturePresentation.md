# Curvature presentation implementation

Landform owns the presentation projection; the compiled planet, native VPP
field, collision, gravity and authoritative gameplay positions remain canonical.
The first target is one local player and one bounded chart on a Nanite planet.
Curvature is continuous from 1 (natural sphere) to 0 (tangent plane), including
both endpoints. Repeated distant scenery is a later, explicitly bounded layer.

## Performance gate

The host's [packaged Nanite baseline](../../Projects/LandformLab/Docs/NaniteBaseline.md)
records UE 5.7.4 / VPP 2.0p8 / M3 Max. Nanite, cooking and native walking pass;
the strict 60 fps wall-clock game-frame p99 gate remains open. At 1080p High, GPU p99
is 12.84 ms and wall-clock game-frame p99 is 17.78 ms. At Epic, GPU p99 is 16.36 ms,
leaving insufficient room for the provisional 0.8 ms GPU projection budget.
These are bounded Development-build results, not a minimum desktop specification.

Measure curvature 1/0.5/0 and continuous changes in the same packaged workload,
at the same explicit quality, resolution and streaming state. Keep an undeformed
control in the same binary. Provisional incremental limits are 0.8 ms GPU and
0.2 ms on each CPU thread; both incremental cost and total frame-time gates must
pass. Record world/mesh generation and collision-cook counters: changing
curvature or moving the anchor must not trigger those operations. Traces,
visualizations and offscreen diagnostics are separate measurements.

## 1. Projection contract

`FCLPlanetProjection` is an immutable, allocation-free CPU value. It validates a
centre, normalized anchor direction, physical reference radius, curvature,
angular cap and altitude band. The reference radius must be chosen from the
physical guide/ground contract; the recipe's outer sky radius is not that value.
All positions and distances use one frame and Unreal centimetres. Recipe data
and saved assets do not change.

For a source point with radial altitude `h`, angular distance `theta` and tangent
direction `d` from anchor unit vector `n`, let `a = curvature * theta` and
`sinc(x) = sin(x)/x`, with its analytic limit at zero. Its presented point is:

```
anchor = centre + referenceRadius * n
tangentDistance = (referenceRadius + curvature * h) * theta * sinc(a)
vertical = h * cos(a)
         - 0.5 * curvature * referenceRadius * theta^2 * sinc(a/2)^2
presented = anchor + tangentDistance * d + vertical * n
```

This avoids a huge `referenceRadius / curvature` and subtraction of near-equal
values. The inverse is analytic and bounded. The analytic Jacobian maps vectors;
its inverse transpose maps normals. The chart is strictly inside a hemisphere,
defaults to 60 degrees and excludes the planet centre. Invalid queries fail and
preserve their output. Flat projection retains physical radial altitude and maps
great-circle distance to the plane; it does not erase terrain relief.

Acceptance: endpoint identities, near-zero curvature, arbitrary anchor frames,
multiple radii/altitudes, round trips, finite-difference tangent and normal
oracles, conservative chart bounds and rejection/failure isolation. Chart-wide
bounds are for admission and verification, never a replacement for tight Nanite
cluster bounds.

## 2. Shared CPU/GPU state and VPP/Nanite bridge

Implement one matching shader contract and verify GPU readback against the CPU
oracle before integrating materials. Publish current and previous projection
state once per view/frame with explicit discontinuity handling. A local-player
anchor keeps the camera neighbourhood fixed in canonical space; anchor movement
must retain previous state for velocities rather than resetting it every frame.

Apply the projection after the VPP source-position/material evaluations, including
inverse-transpose normals and tangent-frame handling. Verify the exact cooked
MegaMaterial paths, Nanite programmable rasterization, depth, velocity and VSM
passes. A WPO screenshot alone does not satisfy this milestone.

Prototype a hybrid bridge first: retain each VPP Nanite chunk's canonical
transform, apply a rigid presentation transform to the render component, then
apply only the bounded nonlinear residual in the shader. VPP already separates
its no-collision Nanite components from collision. This lets Nanite's existing
instance transforms carry most displacement and keeps residual WPO bounds local.
It needs a conservative residual bound, a Jacobian-based detail/error factor,
canonical material coordinates, previous transforms and a bounded component-update
budget. The source chunk/mesh and collision transforms remain unchanged. Measure
this approach before considering more invasive engine cluster-culling hooks;
it is a candidate until culling, seams and timing tests pass.

Maintain the smallest version-checked VPP extension required for projected
per-cluster bounds and conservative screen-space detail selection. Both frustum
and occlusion decisions must cover the projected chart, including source chunks
outside the canonical view that become visible. Shader WPO displacement limits
must not clamp the deformation or make every cluster use planet-wide bounds.
Identify where VPP residency/invokers need the chart footprint instead of only
the canonical camera frustum. Reuse source geometry and Nanite data; do not
regenerate them as curvature changes.

VPP's rigid shadow-cache invalidation needs a projection-aware policy so VSMs
cannot reuse stale deformed geometry. Test previous-frame motion, temporal
reprojection and Lumen representations explicitly. If a representation cannot
follow the deformation, qualify a supported rendering profile before enabling
it for gameplay. Do not silently keep incorrect lighting or collision semantics.

Vendor source remains local/licensed. Patches must check exact version and source
context/hash, preserve originals, fail on unrelated edits and have regression
checks. Keep this bridge separate from the existing native-registration and
Nanite-hook compatibility patches, so an upstream upgrade can retire each fix.

## 3. Gameplay integration

Keep simulation and radial-gravity movement in canonical coordinates. Present
nearby characters, props, effects and attached frames through the same projection
as the terrain. Account for animation and bounds without duplicating authoritative
actors. Camera, picking and interaction use an explicit presentation adapter.
Inverse-transforming a screen ray's endpoints and tracing a single straight
canonical segment is incorrect: map the presented ray to a bounded canonical
curve and refine segments/hits with a stated error tolerance. Validate projected
hit positions and normals, then report canonical hits to gameplay.

Integrate the runtime recipe GameMode first behind an experimental switch, without
editing the designer's unrelated schematic preview. Qualify walking, turning,
jumping, landing, interactions and camera motion while curvature changes and the
anchor crosses chart/rebase boundaries. Canonical field/collision fingerprints
and build counters must remain unchanged.

## 4. Distant vista charts

Use a fixed, explicit chart/instance budget and visibility admission. Repeated
source sections are presentation instances sharing canonical data, with a stable
canonical identity for any interactive hit. Do not naively tile a sphere's tangent
map as a globally seamless periodic plane: chart topology and joins require an
explicit atlas policy. Start with non-interactive vistas beyond the interaction
range and a controlled overlap/fade; exposing repetition is a game setting.
Fading may reduce artifacts but does not replace distance, residency, shadow or
instance budgets. Measure the no-fade repetition case separately.

## 5. Packaged release gate

Repeat fixed-route and worst-transition runs at all endpoints, representative
planet sizes, terrain complexity and camera horizons. Include rapid changes,
stationary and moving anchors, chart handovers, caves/overhangs, grazing views,
near/far props, shadows and interactions. Check visual culling/LOD continuity,
motion vectors and lighting as well as GPU/CPU p99, hitches, memory and rebuild
counters. Agree minimum desktop hardware/resolution before certifying 60 fps.
Keep the feature experimental until both correctness and performance gates pass.

## Projection-core checkpoint — 2026-11-02

The UE 5.7.4 Mac arm64 Editor Development build and all three projection tests
pass (zero test warnings/failures). They cover endpoint/altitude invariants,
1,800 round trips and finite-difference vector/normal oracles, chart-edge bounds,
invalid/non-finite/overflow input and output preservation. The maximum inverse
error was below `1e-9 cm` over the tested 10 m, 1 km and 10 km reference radii.
The [qualification record](CurvatureProjection-mac-arm64.json) identifies exact
source hashes and results. The null-RHI geometry run provides no GPU or gameplay
performance evidence. No existing actor, material, recipe or runtime frame path
uses this projection yet.

Next bounded action: GPU parity harness and a render-only hybrid-chunk prototype,
including conservative residual WPO bounds, canonical material coordinates and
current/previous frame state. Retain the unprojected control and the existing
packaged gameplay/Nanite checks. Measure the 60 fps gap separately from any added
projection cost before enabling the feature for gameplay.

## GPU and chunk-fit checkpoint — 2026-11-02

The shared shader, asynchronous cooked GPU verification and conservative chunk
fit are implemented and tested. See [the GPU/chunk qualification](ProjectionGPU.md)
for exact results, float-boundary semantics, the near-anchor CPU correction and
live VPP chunk admission. These diagnostics do not apply render deformation.

Next gate: implement the render-only VPP scene-proxy/material bridge with local
WPO bounds, conservative detail selection, canonical coordinates and current/
previous state. The large source chunks observed in the live probe require
explicit culling-cost measurement; do not assume a passing analytic bound is a
fast bound. Keep the original cooked Nanite control and the 60 fps gate intact.
