# Rock Character and excavation

Rock Character is a schema-20 geological material/property layer over the existing
spherical terrain. Algorithm 2 replaces the global striped profile for new Character
gestures. It does not add terrain height. In Planet Designer choose
**Geology → Rock → Character**. Enable explicitly, use Variation for a stable
alternative structure, and reveal Advanced to edit the resolved parameters and
three-unit property palette. The new review is `RockHistory/DA_RockHistory`;
the original `RockCharacter` review remains an algorithm-1 compatibility fixture. A complete knob gesture has one Undo. Existing schemas and
saved planets retain their original geology until explicitly edited.

## Native audit and ownership

The installed VPP 2.0p8 source was inspected before implementation:

| Native feature | Use |
| --- | --- |
| `AVoxelWorld`, native stamps, layer stack, Nanite, LOD and moving collision invokers | Sole terrain generation, presentation and contact platform; same native owners through edits |
| `FVoxelVolumeModifier::GetUsage`, sphere/cube modifiers | Native mining changes distances without painting the remaining geological volume |
| `FVoxelVolumeSculptCache::ComputeChunk` | Native base-distance cache, not a geological store; unpainted materials/properties still come from the underlying stamp |
| `FVoxelPaintVolumeModifier`, metadata overrides and native surface types | Explicit backfill material, resistance, erodibility and placed-material contribution |
| `UVoxelQueryBlueprintLibrary::MultiQueryVoxelLayer` and native futures | Query current edited solid, material and properties, including native readiness |
| `AVoxelVolumeSculptActor::GetSave/LoadFromSave`, `FVoxelVolumeSculptSave` | Native compressed distance/material/metadata serialization; UE SaveGame is only the qualification envelope |
| `FVoxelNode_HeightSplitter` | Audited native graph strength splitter; it supplies no geological history, finite bed identity or canonical planetary recipe. This profile classifies beds once for semantic IDs/properties and emits weights into native VPP blending; it adds no generic graph splitter |
| Native Perlin via the existing `FCLNativeNoiseKernel` | Retained v1 compatibility kernel; v2 uses the existing plate warp and adds no independent noise field |
| Existing recipe volume operations | Authored cave subtraction lowered to native sphere/cube and native blends/index |

VPP already supplies sculpting, material blending, metadata and save/load. It does
not supply Catalyst's recipe-specific stratum identities or the relationship
between planetary guides, tectonic displacement and those units. The previous
Landform `FCLGeologicalUnit` only selected radial material intervals: it had no
bed identity, rock properties, folding/dip or shared excavation query. That is the
demonstrated semantic gap addressed here. No streamer, mesher, collision manager,
sculpt cache, custom edit format or replacement material renderer was added.

`FCLCompiledRock` compiles a finite deposited stack with seeded individual bed
thicknesses and stable bed identities above competent basement. There is no
modulo cycle in algorithm 2. Initially, each bed follows a body-radius interval:
locally horizontal sedimentary bedding on the spherical body.

The existing compiled tectonics owns the warped, finite plate segments and their
relative motion. `SampleRockDeformation` uses that same network and envelope to
provide local compression folds, tilt and signed normal/reverse throw. Rock
restores a pre-deformation radial coordinate; it does not move UE actors or add
a terrain-height contribution. With no plate drive, the beds stay radial.
Folds vanish in extension and all added dip/fold/throw vanish outside their belts.
No independent global orientation, fault plane or Perlin fold field is used by v2.

History has one explicit order: older deposition → local plate deformation →
radial truncation → undeformed younger deposition. `YoungerCoverRadiusFraction`
sets the thickness above that truncation, measured down from SurfaceMax. It is
an authored angular unconformity, not simulated erosion or transported sediment.
The existing terrain solid exposes or removes geology; mining never uses its
edited surface height to reclassify the remaining volume.

The finite older stack spans SurfaceMin minus the configured crust depth through
SurfaceMax. Its terminal unit continues where restoration exceeds that original
upper datum; this is a bounded infill approximation, not a basin fill solver.
At most 1024 beds are accepted; excessively thin Advanced compositions fail closed.
No automatic coarsening changes the authored thickness.

This first history profile does not simulate drift, mechanics/rheology, overturned
folds, strike-slip restoration, region-specific facies, intrusions, metamorphism,
core/mantle, ore deposition or erosion/deposition transport. The fold shape is a
bounded kinematic approximation, not a calibrated implementation of the cited
papers. See [research and acceptance](RockStratigraphy.md).

Algorithm 1 is retained only for serialized recipe/save compatibility. Its global
dip, global noise and repeating three-unit cycle caused the rejected pole-to-pole
striping. The earlier numerical and save/load passes established consistency, not
geological realism. A Character gesture explicitly adopts v2; simply loading,
previewing or excavating an old recipe retains its accepted version. Arbitrary
migration of excavated worlds between versions remains unqualified.

## Character progression

The knob uses independent smooth tracks, stored as resolved recipe values:

| Range | Structure |
| --- | --- |
| 0 | Homogeneous competent bedrock; no added stratification, dip, folds, fault throw or tectonic displacement response |
| 0–25% | Broad finite beds and sparse weak interbeds develop |
| 25–55% | Interbeds and local plate-driven deformation enter |
| 55–80% | Thinner beds, local folds/throw and younger cover enter |
| 80–100% | Established structure becomes finer and more folded; three material units remain available |

Zero removes added rock structure while retaining bedrock and its mining properties.
Disabling the layer restores the prior surface-stack path. The enabled profile
owns exposed-rock material classification; weathering and topsoil cover are
separate future layers. The knob never changes
terrain heights, including exact SurfaceMin/Max fitting. Advanced values can be
edited directly; the next gesture reapplies the profile's structural tracks while
preserving palette IDs/properties and Variation seed.
Orientation is a legacy-only control; v2 structure follows the plate network. Numerical sweeps and native
rendered frames qualify a bounded set of seeds and positions, not every possible
Advanced composition.

Unit GUIDs identify the three prototypes. Stratum GUIDs additionally identify an
individual bed in the sequence and are deterministic for the accepted immutable
recipe, independent of caves and edits. Changing the geological recipe changes
the world definition; migrating existing excavations to a different recipe is
not qualified. Structural boundaries are distinct beds even when adjacent artwork
looks similar. Generic Rock/Scree/Soil client surfaces are provisional artwork
bindings; a weak interbed is not scientifically classified as topsoil.

## Native properties and querying

`MD_RockProperties` is a native VPP LinearColor metadata asset with full-float
channels. R is normalized mining resistance, G normalized erodibility, B is the
valid rock-property contribution, and A is placed-fill contribution. Native smooth
contacts blend these values. GUIDs are never encoded into interpolated floats.
They are reconstructed from the same accepted canonical body-space recipe.

`UCLRockQueryLibrary::QueryRock` / Blueprint `QueryExposedRock` queries the live VPP
layer first. It refuses unavailable worlds and native state transitions, stale snapshot/runtime/actor
poses, absent metadata and air more than 2 cm outside a contact. Unmapped native surfaces fail closed. The returned
surface is VPP's current dominant client surface; natural unit/stratum IDs come
from the immutable geological model. Painted fill has an explicit placed-material
category once its native contribution is at least half. Contact blends retain
fractional resistance, erodibility and placed contribution.

`FCLScene::SampleGeology` answers what geology occupies a position in the immutable
recipe, including positions that excavation has made air. Gameplay must use the
live query when checking what can actually be mined. Do not use a height/slope
surface rule to choose underground rock. Backfill must explicitly paint its
material and properties with VPP's native tools; distance-only addition inherits
the underlying geology by native design. The proof demonstrates both operations.

Schema 20 currently accepts whole spherical recipes with an active native surface
library. It rejects simultaneous legacy radial geological units and measured
profiles instead of guessing a migration. Caves use existing recipe volume
subtraction. This profile does not introduce a custom cave generator or soil
weathering/erosion process. Those are separate authoring layers.

## Proof and review

See the [host qualification and launch guide](../../Projects/LandformLab/Docs/RockCharacter.md)
and its machine-readable record. The bounded excavation host loads a portable
recipe, exposes native cave-wall contacts, mines with native sphere sculpting,
places native painted fill and saves VPP's compressed sculpt data. A separate
process reconstructs the recipe, loads the native save and compares fields,
materials, properties, unit/bed identities and actual native contact. Its save
fixture is a qualification envelope, not a production persistence service.

The proof holds one accepted geological recipe while editing; it does not qualify
editing that recipe after mining, arbitrary nonuniform actor scaling, mixed
geological profiles or a planet-wide performance budget. VPP's own versioned save
format remains its compatibility boundary.

## Qualification boundaries found during this work

The independent asset bootstrap reproduced a VPP 2.0p8 editor-lifecycle ensure in
`FVoxelTextureUtilities::RemoveBulkData`: a deferred render→game callback found
its weak texture already collected while the startup VPP world was replaced.
That is an editor world-replacement/readiness boundary, not a missing geological,
sculpt or streaming feature. Asset creation now requires an empty editor map and
refuses to replace a live VPP world. The installed VPP source is unchanged.
The cooked excavation and cold reload require clean logs without this ensure.

On this Mac, `RequestExitWithStatus` reports that the generic implementation
ignores the requested status. The qualifier therefore requires the native JSON
outcome and no created world for the missing-save control, rather than trusting
a nonzero process status. Positive cases still require process exit zero.

Seven legacy rendered tests assume an exact 800×600 target. Offscreen mode added
10 pixels to their native target height; normal-display reruns pass. This is a
qualification-launch constraint; their assertions and production rendering were
not weakened or rewritten.
