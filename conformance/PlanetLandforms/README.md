# Planet Landforms UI review

12 November 2026 · UE 5.7 · macOS arm64 Development editor.
[Control contract](../../Docs/PlanetLandforms.md). Planet is the only supported
assembly editor; standalone source windows and their menu/tab entry points are
retired. Definitions remain reusable data assets.

Actual native Planet editor captures, using a copy of the current review recipe:

| View | Capture |
| --- | --- |
| Character 0 | [Planet](planet-0.png) |
| Character 50 | [Planet](planet-1.png), [rows](controls-1.png) |
| Character 100 | [Planet](planet-2.png) |
| Whole-planet Solo | [Planet](planet-3.png) |
| Eye off, generation still included | [Planet](planet-4.png) |

`ui-result.json` records five successful final UI cases: row inclusion/disabled
controls, one-Undo gestures, reseed, preview-only eye/Solo, restoring eye state,
live custom-source rows, serialization, retired editor routes/tabs, generic noise
controls, native province UI/navigation, and actual Rift Character/preview modes.
`native-review.json` reports 22.17 ms p95 at the captured whole-planet camera
(zoom 0.7); the limit remains 50 ms. No vendor quality or chunk-size defaults were
changed. `preview-modes.json` records native publication for Solo and eye off.
These are UI/delivery checks, not geological visual approval or close-up fidelity
qualification. Sixteen catalogue provinces remain neutral planned definitions.

`broad-result.json` and `repeat-result.json` retain unsuccessful runs as well as
passing field checks. A preliminary navigation harness counted waits for native
LOD work as frame time; this was corrected. A real closer-view (zoom 0.35) run
measured 72.56 ms p95 and remains outside this UI camera qualification.

CurrentRift cave inspection passed twice earlier in the change set
(`earlier-cave-results.json`), then exceeded its unchanged 90-second readiness
budget in both retained later runs. All 30 cave domains had generated, but native
tasks were not settled. Cause is not established. The UI work does **not** claim
reliable cave readiness or resolve close-view rebuild latency. No cave morphology
or VPP source changes were made. The failed runs must not be described as green.

`authoring-before-sha256.json` / `preservation.json` record 108 unchanged authoring
assets and rechecked stock VPP source hashes. `source-sha256.json` identifies the
staged source bytes; `ubt.log` records the successful native build. Command files
retain exact automation filters and project paths.

`saved-profile-result.json` records four successful checks after the last
preview-delivery change, including actual spherical height effects for saved
Rift profile 1 in Solo, row state/serialization/custom insertion and the native
province window. Normal saved-profile generation remains pinned.
