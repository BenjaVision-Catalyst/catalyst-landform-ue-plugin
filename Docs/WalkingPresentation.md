# Planet walking presentation

Landform owns the existing-world inspection host, VPP character, 20 m/s radial
movement and shared terrain/scenery/camera presentation. Walking starts from the
Planet editor's player marker in a centered 2560 × 1440 client window.
`landform.WalkingPreviewWidth` and `landform.WalkingPreviewHeight` configure
the next launch without rebuilding; startup sizing verifies the requested pixels.
The editor world and its unsaved recipe, guides, instances and materials remain authoritative. PIE duplicates that world.

## UE 5.8 integration

The render module's build rules stage a version/hash-checked copy of local UE
5.8.4 Nanite shaders into the host's ignored `Saved/NaniteProjection` directory.
Engine and VPP files are read-only. Unknown source or edits to a generated overlay
are refused. The runtime module registers the isolated shader directory before
shader compilation; cooking and execution must use the same projection setting.
`[CatalystLandform.Presentation] bEnableProjectionShaders` defaults to true.
No personal checkout, Lab asset or host C++ adapter is required.

Walking uses `FCLPlanetProjection` at curvature **0.05**: the spherical carrier
has 20 times the physical reference radius, while radial terrain relief stays
1:1 and canonical field/collision/gravity remain unchanged. This retains a real
horizon and extends the eye-level horizon distance by about 4.47 times. The
walking chart admits directions within 89 degrees of its moving anchor; it is
still finite. Walking admits native chunk/scenery boxes from 0.1 to 2 times
the landing reference radius so coarse boxes enclosing mountains are not
rejected by a narrow surface shell. This is render admission, not a terrain-height
change. `landform.WalkingCurvature` supports a smooth two-second change
from zero (diagnostic tangent plane) to one (natural curvature).
Current and previous packets drive WPO, normals and conservative projected
Nanite instance, node and tessellation-patch bounds. The split stage projects
the same base triangle vertices as the raster domain before patch interpolation,
culling and dicing, retaining native tessellation/displacement. Bounding boxes
use a conservative sphere envelope when projection rotates source axes.
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
An early `OnExit` ticker cleanup was insufficient. On this editor/version profile,
the PostConfigInit render module releases remaining ticker captures during its
late module retirement, after editor/Slate modules and before ICU teardown. It
uses public ticker reset APIs only when the engine is exiting; live-session and
hot-unload tickers are untouched. Normal GUI quit with a deliberately late
notification exited with code 0. The development command
`landform.VerifyLateSlateNotification` and fixture
`Tools/tests/fixtures/verify-graceful-shutdown.py` reproduce that path. Automated
`-TestExit` uses forced exit and does not qualify graceful shutdown.

The installed existing-world follow-up passed private checkpoint save after
restoration and normal GUI quit both before and after walking (exit 0). The raw
no-sky/no-cloud/no-fog walk contains 26,734 unchanged canonical instances, including
12,509 grass clumps and 2,565 wood anemones; all 212 scenery and 205 visible terrain
packets match. Movement reached 20 m/s over 38 grounded/presented samples, followed
by Escape restoration. All 26 protected original file hashes remained exact.
The VPP startup ensure and finite-chart/runtime qualification limits remain open.

## Gentle vistas and tessellation qualification

The [vista record](../conformance/walking-vistas-mac-arm64.json) supersedes the
earlier flat walking default. The installed existing-world walk reproduced the
black diamond holes and isolated them to the tessellated software raster path.
The split stage had been omitted from projected culling: canonical patch bounds
and dicing factors disagreed with material-projected raster vertices. The
isolated overlay now includes that stage; native tessellation, software
rasterization and normal HZB/frustum culling remain enabled.

Fifteen rendered regressions and eight installer/overlay tests passed without
test warnings. A 77 m vista route captured 13 normal frames with zero dark
terrain-hole pixels in the audited region (5,018 before), plus a natural/gentle
curvature comparison at one canonical viewpoint. The forest retains all 26,734
instance transforms, 212 scenery packets and native leaf-litter texture bindings.
A grounded forest walk reached 20 m/s. Both walks restored the editor state on
Escape; private checkpoint and normal quit exited 0 with all 26 protected disk
hashes exact. The existing world reopened successfully. No sky/cloud background,
fog or haze is rendered. The VPP startup ensure and finite-chart/runtime limits
remain open.

## Distant coverage and 4K walking window

The [installed 4K record](../conformance/walking-distance-4k-mac-arm64.json)
qualifies the 89-degree cap and wider radial bounds. At the captured existing-world
location, the old policy admitted 177 of the same native generations; the new
policy admits and renders 189. VPP's LOD quality, chunk size and unlimited maximum
LOD remain unchanged. Terrain height and curvature stay at 1:1 and 0.05.

Native walking launch requests 3840 × 2160, then measures and corrects the
viewport's rendered pixels during startup. This handles the observed Mac
3840 × 2180 result and DPI/title-bar differences without a fixed platform offset.
The correction stops when matched, when walking returns or at its retry/time
limit; subsequent user resizing is allowed. Both the viewport API and five
regular walking PNGs measured exactly 3840 × 2160. The 47 m route remained
presented across 49 samples, including a terrain drop; the inspected ground
regions contained zero black hole pixels. Sixteen rendered and eight installer/
overlay checks pass. Escape restores the existing editor world and scoped
rendering state; all 26 protected original hashes and 26,734 forest instances
remain intact. The chart remains finite; no sky, fog or haze is added.
