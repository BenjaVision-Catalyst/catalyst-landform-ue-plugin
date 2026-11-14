# Planet walking presentation

Landform owns the existing-world inspection host, VPP character, 20 m/s radial
movement and shared terrain/scenery/camera presentation. Walking starts from the
Planet editor's player marker. The editor world and its unsaved recipe, guides,
instances and materials remain authoritative. PIE duplicates that world.

## UE 5.8 integration

The render module's build rules stage a version/hash-checked copy of local UE
5.8.4 Nanite shaders into the host's ignored `Saved/NaniteProjection` directory.
Engine and VPP files are read-only. Unknown source or edits to a generated overlay
are refused. The runtime module registers the isolated shader directory before
shader compilation; cooking and execution must use the same projection setting.
`[CatalystLandform.Presentation] bEnableProjectionShaders` defaults to true.
No personal checkout, Lab asset or host C++ adapter is required.

The existing `FCLPlanetProjection` chart maps curvature zero to a tangent plane,
preserving radial terrain relief and canonical field/collision/gravity. Current
and previous packets drive WPO, normals and projected Nanite culling bounds.
Rigid Planet rotation/translation and unit scale are supported. The native VPP
components retain their mesh/material/transform ownership; Landform observes
completed generations before camera evaluation and again before render submission.
Public component bounds provide the coarse envelope. No vendor lifecycle patch
is required. Recycled generations reset their projection history.

The editor host creates transient copies of the selected MegaMaterial and
scenery material hierarchies. It composes the existing terrain appearance with
`MF_PlanetPresentation`; geological surface IDs and forest-floor textures are
preserved. Scenery copies retain source parameters, normal maps and authored WPO,
then apply the same projection. Static and instanced-skinned scenery keep native
instance transforms, animation providers and collision. Projection packets and
render-only bounds are restored when their presentation owner retires.

The camera modifier publishes and consumes one chart in the same camera update.
It checks the inverse and canonical Planet pose. The inspection report includes
native binding state, scenery components, camera frames and inverse error.
Errors stop inspection instead of silently presenting an unflattened world.

## Walking lighting

The PIE sun and sky atmosphere follow the local tangent. The realtime skylight
capture tracks the presented player eye at 10 m intervals, allowing its
time-sliced updates to finish. The authored sun's
local elevation is retained. The walking copy disables exponential height fog, volumetric fog, atmospheric
aerial perspective and Mie haze. Its game viewport also disables atmosphere and
cloud background rendering, restoring the previous view flags on exit. Existing
sky capture illumination remains independent of the visible background. This exposes
the raw terrain/scenery rendering, including the finite chart boundary; there
is no fog veil, generated distant terrain or repeated vista. Authored
source-world lighting is untouched.
The outdoor walking camera uses a -0.75 EV exposure offset to retain sunlit
organic-floor colour, while the authored auto-exposure range remains active.
Cached lighting pre-exposure is scoped to EV10 for the outdoor exposure range and
restored on exit; the native presentation's supported direct-light/sky profile
scopes out canonical Lumen and distance-field representations.

## Qualification

This implementation must pass the shader-staging refusal/idempotence tests,
projection CPU/GPU tests, native inspection/marker tests and a rendered walk in
the existing forest world. A terrain-only image or successful movement does not
qualify scenery alignment, material coverage or lighting. Current/previous
projection packets retain motion history; packaged performance and broader
multi-chart content remain separate qualification gates.

The installed UE 5.8.4 Mac arm64 raw forest walk is recorded in
[`walking-presentation-mac-arm64.json`](../conformance/walking-presentation-mac-arm64.json):
11,660 canonical instances unchanged, 204 scenery and 205 visible terrain packets,
35 grounded/presented movement samples reaching 20 m/s, and Escape restoration.
Height/volumetric fog, aerial perspective and Mie scales were checked directly in
the installed walking world without test-script overrides. The raw distant chart
edge and a pre-existing VPP startup shading-pipeline ensure remain open. Fourteen
rendered checks passed; MaterialComposition retained 17 isolated fixture cache
warnings. Seven installer/overlay tests passed. This does not qualify packaged
performance or global/distant biome presentation.

## Recovery file readers

Before a recovery controller restores original package files, call
`CLInspectionRecoveryLibrary.detach_recovery_packages` with the exact recovered
package names while their checkpoint files are still active. The editor-only
API refuses PIE, empty/invalid paths and unloaded packages. It fully loads the
packages, finishes compilation, reads deferred thumbnails and calls Unreal's
`ResetLoaders`, then verifies that the readers are gone. It does not save assets
or clear their dirtiness. The controller must verify its manifest acknowledgement
before replacing files; original/checkpoint hashes remain separate evidence.

Restoring an 18 MB original map underneath a reader holding offsets from a larger
checkpoint caused seek-past-end crashes during private checkpoint/save/quit work.
Pre-saving thumbnails alone did not remove that reader. Engine and VPP source
remain unchanged. The two scripts under `Tools/tests/fixtures/*recovery-probe.py`
exercise the reader-release/save-after-restore boundary in an independent
`LandformWalkBuild` project with a deliberately enlarged package.

A second Mac 5.8.4 exit issue retained a deferred async-notification widget until
static ticker destruction, after ICU teardown (`ubidi_close_64` invalid free).
On this exact version/profile, the editor module repeats the engine's exit-time core-ticker reset on `OnExit`,
after the notification manager has shut down and before ICU teardown.
The engine's initial reset precedes asset shutdown, which can queue more updates.
This guard changes shutdown only; it does not discard live-session tickers.
The development command `landform.VerifyLateSlateNotification` deliberately
queues an async notification during `OnEditorPreExit` for process-exit validation.
