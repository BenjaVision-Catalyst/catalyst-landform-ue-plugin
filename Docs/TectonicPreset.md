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
Obsolete full-resolution globe work cooperatively stops between bounded sampling
batches, allowing a new drag to produce drafts promptly. Drafts in the same drag
can finish while the latest value is queued; switching modes, cancelling or
closing the editor retires that work. Cancelled buffers never reach the renderer.

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
Both algorithms derive a fixed candidate sequence from the planet seed, with stable
plate IDs and angular velocity axes. Independent continental domains prevent a
plate from automatically being wholly continental or oceanic. The new model uses
body directions with +Z north and +X at the prime meridian; it does not reinterpret
legacy editorial feature coordinates.

The compiler constructs spherical Voronoi boundary arcs, rejecting their
extensions through other plates. These are the structural scaffold, rather than
the visible outline of the terrain. Relative tangent motion along each arc
supplies convergence and shear.

New presets use **tectonic algorithm 2**. A shared seeded, two-scale 3D domain
deformation bends the entire network, including junctions. It samples unit body
directions, without longitude seams or a special pole treatment. Nonperiodic
width and strength variation forms unequal massifs and saddles. Broad shoulders
and narrower irregular crests replace the thin, regularly scalloped ribbons of
algorithm 1. Crust is sampled along each boundary, smoothly mixing continental
collision and arc/trench profiles instead of assigning a fixed profile to the
entire edge. Extension produces rifts or oceanic ridges; shear adds modest relief.
Smooth finite support tapers the belts, including their ends. Broad crust relief
and boundary contributions are smoothly bounded before the planet envelope clamp.

Algorithm 1 remains available for pinned portable recipes and saved assets; the
serialized default stays 1 to preserve older assets that omitted that property.
The new-preset factory and enabling the preset in Planet Builder select version
2. LandformLab's `scripts/upgrade-tectonic-belts.py` explicitly upgrades only the
three owned tectonic examples while retaining their settings and guide links.
This changes the geometric treatment, not the plate IDs or activity mapping.

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
The organic-belt regression retains algorithm-1 reference samples, compares
cross-belt relief paths over long boundaries at maximum activity, checks
continuity on all six sphere faces, and renders both algorithms from three
directions. These checks support a geometry regression review; they do not
establish that a noise deformation is a physical plate-history simulation.

In LandformLab, `scripts/create-tectonic-preset.py` creates only the owned assets
and exports `Content/RuntimeRecipes/Tectonic`. Run
`LandformLab.Inspection.TectonicRoundTrip` for the actual editor walking handoff.
After packaging, `scripts/verify-packaged-runtime-recipe.py --scenario Tectonic
--inspection` tests poles, equator, a patch boundary, plate interiors and tectonic
boundary scaffold anchors on native collision. In algorithm 2 these are nominal
locations; the visual deformation belts curve around the scaffold. Raw evidence is retained under `Saved/Verification`;
the published conformance record gives exact counts and platform limits.

The [algorithm-2 Mac qualification](../conformance/tectonic-organic-mac-arm64.json)
records the geometry comparisons, live input/refinement checks and packaged
maximum-activity walking run. The earlier algorithm-1 record remains historical.
