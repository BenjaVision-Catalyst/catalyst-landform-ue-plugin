# Body-fixed idle gameplay proof

4 November 2026. This qualifies the bounded P0/P1 diagnostic slice from
[the frame design](BodyFixedGameplayFrames.md), on UE 5.7.4 / VPP 2.0p8,
macOS arm64 Development with native Nanite/Metal. It does not qualify ThatGame:Catalyst,
ACF, a production frame authority, or physically correct rotating-frame dynamics.
The [qualification record](../conformance/body-fixed-idle-mac-arm64.json) pins
sources, binary, cases, controls, preservation checks and raw report hashes.

## Ownership and the demonstrated specialization

VPP remains the only terrain generator/presenter: its native layers, volume stamp,
world/runtime, materials, LOD, collision and attached moving collision invoker are
retained. No VPP/engine patches, shaders, assets, production movement or generation
code were changed for this proof. Ordinary UE box bodies and a fixed constraint
supply the physics fixtures; their visual cylinders are ordinary engine meshes,
not another terrain path.

All new code is diagnostic host code. The existing `FCLBodyFrame` provides spatial
metre/centimetre mapping. A private test sample adds explicit body/frame/parent,
authority/revision, sample time, signed-axis convention, celestial pose and linear/
angular kinematics. Native rigid transforms alone do not encode those semantics.
This is a test-provided value snapshot validated on each conversion, not a new
solver, ephemeris service, streaming manager, persistent recipe or public API.
The real body/physics authority remains an integration gate.

The installed `VoxelWorldRootComponent` explicitly defaults to Static, with a
source comment connecting that choice to native movement-base behavior on planets.
UE's component move guard refuses a static root move in a game world. The bounded
negative-control correction confirmed that protection; it does not establish
support or cost for continuously moving/rotating a VPP world.

The conversion reuses UE double vector/quaternion math. It includes origin
velocity, `omega cross r`, Euler, centripetal and Coriolis terms, and the determinant
sign when mapping axial quantities across a reflection. Native local state uses
centimetres and seconds; universe state uses metres and seconds. The fixture's
numeric axis convention is explicit; this is not scientific CRS qualification.

## Executed slice

The kinematic oracle runs in an editor test and in every cooked case. Its 62 checks
include independent analytic position/velocity/acceleration cases, fixed and large
angles, translated centres, both axis parities, arbitrary anchored local bases,
finite differences and fail-before-output-mutation controls for missing identity,
epoch, kinematics, rigid rotation and invalid axis profiles. Native positive-Z
quaternion angle direction, independent orientation triads and angular-direction
finite differences are separately checked in the editor.

Budgets: position round trips 1 mm, velocity 0.01 cm/s, acceleration round trips
0.01 cm/s², angular velocity 1e-8 rad/s and quaternion components 1e-8. Analytic
acceleration uses 1e-8 m/s²; independent finite differences use 1e-6 m/s and m/s².
These are diagnostic budgets, not game-wide physical accuracy guarantees.

The cooked fixture uses one native solid 100 m body at natural curvature, a static
building fixture, one independent dynamic box, two dynamic boxes joined by a
native fixed constraint, a grounded proof character and a fixed local camera.
VPP collision covers every resting fixture through the character's attached
invoker. Boxes use native local world-Z gravity near the north pole; the character
uses the existing radial adapter. This deliberately prescribed local gravity
profile does not simulate celestial fictitious forces.

After native contact, pending tasks and all three bodies have settled, a three-
second quiet window precedes 30 measured seconds. Warmup fails after 60 seconds;
the process is capped at 120 seconds. No body is forced asleep, no motion is reset,
and generation is not disabled to pass the measurement.

Matched fixed-pose and advancing-pose cases use the same binary at 30/120 fps caps,
with origin and `(50000,-30000,10000)` cm initial placements: eight cases. The
prescribed celestial angular rate is `7.292115e-5 rad/s` about the sample Y axis.
A stationary body point has changing universe coordinates/velocity; a separate
non-terrain UE marker consumes its reduced-universe display position. An
independent trigonometric oracle checks that query and the actual marker transform.
The diagnostic elapsed time comes from the host monotonic clock, not a production
simulation-time provider.

Every measured frame checks local pose, native sleep, constraint integrity,
grounded native floor, runtime/stamp/sampling/invoker identity, native mesh and
material identity, fixed terrain/building transforms and camera pose. Native
transform notifications, invalidation and Nanite ready/retire events are counted.
Every quarter-second sample checks native terrain under the character and each
box, nearby box footing, an identity-chart native pick and the known radial field.
Raw traces retain universe values, marker position, sleeping-body count, constraint
state and lifecycle counts. The settled screenshot is a fixture reference, not
GPU temporal/pixel-material qualification; the marker is checked numerically.

## Results and limits

All eight cooked cases passed, with 17,076 measured frames, 925 native picks
and 3,700 resting contact witnesses. Every case recorded zero measured
static-transform notifications, terrain invalidations, Nanite ready/retire events,
wake/sleep events and local pose/velocity drift. Advancing-pose markers travelled
over 20 cm; fixed-pose markers remained fixed. Maximum native radial field error
was 0.03125 cm; maximum pick ray error was
0 cm. All cases verified native cleanup.

The focused editor suite passed 61 tests with zero automation warnings. The
three negative controls were recognized, and the static two-body and separate
live-origin presentation regressions passed on the same cooked binary. All
145 protected files and 270 asset/config fingerprints were unchanged.

Three cooked negative controls must fail through the fixture's ordinary checks:
unused celestial pose; a ten-centimetre terrain-root move; and omitted rotational
velocity. The runner accepts each only with the expected diagnostic and verified
cleanup. Native UE first refused the initial root-move injection because the
root was static; that preliminary control was invalid. The final control explicitly
opts into native root mobility before moving it. Normal cases retain the static
root. This is fixture setup, not a production engine/VPP fix. Cleanup removes observers,
requires actor destruction/component unregistration, checks native physics/constraint
state where the components remain queryable, and confirms native runtime retirement.
Installed UE source unregisters components and destroys physics state before marking
them garbage. This checks ownership retirement, not allocator/leak certification.

The existing static two-body transfer and cooked live-origin presentation cases
are rerun on the same binary. Live origin rebasing remains a separate qualification;
these idle cases do not mix it into celestial rotation.

ThatGame:Catalyst's combined ACF+VPP gate remains mandatory and **unqualified**.
The next bounded work is to pin its intended runtime/assets and exercise actual
ACF movement/combat/animation with native VPP, including enabled non-Z-up paths,
terrain edits, persistent structures and shared interaction state. The proof
character cannot substitute for ACF. Concurrent independently rotating gameplay
spaces, authority/time/rewind, cross-frame attacks, reanchoring and non-inertial
forces remain open. Local idle success does not select a multi-body topology.

## Replay

From `Landform/Projects/LandformLab`, build the pinned arm64 editor target and run
`LandformLab.Frames.Kinematics` plus the focused existing suite. Then:

```bash
python3 scripts/package-runtime-recipe.py --scale-lab --archive "$PWD/Saved/Packages/FixedFrame"
python3 scripts/qualify-fixed-frame.py --archive "$PWD/Saved/Packages/FixedFrame" --output "$PWD/Saved/Verification/FixedFrame/repeat"
```

The recorded build used guarded `--skip-cook` for this code-only change: shader/
material fingerprints matched the prior successful cook. Use a full cook when
those inputs change. Add `--fps 60 --offset origin --pose advancing` for one case.
Use `--control unused-pose`, `--control moving-root` or `--control missing-omega`
for the bounded failure controls. Output paths must be new directories.

## Subsequent actual ACF qualification

The [bounded TGC native gameplay profile](TGCNativeGameplayQualification.md) now
qualifies selected actual ACF movement, animation, GAS trace damage and native
VPP editing alongside UE structure/stamp save-load. It includes separate cooked
live-origin cases and source-pinned corrections for demonstrated ACF measurement
gaps. It does not close complete TGC multiplayer, persistent-server, combat-asset
or multi-body frame acceptance. The idle proof above retains its original scope.
