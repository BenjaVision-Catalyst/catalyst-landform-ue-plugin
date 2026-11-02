# Projection GPU and chunk-fit qualification

The shared CPU/GPU mapping and conservative chunk-fit contract are implemented.
This checkpoint does not deform rendered terrain, change collision, or qualify a
gameplay performance budget. The Nanite packaged baseline remains separately scoped.

## Shader contract

`CatalystLandformRender` loads at `PostConfigInit` and maps the plugin's `Shaders`
directory before global shader compilation. It depends on Core/Projects/RenderCore/RHI;
it does not pull editor modules or the terrain compiler into early shader startup.
`Shaders/Public/CLProjection.ush` contains the common position, inverse,
Jacobian-vector and inverse-transpose-normal mapping for later material integration.

GPU positions are relative to the planet centre and divided by the physical
reference radius. The caller retains the double-precision origin/radius; absolute
world positions are not demoted to floats. Normal input is scaled before
normalization so finite magnitude does not alter its direction. CPU near-anchor
bases use cross products, avoiding cancellation when subtracting nearly parallel
unit vectors. A far-origin regression covers that correction.

The FP32 shader admits an explicit `2e-6` normalized angular/altitude margin for
input quantization. The parity oracle uses the same admission band with the exact
double-precision CPU mapping. Invalid cases lie clearly beyond that band. This
does not expand renderer chunk admission: its whole source ball must lie inside
the strict CPU chart. Future bounds must include a separate FP32 error allowance.
Position/inverse error limits are `2e-6` reference radii; vector/normal limits are
`5e-6`. The shader rejects invalid/non-finite inputs and writes zero plus a false
status for rejected outputs. Tiny normals use the CPU's component-wise rejection
threshold. The natural-curvature shader path preserves identity directly.

## Asynchronous diagnostic

`FCLProjectionGPUReadback` admits 1..4096 queries per request. Game-thread callers
own its lifetime. Dispatch, staging-buffer polling, copying and resource retirement
run on the render thread. At most one poll command per request can be queued.
There is no rendering flush or GPU-idle wait. `FCLProjectionGPUVerification` owns
one request at a time, times out after 60 seconds, and retires it on every outcome.
It is an explicit qualification diagnostic, never a gameplay-frame operation.

The same verification path runs in Unreal automation and through the packaged
lab's `-CLProjectionGPUVerify` switch. It covers 18 arbitrary-frame configurations:
six curvatures (0, 1e-12, 1e-6, 0.1, 0.5, 1), three physical radii (10 m, 1 km,
10 km), distant origins, near-anchor and edge cases, invalid inputs and normal
magnitude extremes. Each run checks 2,520 queries. A null RHI fails admission;
it cannot silently count as GPU evidence.

## Conservative chunk fit

`FCLProjectionChunk` accepts a canonical mesh box only when its entire
box fits the angular and altitude domain. It aligns the pivot and radial frame
through a rigid transform and leaves source geometry untouched. It returns an
upper bound for residual WPO, a maximum stretch for detail selection and an
expanded presented box. No renderer components are moved by this helper.

For box half-diagonal `b`, minimum source radius `rhoMin`, and maximum chart angle
`thetaMax`, the Jacobian has polar form `J = Q S`. Meridian/azimuth singular-value
bounds give `delta >= ||S-I||`. The rotation-vector derivative is bounded by
`alpha = (1-c) * (thetaMax/sin(thetaMax)) / rhoMin`. Integrating `J-Q(pivot)`
along any segment in the box gives:

```
residual <= delta * b + 0.5 * alpha * b^2
```

This is a conservative analytic bound, not a sampled estimate. Tests independently
check 4,896 corners/interior positions and Jacobian vectors across radii, anchors
and curvatures. The measured residual and stretch ratios are retained in the test report;
every sample stays within its analytic bound. Radial limits use exact box-to-centre
distances; angular limits use the maximum corner angle. The admitted cone is
convex, so it contains the whole box. This avoids the excessive slack of an
enclosing sphere and also admits thin boxes whose spheres cross the altitude band. Boundary-crossing chunks are refused rather
than admitted by their pivot alone. Natural curvature has zero analytic residual.

The packaged walking proof's opt-in `--chunk-probe` applies this admission/bound
calculation to actual VPP Nanite mesh boxes, using `RadiusMeters + BaseHeightMeters`
as the physical reference surface, not the outer sky radius. It writes counts,
source box sizes, residual bounds and stretch. The probe runs before walking,
outside warmed timing, and does not apply any render deformation.

## Reproduction

Stage/build the host as described in its README, then run real-renderer automation
for `Catalyst.Landform.Presentation`. Do not use `-nullrhi` for GPU parity.

```sh
python3 Landform/Projects/LandformLab/scripts/package-runtime-recipe.py --archive /absolute/path/to/CurvatureProof
python3 Landform/Projects/LandformLab/scripts/verify-packaged-projection-gpu.py --package /absolute/path/to/CurvatureProof
python3 Landform/Projects/LandformLab/scripts/verify-packaged-runtime-recipe.py --package /absolute/path/to/CurvatureProof --renderer nanite --quality high --chunk-probe --output /absolute/path/to/chunk-probe
```

The app retains its macOS sandbox. Reports use its container and are copied under
`Saved/Verification`; no authoring assets are edited by these runs. The committed
[qualification record](CurvatureGPU-mac-arm64.json) preserves exact results and
source/artifact hashes. Raw logs, traces, cooked outputs and vendor files stay local.

## Next integration gate

Add the render-only VPP bridge after reviewing measured chunk admission/bounds.
Retain canonical source transforms and material coordinates, publish current and
previous state, and preserve source identity when VPP retires/reuses components.
The engine's Nanite WPO clamp/culling uses primitive `MaxWPOExtent`; ordinary
dynamic material instances inherit that setting from their parent. A small
version-checked VPP scene-proxy extension is therefore a candidate for per-chunk
budgets. Do not replace local bounds with a planet-wide material displacement.

Residual bounds alone do not fix Nanite detail error under stretch, VPP source
residency, velocity passes, VSM invalidation, Lumen representations or projected
gameplay queries. Those remain explicit gates in the
[implementation plan](CurvaturePresentation.md), along with actual dynamically
changing curvature timings and the still-open 60 fps presentation target.

## Measured checkpoint — 2026-11-02

All seven real-renderer editor tests pass, including projection/chunk/GPU checks,
native VPP registration and Nanite PIE inspection return. The standalone GPU test
passes all 2520 queries across 18 charts.
Maximum normalized position/inverse errors are 3.14594015e-07
and 3.54084401e-07; vector/normal errors are
7.01702644e-07 and 3.14189908e-07.

The 1080p High unprojected control passes native walking with 240 Nanite scene
proxies. GPU p99 is 12.97 ms and wall-clock game-frame p99 is
17.71 ms; 60 fps is not qualified. At the initial anchor,
183 chunks fit the strict 60-degree chart and altitude band.
The largest flat residual bound is 82.44 m, with maximum
residual/source-radius ratio 0.795 and maximum
stretch 1.616. This is geometry admission evidence, not a culling
performance result or proof that the whole chart is visibly covered.

The current large chunks still produce an 82 m worst-case flat residual bound.
Although the box refinement tightens the previous 92 m result, this is not yet a
good default for fine Nanite cluster padding. The bridge prototype must measure
culling cost and compare tighter subdivision or cluster-level projection bounds
before adopting it. Cache source-invariant box data rather than recalculating it
in every gameplay frame. No frame-rate claim follows from this admission test.
