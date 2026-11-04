# Preview navigation and progress

5 November 2026. Corrects the background-preview UI introduced in `6008f53`.
The requested behaviour is ordinary responsive globe navigation and property
scrolling, whether Watershed is enabled or disabled.

## Demonstrated problems

The prior authoring indicator used native VPP readiness for every camera-driven
LOD state. In the single-editor, Watershed-off reproducer, 179 of 180 moving
frames showed authoring activity although there were no recipe evaluations or
new native recipe inputs. The same readiness test refused the initial left
button press, so a new orbit could be blocked immediately after zooming or while
a derived field was being evaluated.

`FCLNativePreview::Poll` also copied every published native vertex and triangle
for test inspection, including publication after camera LOD changes. Normal
presentation never rendered that copy: VPP's mesh components already owned the
visible geometry. The bounded baseline copied 73,308 vertices once as navigation
settled, taking about 2.46 ms. This was unnecessary work, not evidence that it
explained all reported lag.

Four older review editor processes were initially active, using roughly 44–106%
CPU each at inspection. The user closed all of them before the controlled runs.
Both baseline and fixed measurements use one editor; no measured four-versus-one
rendering speedup is claimed.

## Native correction

VPP continues to own generation, LOD, meshes, materials and collision. Polling
observes native mesh identities and publishes readiness without copying geometry.
Dense inspection is now explicit and lazy, cached by native publication identity.
Its one-million-vertex inspection bound no longer limits ordinary presentation.
Existing native geometry/material tests still inspect the actual VPP output.

Authoring activity is tied to pending compiled input and its native publication.
Ordinary camera LOD never shows the authoring indicator. Terrain picking retains
its native readiness and stale-input fences, while camera capture/orbit is allowed
during LOD work, field evaluation and invalid authoring states. A click that was
inadmissible for terrain picking cannot change selection on release.

Progress uses installed VPP's `AVoxelWorld::GetProgress()` for an applied native
input, plus its pending-task count. A queued input never displays an older LOD
state's percentage. Derived native-height sample batches and the eight extrema searches expose
completed/total counts for their current pass. Nested probes retain their parent
search progress instead of resetting the bar for every tiny batch. The standard UE bar is determinate when that
measurement exists. Unmeasured preparation, finishing or publication phases show
stage text; there is no animated marquee or fabricated overall percentage.
Pass-local counts may restart when the next measured pass begins.

## Qualification

The [native record](../conformance/PreviewNavigation/qualification-mac-arm64.json)
contains the baseline, globe zoom/orbit timings, expanded Geology-tree scrolling,
actual routed camera capture during busy/stale states, retained native world and
recipe identity, explicit inspection/material checks, worker parity and walking.
Frame timing includes rendering and scheduling. The record does not claim a
universal frame rate or a large rendering speedup from removing inspection copies.
Both original workspace inputs and saved review-host Content/Config are protected.

Review in the existing `/private/tmp/landform-background/LandformLab` host. Keep
one review editor open. Disable Watershed; zoom/orbit the globe and scroll the
expanded Geology fields. These actions must not show authoring progress or change
the recipe. Change a setting to see measured evaluation/native generation progress.
The [background evaluation contract](BackgroundEvaluation.md) still owns edit
coalescing, cancellation, Undo and retained terrain after derived failures.
