# Generated structural cave volumes

The cave layer generates connected underground negative spaces in the planet's
native VPP field. Its floors, walls and ceilings are terrain surfaces with the
same canonical rock strata as the exterior. Native VPP sculpting can enlarge,
connect, mine or fill those spaces. The cyan debug view displays those same void
boundaries through a dimmed planet, with no camera change or independent geometry.

## Correction and acceptance

The previous three straight capsule segments, optional branch and uniform oval
section were a field/presentation prototype. They failed the intended cave
morphology. Earlier passing qualifications establish their recorded plumbing,
minimum counts and editor behavior; they do not establish natural cave realism.
This change intentionally replaces that unreleased generated geometry. It retains
the recipe fields/schema and does not rewrite existing saved assets.

A complete structural network now has shared junctions, curved passages,
spatially correlated widths/heights, irregular enlarged junction spaces and side
passages. The knob develops physical extent, section sizes, topology, loops and
lower levels with steep connectors. Seeds vary joint orientation, route bends,
section changes and chamber proportions. Native VPP noise adds restrained wall
relief in physical coordinates; it is not the source of network topology.

The retained research separates geological constraints, network construction,
passage volumes and wall details:

- [Paris et al., 2021](https://diglib.eg.org/items/a4e7985c-af52-4b55-bd1a-eeeff6c47422): geological network skeletons and implicit conduit construction.
- [Franke and Müller, 2022](https://doi.org/10.1016/j.cag.2021.10.002): efficient phenomenological cave networks, passage forms, surfaces and suitable speleothems.
- [KarstNSim](https://github.com/ring-team/KarstNSim_Public): geological/hydrological network controls and separately generated passage sections.

Those works inform the separation of responsibilities. This structural profile
is not an implementation of their karst algorithms, a geomechanical fracture
solver or a calibrated reconstruction of cave history. It assumes explicitly
fracture-capable hosts and approximates openings along primary/conjugate joints
oriented by the existing extension/shear evidence. Enlarged joint intersections
are an authored structural opening prior. Soluble-rock dissolution, water-table
levels, lava-flow history and speleothem growth remain separate family work.
They must not be implied by this profile or by a broad province label.

## Native feature reconciliation

VPP 2.0p8 supplies the spline/shape/blend/noise/index operators and remains the
sole voxel generator, mesher, LOD, collision, sculpt and save platform. Audited:
`FVoxelVolumeSplineStampRuntime`, `UVoxelSplineStampFunctionLibrary`,
`FVoxelSplineSegment`, `FSplineCurves`, `FVoxelSphereShape`, native volume blends,
`FVoxelAABBTree`, PerlinNoise3D and `Get Voxel Flat Normal`.

`FCLCompiledCaveVolume` lowers immutable geological construction data into those
native operators inside the existing canonical scene provider. It calls the same
exported nearest-cubic-spline kernel as VPP's GetClosestSplineKey3D node. UE owns
curve interpolation and interval bounds. VPP's sphere kernel evaluates normalized
elliptical sections and ellipsoidal chamber lobes; native smooth union joins them,
and native subtraction cuts the planet. VPP Perlin supplies wall relief. No
spline-distance algorithm, noise implementation, mesher, sculpt persistence or
streaming manager is copied or replaced.

Catalyst's specializations are the geological graph, body-frame section placement,
physical dimensions, host/province selection, deterministic feature identity,
roof checks and the final scene ordering. A stable section frame handles steep
shafts without a radial/tangent singularity. The existing worker retains plain
immutable data; it never creates spline actors/components or reads mutable graph
assets while sampling. The native kernel ABI is pinned to the qualified VPP build.
A future ABI/platform change requires requalification.

The old 64-operation mask belongs to the authored-edit qualification profile. It
cannot accommodate meaningful natural networks. Generated caves now use VPP's
native AABB tree directly with a separate bounded natural-feature collection:
4,096 passages and 2,048 chamber lobes, in addition to the existing 16,384 placement
candidate limit. Overflow refuses the complete build. Authored edits retain their
existing order and limits. These are bridge budgets, not VPP feature limits.

## Generation and editing contract

Only selected rift/shear Landforms with positive local evidence and explicitly
fracture-capable, known non-ice rock hosts qualify. The current conservative host
rule requires the entire three-unit palette to be compatible. Sampled footprint,
roof and cover checks reject a whole candidate before publication; these checks
are not a continuous mathematical roof/host certificate.

Enabled caves request at least one complete eligible network at every knob
position. The eye is the way to request zero. Physical density adds networks;
failed density draws use the existing deterministic eligible-candidate minimum.
Missing prerequisites or an impossible complete fit produce an explicit error.
The minimum never fabricates eligible geology or an independent debug-only cave.

One immutable result defines the actual solid/air field, geological queries and
diagnostic boundaries. Authored earthworks and native sculpting follow natural
cave generation. Backfill can deliberately fill a generated cave; the minimum
does not undo player edits. Rock identity remains a canonical XYZ query.

The UI remains the standard eye, indicator, one knob, reseed and directly expanded
advanced properties. Cave debug dims the current planet and displays every network
at its real physical scale. Ordinary navigation, camera position and native
owners remain unchanged. Existing native bounded diagnostic domains are retained;
all networks are active together and none are camera-streamed. Debug uses VPP's
native voxel-normal material node to reveal surface form.

## Verification

Acceptance requires connected open volumes, measured section/chamber variation,
multiple graph layouts, native surface geometry and visual review. Native editor
checks also cover exact saved 500 m and reference 2 km planets, all-cave/far-side
visibility, no camera changes, normal navigation cost, native field parity,
canonical geology, backfill, deterministic recipe reload and background equality.
Test-only detail captures happen after the navigation benchmark; they do not add
product navigation behavior.

`ACLGeneratedCaveProofGameMode` is an isolated Lab qualification host. It uses the
actual generated 500 m recipe, ordinary `ACLPlanet`, native VPP generation/LOD and
moving collision invoker, then puts the normal character inside a generated
chamber. It checks floor/ceiling/wall contacts, grounded character frames,
underground native sphere mining, solid placement and compressed native sculpt
saving. A fresh process loads the recipe and sculpt save and compares native
fields, surfaces, rock properties and stratum identities at retained probes.

Exact results, visual evidence and remaining limits are recorded in the adjacent
qualification record. Passing these bounded examples does not qualify every
advanced dimension, whole-planet scale, geological family or cooked target.


[Qualification record](../conformance/CaveVolumes/qualification-mac-arm64.json):
60 distinct native/editor checks pass across the full regression and focused
rendered-test correction. The latter uses a matched hidden-cave image control to
check shaded, subpixel far-side coverage rather than assuming the old bright
cyan material. Recorded cave navigation p95 is 19.34 / 18.64 / 20.88 ms for the 2 km,
500 m at 1 and saved 500 m at 100 cases, with no input starts or mesh copies during
navigation. Normal globe/property-scroll regressions pass as well.

The standalone native writer retained four floor/ceiling/wall contacts and 1776
grounded character frames. A native wall probe changed from -95.34 cm solid to
137.44 cm air after mining, then -74.47 cm solid after placement. A control outside
the brush remained unchanged. The compressed native save is 174493 bytes. A fresh
process reproduced all 34 retained field/surface/property/geology probes with
zero measured distance error. This establishes the recorded editor/standalone
case; new cooked and route-by-route traversal qualifications remain open.

The saved recipe keeps its authored advanced dimensions. Moving the knob applies
the revised profile. At globe scale some metre-sized passages are subpixel;
ordinary zoom exposes their actual surfaces. No diagnostic size inflation is used.

![Native surfaces of a complete developed network](../conformance/CaveVolumes/500m-100/network-detail.png)

![Inside the generated cave, using native terrain materials](../conformance/CaveVolumes/interior.png)
