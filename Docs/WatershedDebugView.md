# Watershed debug view

5 November 2026. Editor-only presentation derived from the accepted Watershed
analysis and final native terrain. This extends diagnostics, not water simulation
or terrain generation. VPP remains the only visible terrain producer and owns
its generation, material layers, stamps, LOD and collision.

## Native feature audit and justified adapter

The installed VPP 2.0p8 `VoxelHeightSplineStamp` and `VoxelVolumeSplineStamp` expose
native spline terrain producers. Applying those to display a drainage graph would
change terrain. This task needs a read-only consumer of the existing graph.

UE 5.7 Water's `WaterBodyLakeComponent::GenerateWaterBodyMesh` converts a spline
to an XY polygon and emits vertices at local Z=0; `WaterBodyOceanComponent` checks
`IsFlatSurface()`. Those planar body meshes do not directly provide spherical,
constant-radial-level debug surfaces across this globe. This is not a rejection
of UE Water for a future local gameplay-water integration.

The adapter therefore reuses UE GeometryCore's Dynamic Mesh, native normal
overlays and fan splitting, the existing UE translucent ocean-reference material,
`FInterpCurveVector` spline data and native primitive drawing. It adds spherical
shore clipping and receiver-chain extraction specific to our retained analysis.
UE's `USplineComponent::Draw` was also inspected: it draws editing handles at every
key and twenty substeps per segment. Those additions are unnecessary for these
already-draped linear diagnostics, so the native spline samples are drawn through
PDI without creating thousands of spline actors/components or editing handles.
No vendor source, custom mesher for voxels, streaming manager or additional
terrain/routing solver is introduced.

## Display contract

Use the wireframe indicator icon beside **Geology → Watershed**. It controls all
of the following together, independently of the terrain eye:

- Connected reference ocean: a translucent blue surface at `OceanHeight`, clipped
  to the connected ocean classification and its terrain shore.
- Inland potential lakes: translucent surfaces bounded by their shores. Connected
  wet cells with a common spill elevation form a body. Separate levels remain
  separate. These surfaces follow constant radial elevation on the sphere.
- Valley/river paths: native spline curves joining the final selected receiver
  links, split at junctions and at active-channel classification changes. Blue
  denotes active constructed channels; muted blue denotes other selected routing.
  Paths stop at water shores and do not continue across lake interiors.
- Shore outlines: blue for ocean, cyan for potential lakes. Like the spline lines,
  they use native screen-pixel line widths and are foreground diagnostics on the
  front hemisphere, so native terrain
  discretisation does not bury them. Water surfaces use ordinary depth testing;
  diagnostic lines are not a visibility/occlusion or physical-flow solution.

Water is clipped on the retained analysis triangulation; unique shore crossings
are refined with eight bisection rounds against final native height samples.
Spline links have four draped samples per analysis edge, using native linear
interpolation to avoid cubic overshoot across ridges. UE's native fan split makes
water patches that meet only at saddle vertices individually manifold without
moving their geometry. Tiny numerical level differences below 1e-7 m are treated
as zero. The terrain field and routing remain unchanged.

The independent Preview Ocean reference remains available when debug is off. Its
full sphere is suppressed while Watershed debug is on to avoid duplicate surfaces.
No new authoring controls, seed semantics or persisted water assets are added.

## Background work and publication

Geometry derives inside the existing `FCLRecipeEvaluation` job, with the existing
one-running/one-latest queue, cancellation and module-shutdown join. Its measured
phase appears as **Building watershed view…**. Worker code owns plain immutable
analysis/data and UE geometry; component upload stays on the game thread.

The preview installs geometry only for the plan VPP has actually published.
Pending authoring retains the preceding terrain/debug pair. Camera LOD readiness
no longer hides indicators: their visibility depends on the published source plan,
not `IsUpdating()`. Navigation performs no debug rebuild, recipe evaluation,
terrain mesh inspection copy or new native input. Hidden geometry remains bounded
to the current accepted result/component, not a cache of recipe history.

## Meaning and limits

Lakes mean **potential fill to spill**, not a measured current water stage. An
explicit closed terminal does not provide a current level, and no level is
invented for it. Reference rain/runoff remains discharge bookkeeping. There are
no river water ribbons, flowing-water simulation, water collision/gameplay
volumes, waves, erosion, climate or sediment changes.

The existing finite analysis budget and physical-resolution refusals still apply.
Shore topology and path selection remain limited by that graph; refinement cannot
discover an unsampled island, narrow channel or independent basin. Triangle
interiors approximate a curved surface. Caves/earthworks do not currently trigger
hydrologic reanalysis, so this is natural-surface debug, not edited-world water.
Multiplayer and cooked gameplay-water qualification remain out of scope.

The [native qualification](../conformance/WatershedDebug/qualification-mac-arm64.json)
records actual geometry, rendered ocean/lakes, navigation and replacement checks,
source hashes, preservation and exclusions.
