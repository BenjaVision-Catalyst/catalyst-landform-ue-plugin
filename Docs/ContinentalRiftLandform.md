# Continental Rift Landform

Continental Rift is the first province with a physical local surface expression.
The other sixteen definitions remain neutral placeholders. This is a bounded
kinematic model of secondary fault blocks, not a reconstruction of plate history.
Cave morphology remains deferred after failed visual acceptance.

## Geology and native audit

Normal faults accompany extension. Tilted fault blocks and asymmetric grabens
provide a better starting point than generic noise ridges or symmetric trenches.
The [USGS fault classification](https://www.usgs.gov/faqs/what-a-fault-and-what-are-different-types)
and [USGS Basin and Range study](https://www.usgs.gov/publications/basin-and-range-structure-a-system-horsts-and-grabens-produced-deep-seated-extension)
support those structural forms. The [NPS horst/graben account](https://home.nps.gov/articles/horst-and-graben.htm)
also distinguishes raised and lowered blocks. These sources guide an artistic
procedural approximation; they do not validate these defaults as a physical solver.

VPP supplies volume layers, native stamps/graph and spline composition, surface
metadata, meshing, LOD, collision and editable voxel storage. Those facilities do
not assign Catalyst's geological provinces or restore beds through a shared
fault-block displacement. Landform therefore specializes its existing spherical
recipe field and restored Rock coordinates. The result goes through the existing
native planet stamp and VPP volume layer. No alternative mesh presenter, streaming
manager or collision implementation is added.

The existing tectonic model supplies warped finite boundary belts, extension
and continental context. A shared belt query retains the old Rock deformation
calculation and accumulation order. Rift uses that query rather than rebuilding
plate geometry or adding another broad tectonic trench.

## Field and one knob

The source editor has one native **Character** row with a 0–100 readout, reseed
and a standard disclosure arrow. Opening it exposes seed and physical ranges
directly beneath the row. It has no extra Advanced wrapper or noise stack.
Planet Builder retains the Include Landforms checklist; placement chooses only
included sources with positive upstream evidence.

Character smoothly varies the physical fault spacing from 500 to 100 m, nominal
throw from 5 to 80 m, scarp transition width from 15 to 2 m, fault asymmetry and
along-strike segment length. Reseed varies trace phase, block polarity and segment
strength. It does not move plate boundaries or province assignments. The same
metre spacing applies across body sizes: a larger surface contains more blocks.
Small bodies can expose only a fraction of a broad block; they are not given a
separate density algorithm. Throw is capped by 4% of the mean physical surface
reference radius and 40% of the configured surface interval.

A smooth analytic tilted-block profile has a narrow normal-fault scarp and a long
back slope, blended with paired-basin form. Smooth along-strike transitions,
trace warping and existing belt envelopes avoid endless stripes and hard cell
cuts. Physical output is locally zero outside positive continental and extension
evidence, even inside a selected regional cell. It creates neither invented
volcanism nor sediment chemistry, erosion history, lakes or caves.

Terrain and Rock use exactly the same radial displacement in metres. Restoring
a point translated by that displacement recovers the original bed and unit.
Existing whole-planet surface calibration then maps relief into SurfaceMin/Max;
that global mapping can affect final elevations beyond the displaced blocks.
It does not stretch bed thickness or add a second deformation to Rock.

## Version and worker boundary

Terrain source schema 10 carries Rift profile 1. Region algorithm 4 resolves the
unmodified base and canonical Rock evidence before province selection and physical
relief. The same immutable field then reaches downstream surface distribution,
Watershed and caves through their existing ordering. All included native children
must compile in this mode. Preparation resolves native inputs on the game thread;
worker completion replays the same stable input order. Character edits reuse those
inputs and retain the existing latest-only background publisher.

Default Rift version 0 and region modes 1–3 remain frozen. Old saved palette
snapshots remain neutral until explicitly refreshed from the updated source.
Mode 4 currently requires a whole procedural spherical recipe; measured terrain,
local-window/face recipes and a second legacy radial geology source are rejected.

The source preview reuses the native globe and a disposable contextual reference.
The native VPP minimum voxel size follows one quarter of the resolved scarp width,
clamped to its supported 1–50 cm display-space range. Camera-driven native LOD
still controls refinement. This is a finest-level limit, not a promise that a
whole-globe view resolves every small scarp. Read-only mesh inspection invalidates
its cache only for newly accepted inputs, not camera navigation.

## Review

In LandformExamples enable **Show Plugin Content**, open **CatalystLandform
Content → TerrainTypes → Provinces → DA_Province_ContinentalRift** and move Character.
The source preview supplies tectonic/continental context; it is not a new starter
planet. Use the ordinary globe orbit and zoom controls. No automatic focus or
selection mode is introduced.

LandformLab owns the separate **/Game/LandformDesigner/Provinces/DA_ContinentalRiftReview**
qualification recipe. It includes only the Rift source, canonical Rock and the
existing continent/tectonic base. The portable server recipe is
[ContinentalRiftProvince.json](../Examples/ContinentalRiftProvince.json).
[The native authoring tool](../Tools/create-rift-landform.py) updates only the
known pristine neutral Rift source and creates a missing Lab review. It preserves
modified sources and existing reviews. It is not part of ordinary project startup.

The [qualification record](../conformance/ContinentalRift/qualification-mac-arm64.json)
separates field, editor and cooked evidence. Acceptance of the artistic landform
still requires human review. Erosion, sediment fill, connected fault history and
other provincial expressions remain future work.
