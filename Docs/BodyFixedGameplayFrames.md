# Body-fixed gameplay frames: contract and proof design

4 November 2026. **Design/source audit for review; no runtime implementation or
new compatibility qualification.** This supersedes interpreting the previous
“body rotation proof” checkpoint as a plan to spin a live terrain root every tick.
The user-approved target is ordinary stable surface gameplay while the body
has a physically meaningful position/orientation relative to the wider universe.

VPP remains the sole voxel generator/presenter, including layers, stamps,
materials, LOD and moving collision invokers. The body/physics integration owns
reference-frame meaning and motion. Landform supplies body-relative terrain and
native readiness; it does not acquire a new celestial, streaming or physics manager.

## Required game target: ThatGame:Catalyst

The user has explicitly confirmed that **ThatGame:Catalyst (TGC) uses ACF and
VPP together**. Their combined compatibility is a required acceptance gate for
this architecture, not an optional plugin exercise or an interchangeable future
choice. Retain ACF's configured character, movement, combat and animation
lifecycle, and VPP's native terrain, materials, collision demand and editing.
LandformLab-only success cannot satisfy that gate.

The existing TGC vertical-slice brief targets shared settlement gameplay,
third-person movement, spatial social audio, repair/construction and persistent
visible world changes in a small multiplayer session. Advanced combat and
multi-world travel can be staged by feature scope; that does not make the
underlying ACF+VPP integration optional. Qualification must name the enabled
ACF features, native VPP path, gravity/up/curvature profile and actual game assets.

The discovered CatalystPlugins project is an existing integration candidate
with ACF declared, not a verified identification of TGC's shipping executable.
Pin the intended TGC runtime project/configuration before modifying its assets
or treating a generic host as game conformance. No replacement of ACF's movement
class with LandformLab's proof character is permitted as evidence of compatibility.

## Architectural direction and unresolved topology

Keep the active local gameplay space fixed to its body. A stationary building,
terrain edit or sleeping prop retains its local transform and zero body-relative
velocity while celestial orientation advances. Do not continuously rotate their
UE actor hierarchy, publish artificial moving-ground velocity or regenerate VPP
terrain to express celestial rotation. Change distant/universe-relative views
and query results through the explicit frame relationship.

This is a preferred contract, not an established production implementation.
One common UE coordinate basis cannot make two independently rotating bodies
simultaneously stationary. Concurrent surface activity on multiple bodies,
space flight and cross-body combat are **blocking full multi-body topology gates**. They need not all be implemented
for the first same-settlement vertical slice, but its local-frame choice must
not be advertised as solving them.
No decision has been made to allocate a UWorld/server per body, create custom
Chaos scenes, route ordinary actors into solver islands, or maintain another
streaming/actor manager. World Partition/level streaming is not evidence of
independent physics frames. Native-supported candidates must be compared with
plugin, authority, query and replication behavior before choosing a topology.

A fixed local basis can align Z with up at its anchor, but a curved planet does
not have one global up. Body-fixed simulation alone cannot correct plugins that
hard-code world Z or world XY. Do not hide that problem by rotating the local
world continuously to follow each player's up. Traversal/reanchoring needs an
explicit bounded transition, qualified separately from celestial rotation.

## Frame contract (proposed version 1, not a new serialized recipe)

Three meanings remain separate:

| Space | Meaning / owner | Expected mutation |
| --- | --- | --- |
| B: canonical body-fixed | Double metres, explicit body/frame/figure identity; terrain and persistent building/edit coordinates | Geometry/edit changes only; no pose change from celestial spin |
| L: local gameplay/physics | Bounded native UE centimetres, fixed rigid anchor/basis relative to B; characters, combat and construction interact in one coherent local space | Ordinary gameplay motion; explicit frame-transition events only |
| U: common universe reference | High-precision parent frame and shared time/epoch; body pose, velocity and acceleration from the body/physics authority | Celestial motion / accepted authoritative snapshots |

The existing `FCLBodyFrame` already supplies the spatial B/L mapping. It contains
an anchor and quaternion, **not** parent identity, time, angular velocity or a
live simulation-space service. Reuse it; do not treat its rotated numeric tests
as qualification of a rotating native physics world.

At one accepted sample time t, let `c_U` be the body centre, `R_UB` its rotation
from B to U, and `omega_U`/`alpha_U` its angular velocity/acceleration, expressed
in U. Let `x_B` be metres from the body centre and `v_B = dx_B/dt`:

```text
x_B = a_B + Q_BL * J * (native_cm * 0.01) // fixed anchor/basis and explicit axis/parity map
r_U = R_UB * x_B
x_U = c_U + r_U
v_U = dc_U/dt + omega_U cross r_U + R_UB * v_B
```

Here B and U use one declared compatible canonical handedness. `J` is the
explicit fixed axis/handedness mapping from native UE components; it may be the
identity for an already reconciled numeric profile. A quaternion cannot hide a
reflection. The existing `FCLBodyFrame` is the rigid numeric mapping after that
convention has been reconciled, not an already-qualified scientific CRS bridge.
Do not silently change its legacy numeric behavior.

Cross products above are evaluated in that common canonical basis. Linear
quantities and angular (axial) quantities must not be blindly transformed alike
across a reflection: an axial-vector mapping carries the determinant sign.
Qualify actual UE angular-velocity conventions with positive-angle/north-east-up
oracles before using native angular API values. Mesh winding/normal conventions
remain an explicit renderer boundary, not a negative quaternion scale.

The inverse velocity must subtract origin and rotational velocity before inverse
rotation. A building with `v_B=0` generally has nonzero universe velocity. Do
not send that value to its local UE body as physical motion. Directions/normals
use the appropriate rotation; position translation is not applied to a velocity,
impulse, normal or direction. For rigid angular velocity, account for the frame's
angular velocity as well. A cross-frame orientation is an orientation transform,
not an arbitrary controller-yaw reset.

A proposed immutable frame sample must identify body, frame, parent frame,
authority/revision, common timestamp/time scale, units/axis convention, pose,
linear/angular velocity, and any accelerations required by the physics profile.
Transforms from different times must not be mixed. A frame transition must retain
its before/after frame identity and epoch, position/orientation, linear/angular
velocity and previous presentation state, or refuse before mutation. Native
origin shifts, passive basis changes and physical frame motion are distinct.
No recipe or persistence migration is authorized by this design.

For dynamic equivalence, the body/physics owner must also define the rotating-
frame force profile. In U components, with `v_rel_U = R_UB*v_B`:

```text
a_U = d2c_U/dt2 + alpha_U cross r_U
    + omega_U cross (omega_U cross r_U)
    + 2 * omega_U cross v_rel_U + R_UB * a_B
```

This identifies the translation, Euler, centrifugal and Coriolis terms that an
inverse conversion must handle where the gameplay physics profile requires
them. Do not silently ignore them, apply them twice (e.g. effective gravity plus
another centrifugal term), or replace UE's force integration. Native mechanisms
must be qualified at their actual scope. Advancing an ephemeris or rotating a
sky is not by itself proof of dynamically correct spinning-planet physics.

The current Catalyst Physics README describes GT/Bloc packages, not an Unreal
physics plugin. Concrete integration with authoritative body/time providers is
unresolved. Inspect live GT-managed domain source through GT MCP before changing
that owner; this audit neither edits it nor creates a parallel ephemeris service.

## Native capability audit

These rows distinguish source observations from executed conformance. Exact
file hashes, symbols and installed versions are in
[the audit inventory](Audits/body-fixed-frame-native-audit-2026-11-04.json).

| Capability | Evidence / limit | Proposed use |
| --- | --- | --- |
| VPP layers, native worlds and moving collision invokers | Four static two-body cases and the separate live-origin record are already published; no new tests in this audit | Retain native terrain ownership and demand. Those results do not qualify rotation or Ascent combat. |
| VPP world transform | `FVoxelConfig` captures `ActorToWorld`; its equality check compares `LocalToWorld` | A live root transform is configuration, not a harmless celestial metadata field. Measure invalidation if used; keep it fixed in the preferred idle proof. No runtime rotation cost has been measured here. |
| Ordinary VPP stamp component | `UpdateStamp` publishes `GetComponentTransform`, registers/updates the stamp | Keep body-local authoring stable. Real edits may legitimately invalidate terrain; celestial spin alone should not republish stamps. |
| VPP instanced stamps | Current refs retain world-space transforms; moving the component does not relocate them. Parent composition in `PostLoad` belongs to deprecated-data migration | Use native APIs for real instance edits. Do not infer live parent-relative rotation from migration code or attach every stamp to a spinning root. |
| UE origin rebasing | `SetNewWorldOrigin`/request use `FIntVector`; GameplayStatics rebase helpers delegate to `FRepMovement` and are policy-gated | Reuse qualified native translation paths. They do not establish a general quaternion world rebase. |
| UE double transforms and custom character gravity | Existing numeric frames, radial adapter and body-binding proofs | Reuse math and movement mechanisms. Selection and correct frame semantics remain application responsibilities. |
| Chaos simulation-space machinery | `FSimulationSpaceSettings` has acceleration/angular-motion terms; inspected use is ImmediatePhysics and the RigidBody animation node | Candidate for its documented local simulation scope. This is not evidence of transparent multi-frame support for all world actors/plugins. |
| Ordinary rigid body scene routing | Inspected `BodyInstance` paths obtain the owning world's physics scene; `UWorld::GetPhysicsScene` returns its scene | No ordinary-actor FrameId routing API has been established. Concurrent local spaces require a separate native topology proof. |
| Native observability | `USceneComponent::TransformUpdated`, `FBodyInstance::IsInstanceAwake`, primitive wake/sleep events, VPP mesh lifecycle/state queries | Use native notifications plus state sampling. Configure test wake events before settling; do not assume zero event count proves sleep. |

References: [VPP instanced stamps](https://docs.voxelplugin.com/knowledgebase/working-with-stamps/instanced-stamps),
[Epic simulation-space API](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Chaos/FSimulationSpace).
The installed UE 5.7.4 / VPP 2.0p8 source and executed records own the version pin;
newer web documentation does not extend this audit's guarantees.

## Actual integration inventory and compatibility matrix

LandformLab does not enable Ascent, vehicles or the broader gameplay stack.
The existing `DevKit/CatalystDevKit/CatalystPlugins/CatalystPlugins.uproject`
declares Ascent Combat Framework, Chaos Vehicles, StateTree and MetaHuman-related
plugins. The installed Ascent descriptor reports 4.2.3 / UE 5.7.0. Declaration,
installation and this source inspection are **not** a successful build/run of
that project. Do not alter the shared project or enable extra Lab plugins here.

| Surface / plugin | Read-only finding | Required compatibility proof / status |
| --- | --- | --- |
| Static buildings and sleeping props | Native static transforms and sleep observability exist | Celestial-only advance causes no local transform notification, contact loss, induced velocity or extra wakeup versus control. Proposed, unexecuted. |
| Ascent movement | `ACFCharacter` installs and casts to `UACFCharacterMovementComponent`; Landform's proof uses another subclass | Keep Ascent's class and extension lifecycle intact. Replacing it with `UCLRadialGravityMovement` is not an acceptable compatibility demonstration. Integration hooks/configuration remain to be audited. |
| Ascent climbing / orientation | Inspected movement paths use yaw-only standing rotations and `FVector::UpVector` for ceiling/vertical logic | Source risk for non-Z-up gravity, not a demonstrated runtime failure of the whole plugin. Test enabled walking/climbing/falling and root motion on deliberately tilted surfaces. Stable local space does not remove this gate. |
| Template combat / traces / impulses | The project sample uses world sweeps, world-up melee launch and world-space damage/impulse locations | Same-frame hit selection, damage, knockback, ragdoll and timing must match baseline; cross-frame data must carry frame/time semantics. Source risk only. |
| Ascent attacks, projectiles and effects | Installed framework has collision, projectile and spell modules; complete paths are not audited here | Pin enabled features and asset configuration. Test melee sockets/sweeps, projectile spawn/homing/gravity, explosion impulses and persistent effects. No compatibility claim yet. |
| Building placement and terrain editing | Existing native stamps/layers and body-local recipe inputs are available; no named production construction framework identified | Placement ray/snap, ownership, native edit invalidation, save/load and undo must survive celestial advance. Edits may regenerate terrain; rotation alone must not. Named framework remains an inventory gate. |
| Navigation / AI / StateTree | Gameplay project declares these features; lab walking is not navigation qualification | Path, floor/up assumptions and target/blackboard world-coordinate caches; planetary/cavern navigation remains open. |
| Chaos Vehicles / constraints / ragdoll / cloth | Feature declarations/native facilities found; no qualification in the current Lab | Resting constraints, suspension/gravity, root motion, simulation-space settings and transitions need feature-specific tests. |
| Niagara / decals / trails / audio / camera | No frame-conversion guarantees established | Persistent world-position histories, attachment behavior, traces/listener coordinates and previous-view state must be checked. |
| Replication / authority / rewind | Existing body proof is local; origin policy is not a rotation/network contract | Common frame/time for RPCs, hits, impulses, historical poses and server validation; two clients on distinct bodies and a cross-boundary projectile are production gates. |

Use the real chosen integration, not a mock labelled “combat compatible”. Begin
with native baseline actors to isolate the frame contract, then add opted-in
real plugin features. Narrow adapters may be justified by a reproducible failed
feature; blanket plugin rewrites, vendor patches and replacement movement,
physics or generation managers are not approved by this design. Disabling a
required TGC feature or weakening its behavior is not a compatibility fix. Any
feature-scope tradeoff must be explicit and reviewed, not hidden by a passing
fixture.

## First bounded implementation proposal: fixed gameplay, advancing celestial pose

The first implementation is **not started**. Proposed host: an opt-in cooked
LandformLab fixture, retaining its existing maps/materials and native VPP path.
Proposed code placement: diagnostic host values/observers initially; do not add a
production ephemeris/scene manager to Landform. Reuse existing spatial frame math
and UE types. Integration into the real body/physics owner is a separate decision.

### P0: non-vacuous frame math

Define test-provided immutable frame samples at a common time. Compare position,
orientation and velocity conversion with independent analytic references for
fixed and spinning frames, translated centres, explicit axis/parity and an
arbitrary fixed B/L basis. Include positive-angle/east/north/up and angular
velocity sign controls; a position-only round trip cannot establish handedness.
A point stationary in B must have the expected `omega cross r` universe velocity,
while its local rigid-body velocity remains zero. Round trips preserve position
within 1 mm and velocity within 0.01 cm/s; invalid identity/time/units and
incomplete required kinematics refuse before mutation. Angular-velocity and
acceleration cases have explicitly declared budgets before implementation.

This is a diagnostic sample provider, not a fake production authority. Existing
`FCLBodyFrame` alone cannot pass the velocity/epoch part because it has no such
contract. Native math is reused; missing semantic values do not justify another
solver or world manager.

### P1: idle local-world invariance with observable celestial motion

Use one static 100 m native body, a static building fixture, a settled dynamic
prop, a settled constrained pair and a grounded character. Keep native VPP
layers/world, sampling snapshot, material bindings and invoker identities.
Keep the camera stationary in L. Settle contact, native tasks and physics sleep
before measuring; failure to settle is a failed case, not a skipped sample.

Run matched fixed-pose and advancing-celestial-pose controls on the same binary
at 30/120 fps, with zero and translated initial local placement. Proposed duration
is 30 s measured after a bounded 60 s warmup, 120 s process cap. Use a stated
constant angular rate (e.g. 7.292115e-5 rad/s) with fixed local effective gravity.
Large-angle samples belong in P0's kinematic stress test, not an unqualified
high-speed spin simulation. Real rotating-frame dynamics are not claimed by P1.

The celestial pose must be consumed by observable frame queries and a non-terrain
UE diagnostic marker/direction. Its predicted universe motion and velocity must
change correctly, while terrain/building/prop local state remains fixed. This
prevents an unused “rotation angle” variable from passing. The marker is not a
planet proxy or substitute terrain presenter; VPP remains the only voxel path.

Proposed acceptance gates:

- Terrain root, stamp/instance transforms, native runtime and material bindings
  remain identical; zero rotation-induced source ready/retire or recipe events
  after settlement. Record baseline engine background work separately; do not
  disable generation to manufacture this result.
- Zero local transform-change notifications for static terrain/building; inspect
  owned setter calls and actual native state. No per-frame actor rewrites.
- Props are genuinely asleep at baseline and remain asleep, with zero additional
  wakeups/contact losses. Measure native awake state and events; never force sleep
  or restore transforms/velocities during observation.
- Local pose/velocity drift stays within predeclared baseline and numeric budgets;
  every observed grounded frame retains the selected native floor. Frame queries
  and the celestial marker must pass independent numeric oracles.
- Fixed body-local terrain field/material witnesses and actual native picks remain
  within the existing 1 mm numeric / one-voxel contact / 0.5 cm pick budgets.
  Reuse existing GPU functions and source-qualified material tests if needed;
  do not claim raw-pixel/temporal validity from a CPU-only witness.
- Remove observers, retire fixture actors/native runtime positively and preserve
  all unrelated local edits. Record exact binary/native hashes, raw traces and
  failures. No performance/compatibility certification follows from this alone.

Negative controls: (1) unused celestial pose must fail the external observer;
(2) intentionally moving the terrain root must trip transform/invalidation or
motion detection; (3) omitting rotational velocity must fail the kinematic oracle.
A negative root-motion control is diagnostic, never the proposed runtime model.

### Subsequent gates before production adoption

P2 (mandatory TGC gate): actual ACF and VPP together, plus construction/edit
features, in the pinned TGC runtime or an explicitly representative scoped host.
Include the enabled movement/combat paths, collision/hit validation, animation,
knockback, native terrain editing and a persistent placed structure while
celestial pose advances. Compare with fixed-pose controls. Test two players
interacting with the same hit/edit/build state and spatial audio. Deliberately
non-Z-up paths, enabled projectiles and explicit one-time transitions must have
their own declared scope; untested features remain unqualified.
No silent replacement of its movement component, no alteration of shared assets.

P3 (full multi-body gate): simultaneous local gameplay on independently rotating bodies, common authority
time and cross-frame attack/flight validation. Compare native-supported topology
candidates and their limits. Single-local-space success is not sufficient to
claim full multi-body support. Establish that the selected TGC topology has a
viable extension path before committing it as the general architecture.

P4: declared rotating-frame force/velocity semantics, awake rigid bodies,
constraints, navigation, networking/rewind, presentation history and required
performance envelope. Use native capabilities first and record any demonstrated
adapter gap with its owner and removal condition.

These are proof identifiers in this document, not replacements for the broader
PlanetDesignSystem P0-P6 milestone names. Implementation should begin only after
this concrete scope has been reviewed; this change set contains documentation
and source audit data only.
