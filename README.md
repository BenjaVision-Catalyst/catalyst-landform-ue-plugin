# Catalyst Landform for Unreal Engine

Catalyst Landform presents server-defined planetary terrain through Voxel Plugin.
The runtime provides a spherical volume shape, geodesic placement of flat height
data, and radial character movement alongside explicit Foundation/Connect/VPP
dependencies. The first local recipe compiler and Designer section preview are
implemented; backend delivery and the full Designer remain future work.

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
directory alongside the current Connect plugin and Foundation runtime/editor. Edit this
repository, close the lab editor, stage the plugins, then rebuild the host.

Runtime code uses the CL prefix. Editor and automation modules contain authoring
and compiler checks respectively. Module registration owns no connections or world
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

## Recipe-driven Designer foundation

`UCLPlanetRecipeAsset` extends Foundation's model asset. `FCLPlanetCompiler`
resolves planet-global Strata and ordered noise layers into immutable
worker data, consumed by both the flat reference preview and the spherical VPP
volume. Each Strata is a reusable set of named Stratum: normalized radial
references from centre (0) to outer sky boundary (1), rather than geological
layers. Resizing scales their resolved heights. Native heightmap actors compose
after this base field; recipe output is clipped at the outer radius.
`Examples/RollingPlains.json`
is the first versioned local recipe; it is not a backend wire contract.

The editor module extends Foundation's toolkit with Details, Validation and an
orbitable solid core-to-sky cutaway. Select and drag guide ticks/labels, Delete
to remove, use the small Core + to add a Stratum, and rename labels in place.
Ticks, leaders and labels use light gray at 30% opacity; the selected Stratum
becomes fully opaque, with no selection box or change in tick thickness.
Each tick is separated from the cross-section edge by one tick width.
Guide labels use 9-point type, vertically centred on their connection lines.
The Strata popup selects a project catalog style; the contextual planet's applied
copy is the default. Save persists both the recipe and catalog. Schema 1 recipes
migrate to schema 2 while preserving physical terrain. The tests cover compilation, serialization,
snapshots, live updates and native VPP composition. Algorithm 1 uses UE CPU 3D
Perlin noise; native VPP graph and PCG adapters remain future work. The host's
`Docs/RecipeDesigner.md` documents authoring and physical validation.

Guide dragging updates cached projected ticks and labels immediately; it commits
one recipe/mesh/VPP update on release. The scene preview does not render
continuously while idle. Built-in reference layouts are Temperate, Oceanic,
High relief, Airless rocky and Deep atmosphere. They seed once without replacing
edited presets or reintroducing deleted ones.

The section has its own dark studio lighting: warm front-left key, cool right
fill and high rear rim, with restrained ambient light and fixed exposure.
The lights follow the camera's azimuth as you orbit, keeping their composition
without rebuilding terrain. Only the key casts shadows. The rig is independent
of other asset editors' environment profiles; idle redraw remains disabled.

`UCLTerrainTypeAsset` supplies reusable base noise profiles through Foundation.
The Designer's Terrain selector applies a layer snapshot while retaining the
planet's global Strata, radius and seed. Shipped sources are Rolling plains,
Hills and Mountain ridges (`Examples/TerrainTypes`, with assets under
`Content/TerrainTypes`). Schema 3 adds guide-interval heights/deltas and
planet-relative wavelengths, resolved to scalars before worker/VPP evaluation.
Schema 1/2 imports preserve their old field semantics and upgrade for export.
The terrain evaluator still uses the CPU Perlin reference; native VPP graphs,
regional composition and PCG remain subsequent work.

Terrain assets now open in the shared cross-section Designer with a **Noise
layers** tab. Select, add, duplicate, remove, reorder or enable layers; the
selected layer exposes shape, blend, seed, wavelength, octaves, persistence and
guide-based height bounds. Guide pickers use the preview context's Stratum.
Distance labels are unit-neutral; the suffix in each numeric field identifies
its input unit. Preview radius uses UE's unit-aware numeric interface as well.
The recipe's stored metre values and serialized field names are unchanged.
Finished property edits are transactional; invalid drafts leave the definition
unchanged. Undo/redo refreshes the stack and preview.

The terrain editor owns the reusable definition, with an independent snapshot
of the optional planet context. Preview radius, seed and Strata selection do not
change the planet or the project catalog. Stratum editing remains in the planet
Designer. **Apply to planet** explicitly applies the full layer stack while
preserving that planet's radius, seed and Strata. **Solo** isolates a layer over
the underlying height at the preview centre and is never saved or applied.
**New** and **Save as variant…** create project assets; duplication gives a new
terrain identity. Normal Save and Foundation export operate on the terrain asset.
Shipped plugin presets open as project working copies under
`/Game/LandformDesigner/TerrainTypes`. Each copy is seeded once and subsequently
reused, so authoring changes survive plugin staging and templates remain intact.

Terrain-mode previews build geometry and normals on one background worker with
one replaceable pending request. Obsolete results cannot replace the current
scene. UObject/mesh work remains on the editor thread, and camera framing stays
fixed across completion. The `Landform.TerrainDesigner` command opens Mountain
ridges by default; optional arguments are a terrain asset path and planet recipe
path. This is authoring support for the existing schema/algorithm, not native VPP
graph or PCG generation.
