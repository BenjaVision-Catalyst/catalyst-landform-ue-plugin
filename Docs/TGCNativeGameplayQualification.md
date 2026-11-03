# ThatGame:Catalyst: actual ACF + native VPP qualification

4 November 2026. The user selected an isolated `TGCGameplayProof` host for the
first combined qualification. This record concerns actual ACF actors and native
VPP terrain. It does not certify TGC's complete settlement slice. Shared
multiplayer interaction, server restart, configured attack/response assets and
the other open frame gates remain mandatory.

The host is at `Landform/Projects/TGCGameplayProof` in the workspace, alongside
LandformLab. It uses UE 5.7.4 CL51494982, VPP 2.0p8 and licensed ACF 4.2.3 on macOS
arm64. The [machine record](../conformance/tgc-native-gameplay-mac-arm64.json)
retains source hashes, original failures, corrections, test scope and results.
The host README supplies exact replay commands.

## Native ownership and selected profile

VPP remains the sole voxel generation/presentation platform. The existing
Landform spherical recipe adapter submits a natural-curvature, solid 100 m body
through native volume layers/stamps, native materials, Nanite/LOD and attached
native moving collision invokers. The native world/runtime is created once and
retained through gameplay and real edits. No new moving window, terrain cache,
streaming manager, custom collision solver or voxel preview path is introduced.

The selected character is an owned copy of native `ACFPlayerCharacterBP`, using
the native mannequin and unmodified `ACF_SimpleTemplate_ABP`, with exactly
`UACFCharacterMovementComponent`, native ACF ASC/GAS statistics, team manager,
collision manager and GAS damage calculator. Native `GA_ACFJump` is granted and
triggered through the native ability API. The harness supplies radial gravity
selection to UE's existing custom-gravity API; it does not integrate movement or
replace ACF's class. One character is locally possessed; two more native ACF
characters serve as target and drop witnesses. They are not two network clients.

The installed ACF character already has a VPP `SetBase` adapter which selects
the native voxel world's root. It is retained unchanged. The tests require that
base on every walking sample; no parallel base adapter was needed.

The stripped host initially lacked the editor PropertyAccessNode dependency.
Enabling that native plugin restores the original ACF animation Blueprint. An
experimental graph-repair helper was discarded; there is no shipped custom
animation graph or editor repair module. AnimationData is enabled for the supplied pose assets’ serialized data models
during cooking. EnvironmentQueryEditor is enabled for
the supplied native assets' editor imports. VPP’s dynamically loaded `VoxelWorldGridMaterial` is included through the existing
native UE cook label. Packaged reports use the established macOS app-container
path. These are fixture/dependency corrections,
not missing animation/generation features.

## Demonstrated native gaps and bounded corrections

Only the isolated host's ignored local ACF copy is corrected. A source-pinned
staging script reconstructs it from the licensed installation, refuses edited or
additional local staged files, and records before/after hashes. The installed
plugin and its content are not patched or redistributed. This is a qualification
patch awaiting an upstream/native resolution; it is not a general ACF fork.
Remove each staged correction when a pinned upstream release passes the same
checks without it; re-audit version/source pins before upgrading.

| Native measurement | Executed original failure | Narrow correction |
| --- | --- | --- |
| Airborne `GetGroundInfo` | Sideways ACF reports its 3000 cm trace limit while an independent gravity-aligned terrain trace is about 627 cm; maximum error about 2982 cm | Use existing `GetGravityDirection()` for the native trace endpoint. Preserve native caching, queries and movement. |
| Jump/fall animation state | Sideways native GAS jump becomes airborne, but descending drop samples never set the native falling state | Project velocity onto gravity-up before the existing native thresholds/apex calculation. Preserve state transitions and native animation assets. |
| Animation locomotion direction/displacement | Diagonal sideways walking shows about 45° direction error and 102 cm/s displacement error against independent tangent-plane witnesses | Project onto the gravity plane; give native `CalculateDirection` actor-local inputs. No replacement animation logic. |
| Fall height/damage | Actual sideways fall completes, but native world-Z height calculation applies no damage | Retain a fall-start position and project displacement onto gravity-up. Keep native threshold/curve, authority, damage calculator, GAS accounting and landing actions. Rebase the cached position through native actor `ApplyWorldOffset`. |
| Animation cached position at a live origin shift | Relative positions/contact remain coherent, but the cached pre-shift native location produces a 1.96–3.52 million cm/s animation displacement spike | Track UE's integer origin beside the native cached position and remove its translation from the next delta. Initialize the cache from native actor/world state. |

The final failure-control run rebuilds unmodified pinned ACF and requires the
ordinary sideways assertions to fail, with the known ground, animation and damage
measurements and verified cleanup. It then restores and rebuilds the corrected
source. Preliminary runs which failed to build, load a profile or finish a cold
asset-registry scan are not counted as passing qualification cases.

The corrections deliberately do not generalize untested climbing, aiming,
turning, root-motion, swimming, ragdoll or cross-frame combat behavior. A stable
body frame cannot make those plugins' world-up assumptions disappear; each
selected feature still needs an executed profile.

## Executed workload and budgets

Each case admits movement after initial native collision readiness, then performs:

1. Native ACF diagonal walking on actual native VPP collision, checking the
   native movement class, voxel floor, VPP root base, capsule-up alignment,
   tangent animation direction and displacement.
2. Native GAS jump and return to contact; an independent native ACF drop tests
   airborne ground distance, native animation state and native fall-damage/GAS
   accounting. Live-origin cases separately shift UE's origin during this fall,
   retain both standing contacts and check cache continuity. No midgame pause or
   world/runtime recreation is used.
3. Native ACF swipe hit selection, rejection of friendly damage, then hostile
   native damage and equality between final damage and GAS health change.
   These are unarmed native trace calls, not a montage/weapon-combo qualification.
4. A native subtractive sphere stamp produces an approximately 49 cm cut while
   characters remain active. The ACF character walks more than 4 m across the
   edited region using the same native invoker and retained native world.
5. A native footing ray places a native static mesh structure. UE SaveGame writes
   and reads a body-relative record; VPP's existing `FVoxelStampRef` serializer
   preserves the actual native stamp payload. Only the private activation-local
   layer reference is rebound. The structure/stamp actors are reconstructed,
   while the native world/runtime stays alive. Restored collision must match
   within 1 cm, and the structure must have a real mesh and blocking physics.
6. A three-second diagnostic celestial query advances while the restored static
   structure retains its transform and ACF remains grounded. This is a consumed
   numeric witness, not a production celestial provider or presentation proof.

Walking requires at least 30 floor samples and 2 m travel, every sample based on
the native VPP root. Animation direction must be within 1°, displacement within
5 cm/s, airborne ground query within 1 cm. There must be more than 20 actual
jump-air frames and native jump/fall observations; native health/final-damage
accounting must agree within 0.01. Live origin keeps relative positions within
0.01 cm, at least ten standing-contact samples and no animation spike above
50 cm/s during the radial drop. Terrain-root drift outside the explicit origin
translation must remain below 0.001 cm. The final static structure must remain
within 0.001 cm while its diagnostic universe query travels over 1 m.

The native world/runtime and every attached invoker retain identity. A registered native
VPP Nanite mesh is required; clean native logs are mandatory. Actual edit
registration and saved-actor reconstruction are allowed mutations, not celestial
rotation work. The fixture has a 110 s runtime cap, a 15 s edit/restore readiness
cap and a 150 s process cap. Cleanup requires positive actor destruction,
native runtime retirement and deletion of the unique native SaveGame slot.

## Results and limits

All **16 cooked matrix cases plus one focused cooked pilot** passed on the same
fully cooked executable. The final editor cases passed at north/120 fps and
sideways/translated/live-origin/30 fps. The unmodified-native-source failure
control reproduced the expected gaps and verified cleanup; the corrected source
was restored and rebuilt before the final editor cases.

The 17 cooked cases recorded 3,940 native walking/base
samples and 1,416 airborne ground probes. Every case had
at least 322 registered native VPP Nanite meshes. Maximum
native ground-query error was 0 cm, direction error
1.60538e-07 degrees and animation displacement
error 0.00852603 cm/s. Maximum post-origin
animation displacement during the radial drop was
4.35362e-10 cm/s.

All 8 cooked live-origin cases retained relative positions
with maximum error 3.88536e-12 cm and recorded
1,112 standing-contact witnesses. Restored cut
collision, final static-structure position and terrain-root position each had
maximum error 0 cm.
Native fall-damage/GAS accounting passed in every case; one critical fall was
observed and remained nonlethal under the calibrated native curve. Swipe critical
hits were not observed and are not a separate qualified branch.

All cases verified cleanup and had zero native errors/ensures. The full cook
completed with zero errors and 30 reported warnings; runtime cases retained their
actual warning counts (including native sample configuration/deprecation
warnings). All 272 protected existing files and 15 selected installed-native
source/binary fingerprints remained unchanged. No installed ACF asset was edited.


The shipped sample profile has unresolved references to the absent FullSample
project and unset scaling-curve references. Its native initialization gives
50 health in this profile. Warning counts are retained; this is not a zero-warning
sample-game or TGC balance certification. Optional hit/landing action bindings
are not supplied, and their warnings do not establish those features as working.
An earlier matrix run exposed a lethal native fall against the original
50-health fixture; native critical damage can exceed that budget. The final native fall curve maps 1000 cm above the existing
threshold to 50 damage rather than 100, making the bounded drop survivable while
retaining native critical damage. The missing sample death-XP curve and lethal
combat/death lifecycle remain open configuration/qualification gates.

Native damage variation/mitigation is retained; final damage is checked against
actual GAS health change, not forced to a hard-coded result.

Persistence here is a same-process disk save/load and actor reconstruction test.
There is no production construction framework, undo/ownership policy or server
restart guarantee. VPP supplies native sculpt save APIs; this workload edits a
shape stamp and reuses its native serializer rather than inventing sculpt storage.
Body-relative transform storage/layer rebinding belongs to game persistence
policy; VPP does not choose a game's body identity or private layer activation.

The next bounded gate is a real listen/dedicated server plus remote ACF client:
native based movement/prediction, authoritative damage, native stamp replication,
shared structure placement and persisted state after restart. Audit native ACF,
VPP and UE replication/save features before proposing adapters. Do not replace
those with two locally spawned actors or a new streaming manager.

Enhanced-curvature presentation, physical body rotation/non-inertial forces,
concurrent independent gameplay frames, frame transitions, authority/time/rewind,
scientific datum ingestion, navigation, voice/social audio and full asset/game
presentation remain outside this qualification. The earlier body-fixed idle and
live-origin presentation records retain their separate scopes. The combined TGC
acceptance gate is partially exercised, not closed by this bounded host.
