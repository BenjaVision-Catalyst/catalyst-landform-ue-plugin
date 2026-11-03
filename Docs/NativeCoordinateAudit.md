# Native VPP / UE coordinate audit

3 November 2026. Source audit against component `77a9105` and workspace
`d54ac08`, UE 5.7.4 (changelist 51494982), installed VPP 2.0p8. The
[source inventory](NativeCoordinateAudit.json) fingerprints the inspected native
dependency and integration files. No runtime implementation changes or new native
execution results are claimed by that original audit. The reconciliation below records subsequent implementation and execution.

Reuse VPP's generation, LOD and moving collision invokers. The Scale Lab's fixed
coverage, quality floor, character replacement and pause are harness choices.
There is no demonstrated requirement for a custom moving-window or terrain
streaming manager. The goal remains a server recipe producing an ultra-realistic
world, including general volumes, materials and reliable ground interaction.

Execution follow-up: the [persistent native world checkpoint](PlanetDesignSystem.md#persistent-native-world-checkpoint)
and [qualification](../conformance/persistent-world-mac-arm64.json) record the
subsequent native runs and the narrowly scoped origin repairs. The findings
below remain the original source audit against its named baseline.

## Evidence and its limits

The [scale qualification](../../Projects/LandformLab/Docs/ScaleLab-mac-arm64.json)
records 32 packaged size/site/frame cases, 21 focused checks and the same
executable's 12 legacy volumetric walks. It qualifies a 64 × 64 × 48 m synthetic
neighborhood at 25 cm source voxels, fixed quality 8 and source LOD 0. It does
not test a persistent native world crossing coverage boundaries, source LOD
transitions or an actual UE world-origin shift. The reanchor cases destroy and
replace actors while movement is held; they preserve canonical pose numerically.

This audit traces source and checks the retained record; it does not rerun those
cases. Source capability, a source-level risk and a reproduced runtime failure
are different evidence. In particular, no new VPP origin-shift failure has been
reproduced.

## Native capabilities to retain

Vendor paths in this table are relative to the locally installed
`Landform/Projects/LandformLab/Plugins/Voxel/Source/`. Licensed source stays local.

| Capability | Inspected source / primary documentation | Consequence |
| --- | --- | --- |
| Double positions at field queries | `Voxel/Public/VoxelStampQuery.h`: volume bulk queries carry double XYZ starts and float steps/distances; sparse positions are double. `VoxelStampUtilities.h` returns `FVoxelDoubleVectorBuffer`. | A float distance result does not imply float absolute query positions. Keep body-space field evaluation in doubles through this existing interface. |
| Chunk-local geometry | `Voxel/Public/VoxelMesh.h` stores `FInt64Vector ChunkOffset` separately from `FVector3f` vertices. `Voxel/Private/VoxelMesher.cpp` constructs double query starts; `Render/VoxelRenderComponentCreator.cpp::GetComponentTransform` places/scales local meshes. | Do not replace VPP's chunk decomposition merely because Earth-sized absolute centimetres lose precision as a single float. Native local vertices already avoid that representation. |
| Volume transforms | `VoxelStampTransform.h/.cpp`: volume translation is double; rotation and scale are float. Inverse transforms subtract translation before rotation. | Audit each arithmetic boundary. Double translation is available; arbitrary large rotated displacements still need error measurements. This is not an all-double or all-float pipeline. |
| View-driven native LOD | `Voxel/Private/VoxelConfig.cpp` captures camera and actor transform. `VoxelRuntime.cpp::Tick` computes a new state on changed configuration/invalidation/subdivision demand. `Render/VoxelRenderTree.cpp::Update` traverses, collapses, subdivides and reconciles neighbors. `VoxelScreenSizeHelper.cpp` converts the camera into local chunk coordinates. | Keep native surface generation, subdivision and transition ownership. Render detail follows the view; collision invokers are a separate demand path. |
| Moving collision demand | `Collision/VoxelCollisionInvoker.cpp`: registration/unregistration is automatic; `FVoxelCollisionInvokerView::Tick` reads enabled components' current positions/radii and invalidates changed chunk demand. The manager services its views each tick. [VPP invoker documentation](https://docs.voxelplugin.com/knowledgebase/blueprints/collision-navmesh-and-invokers) confirms automatic movement tracking. | Attach the native invoker to the canonical simulation character. No custom registration or collision-window tracker is needed for ordinary travel. |
| Readiness and reuse | `FVoxelCollisionInvokerManager::CheckVoxelWorlds` uses `HasCollision` / `ComputeInline` for `bWaitForVoxelWorld`. `VoxelInvokerCollisionSubsystem.cpp` carries reusable collider references/components between states. `VoxelWorld.h` exposes readiness, progress and state-rendered events. | Exercise native waiting and state lifecycle before adding a second readiness system. Inline computation can cost a frame stall; availability is not a frame-time guarantee. Disable visibility collision when using invoker collision, as the vendor documents and Scale Lab already does. |
| UE large-coordinate rendering | [UE 5.7 LWC](https://dev.epicgames.com/documentation/en-us/unreal-engine/large-world-coordinates-in-unreal-engine-5?application_version=5.7) supplies double core types. [LWC rendering](https://dev.epicgames.com/documentation/unreal-engine/large-world-coordinates-rendering-in-unreal-engine-5?application_version=5.7) supplies DoubleFloat and translated-world operations. | Prefer existing local/translated coordinates and engine precision facilities. LWC does not certify every custom shader demotion, field provider or material operation. |
| UE origin translation | Installed `Engine/Private/World.cpp::SetNewWorldOrigin` offsets rendering, supported physics, visible levels and navigation, and broadcasts origin events. `Actor.cpp` / `Components/SceneComponent.cpp::ApplyWorldOffset` update actor/component hierarchies. | UE has a real origin-shift mechanism. It is distinct from changing a body's canonical frame or rotating that frame. Integration caches must be checked through the actual call. |

## Coordinate ownership

Keep these transformations explicit:

1. **Canonical body XYZ, metres:** immutable scene, geological/resource identity
   and accepted recipe. A UE origin translation does not rewrite this data.
2. **Native terrain coordinates, centimetres:** VPP query positions and its chunk
   decomposition. The current optional `FCLBodyFrame` supplies a rigid conversion
   to body metres. A stable full-body native frame is also a candidate to qualify.
3. **UE world coordinates:** actor/component transforms place native terrain,
   canonical character and colliders. UE can translate this world's origin.
4. **Presented coordinates:** Landform's curvature adapter projects native terrain,
   camera and visuals. Native collision remains in the canonical simulation
   placement; projected picks invert into that placement.

Native chunk offsets, UE origin translation, body-local reanchoring, curvature
presentation and external resource residency solve different problems. None
automatically requires the others to be managed by a new terrain streamer.

`FCLPlanetStampRuntime::SampleDistances` already consumes VPP double positions,
maps through `FCLBodyFrame`, evaluates `FCLScene` in double body metres, then
returns near-surface float centimetre distances. Retain this field boundary.
The float ULP at Earth-radius absolute centimetres is 64 cm (Mars: 32 cm);
these arithmetic facts explain why premature demotion is unsafe. They are not
measured native mesh errors: the native vertices are chunk-local.

## Demonstrated adapter limits

| Established limit | Evidence | Smallest next evidence needed |
| --- | --- | --- |
| Native coverage is explicitly bounded when enabled | `FCLSceneWindow::Validate` admits half extents 1..2,048 m; `FCLPlanetStampRuntime::GetLocalBounds` returns that box. Scale Lab only qualifies its much smaller extent. | Compare the existing full-scene path with native LOD over a bounded route. Do not treat the harness box as a production requirement. |
| Presentation admits restricted origins | Host `FCLNaniteProjectionPrototype::TryCreate` requires identity transform on the unwindowed path; framed presentation requires identity actor rotation/scale and location components within 1 km. | Separate natural-curvature native control from projected rendering; measure large body coordinates and translated actor origins before broadening admission. This is a Landform prototype restriction. |
| Global fine-noise admission is limited | `Model/CLPlanetRecipe.cpp` rejects `radius / wavelength * 2^(octaves-1) > 1e6`; the native Perlin adapter uses float vector buffers. The scale witness uses radius/64, three octaves. | Test the required metre-scale detail spectrum at planetary radius. If it cannot meet the profile, version a precision-aware field provider while retaining native meshing. Moving VPP coverage alone cannot fix canonical noise quantization. |
| Scene sampling has no filtered footprint | `Scene/CLScene.cpp::ValidateQuery` rejects nonzero `FootprintMeters`. | Qualify footprint-dependent field sampling and aliasing at actual coarse native LODs. This is a field/provider gap, independent of who schedules chunks. |
| General LOD and performance remain open | Scale record source LODs are `[0]`; quality 4 failed one sharp-corner budget and quality 8 passed. Existing 16.67 ms wall-frame p99 production gate is still open. | Exercise native adaptive detail, transitions, thin openings and fast movement; record frame tails, task count, peak/steady memory and contact error. Quality 8 is a fixture setting, not a production policy. |
| Scientific datum and server resource integration remain open | Current `FCLScene` is spherical and compiled locally; P2/P3 retain ellipsoid/datum and resource-provider gates. Local JSON import is not a server wire contract. | Prove coordinate/datum witnesses, accepted revision/resource identities, delayed/absent data and immutable resource leases through their owners. These domain/resource responsibilities do not replace native VPP terrain demand. |

## Risks requiring native reproduction

**Captured invoker transform across origin changes.**
`FVoxelCollisionInvokerView` captures `LocalToWorld` at construction.
`FVoxelInvokerCollisionSubsystem::LoadFromPrevious` retains the view; its cache
reset conditions do not include a changed `LocalToWorld`, and `Initialize` creates
a view only when none exists. No VPP `ApplyWorldOffset` / origin-event handler was
found by searching the installed source. In contrast, `HasCollision` and
`ComputeInline` transform positions with the current configuration. This is a
concrete source-level mismatch to test during an actual origin shift or native
world transform change. It does not demonstrate failed ordinary moving invokers
with a stable terrain transform, and it is not yet a measured rebasing failure.

**Stamp and presentation caches.** Ordinary stamp components refresh registered
transforms through `ApplyComponentChangesToStamp`. Instanced stamps instead retain
world-space placement; [the vendor explicitly documents](https://docs.voxelplugin.com/knowledgebase/working-with-stamps/instanced-stamps)
that moving their component does not move existing stamps. Landform also retains
mesh transforms/bounds, current/previous projection frames, camera history,
marker poses and `UCLRadialGravityMovement::PlanetCenter`. No complete origin-shift
reconciliation is established for these together. The current translated-root
Scale Lab case starts in that placement; it is not a shift of a running world.

**Presented view versus native LOD demand.** VPP obtains its game view through
`FVoxelUtilities::GetCameraView` (game camera manager at player index 0);
`FVoxelScreenSizeHelper` treats that view as a
native-world position. Landform's `FCLGameplayPresentation::ProjectCamera` changes
the final view position. Whether the projected view remains a sufficient native
LOD demand source at every curvature, distance and tick ordering is unqualified.
Keep the collision invoker in canonical space and instrument both view positions.
A demonstrated mismatch would call for a narrow demand-coordinate integration,
not replacement of VPP's LOD algorithm. Multiple views/bodies need their own run.

## Original next bounded proof

Use one persistent, stationary native VPP world and the existing scene field;
reuse the native character's attached collision invoker and
`bWaitForVoxelWorld`. Begin with natural curvature and native materials, then
enable the current curvature presentation. Bound runtime, tasks and memory;
terminate and retain evidence if the budget is exceeded.

1. Traverse several native collision/render chunk boundaries and return, without
   changing the recipe, replacing the world or recreating the character. Compare
   the optional bounded coverage with the existing full-scene field path. Record
   actor/runtime identity, native ready/retire events, collider reuse, source LODs,
   pending work, stalls and floor/field agreement. Use coarse-to-fine camera
   distances and travel speeds sufficient to exercise new demand.
2. Repeat selected equator, near-pole and antimeridian samples at Mars/Earth
   radii in the stable body frame. Compare double field witnesses, native local
   mesh placement, collision, material phase and projected picking. Retain the
   existing 1 mm numerical, 25 cm mesh/contact and 0.5 cm projected-pick targets
   in their declared bands; report each separately.
3. In a separate case, call UE `SetNewWorldOrigin` while that world/character
   remain alive. Record return/completion, pre/post transforms, invoker demand,
   stamps, gravity centre, presentation history and first post-shift contact.
   Compare ordinary and instanced stamps explicitly. Treat body-frame rotation
   and two-body transfer as separate cases rather than inferring them from a
   translation.
4. At natural/half/flat curvature, record the canonical camera, final presented
   camera and VPP demand camera with actual native LODs and collision. Include a
   return journey to detect stale caches and retain lifecycle cleanup results.

If these cases pass, use native generation/LOD/invokers within the measured
envelope. If one fails, retain the smallest reproducer and fix its owning
coordinate, cache or field boundary. A custom moving-window manager, distant
representation or replacement streamer requires a separate demonstrated need.
Resource fetching/caching still needs its P3 contract when real server-delivered
data is introduced. This audit does not choose its codec or transport.

## Original audit publication and resumption

This change updates the architectural checkpoint only. Validation checks the
source fingerprints, qualification counts, float-spacing arithmetic, local
document links and whitespace. No native tests were rerun for a documentation
change. The prior native qualification and its retained regional-route failure
keep their exact scope. Existing local assets/configuration and workspace guide
edits are preserved.

Next action: implement the persistent-native-world diagnostic above, starting
with ordinary moving invokers and stable body coordinates. Its acceptance is
retained native evidence that identifies whether any production coordinate gap
exists; it is not permission to assume a streaming architecture in advance.


## Native generation and preview reconciliation — 3 November 2026

VPP is Landform's sole voxel generation and presentation platform. Its layers,
stamps, meshing, materials, adaptive LOD, collision and moving invokers are the
implementation foundation. Catalyst specializations express the server recipe's
planetary/geological meaning inside that platform. A specialization needs a named
requirement and evidence; native behavior must not be rebuilt for convenience.
The goal remains a server recipe producing an ultra-realistic world.

The review found real duplication. The earlier preview used an independent radial
triangulation, or copied VPP triangles into a hidden-producer/visible-Dynamic-Mesh
arrangement. Solid operations also implemented generic primitive distances, smooth
CSG and a bounding tree. Native VPP supplies these capabilities; there was no
established missing feature that justified those implementations. They have been
replaced in this change. This corrects an ownership violation, not a new option
about which terrain platform to use.

| Reconciled behavior | Native implementation and application boundary |
| --- | --- |
| Radial base figure | `UVoxelShapeFunctionLibrary::MakeSphere` evaluates double positions and varying double effective radii, returning native float distances. Radius subtraction occurs before demotion. The immutable height profile supplies the radius input; it does not implement another sphere kernel. The older actor/source profile uses this native call too. |
| Sphere and box solids | `FVoxelSphereShape::Sample` and `FVoxelCubeShape::Sample`. The recipe adapter supplies transforms/dimensions in body metres. |
| Union, subtraction, material contribution | `FVoxelBlendModeUtilities::ComputeDistance`, including its native blend alpha. Application ordering remains explicit priority/accepted identity. Native distances are float; independent composition is checked within the existing 1 mm numerical budget, rather than requiring the former double formula bit pattern. |
| Operation lookup | `FVoxelAABBTree`, including its conservative double-to-float bounds conversion. A positive exterior distance can still affect a continuous blend, so the adapter retains a conservative influence check; bounding-box membership alone is not a field-support test. |
| Section clipping | Native cube distance and native intersection. Exterior corner distances now use VPP's Euclidean cube metric; solid/air signs and planar interfaces are checked independently. |
| Globe and section terrain presentation | One persistent `AVoxelWorld` and native `UVoxelStampComponent` per viewport. The original visible `UVoxelMeshComponent` owns geometry, material bindings and collision. Supported native stamp transforms normalize the UI; the voxel world remains unscaled. No copied terrain mesh is drawn. |
| Preview updates and selection | The UI coalesces the latest recipe, calls native `SetStamp`, and waits for `OnNextStateRendered` plus native readiness. Full accepted input is retained in the reflected stamp so VPP sees parameter changes. Native collision traces pick the displayed terrain, including multiple cave floors and detached geometry. Picking is withheld while an input is pending or native publication/readiness is incomplete, so an updated stamp transform cannot reinterpret retained old collision. No application chunk scheduler is introduced. |

The first cooked legacy-reference run stopped at a former 1 micrometre default
comparison. A separate native/editor export was compared with all 297 original
frozen XYZ witnesses: maximum field change was 0.000003206703 m; heights,
coordinates, identities and other metadata were unchanged. The old 2 cm slope
probe amplified native float rounding into a 0.000121168 material-weight error.
A 5 cm probe reduces that maximum to 0.0000253139, below the unchanged 0.0001
weight budget. This is a measured precision adjustment to the existing slope
adapter, not another shape kernel.

The cooked reference checker now applies the declared 1 mm scene-field and
0.0001 material-weight budgets to those channels, retaining stricter coordinate,
identity and height checks. Its old root-path checks expected a leading slash
that `FString` path joining omits. New tests reject out-of-budget errors and
retain strict coordinate/revision checks. The original input and reference are
kept; reports separately record frozen-field, frozen-weight and native adapter
errors rather than hiding them in one mixed-unit maximum.

`FCLGlobeBuffers::Build`, `FCLSectionGeometry::Build`, the Dynamic Mesh uploader
and the old radial cursor oracle are now automation fixtures only. They remain
independent reference/appearance tests, not product preview paths. Native tests
separately verify visible native components, collision picking, recipe updates,
persistent owner identities and normalized sphere geometry. Read-only native mesh
inspection is bounded to one million vertices and never feeds a renderer. Its
vertex inspection is not a qualification of every native transition displacement.
The ocean sphere is a stock UE mesh used as a non-colliding guide overlay.

### Remaining specializations, compatibility and demonstrated gaps

| Retained code | Reason, evidence and limit |
| --- | --- |
| Recipe compiler and custom `FCLPlanetStamp` | Accepted IDs/revisions, normalized radial guides, geodesic placement, spherical charts, geological units and planetary process profiles are Catalyst domain semantics. The adapter lowers them into a native VPP volume layer and uses native kernels. It is still custom field-provider code; this change does not claim every recipe operation is a separately authored VPP graph or actor. The native heightmap sampler/blender and UE PCG placement pipeline were already used and remain in use. |
| Capsule coordinates | Installed 2.0p8 exposes sphere/cube/plane shapes, but no capsule shape. The adapter maps a point to its nearest axial segment coordinate, then invokes the native sphere kernel. Only this missing coordinate specialization remains; no separate capsule distance formula is kept. |
| Versioned noise compatibility | New base planets and algorithm 2 use VPP Perlin. Algorithm 1 and already-versioned continental, tectonic and material profiles retain their pinned UE Perlin semantics. `NativeGeneration.KernelAndVersioning` demonstrates that native and legacy noise differ, so replacing a kernel silently would change accepted worlds. This is a compatibility exception, **not** evidence that VPP lacks noise. New generic noise implementations are unjustified; changing these profiles requires an explicit version/migration and requalification. |
| Authored VPP surface materials | Project HLSL defines profile-2 scanned triplanar artwork, procedural grain and the geographic grid through ordinary VPP Surface Type/UE material hooks. These are pinned appearance inputs, not a replacement VPP material compositor or evidence that native texturing is unavailable. The separate eight-surface preview material/uploader is retained only by automation reference fixtures. |
| Curvature presentation and canonical picking | The existing projection changes apparent curvature while collision stays canonical. Prior GPU, bounds, phase, history and inverse-pick proofs document why the application adapter exists. VPP still produces and presents the voxels. Ordinary native LOD and invokers remain the owners of terrain demand. See the persistent-world checkpoint and its explicit exclusions. |
| Application navigation and footing | Body-frame transforms, radial-up selection, globe/section navigation and the bounded XYZ spawn-footing locator interpret the planetary model. Character movement remains UE movement and contact remains native VPP collision. The locator is not a replacement physics solver or a certified general ray-miss test; thin-feature/transfer exclusions remain. |
| Origin compatibility and gravity | The retained VPP 2.0p8/UE 5.7.4 transform/collider repairs and Catalyst gravity-cache repair have bounded native reproducers. The cooked follow-up isolated both the stale Catalyst gravity centre and VPP layer snapshots using an out-of-date query frame. The latter needs synchronous `QueryToWorld` refresh before native snapshot publication; neither defect requires replacement generation or moving invokers. See `conformance/origin-contact-mac-arm64.json`. |
| Normalized preview physical material coordinates | Resolved for the installed scanned materials by native full-float metadata and the bounded planetary coordinate adapter below. VPP owns metadata storage, demand, publication and material lookup. Arbitrary materials must consume the same contract; former neutral cut-face styling remains unqualified. |
| Filtered fields, fine planetary noise and external resources | The earlier audit's point-query, float noise-input and server-resource/datum limits remain open. This reconciliation does not qualify erosion, scientific datums, production frame-time tails, resource residency or a server transport contract. |

The native shape library is stateless for the invoked function. Its class default
object is bound at compilation/initialization on the game thread; workers call
only the native buffer kernel, with no lookup or authoring/world UObject reads.
The retained recipe snapshot supplies native hashing; the non-reflected compiled
pointer alone is insufficient to describe changed native input.

Reproduction and measured results are recorded in
[`conformance/native-reconciliation-mac-arm64.json`](../conformance/native-reconciliation-mac-arm64.json).
The bounded scale harness and the persistent travel/origin tests remain distinct.
No custom streaming manager is added, and none is justified by this reconciliation.


Final qualification passes 35 editor checks with no automation warnings, 20
version-pinned dependency checks, 32 cooked scale cases, 12 volumetric walks
against the unchanged original reference, 12 short live-origin contact repeats,
and 12 persistent journeys (10 stable-frame/curvature journeys and two separate
origin journeys). All cooked runs use one executable fingerprint. Final
editor-only publication and explicit-fixture guards were checked afterward;
no runtime source changed. The source/runner/artifact hashes, measured errors
and exclusions are retained in the linked reconciliation record. All 151
protected local files retain their original hashes.


## Normalized preview material coordinates — 3 November 2026

The installed VPP material system supplies the required transport. This change
uses it directly. The initial failure was partly ours: `FCLPlanetStampRuntime`
inherited native `MetadataOverrides`, but its sparse `Apply` ignored them. It now
uses `CreateRuntime`, `ShouldCompute` and `FVoxelStampUtilities::ApplyMetadatas`,
including native blend alphas and affect-shape/surface/metadata behavior. No VPP
or UE patch was needed for this change.

### Native audit and selected path

| Capability inspected | Finding and use |
| --- | --- |
| VPP Surface Type / MegaMaterial | Retain the installed native compositor and material bindings. Existing rock, scree and soil artwork remains in its authored UE master material; no surface renderer or compositor is added. |
| `UVoxelLinearColorMetadata` and `UMaterialExpressionGetVoxelMetadata` | Native `FourBytes` packing carries full-float values to the material. Two constant vector metadata values per stamp carry high/low inverse scale, reference radius and mapping mode. Native material expressions read them with interpolation disabled. No custom vertex stream, metadata buffer or texture upload is implemented. |
| Native stamp metadata overrides | Supply mapping values with the accepted stamp input. Native meshes retain their own published values. Old mesh publication, simultaneous viewports and native state retirement need no global material parameter or per-frame MID mutation. The native renderer prunes metadata for meshes whose materials do not use it; untextured default/core faces correctly have no payload. |
| Native transforms and UE world position | `FVoxelVolumeTransform::Create` supplies the actual float stamp scale used by VPP, rather than assuming UE's double transform and VPP's transform are identical. UE `Parameters.AbsoluteWorldPosition` supplies its native DoubleFloat position. The preview world is unscaled; only the native stamp is normalized. |
| UE material transforms / default coordinates | Mesh-local coordinates refer to native chunk geometry, not the accepted recipe's stamp or nonlinear spherical section chart. Default world/display coordinates therefore cannot supply physical metres after normalization. VPP's metadata makes that domain mapping possible without replacing its presentation. |
| UE DoubleFloat | Reuse its arithmetic and the already-qualified `CLCanonicalPhase` helper. The first GPU witness using default `DFMultiply` failed by 31.32788 cm at Earth scale on Metal. UE's explicit `DFTwoProductNoFMA` plus native `DFAdd` cross terms fixes that measured scale-product failure. No new arbitrary-precision library is introduced. |

### Justified specialization and bounds

`CLPreviewCoordinates.ush` reverses the preview's native normalization and, for
the section, evaluates the same spherical chart as the existing CPU adapter.
The nonlinear chart and accepted reference radius are Catalyst planetary
semantics; native affine material transforms do not encode them. A stable
half-sine expression retains small-angle radial sag. Scanned triplanar sampling
uses the existing physical tile widths (2, 2.5 and 2.7 m), compensated phase and
unwrapped derivatives with UE `SampleGrad`, avoiding wrap-boundary mip/normal
seams. The geographic grid receives the mapped body position too.

The two metadata vectors describe an identity-rotation, zero-origin preview
stamp with equal X/Y scales. This is the current private globe/section world
invariant, not a general material mapping for arbitrary actor/world transforms.
Mode zero retains ordinary runtime sampling. The shared native `M_Surface` graph
still demands its two full-float vector metadata channels in ordinary VPP worlds;
VPP owns their default values and packing. Additional memory/frame cost has not
been qualified. Existing procedural grain
and macro-noise keep their pinned float semantics; this change does not migrate
appearance versions or claim planetary precision for every procedural channel.
Third-party material graphs must consume the same native metadata/mapping
contract to obtain this behavior; it is not inferred for arbitrary materials.

The GPU witness compares 120 points with an independent CPU double transform and
section chart: four radii (100, 1,000, 3,389,500 and 6,371,000.12345 m), both
mapping domains, three section widths and core/axis/edge probes. Maximum coordinate
error is 0.022493085 cm; maximum physical tile-phase error is 0.022500754 cm.
Both meet the unchanged 0.1 cm numerical budget. These are coordinate errors,
not claims of millimetre terrain meshes at planetary overview scale. The native
preview still uses 50 cm display voxels and its separately measured mesh limits.

Native mesh inspections check mapping values on textured meshes at 100 m, 1 km
and Earth radii, plus concurrent globe/section worlds. A rendered on/off/on
control changes only the globe's mapping mode through native stamp publication,
checks that the section retains its own payload, and verifies visible sampling
changes beyond repeated-frame noise while preserving native world/runtime owners.
The numeric witness tests the exact coordinate function used by the material;
the rendered control verifies that the actual graph consumes it. Neither test
restores the former neutral cut-face styling or qualifies every transition face,
third-party material, datum, GPU backend or production performance envelope.

Reproduction, fingerprints and final regression/cooked results are retained in
[`conformance/preview-material-coordinates-mac-arm64.json`](../conformance/preview-material-coordinates-mac-arm64.json).
The host's `scripts/update-preview-material-coordinates.py` updates only the product
master graph and two metadata assets; full surface-library generation
calls the same function. It does not rebake catalogs or replace scans. The
retired reference-only `M_GlobeGrid` asset retains its original bytes; the product
geographic overlay is already part of `M_Surface`.


Final qualification passes 46 editor checks without automation warnings, the
120-point GPU witness, repeatable targeted material updates, a fresh Development
build/full cook/package, the ordinary native-material walking control, 12 legacy
volumetric walks, 12 separate live-origin contact repeats and 32 bounded scale
cases. All cooked checks use one executable fingerprint. The volumetric walks
retain 4,320/4,320 native grounded frames; scale retains 31,998 native grounded
frames and 384 projected picks. The 226 unrelated protected files retain their
original hashes, including all catalogs, scans, source recipes/config and the
local generated material cache. Only the product master material is an intended
change among the 227 existing protected files; two metadata assets are new.
