# Experimental render-only Nanite projection

The current UE 5.8.4 walking integration is documented in
[WalkingPresentation](WalkingPresentation.md). It supersedes the historical
host-owned staging and patched-VPP lifecycle requirements below.

This opt-in UE 5.7.4 prototype deforms cached VPP 2.0p8 Nanite terrain from natural curvature to a tangent plane. Canonical vertices, actor transforms, field/runtime objects and collision remain authoritative. It is a bounded render experiment, not yet the gameplay presentation adapter.

## Render contract

`CLProjectionPosition.ush` uses the qualified projection in radius-normalized coordinates, with exact natural/flat fast paths. Material coordinates remain canonical; normals use the qualified inverse-transpose differential. Custom primitive data carries current/previous anchor and curvature, physical radius, one conservative Jacobian bound, diagnostic mode and canonical source bounds. Previous-frame WPO consumes the previous packet. Motion-vector quality still needs a separate visual test.

The CPU admits entire source boxes within a 60-degree chart and radial band `[.6R,1.3R]`. It does not evaluate vertices, regenerate terrain, move actors or rebuild collision when curvature changes. The host binds native streaming generations through a world-scoped producer lifecycle hook, restores presentation state before recycling and resets history for newly ready meshes. Unannounced mesh/transform changes fail the contract. At most 512 live components and 32 material slots per component are admitted.

The prototype packet is five `float4`s (20 custom-primitive floats):

| Index | Contents |
| --- | --- |
| 0 | Current unit anchor XYZ, curvature |
| 1 | Previous unit anchor XYZ, curvature |
| 2 | Physical radius in cm, maximum current/previous Jacobian norm, mode 1/2, marker 9137 |
| 3 | Canonical source local box centre XYZ, host native-control bypass flag W (0/1) |
| 4 | Canonical source local box half extent XYZ, reserved |

The host C++, material generator and Nanite helper must change this protocol together. The version-pinned source/hash record identifies the matching shader and host build.

## Two levels of bounds

The earlier scene-wide instance hierarchy sees CPU primitive bounds before the Nanite instance shader. A small version-pinned VPP extension adds a fixed `4R` render-only padding once when the prototype attaches. For admitted geometry, the projection displacement is less than `4R`: the presented position is bounded by `R + R*theta + .4R`, source radius by `1.3R`, and `theta <= pi/3`. This intentionally broad **coarse** envelope survives every admitted anchor/curvature; it does not change per frame. The extension restores its previous value on detach and resets it when a pooled component changes mesh.

Fine culling does not inherit that envelope. An isolated, generated copy of two Nanite shaders projects the canonical instance box and each hierarchy-node/cluster box on the GPU. For source centre `x`, half extent `e`, and a conservative maximum Jacobian norm `L` over the admitted convex source box, the projected cube is centred at `f(x)` with half extent `L*length(e) + FP32 margin`. Current/previous boxes are unioned for temporal culling. LOD spheres and error scales use the same primitive-wide `L`, rather than unrelated scales at each DAG level. Natural curvature retains the original boxes.

The `instance` diagnostic substitutes the whole source component box at every node/cluster. Compare it with the default `cluster` mode to measure the value of tighter bounds; never use disabled culling to qualify performance. Fixed-pose images are compared against a separate no-frustum/no-HZB/no-distance-culling reference, including missing terrain against the black sky.

## Integration and isolation

The host owns the VPP extension, overlay staging, diagnostic material and qualification scripts. The plugin owns projection math, shaders, early shader-directory registration and GPU oracle. Projection staging never edits engine files. VPP's required base shader-hook installation can update engine shaders through its own setup commandlet; the host separately qualifies that prerequisite. The overlay copies licensed local engine shaders into ignored `Saved/NaniteProjection`; only the patch generator and source hashes are published. The generator refuses unknown UE versions, altered target source files and unrelated generated edits. Enable `-CLNaniteProjectionShaders` both during cook and execution. The diagnostic material includes an overlay marker so an ordinary cook cannot silently build it against unmodified culling shaders.

This build requires a planet at the world origin with an identity transform. It supports one camera-centred chart, no distant repeats or chart handover. The direct-light diagnostic material replaces terrain textures; Lumen, distance-field AO and distance-field shadows are disabled because their canonical representations have not been adapted. VSM remains enabled with conservative invalidation. Caves, props, interactions, projected camera rays, general transforms, temporal artifacts and a production material bridge remain unqualified.

See the host's `Docs/NaniteProjectionPrototype.md` and its qualification JSON for packaged measurements. A passing bounds oracle alone does not establish 60 fps.

## Measured host checkpoint — 2026-11-02

On M3 Max / 128 GB at 1080p High with the explicit direct-light diagnostic profile, projected cluster bounds save 1.02–1.09 ms GPU p99 against whole-instance bounds in two final-build pairs. Flat/sweep GPU p99 are 10.59/11.28 ms. Fixed-pose CPU update p99 is approximately 0.10–0.14 ms; the streaming walking case is 1.56 ms including callbacks. All 4,505 walking frames retain native collision contact across three joins and 6,496 ready/retire pairs. Fixed-pose mesh/collider identities remain unchanged.

The 1080p flat/half culling-reference images match with zero missing terrain pixels. Seven editor tests, the cooked 2,520-query GPU/bounds oracle and nine patch-safety checks pass. Wall-clock frame p99 remains above 16.67 ms; **60 fps is not certified**. The host's `Docs/NaniteProjectionPrototype-mac-arm64.json` records exact build hashes, all counters, repeats and remaining gates. The earlier hybrid rigid-chunk fit remains a diagnostic oracle; the first render bridge instead uses GPU node/cluster projection because the observed large source chunks made residual WPO padding too broad.

The host now has an isolated native MegaMaterial/normal-map fixture; see the [host qualification](../../Projects/LandformLab/Docs/NativeMaterialFixture.md). It preserves native material roles and uses VPP's supported attribute postprocess. The admitted normal shader computes the exact inverse-transpose frame without repeating position/domain validation per pixel; source-box admission remains mandatory. The full production/gameplay gates above remain open.
