# Tectonic base planet preset

The first tectonic preset adds present-day structural relief to the shared planet
field. Planet Builder exposes it in the left **Base planet preset** section.
Enable **Tectonic rocky world**, then adjust **Tectonic activity** from Quiet to
Intense. The globe and cross-section regenerate while the slider is being used.
A drag produces one undoable change;
camera navigation and idle frames do not regenerate terrain. Rapid input is
coalesced to the newest value at up to roughly eight updates per second. Draft
meshes use 64 cells while dragging; release schedules the normal quality mesh.
Completed draft frames can publish while a newer value is pending, preventing
preview starvation during a sustained drag. They never replace a final mesh
after release or an explicitly cancelled build.

The owned example is `/Game/LandformDesigner/TectonicBase/DA_TectonicBase`, with
Quiet and Intense comparison assets alongside it. All three reference the new
`DA_TectonicGuides` catalog and use the existing scanned rock, scree and soil
materials. They do not replace the earlier saved planets. **Walk here** uses the
same accepted recipe through the existing collision-ready inspection host.

## One control and advanced overrides

Activity is a normalized artistic progression with coordinated curves:

| Driven quantity | Quiet to Intense |
| --- | --- |
| Plate partition | One plate to twelve; adjacent partitions crossfade as new plates appear |
| Relative motion | Zero to 1.6, following activity squared |
| Relative crust thickness | 0.008 to 0.04, using a smooth curve |
| Belt support width | 3 to 7 degrees |
| Structural relief weight | 0.12 to 0.92 |

The crust parameter changes the broad continental elevation contrast; it is not
a measured physical thickness or a mantle simulation. Motion is a relative
kinematic parameter, not a calibrated plate speed in centimetres per year.
**Advanced** opens the same recipe's detailed settings. Optional plate-count,
motion and relief overrides remain fixed while the main control changes the
other quantities. Lower and upper Radial Guide names determine the relief scale.

## Construction

Schema 14 adds optional `Tectonics`. Disabled defaults preserve older recipes.
Algorithm 1 derives a fixed candidate sequence from the planet seed, with stable
plate IDs and angular velocity axes. Independent continental domains prevent a
plate from automatically being wholly continental or oceanic. The new model uses
body directions with +Z north and +X at the prime meridian; it does not reinterpret
legacy editorial feature coordinates.

The compiler constructs actual spherical Voronoi boundary arcs, rejecting their
extensions through other plates. Relative tangent motion along each arc supplies convergence and shear. Crust on the two sides selects continental
collision relief or an asymmetric arc/trench profile. Extension produces rifts
or oceanic ridge profiles; shear adds modest lateral relief. Deformation varies within the boundary corridors; compact ribbon
supports meet continuously, including junctions. Broad crust relief and boundary
contributions are smoothly bounded before the planet envelope clamp.

Activity blends adjacent seeded partition fields with a quintic transition, so
adding a plate does not pop the terrain at the threshold. This is a continuous
morph between kinematic constructions, not simulated plate splitting or geological
time evolution. Crust advection, thermal evolution, erosion, sediment and volcanic
history remain later stages in the geological design proposal.

The immutable compiled field is applied once after regional terrain composition.
Regional child fields do not inherit a second tectonic contribution. Native VPP,
the globe and cross-section sample the same result. No engine or vendor code is
changed. The first profile is bounded to twelve plates; large planetary scales
and broader geological realism require further qualification.

## Verification and reproduction

`Catalyst.Landform.Tectonics` checks coordinated mapping and pins, boundary
adjacency, tangent relative motion, plate identity, threshold continuity, bounded
relief, portable JSON, guide renaming, spherical atlas/native parity and rendered
Quiet/Active/Intense comparison frames.

In LandformLab, `scripts/create-tectonic-preset.py` creates only the owned assets
and exports `Content/RuntimeRecipes/Tectonic`. Run
`LandformLab.Inspection.TectonicRoundTrip` for the actual editor walking handoff.
After packaging, `scripts/verify-packaged-runtime-recipe.py --scenario Tectonic
--inspection` tests poles, equator, a patch boundary, plate interiors and tectonic
boundaries on native collision. Raw evidence is retained under `Saved/Verification`;
the published conformance record gives exact counts and platform limits.
