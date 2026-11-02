# Continental foundations

**Continental rocky world** extends the base-planet producer with continental
interiors, shelf transitions, ocean basins and motion-dependent deformation.
Select it in Planet Builder's **Base planet preset** menu. The live activity
control remains visible above the standard inline **Advanced** disclosure.

This is a controlled, artistic kinematic construction. It does not simulate
crust advection, mantle dynamics, geological time, erosion or sediment transport.
The ocean height guide is a datum; this proof renders no water.

## Contract

Recipe schema 14 retains nested tectonic algorithms 1 and 2 unchanged. Algorithm
3 owns this new construction; the serialized default remains 1. The standalone
portable input is [ContinentalBase.json](../Examples/ContinentalBase.json).

Three continental domains are independent of two explicit plate half-spaces.
Seeded three-dimensional domain deformation bends their coastlines and the
structural corridor without a longitude seam. Plate Euler velocities supply
relative convergence and shear. Stable plate and boundary IDs and crust domains
remain fixed while activity changes. This controlled profile has two plates;
the older multi-plate preset remains available separately.

The one control coordinates motion, belt width, structural relief and consequent
thickening. Advanced exposes the setting (collision, subduction, rift or
transform), signed motion direction, foundation orientation, continental extent,
and existing motion/relief overrides. A zero motion direction creates no fresh
boundary deformation. Reversing collision motion produces extension.

Collision has a broad plateau, narrower irregular uplift and flanking basins.
Subduction has asymmetric uplift and a trench. Extension distinguishes
continental rifting from oceanic spreading. Transform motion has modest fault
relief. Broad interiors and ocean floors remain present in the stationary control.

`FCLCompiledTectonics::SampleGeology` publishes continentalness, base elevation,
deformation, a scaled crust-thickness approximation, convergence, shear, setting
and boundary identity from the same immutable compiled field used by terrain.
Thickness is proportional to the resolved guide interval; it is not a measured
physical crust thickness. Output is bounded and proportionally scales with that
interval. The field contributes once to the existing planet composition.

## Inspection boundary

LandformLab's supported Rock/Scree/Soil inspection profile now uses the shared
curvature terrain bridge, camera and inverse-ray interaction path by default.
The globe remains spherical; physics, native collision, gravity and returned
locations remain canonical. Natural/half/flat presentation does not alter the
recipe. See the [host checkpoint](../../Projects/LandformLab/Docs/ContinentalPreset.md)
for fixtures, reproduction and measured limits. General custom-material and
large-relief presentation is not qualified by this profile. The existing strict
60 fps gate remains open.

## Verification

The focused tests are `Catalyst.Landform.Continental` and the Tectonics live-knob
cases. They cover shared-field/native parity, motion reversal and stationary
controls, identity, guide scaling, orientation, spherical continuity, persistence,
rendered comparisons, routed live slider input and undo/redo. The host separately
checks projected walking and canonical return; compile success alone is not that
proof. Exact results are recorded in the host checkpoint.
