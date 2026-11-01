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
resolves planet-global Radial Guide Sets and ordered noise layers into immutable
worker data, consumed by both the flat reference preview and the spherical VPP
volume. Each Radial Guide Sets is a reusable set of named Radial Guide: normalized radial
references from centre (0) to outer sky boundary (1), rather than geological
layers. Resizing scales their resolved heights. Native heightmap actors compose
after this base field; recipe output is clipped at the outer radius.
`Examples/RollingPlains.json`
is the first versioned local recipe; it is not a backend wire contract.

The editor module has separate **Landform Designer** and **Planet Recipe**
entry points, sharing Details, Validation and a core-to-sky cutaway. The former
owns a reusable terrain definition; the latter owns planet settings/assembly.
In Planet Recipe, select and drag guide ticks/labels, Delete
to remove, use the small Core + to add a Radial Guide, and rename labels in place.
Ticks, leaders and labels use light gray at 30% opacity; the selected Radial Guide
becomes fully opaque, with no selection box or change in tick thickness.
Each tick is separated from the cross-section edge by one tick width.
Guide labels use 9-point type, vertically centred on their connection lines.
Planet Builder's Radial Guide Sets popup references a project catalog entry by
asset and stable set ID. Its Save persists the recipe and catalog; Duplicate guide
set creates an independent variant. Exported recipes retain resolved values.
Landform Designer instead has a read-only **Preview Radial Guides** selector and saves
only its terrain definition. Schema 1 recipes
migrate to schema 2 while preserving physical terrain. The tests cover compilation, serialization,
snapshots, live updates and native VPP composition. Algorithm 1 uses UE CPU 3D
Perlin noise and remains reproducible. Algorithm 2 uses the native VPP Perlin
3D compute kernel. Bounded CPU PCG stamp distribution is now available; general graph assets remain future work. The host's
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
Planet Recipe's Terrain selector assembles a layer snapshot while retaining the
planet's global Radial Guide Sets, radius and seed. Landform Designer's selector opens a
terrain definition and keeps the current preview guide snapshot. Shipped sources are Rolling plains,
Hills, Mountain ridges and Plateaus (`Examples/TerrainTypes`, with assets under
`Content/TerrainTypes`). Schema 3 adds guide-interval heights/deltas and
planet-relative wavelengths, resolved to scalars before worker/VPP evaluation.
Schema 1/2 imports preserve their old field semantics and upgrade for export.
Mountain ridges now selects native algorithm 2. Plains and Hills retain the
algorithm 1 reference. General VPP graph execution and regional composition remain subsequent work; stamp distribution is described below.

Terrain assets now open in the shared cross-section Designer with a **Noise
layers** tab. Select, add, duplicate, remove, reorder or enable layers; the
selected layer exposes shape, blend, seed, wavelength, octaves, persistence and
guide-based height bounds. Guide pickers use the preview context's Radial Guide.
Distance labels are unit-neutral; the suffix in each numeric field identifies
its input unit. Global planet inputs are absent from Landform Designer.
The recipe's stored metre values and serialized field names are unchanged.
Finished property edits are transactional; invalid drafts leave the definition
unchanged. Undo/redo refreshes the stack and preview.

The terrain editor owns only the reusable definition. Its entry point accepts
an optional Radial Guide Sets value snapshot, not a planet. Radius, global seed, planet
identity and Apply-to-planet controls are absent. Opening from a planet defaults
only the preview guides; no live planet pointer/binding is retained. The guide
selector changes a transient evaluator and catalog copy without transactions,
dirtying assets or saving guides into the landform. Radial Guide authoring remains
outside this preview. **Solo** is preview-only and never changes the definition.
The evaluator uses a fixed 1 km reference scale and deterministic seed; actual
planet globals and guide values are resolved by recipe assembly/compiler owners.
**New** and **Save as variant…** create project assets; duplication gives a new
terrain identity. Normal Save and Foundation export operate on the terrain asset.
Shipped plugin presets open as project working copies under
`/Game/LandformDesigner/TerrainTypes`. Each copy is seeded once and subsequently
reused, so authoring changes survive plugin staging and templates remain intact.

Terrain-mode previews build geometry and normals on one background worker with
one replaceable pending request. Obsolete results cannot replace the current
scene. UObject/mesh work remains on the editor thread, and camera framing stays
fixed across completion. The `Landform.TerrainDesigner` command opens Mountain
ridges by default; optional arguments are a terrain asset path and a recipe path
supplying only initial preview Radial Guide Sets. `Landform.Designer` / Window → Landform
Designer open the terrain workbench; `Landform.PlanetRecipe` / Planet Recipe open
the separate existing recipe editor. The optional recipe's radius/seed are ignored.
This authoring path supports both generation versions; it does not execute
arbitrary VPP graph assets. Stamp distribution runs its own bounded CPU PCG pipeline.


## Native Mountain ridges generation

Terrain type schema 2 introduced explicit `algorithmVersion`, copied into the
planet snapshot on application. Terrain schema 3 added ordered shaping
operations; recipe schema 4 stored their applied snapshots. Terrain schema 4
and recipe schema 5 introduced stamp distribution. Terrain schema 5 and recipe
schema 6 add explicit local coordinates and guide-relative stamp relief. Terrain
schema 6 and recipe schema 8 add patch-independent spherical sampling. Schema 1
imports retain algorithm 1 while terrain JSON imports export the current schema. Previously saved planet recipes keep their pinned algorithm and
appearance. Native algorithm 2 requires recipe schema 3..8 and the VPP Perlin 3D
kernel; unavailable kernels or changed pin layouts fail compilation without a
silent fallback. This is still a local authoring format, not a backend protocol.

The adapter calls VPP's registered `VoxelComputeNode_PerlinNoise3D` implementation
through its public compute-buffer interface, qualified against **VPP 2.0p8 on
UE 5.7.4 Mac arm64**. It uses the actual plugin kernel, without copying vendor
noise source. Layer blend/fractal semantics remain Landform's versioned compiler
operations. Requalify the adapter and numerical fixtures before upgrading VPP;
this is native kernel generation, not a dynamically assembled `UVoxelGraph`.

Compilation resolves guides, distance scales and deterministic seeds into an
immutable plan and captures the function pointer. Batches of at most 512 samples
reuse scratch arrays. Both the spherical volume stamp and the Designer call this
batch evaluator; workers do not access graph assets or editor objects. The
Designer resolves its surface-distance chart once per build, then unwraps radial
heights into the fixed flat presentation frame. Native heightmap stamps still
compose after the base field on the planet; this base-terrain preview does not
include separately placed stamp actors, materials, erosion or foliage.

`NativeGeneration.KernelAndVersioning` checks the native node oracle, partial
batches, scalar/worker determinism, explicit version selection and JSON
roundtrips. `NativeGeneration.DesignerSphereParity` checks Designer source
vertices at 100/500/1000 m coverage against the actual spherical stamp's radial
field, including globe/pole/seam samples, changed seeds, radius, wavelength,
enable/blend state and outer-sky clipping. The host adds a separate Chaos
collision-triangle check; field equality alone is not collision verification.
See the host's `Docs/NativeMountainGeneration.md` and
`conformance/native-generation-mac-arm64.json` for retained results and limits.


## Plateaus and editable terracing

The terrain layer stack supports Noise, Terrace and Stamp distribution operations. Use **Add layer**, choose an operation, then select its entry to edit controls. All kinds can be
enabled, duplicated, reordered, removed and undone. Noise-specific controls hide
for a Terrace entry. Existing serialized `NoiseLayers` and `FCLNoiseLayer` names
remain for compatibility; the Designer labels the shared stack **Terrain layers**.

A Terrace entry transforms the accumulated preceding field. Step height and
origin use the same contextual guide references and unit-aware distance inputs
as other heights. The preset resolves its step to 12% of the SurfaceMin–SurfaceMax
span and anchors the first level at SurfaceMin, so both scale with planet radius.
Softness (0–1) controls the fraction of each interval occupied by a smoothstep
riser. Zero gives hard steps; one uses the entire interval as a riser. Strength
blends the shaped result with its input; zero is an exact bypass. Noise added
later supplies surface detail without erasing the main steps.

For height `h`, origin `o`, step `s`, define `q=(h-o)/s`, `k=floor(q)` and
`f=q-k`. For positive softness `w`, `a=clamp((f-(1-w))/w,0,1)`; otherwise `a=0`.
The shaped height is `o+s*(k+a*a*(3-2*a))`, blended with `h` by Strength. This
monotone transform also determines compiled height bounds. Resolved steps must
be at least 0.1m. Invalid settings or missing guides preserve the last valid scene.
The shaping operation is Landform-owned and uses the existing native VPP base
noise and spherical volume path; it does not add a new VPP graph asset adapter.

Terrace **Solo** keeps its upstream input and omits downstream operations. Noise
Solo retains its existing isolated-layer behavior. Solo affects neither Save nor
Apply. Guide renaming updates the terrace's origin and step references as well.

Old recipe and terrain JSON import into current schemas while retaining their
noise algorithm and field. Binary load upgrades the in-memory schema without
rewriting the asset file. Old schemas cannot execute Terrace entries. Native
kernel versions are unchanged; terracing is an explicit operation in the new
schema, not a silent alteration of noise generation. The VPP buffer ABI is now
private to an immutable runtime adapter rather than exposed in the recipe header.

`Examples/TerrainTypes/Plateaus.json` defines native broad noise, terracing and
fine detail. The host's `Docs/Plateaus.md` and
`conformance/plateaus-mac-arm64.json` record the authoring, field and actual
VPP/Chaos collision qualification, including its resolution and sampling limits.


## PCG stamp distribution

**Add layer → Stamp distribution** selects a versioned client collection and
exposes seed, density, size range, relief range, native blend mode, smoothness
and edge fade. The bounded CPU PCG pipeline uses a Landform spherical
point/weighted-selection node plus UE Transform Points for heading, footprint
and relief. Compiled placements and native source runtimes are immutable;
PCG and asset resolution do not run on VPP sampling workers. The flat Designer
and spherical volume evaluate this same composed field.

Stamp-only authoring starts on a flat base halfway between the contextual
SurfaceMin and SurfaceMax guides when the recipe still has its default sky-level
base. This also applies to an empty/all-disabled stack. Applying the terrain
stores that resolved base in the planet recipe; explicit non-default bases are
preserved. Existing saved recipes are not silently migrated. Stamp **Solo**
isolates that layer on the guide midpoint, while Terrace Solo retains upstream
terrain. Legacy sphere-coordinate distributions compile on that reference sphere;
LocalPatch distributions compile the metric preview region and its halo.

Distributions blend native VPP heightmap relief relative to their incoming terrain.
Order is stable PCG point order. In legacy sphere coordinates, each footprint's
geodesic chart uses the upstream local surface radius; density uses the upstream
north-pole reference sphere's area. LocalPatch uses one common metric XY field
and deterministic tiled density. Explicit planet-relative sizes use the outer sky
radius in either mode. The
initial limits are 256 placements per layer, 1024 per recipe and 256 MiB of
source data per collection, with explicit errors rather than truncation.

`UCLStampLibraryAsset` extends Foundation's model asset. The configured client
library resolves collection ID/revision into heightmap sources; recipes contain
no source asset paths. Published revisions pin source bytes, sampling options,
weights and relief scales with digests. Keep old revisions/sources when adding
new ones. Project Settings supplies a project library override.

`Content/Stamps` ships six owned linear 16-bit starter sources: Ridge, Massif,
Mesa, Butte, Crater and DoubleCrater, organized into Mountains, Plateaus and
Impacts collections. Reproducible sources and provenance are in
`SourceArt/Stamps/LibraryV1` and `Tools/generate-stamp-library.py`. The host's
`Docs/StampDistribution.md` covers curation, authoring and the matching preview/
planet proof. `conformance/stamp-distribution-mac-arm64.json` records qualification.

Local schemas remain recipe 7 and terrain type 5; spherical-surface sampling
uses recipe 8 and terrain type 6. Old assets retain their
noise algorithm and field. This is a CPU PCG element pipeline, not arbitrary PCG
graph scheduling or GPU execution. Streaming for large planets, backend delivery,
cooking, surface metadata and a finished photoreal stamp collection remain future
work. The radial field remains distinct from an exact Euclidean SDF.

### Mountain ridges with stamps

`MountainRidgesStamped.json` and `DA_MountainRidgesStamped` combine the existing
native Ridge backbone and Ridge roughness layers with a Mountains distribution
between them. The noise settings are unchanged. Ridge/Massif sources add larger
relief with 16–30% radius footprints, smooth native Max merging and a 22% edge
fade; the final noise layer roughens both the backbone and added forms.
The host's `Docs/MountainRidgesStamped.md` describes the matching Designer and
planet proof. Qualification is retained in
`conformance/mountain-stamps-mac-arm64.json`.

### Detailed Motion Forge terrain library

`Content/Stamps/MotionForgeV1` contains four native heightmap sources derived
from Chris J Mitchell's CC0 Motion Forge heightmaps: Grand Mountain, Mountain
Range, Rocky Peaks and Terrain Ridge. Prepared linear 16-bit elevations retain
the source ridge/drainage structures and use feathered borders for blending.
The client catalog publishes Mountains revision 2 and individual source
collections; new editor stamp layers select the newest sealed Mountains revision.
Existing recipes retain their selected revisions.
Stamp terrain previews now sample at 1 m spacing at the default 500 m coverage,
with a 513 × 513 surface-vertex cap; base-noise previews retain the 5 m grid.
Geometry generation remains on the existing bounded background worker; indexed
normal/tangent generation is linear in mesh size and preserves cut-face edges.

`MotionForgeMountains.json` and four individual presets provide editable PCG
distribution over the guide-midpoint input. `SourceArt/Stamps/MotionForgeV1`
records source URLs, archive/member/output hashes, crops, precision and authored
scale conventions; `Tools/prepare-motion-forge-stamps.py` reproduces the sources.
The host's `Docs/MotionForgeStamps.md` documents use and qualification. Source
licence: [Motion Forge Pictures](https://www.motionforgepictures.com/height-maps/).

### Isolated mountain structures

Mountains revision 3 contains five independent forms: Grand mountain spine,
Curved mountain range, Rocky massif, Western ridge and Eastern ridge. They are
extracted from the original Motion Forge elevations along selected connected
contours. Surrounding terrain is removed, the contour floor is subtracted, and
a quintic transition follows the structure's toes. Every stamp has a zero moat;
the image rectangle is no longer its relief footprint. The southern rocky peak
was rejected because its contour intersects the source boundary.

`MountainStructures.json` and five individual presets use these sources.
The default uses native Max without additional smooth-max bias, with the native
rectangular fade contained in zero padding. Positive overlaps can still make
creases; this preparation removes artificial source skirts, not every possible
placement intersection. Existing Mountains revisions 1 and 2 remain unchanged.
New editor stamp layers select revision 3.
`Tools/prepare-mountain-structures.py` reproduces the extraction from the original
archives; `SourceArt/Stamps/MotionForgeStructuresV1/sources.json` records seeds,
floors, contours, toe widths, hashes, core preservation and boundary checks.
See the host's `Docs/MountainStructures.md` for use and qualification.

### Mixed layer previews

Noise/stamp Solo filters the preview definition **before** applying or compiling
it. Hidden stamps cannot consume placement budgets or block a noise preview.
Terrace Solo retains enabled upstream layers and excludes downstream layers.
Solo is available only for enabled layers; disabling the soloed layer clears it.
Noise Solo shows its source values on the flat guide base, rather than comparing
Max/Min against absent terrain. These preview rules do not rewrite saved blends.

An unseeded additive/shaping terrain resolves its base to the SurfaceMin/Max
midpoint, as stamp-only terrain does. Explicit non-default bases and absolute
Override noise are preserved. This is an authoring default: serialized recipes
still compile their stored base and retain their existing field.
Finished noise edits resolve and validate their bounds in the current Radial Guide Sets;
a reversed range is rejected without compiling unrelated stamps or changing the
last accepted definition. Compile failures explicitly mark the displayed terrain
as out of date. The stamp limit remains 256 per layer; budget diagnostics now
include the requested count and a usable density limit.
Qualification is in `conformance/layer-controls-mac-arm64.json`; the host's
`Docs/LayerPreviewControls.md` records the repaired user case and checks.

### Designer ownership boundary

The landform asset stores procedural layers and named guide references. It does
not store a Radial Guide Sets selection or values, planet radius/global seed, or a planet
binding. Guide values are supplied on assembly. Its preview has a transient,
read-only Radial Guide Sets snapshot. Preview Save/export targets only the terrain asset;
preview guide choices do not create undo transactions or dirty the shared catalog.
The editor-session planet-write method has been removed. Explicit assembly
remains on the model/recipe side, used by Planet Recipe and future World Builder.
`conformance/designer-boundary-mac-arm64.json` records qualification; the host's
`Docs/DesignerOwnership.md` explains the current workflow and reference evaluator.

### Local terrain fields

`Examples/TerrainTypes/MountainRidgesLocal.json` combines a metric native VPP
ridged backbone, contour-isolated Mountains revision 3 stamps and fine noise.
Its explicit LocalPatch coordinates evaluate XY directly. Deterministic PCG
cells generate only the viewed region and a conservative contributing-stamp halo;
changing the area reveals the same field rather than reshuffling stamps or
scattering over an implicit preview sphere. Native PCG source selection,
rotation, width and relief transforms keep stable cell/point seeds.

Width/wavelength can use absolute metres or explicit outer-radius fractions.
Stamp relief now also supports named guide-interval deltas, resolved alongside
noise and shaping references. This preset uses 220–400 m footprints, 360 m
backbone and 28 m detail wavelengths, with all vertical ranges supplied through
SurfaceMin/SurfaceMax. Existing sphere profiles and footprint-relative relief
retain their earlier semantics.

The planet owns the local field's anchor/tangent frame, extent and boundary
transition. Its native spherical volume maps directions into the same metric
field, then closes the finite patch into the neutral base sphere. No assembly
settings enter the terrain definition or Designer Save. This is one bounded
patch proof, with the existing placement budgets; whole-planet chart coverage,
streaming remains future work. The following proofs add regional composition
and full spherical coverage through separate versioned presets.
`conformance/local-terrain-mac-arm64.json` records actual qualification. Host
workflow and detailed contracts are in `Docs/LocalMountainField.md`.

### Planet-owned terrain composition

Recipe schema 7 adds independent applied terrain regions inside the shared local
assembly patch. `Examples/MountainPlateauComposition.json` uses Mountain ridges
as its base and the new `PlateausLocal` preset in a band with two 200 m
transitions. The planet owns mask shape/location/heading/width, priority and seed
offset. Both fields resolve its global guides and evaluate their own complete
stacks before their heights blend; terracing never processes the other terrain.
Band, disc and half-plane masks use quintic transitions with zero first and
second endpoint derivatives. Priority and persistent ID define canonical order.
The native spherical volume and flat Planet Recipe preview share this compositor.

Planet Recipe has source selection and add/remove controls for regions; masks
are edited in Details. Single-terrain Designer ownership is unchanged. Numeric
mask drags broadcast one rebuild on commit, and composed cutaways use the
existing coalesced background mesh build. Applied snapshots contain no asset
paths and remain independent of later source edits until explicitly refreshed.

The bounded composition supports up to eight regions and 1024 total stamps,
with the existing per-field/source/query limits. It remains one local chart;
a planetary atlas, arbitrary surface masks, metadata/material blending and
streaming are separate work. The host's `Docs/TerrainComposition.md` describes
the playable two-join proof. Qualification is recorded in
`conformance/terrain-composition-mac-arm64.json`.

### Continuous spherical patches

`Examples/SphericalPatchComposition.json` extends new Mountain ridges and Plateaus
presets across six independently compiled cube-sphere patches. Terrain schema 6
and recipe schema 8 declare `SphericalSurface` coordinates. Native VPP Perlin
samples planetary XYZ at the neutral reference surface, with explicit metric
wavelengths. Great-circle band, disc and half-plane masks blend complete fields
without patch-local origins. Existing local and legacy sphere presets retain
their earlier coordinate policies.

Each patch receives a subset of one bounded global native CPU PCG registry.
Selection, transforms, relief and persistent IDs are assigned before filtering.
Conservative source supports and a sampling guard retain neighbour-anchored
heightmaps that reach across boundaries. Dominant-axis ownership routes samples
to a patch; charts do not enter procedural seeds. All plans feed one continuous
native VPP volume and its cooked collision, using VPP's mesh/LOD stitching.

Qualification compares all twelve edges and eight three-way corners against a
whole-field reference, including heights, normals and stamp identities. The
host's `Docs/SphericalPatches.md` describes the playable plateau/blend/mountain
boundary loop and measured collision checks. Results are recorded in
`conformance/spherical-patches-mac-arm64.json`.

This is a fixed six-patch layout, with 256 global stamp candidates per layer
and 1024 unique placements per recipe. A streamed registry for large planets,
adaptive paging, material/biome channels, navigation and backend delivery remain
separate work. This radial field is not an exact Euclidean SDF.

### RadialGuide naming and compatibility

Recipe schema 9 names the planet snapshot `RadialGuides`, containing an
`FCLRadialGuideSet` of `FCLRadialGuide` values. JSON uses `radialGuides`/`guides`.
Imports of schemas 1–8 migrate the former field names explicitly, rejecting
ambiguous aliases. Unreal Core Redirects retain existing binary assets and the
old project catalog storage path; values and IDs stay unchanged. New UI labels,
classes, source files and authoring APIs use RadialGuide terminology. The host's
`Docs/RadialGuides.md` records ownership and the preservation checks.

### Editorial base planet and Builder globe

`Examples/BasePlanetStructure.json` is the first manually placed broad-layout
profile. Recipe schema 10 adds stable uplift/basin instances with longitude,
latitude, heading, angular extents and signed guide-relative relief. Smooth
compact geodesic ovals add broad offsets once beneath the complete regional
terrain. Region children do not reapply them, and regional PCG identities and
reference-sphere dimensions remain unchanged. This is the first structure primitive profile. Schema 11 below adds
reusable Landform placement and structural region bindings.

Planet Builder opens a worker-built globe with bounded orbit/zoom, editable name,
new neutral planets, Add uplift / Add basin and Details-based placement controls.
The globe and cutaway share editable guide ticks/labels, with concentric radial
section circles on the globe. Navigation includes bounded pan when cropped;
the outer sky sphere defines stable framing. The Designer constructs
neither planet controls nor a globe and still saves only its reusable source.
Its initial context receives only a guide-value snapshot. Tests guard this
working boundary as assembly develops.

The same immutable field supplies globe vertices, cutaways and the native VPP
volume. The globe now uses 512-cell faces (3,145,728 triangles), with field sampling
batched on a worker; native walking detail remains VPP-owned. Host workflow and limitations are in `Docs/PlanetBuilder.md`;
qualification is in `conformance/planet-structure-mac-arm64.json`.

### Referenced Radial Guide Sets

Planet assets retain `RadialGuideLibrary` plus `RadialGuideSetId`; loaded references
resolve on load and on committed catalog edits. Recipes sent to generation or
JSON export contain the resolved guide values and persistent ID, without Unreal
asset paths. Imported/legacy recipes remain self-contained until explicitly
linked or edited. Conflicting legacy snapshots fork a new entry. Guide renames
carry planet bindings, and missing sets/inputs are diagnosed rather than silently
replaced. New planets reference a set; duplicated sets have independent IDs.

Six starter radial layouts cover the general Temperate layout, Oceanic, High
relief, Airless rocky, Deep atmosphere and Thin atmosphere. Catalog upgrades
preserve existing edits/deletions. They define radial proportions only.

Guide dragging updates overlays immediately and commits terrain once on release.
Shared sets notify only referencing planets whose resolved values changed, plus
the authoring planet. The Designer still receives only a read-only guide snapshot.
See the host's `Docs/RadialGuides.md` for navigation, controls and qualification.

### Authored Landform assembly and canyon

`Examples/PlanetAssembly.json` uses schema 11 to place Mountain ridges and
Plateaus as source-local spherical instances connected to the same broad uplift.
The planet owns location, heading, coverage, transition and relief strength;
source definitions remain reusable. Bindings use stable structure IDs. Changing
the parent frame carries the regions and their deterministic regional PCG
placements. Broad relief is added once after regional blending. Earlier mask
profiles retain their existing semantics.

Planet Builder adds Landform source selection, named structure connections,
direct globe selection/move/rotation/resize, Edit source and explicit Refresh source. Editor source
links are stored outside the portable applied recipe. Imports clear these links;
exports contain source snapshots and resolved guides, without Unreal paths.

An editorial heightmap structure profile supports signed native VPP relief.
The first source, Canyons v1, is a connected CC0 Motion Forge river canyon with
tributaries, closed ends and a clean zero margin. It is placed manually, with
negative relief cutting into the assembled terrain. Its matching river mask is
retained for later work. The proof contains no water integration.

The same field feeds the Designer cutaway, globe and native VPP volume/collision.
The host's `Docs/PlanetAssembly.md` records ownership, controls, reproducible
source preparation, proof map and limits. Asset custom-version 3 preserves the
omitted schema-10 defaults of earlier version-2 packages. No engine or vendor
source changes are part of this feature.

### Detailed globe preview

The Planet Builder globe uses 512 cells per cube face, up from 64: sixty-four times
the rendered triangles and eight times the linear sampling density. It samples
the same compiled terrain, with field-derived normals and unchanged terrain
settings. Position/normal samples are evaluated in batches of at most 4,096
vertices (20,480 field queries), bounding temporary sampling memory.

The existing single-worker, replaceable-pending-request and revision-fencing
flow remains. Completed worker buffers move into the UI handoff without an
extra full-mesh copy. Orbit/zoom and idle frames do not regenerate the terrain mesh.
`PlanetBuilder.GlobeDetail` measures triangle-plane error between vertices
against the actual assembly field and checks face boundaries. Native VPP terrain
and collision resolution are unchanged.

### Direct globe placement and rising canyon beds

Planet Builder now selects authored Landforms and structure features on the
globe, with centre, rotation-ring and footprint resize handles. A Selection popup
reaches overlapping/hidden targets. Move/rotate retain a Landform's structure
binding; resizing the instance stores explicit extents in that same parent frame.
A drag updates its outline only, then validates and commits one undoable change
on release. Escape/capture loss discard the draft. The source Designer and
portable recipe schema remain unchanged.

Canyons revision 2 adds a rising bed at each end, measured along the river path.
The bed reaches surrounding terrain over 32% of the path at either end, while
central source relief and shallow banks are preserved. Version 1 remains
immutable. `Tools/prepare-canyon-ramps.py` reproduces the processed quantitative
source. The host's `Docs/GlobePlacement.md` documents controls and ownership.
