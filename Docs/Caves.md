# Structural fissure cave layer

Caves supplies the first bounded underground generator: short, branching fissure
networks in selected rift/shear Landforms with explicitly declared fracture-capable
rock. The same immutable result cuts the native VPP scene and supplies its debug
view. Karst, lava tubes, large caverns and ice cavities remain future families.

## Review

Open `/Game/LandformDesigner/Caves/DA_FissureCaves`. The portable source is
[Examples/Caves.json](../Examples/Caves.json). Its sandstone, siltstone and shale
succession explicitly declares clastic lithology and fracture potential; it is a
review recipe, not an inferred reconstruction of geological history.

In **Geology → Caves**, use the ordinary eye, 0–100 knob and reseed. The native
chevron exposes advanced properties directly beneath that row. The second icon
switches cave inspection on independently of the eye. No additional tab, preview
checkbox, selector or settings panel is introduced. **Landforms → Include** remains
the sole default choice of available regional Landforms.

Cave inspection dims the existing native preview lighting and shows cyan cave
outlines/axes through the surface; entrance cutters are amber. The back hemisphere
is culled so front-side networks remain readable. Zoom in to inspect physical
apertures and short branches; the highlighted centreline has a fixed screen-pixel
width so a small fissure remains discoverable at globe scale. Turning inspection
off restores normal lighting. These display preferences never alter the recipe,
regenerate terrain, extract a terrain mesh or schedule cave work during navigation.
The debug view is currently a globe diagnostic.

## Native ownership and feature audit

VPP remains the sole voxel generator and presenter, including LOD, collision and
sculpt/save behavior. Its volume spline stamps/graphs, volume layers, sphere/cube
shapes and native volume blending were inspected. Native spline stamps can express
curved passages, but do not supply the geological network, host eligibility or
planetary placement policy. This first straight-segment family lowers into the
existing `FCLCompiledVolumes` path. That path already uses VPP's sphere kernel,
subtraction blend and conservative AABB index. Its previously qualified capsule
coordinate specialization is reused; no spline-distance implementation, voxel
mesher, separate presenter, scheduler or streaming manager is added.

Native UE lighting provides dimming. UE's `DrawWireCapsule` circularizes nonuniform
cross sections and draws some caps at world depth, which does not represent these
elliptical underground cutters faithfully. Native circle/arc and foreground line
primitives instead use the actual cutter transforms and dimensions. They are
read-only diagnostics derived from the same native cutter list.

The structural prior follows the distinction in [NPS open and closed faults](https://home.nps.gov/grba/learn/nature/geologic-faults-and-earth-movement.htm)
and [USGS cave fractures](https://www.usgs.gov/media/images/cave-fractures): structural
orientation can guide an opening, while fault proximity alone does not establish a
cave. Here an explicit host fracture potential supplies an authored opening prior.
The resulting recipe is a procedural geological approximation, not a geomechanical
fracture, collapse, dissolution or groundwater simulation.

## Geological prerequisites

Schema 23 adds Caves and typed rock-host metadata. A Rock unit retains its existing
identity, surfaces, mining resistance and erodibility, and can now declare lithology
and fracture potential independently. Old units remain `Unknown` with zero fracture
potential. No lithology is guessed from hardness, artwork or a unit's name, and no
old recipe is upgraded or repainted on load.

This first profile requires Rock, tectonics and generated regions. The candidate's
selected region must be a Continental rift or Transform/shear Landform, and its
local base-geology evidence must positively satisfy that profile. Node positions
remain within that same qualified profile; missing evidence, unsupported selected
Landforms or an empty available palette do not create random substitute caves.

The host rule is deliberately conservative: all three units in the existing Rock
palette must have known non-ice lithology and positive fracture potential. The
minimum of those potentials scales occurrence. A mixed unknown or protected unit
leaves the body uncut, with a readiness reason on the compiled result. This avoids
claiming selective 3D host clipping before that feature exists. Typed units are a
foundation for formation-aware generation; they are not a complete depositional,
chemical or eruption history. Province evidence versions retain their prior rules.

## Geometry and the knob

A fixed seeded equal-area candidate set is independent of the camera and mesh LOD.
Candidate density is 16/km², using the midpoint of the fitted minimum/maximum surface
radii as an explicit reference sphere. Acceptance uses requested network density ×
reference area / candidate count × host potential, including the small-body minimum
of 16 samples. This preserves physical occurrence scaling despite integer rounding.
It does not claim the exact area of a rough triangulated terrain surface.

The knob develops occurrence, physical passage length, aperture, depth and branching.
Zero development creates no cuts. Advanced values are resolved physical quantities;
changing the knob resolves the profile again. Reseeding changes candidate rotation,
occurrence and shape variation while retaining deterministic body-space identities.
Local extension/shear gradients guide the main axis. Three joined elliptical native
capsules form a fissure; optional branches and surface connectors use the same native
cutters. Radius, cover and depth are in metres, independent of preview scale.

The current proof admits at most 16,384 candidates and the existing 64 native volume
operations. Exceeding a budget refuses the complete result with a diagnostic; it
never silently drops networks, stretches features or coarsens the physical density.
These are qualification limits of this bridge, not limits claimed for VPP. The
finite candidate and node sampling is a placement approximation rather than an
exact continuous province/roof mask. Broad networks, exact contacts, larger-scale
placement and stable host-specific clipping require further qualification.

## Shared scene and persistence

Compilation finishes base terrain, elevation fitting, Rock and optional Watershed
before deriving cave cuts. Authored solid operations follow the natural caves,
allowing later backfill. Explicit backfill material is blended against the already
cut field; a cavity does not acquire a painted replacement geology. Cave walls use
the canonical Rock field, so strata and unit identities remain coherent at exposed
positions. Height-only fallback refuses a cave volume scene.

The existing `FCLRecipeEvaluation` worker owns generation, measured candidate
progress, cancellation, latest-result publication and shutdown. No second queue or
worker is added. Native VPP publication owns visible terrain; debug always reads
that published plan, retaining the preceding pair during pending authoring or LOD.

The recipe stores the parameters and host declarations, not a camera-derived cave
mesh. Reload recreates deterministic native cuts. [The native qualification](../conformance/Caves/qualification-mac-arm64.json)
records actual bulk/sparse field parity, canonical rock/material identity, explicit
backfill, recipe reload, background equality, all eleven knob positions, editor
Undo, cold native-asset reload, dimming/restoration and navigation checks. It does
not establish a new cooked mining/character/contact/save qualification for caves;
the prior native sculpt-save pipeline is reused unchanged. Water reanalysis after
excavation, groundwater, speleothems, support/collapse physics, new cave families
and multiplayer remain outside this proof.

The next geological step is selective host/formation support and one eligible cave
family at a time. Karst needs soluble formations and an explicit water-flow/level
approximation; lava tubes need suitable flow history and geometry. A broad province
label cannot stand in for those inputs.
