# Native VPP / UE coordinate audit

3 November 2026. Source audit against component `77a9105` and workspace
`d54ac08`, UE 5.7.4 (changelist 51494982), installed VPP 2.0p8. The
[source inventory](NativeCoordinateAudit.json) fingerprints the inspected native
dependency and integration files. No runtime implementation changes or new native
execution results are claimed here.

Reuse VPP's generation, LOD and moving collision invokers. The Scale Lab's fixed
coverage, quality floor, character replacement and pause are harness choices.
There is no demonstrated requirement for a custom moving-window or terrain
streaming manager. The goal remains a server recipe producing an ultra-realistic
world, including general volumes, materials and reliable ground interaction.

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

## Next bounded proof

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

## Publication and resumption

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
