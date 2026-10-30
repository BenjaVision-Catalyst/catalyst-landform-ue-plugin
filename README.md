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
