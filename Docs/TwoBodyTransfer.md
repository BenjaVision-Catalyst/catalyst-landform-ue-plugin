# Native two-body transfer proof

4 November 2026. A bounded P2 body/physics handover on UE 5.7.4, VPP 2.0p8,
macOS arm64 Development. Two complete 100 m solid spheres remain in one UE
world, 300 m apart. Each retains one native VPP runtime and accepted recipe.
VPP is the sole terrain generator/presenter, including native stamps, layers,
materials, adaptive LOD and collision invokers. There is no custom streamer,
mesher, terrain proxy or generation scheduler.

The fixture uses the existing recipe's outer-sky convention: the solid radius
is 100 m and the outer envelope is 200 m. It does not redefine `RadiusMeters`
or migrate saved planet recipes. Native terrain voxel size is 25 cm.

## Native audit and demonstrated gaps

| Boundary | Native capability and owning correction |
| --- | --- |
| Body geometry isolation | VPP layers and layer stacks select which stamps a voxel world consumes. Stamp ownership by a UE actor does not itself isolate a planet. `ACLPlanet` previously hard-coded its stamp to VPP's global default volume layer. `TerrainLayer` now exposes the native choice; the caller includes it in that world's native `LayerStack`. Null retains the prior default. The legacy host shape fixture honors the same choice. No custom spatial/body filter is added. |
| Moving demand across worlds | VPP's `FVoxelCollisionInvokerManager` transforms the attached invoker demand per runtime and checks all native voxel worlds. `bWaitForVoxelWorld` uses native newest-state collision checks and inline generation at the character position. Keep it enabled and retain the same component throughout the trip. No lookahead/residency manager is needed for this proof. |
| Gravity source selection | UE `SetGravityDirection` owns custom-gravity transforms and character simulation. The existing `UCLRadialGravityMovement::UpdateLocalGravity` already adds the application's radial centre and minimal-rotation heading policy. VPP does not select celestial gravity sources. |
| Body owner/cached gravity ordering | Movement ticks before its character owner. Merely assigning the host's `Planet` pointer leaves the movement component's centre/direction stale until a later owner tick. The negative control measured a -0.189 gravity dot product and a 1.542 heading-vector discrepancy. `TryBindAirbornePlanet` publishes the explicit target pointer, centre and gravity scale, then calls the existing `UpdateLocalGravity` immediately, before the next movement step. It adds no gravity integrator or automatic body selector. Invalid admission changes nothing. |
| Floor/base transitions | Reuse UE `SetMovementMode` for flying/falling/walking. It clears inappropriate floor/base state. Reuse VPP's `AVoxelCharacter::SetBase` behavior, which binds procedural collision to the retained voxel-world root. No replacement floor or replication implementation is introduced. |
| Picking | The existing presentation trace delegates to a native world line trace at natural curvature. Every walking hit must be a native VPP collider owned by the selected body. |

The shared-layer negative control renders another body's surface and is rejected
by native mesh/surface inspection. This is a previously unjustified adapter
restriction on a feature VPP already provided, not a missing VPP feature. The
binding control is an application publication-order gap, not a UE physics defect.
Both controls retain native cleanup and are kept separately from passing cases.

Installed sources audited: VPP `VoxelLayerStack`, `VoxelVolumeStamp`,
`VoxelCollisionInvoker`, `VoxelInvokerCollisionSubsystem`, `VoxelCharacter`, and
UE `CharacterMovementComponent`. Primary guidance:
[VPP invokers](https://docs.voxelplugin.com/knowledgebase/blueprints/collision-navmesh-and-invokers)
and [UE Character Movement](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UCharacterMovementComponent?application_version=5.7).
Source hashes and actual pinned execution own the qualification; online docs do
not upgrade its native version. No engine or licensed VPP source is changed.

## The bounded journey

1. Wait for native initial readiness, land on A, and walk at least 4 m and 64
   grounded frames. Initial movement suspension is a harness warmup choice.
2. Enter UE flying mode, ascend above the solid and fly toward B using ordinary
   native movement input. The flight controller and midpoint selection plane
   are test inputs, not a production nearest-body or gravity-zone manager.
3. Select B while moving at 24 m/s in clear air. Observe world position/velocity
   immediately before/after, source/target body-local coordinates and the
   independent minimal-rotation heading oracle. No teleport or pose/velocity
   restoration is performed at the handover.
4. Use UE falling mode for eight actual physics frames. Independently derive
   expected acceleration from the selected centre, previous position, world
   gravity and that body's declared scale. Check native velocity changes.
5. Finish the flight, descend and land using native collision. Walk on B, then
   repeat the transfer back to A and walk there again.

The body gravity scales are fixture inputs, 0.15 on A and 0.30 on B. Magnitude
is constant for each selected body; this is not an inverse-square field or an
N-body simulation. The two bodies have no angular/frame velocity. Heading is
parallel transported by the minimal rotation between old/new up; it is not a
claim that the world-space forward vector or camera quaternion stays identical
when the selected up changes abruptly. World position and linear velocity do
stay continuous at selection. Blended gravity, inertial camera/animation and
angular-velocity continuity remain separate policies.

The host binding only admits live same-world targets, finite positive gravity,
an airborne character and a noncoincident centre. Grounded floor/base transfers
are deliberately refused before mutation; that policy is not silently generalized.

## Geometry, materials and resource evidence

Native runtime, sampling snapshot, actor, character and attached invoker identities
are retained. Native ready/retire events are expected: they demonstrate VPP's own
LOD/demand work during travel. Read actual displaced native meshes; reject foreign
body geometry and surface IDs. Check near-walker mesh and native floor error
against the 100 m analytic sphere separately from numerical handover errors.
No material slots, primitive packets or terrain transforms are changed by the
handover. Each body uses a different accepted surface ID (Soil/Rock).

The existing scale-lab material is reused at natural curvature. Its world-minus-
actor coordinate path and zero primitive-data quaternion give the identity
body-local mapping for these static, unrotated roots. The proof verifies zero
primitive packets, accepted native surface bindings, and uses the existing
`FCLAnchoredGPUReadback` for 60 fixed body-local probes across A/B/A visits.
Tile sizes come from the accepted native surface library; the existing texture
and macro phase functions are checked with a 1 mm budget. No shader or material
asset is changed. This is a source-qualified function/coordinate witness; it
does not read raw pixel UVs, native GPU velocity buffers or certify arbitrary
materials/temporal reconstruction. Actual gameplay images are retained for all
three visits and reviewed separately.

Limits: 1 mm numeric body-frame round trip; 0.01 cm zero-time position and
0.01 cm/s velocity handover error; 1e-6 heading-vector error; 0.1 cm/s native
falling-step velocity error; 25 cm native mesh/contact error; 0.5 cm picking
error. Natural curvature stays native/identity. The proof bounds meshes to 2,048
per body, pending tasks to 50,000, resident memory to 16 GiB and a case to 180 s.
These caps and measured frame steps are diagnostics, not performance qualification.

The final matrix uses 30/120 fps and common root translations of zero and
(50000,-30000,10000) cm. These are initial placements, not live UE origin shifts.
The existing separate live-origin presentation proof is replayed as a regression.
The package rebuild reuses the unchanged, fingerprint-qualified material/shader
cook with `--skip-cook`; it does not claim a new full cook or changed content.

Executed counts, errors, both negative controls, native fingerprints, images,
regressions and preservation hashes are in
[`conformance/two-body-transfer-mac-arm64.json`](../conformance/two-body-transfer-mac-arm64.json).

The final four-case run passes all eight handovers, with 1,753/1,753 native
grounded frames and picks, 64 independent native falling steps and 240 GPU probes.
Maximum recorded handover position, velocity and body-frame round-trip errors
are zero. Heading-vector error is 1.165e-10; native contact error is 0.0342 cm,
near-walker mesh error 0.0165 cm and texture/macro phase error 0.000413 cm.
These are controlled analytic-fixture measurements, not scientific source accuracy.

Sixty editor checks pass with zero automation warnings. The same final cooked
executable passes both expected-failure controls, the 630-point geometric datum
oracle, one Earth-pole half-curvature live-origin presentation replay and all
12 legacy volumetric walks. All 145 protected existing files retain their hashes;
all 117 canonical content/source-art/shader files and previously audited native
VPP/engine source fingerprints remain unchanged.

## Replay

From the LandformLab host directory, close its editor before packaging. Stage
before building the editor; staging removes generated plugin binaries.

```sh
python3 scripts/stage-plugins.py
# Build LandformLabEditor using the pinned UE 5.7 Mac Build.sh command.
python3 scripts/package-runtime-recipe.py --scale-lab --archive "$PWD/Saved/Packages/TwoBodyTransfer"
python3 scripts/qualify-two-body.py --archive "$PWD/Saved/Packages/TwoBodyTransfer" --output "$PWD/Saved/Verification/TwoBody/repeat"
```

Use `--fps 60 --offset origin` for one case. `--control shared-layer` and
`--control legacy-binding` expect a native failure report, validate its reason
and cleanup, and keep those results distinct from the passing matrix. Select a
fresh output directory. This automatic proof disables input; it is not the
interactive playground. It requires the initialized LandformLab macOS app
container; the existing recipe launcher initializes it on first use.

Editor checks: `Catalyst.Landform.NativeGeneration.BodyLayerRouting` and
`LandformLab.Gravity.AirborneBodyBinding`, plus the existing gravity/native-
generation/scene/scale/presentation suite.

## Remaining gates

This closes the measured static-body airborne-handover slice, not all of P2/P4.
There is no production body-selection manager or general body/physics lifecycle
service here. Keep selection/gravity policy with the body/physics owner and
terrain demand with VPP. The next rotation/frame work is refined by the
[body-fixed gameplay contract](BodyFixedGameplayFrames.md): audit a stable local
gameplay space with changing celestial pose before any physical-root rotation
strategy. VPP instance/material/collision semantics and real gameplay plugins
remain explicit gates.

Rotating/moving bodies, angular/frame velocities, antipodal heading policy,
collision between moving terrain bodies, grounded/in-contact source changes,
replication, multiple views, full-scene curvature, large spatial separation,
scientific datums, geoid/areoid providers and real resource delivery remain open.
The measured-data path must retain actual coordinate/height conventions before
adopting a Mars DEM; this proof does not qualify scientific ingestion.
