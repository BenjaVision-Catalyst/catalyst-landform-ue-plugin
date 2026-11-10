# Cave streaming: efficient field delivery

11 November 2026. Mac arm64, UE 5.7.4 / VPP 2.0p8, M3 Max 128 GB.
This replaces the withdrawn [whole-floor residency experiment](ChamberCollisionResidency.md). The opt-in spatial family keeps its authored Mammoth/Carlsbad passage/chamber definitions, canonical geology, native edits and existing UI. Profile 7 remains the UI default; this qualification concerns profile 8.

## What stopped movement

Our radial movement guard deliberately called `StopMovementImmediately`, consumed input and skipped CharacterMovement when the next step lacked published native physics. VPP's computed foot-chunk boolean did not prove that its collider was published. Collision could be computed while the native state still awaited render/field work. The original long stops occurred with ordinary rendered frames, rather than multi-second game-thread frames.

The supplier was the underlying performance problem. The moving-demand 30 m/s reproduction evaluated 365,977,393 density points. It accumulated 955.73 seconds of elapsed evaluator intervals across workers: 644.74 in repeated planetary bedding/deformation, 218.15 in planet evaluation, 61.07 in scalar morphology, and just 6.86 in cave noise. Native coarse rejection was working; our expensive callback ran before it could reject solid cells.

Loading the entire cave before walking masked that delivery failure. All 91 static floor invokers and their demand API are removed. The original 32 m moving sphere and 1 m foot anchor remain. Actual world-component enumeration records exactly two enabled native invokers, both owned by the walker, and zero other demand owners. Native terrain, collider publication and the movement guard retain their owners and behavior.

## Delivery correction

- Compile directional bedding deformation and spherical surface input into immutable adaptive cubic charts for each accepted recipe. Nonconvergent chart cells retain the original evaluator. Surface inputs cover all six cube faces; watershed conditioning remains canonical. These are scalar recipe coefficients, not voxel samples, meshes or collision.
- Evaluate the spatial passage/chamber field in an owned ISPC packet kernel on qualified Mac arm64. Keep the original scalar definition for independent comparisons. Polygon footprints, bed contacts, roof masses and the single floor field retain their equations and seed.
- Reuse exact native watertight triangle intersections within coherent watershed packets. Strict interior hits have unique ownership on the spherical mesh; edges, vertices and misses use the original tree. The independent check returned zero numerical difference and reduced 8,192 coherent samples to 256 tree traversals.
- Pass constant radii as native uniform buffers. Reuse the accepted import preflight plan for material selection and immediate activation, with a recipe-equality check before reuse. Ordinary later editor rebuilds still compile current inputs.
- Remove the eight MiB per-point memo table and its read/write locks. The immutable inputs and packet kernel are the production fast path for this prototype. Concurrent, reordered and repeated packets retain exact parity with each other.

[Spatial delivery](../Source/CatalystLandform/Private/Model/CLSpatialCave.cpp), [directional charts](../Source/CatalystLandform/Private/Model/CLDirectionalScalarField.cpp), [SIMD field](../Source/CatalystLandform/Private/Model/CLSpatialMorph.ispc), [watershed packets](../Source/CatalystLandform/Private/Model/CLWatershed.cpp), [activation](../Source/CatalystLandform/Private/CLPlanet.cpp).

No additional VPP or engine source changes, substitute floor, custom collider, quality reduction or new streaming owner were used.

## Precision and ownership

The equations and geological classifier remain authoritative; compiled evaluation is not bit-identical to the old scalar density values. Independent comparison covered 314,896 cave probes and 227,096 whole-sphere/local/seam/pole height probes. Maximum cave-field difference was 0.047851 mm; maximum surface-height difference was 0.457471 mm, against 200 mm voxels. No cave sign changed outside the 0.5 mm comparison band. Canonical rock positions, strata, physical properties and native edit attribution are unchanged.

Compiler off-knot tolerances are 0.025 mm for bedding and 0.25 mm for surface input; uncertain cells fall back to the original evaluator. The independent acceptance limits are 0.1 mm cave-field error and 1 mm surface-height error. These are measured fixture qualifications, not an analytical error bound for arbitrary recipes. This prototype remains opt-in.

The accepted input tables occupy 23.96 MiB of CPU data. They retire with the immutable recipe snapshot. Native edits compose after the base field and are saved by VPP. The writer/reader processes compare 14 probes with 0 cm maximum reload error; writer and reader PIDs differ.

## Moving-demand results

Every row uses the same recipe, 20 cm voxels, native LOD quality 2, eight configured Unreal workers (six native background workers), existing manual-launch render settings and a 30 m/s speed cap. Final chamber runs reached the requested cap and published new native states throughout the route. No cave geometry was preloaded.

| Run | Travel (m) | Grounded/input frames | Guard holds (s) | Longest stopped input (s) | Maximum publication interval (s) |
| --- | ---: | ---: | ---: | ---: | ---: |
| Baseline | 1,041.60 | 7,958/7,969 | 103.774 | 8.830 | 9.836 |
| C++ A | 1,041.69 | 2,168/2,168 | 0.000 | 0.000 | 0.408 |
| C++ B | 1,041.66 | 2,154/2,154 | 0.000 | 0.000 | 0.576 |
| Graph | 1,041.03 | 2,019/2,019 | 0.000 | 0.010 | 0.769 |

The final C++ A run evaluated 434,892,030 density points and accumulated 96.03 evaluator seconds. Aggregate evaluator/stage times sum elapsed intervals across workers, including overlap and preemption; they are not process CPU time. Grounded movement, actual physics and measured waits are the primary acceptance evidence. Startup readiness was 11.59/11.62 seconds for the final C++ runs versus 19.96 in the failing baseline.

The passage/return route, steep-wall contact/reversal, pristine review, native excavation/backfill, unchanged control and cold reload also passed. Native runtime/invoker identities remained stable.

## Rendering headroom under movement

Each level is a fresh native process traversing the same route with a camera-attached native ISM sphere lattice, forced LOD0, no object collision, ecology, PCG or animation. The camera-relative opaque fixture can occlude terrain. These single-trial measurements describe this stress fixture, not a capacity forecast for ecosystem assets or a VR frame budget.

| Instances | Moving frame p95 / max (ms) | Moving GPU p95 (ms) | Process CPU seconds/wall second | Resident GiB | Guard holds (frames) |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 42.85 / 66.34 | 40.22 | 4.03 | 3.30 | 0 |
| 1,024 | 58.64 / 80.95 | 56.33 | 4.41 | 3.17 | 0 |
| 4,096 | 83.65 / 99.55 | 80.48 | 3.73 | 3.17 | 0 |
| 16,384 | 179.97 / 204.77 | 177.14 | 2.72 | 3.21 | 0 |

All four loads kept native floor contact on every input frame with two moving invokers and zero guard holds. The 16,384-instance run failed the 100 ms rendering gate: its maximum moving frame exceeded 200 ms and the GPU timing closely tracked it. It is retained as an overloaded render result, not a passing overall chamber qualification. The 4,096-instance level was already near that gate. Actual ecosystem materials, placement, collision, LOD, shadow policy and animation require their own budgets.

## Reproduce and evidence

Mac arm64 builds require ISPC on `PATH` or `LANDFORM_ISPC`; this host used ISPC 1.31.0. The content-addressed build script produces an owned kernel archive/header under `Intermediate/SpatialSIMD`, runs through the plugin pre-build step and checks compiler/flags/source hashes. The idempotence check preserved artifact contents and timestamps. Generated binaries are not committed.

```sh
python3 Tools/qualify-spatial-cave.py --project /path/to/LandformLab.uproject \
  --phase chamber --manual-settings --walk-speed 3000 --delivery cpp \
  --render-proxy 0 --output /tmp/cave-cpp
```

Repeat with `--delivery graph` and `--render-proxy 1024`, `4096`, `16384`. The qualifier requires zero holds, every input frame grounded, maximum moving frame under 100 ms, at least 1 km travel, measured 30 m/s peak, exactly two enabled native demand components and repeated native publications. `review`, `reader`, `roam` and `wall` use the existing host flows.

[Retained evidence](../conformance/CaveStreamingDelivery/README.md) includes raw reports, commands, the failed baseline and partial-optimization repeat, precision tests, component/host source hashes and compiler artifact provenance. All 24 affected native tests passed with zero errors. Three logged warnings were the engine's identical Google connectivity-probe timeouts; no vendor change was made for those warnings. The current UI and native VPP ownership tests passed.
