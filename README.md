# Catalyst Landform for Unreal Engine

Catalyst Landform presents server-defined planetary terrain through Voxel Plugin.
The runtime provides a spherical volume shape, geodesic placement of flat height
data, and radial character movement alongside explicit Foundation/Connect/VPP
dependencies. The Designer and server recipe compiler remain future work.

`FCLFlatStampGrid` holds ordinary rectangular height samples. `FCLPlanetShape`
maps surface directions into that flat grid using the spherical logarithm map,
and evaluates a closed radial field for VPP's standard shape stamp. Its zero
surface drives both VPP rendering and collision. The radial field is not an exact
Euclidean signed distance field; SDF export needs separate qualification.

`UCLRadialGravityMovement` feeds the planet-centred direction into UE's character
movement and parallel transports the capsule's heading as local up changes.
Characters using VPP collision must also use VPP's character/base handling.

The development host is `Landform/Projects/LandformLab` in catalyst-workspace.
Its staging script copies this repository into the project's ignored Plugins
directory alongside the current Connect plugin and Foundation runtime. Edit this
repository, close the lab editor, stage the plugins, then rebuild the host.

Runtime code uses the CL prefix. Editor and automation modules will be introduced
when they have responsibilities. Module registration owns no connections or world
state; those owners must have explicit GameInstance/world/planet context.

Commercial VPP sources and binaries are supplied through the vendor installer and
are not distributed by this repository.

The 100 m lab proof uses a 65 x 65 flat grid over a 60 x 40 m footprint with an
8 m ridge. Its geodesic field test and physical 360-degree walk, including an
underside jump, passed on UE 5.7.4 / VPP 2.0p8 / Mac arm64. Qualification and
limits are recorded in `conformance/planet-proof-mac-arm64.json`.

## Native VPP heightmap authoring

`ACLPlanet` composes native VPP heightmap stamp runtimes into a radial volume.
Place `ACLSphericalHeightmapStamp` actors, assign their planet and native
`UVoxelHeightmap` assets, then use actor translation, local-Z heading and
nonuniform scale. X/Y scale the footprint; Z scales relief. Actors snap onto
the base sphere and attach to the planet. The planet actor stays at scale 1;
use its radius property to resize it.

The adapter calls the actual `FVoxelHeightmapStampRuntime::Apply` for height
sampling and Max/Min/Override blending, including smoothing, bicubic/bilinear
sampling and native height normalization. A smooth edge window blends each
result into the preceding terrain. Priority and persistent GUIDs determine
order. Placement descriptors and source data are immutable while VPP evaluates
them on workers. Geographic culling skips unrelated source queries, and volume
bounds normalize compressed uint16 extrema into physical heights.

Actor/property edits, source-asset changes, duplication, enable/delete and undo
refresh the planet. Generated editor splines show curved source footprints;
runtime callers invoke `RefreshStamp` after changing actor transforms/properties.
`UCLHeightmapLibrary` configures texture-backed native heightmap sources through
VPP's public C++ API, bridging source settings not exposed to Unreal Python.

The host's `NativeStampProof` map contains an overlapping ridge, plateau and
carved crater. Native/flat equivalence, authoring operations and a physical
360-degree walk through both joins passed on the same Mac configuration.
`conformance/native-heightmaps-mac-arm64.json` records all eight host checks,
measurements and source hashes. Host instructions are in
`Landform/Projects/LandformLab/Docs/NativeStampAuthoring.md`.

This covers native height geometry. Surface-type weightmaps/metadata, graph and
mesh stamp adapters, cooking, networking and multi-planet gravity transfer remain
unqualified. Chart widths compress away from their anchor, and the resulting
radial field remains distinct from an exact Euclidean SDF.
