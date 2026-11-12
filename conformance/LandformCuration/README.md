# Planet disclosure and curated Rift — review evidence

12 November 2026. UE 5.7, macOS arm64 Development, stock VPP 2.0p8.
This implements the user's request for editable inline values and a useful,
landscape-fitting Character range. It does not establish visual approval of the
Rift province or complete its remaining anatomy/stamp work.

## Change

Planet is the only editor. Included Landform rows have a disclosure triangle and
native property editors over the planet's applied snapshot, with validation and
Undo/Redo. Disabled rows disable the disclosure and its values. Identity,
algorithm and duplicate Character fields are protected/hidden. Rift distances
are displayed in metres; its own seed is shown instead of the unused generic
seed. Source assets are not edited by row actions.

Rift profile 3 uses spacing 440→180 m, throw 8→28 m and scarp width 26→8 m
across Character, with a gentler shape/noise expression. Manual ranges can exceed
these defaults. A subsequent Character gesture restores the curated physical
ranges. Saved profiles 1/2 remain pinned until an explicit edit. The new candidate
source is schema 12; the canonical catalogue remains unchanged.

The physical contribution is added after the geological base's height calibration
and elevation distribution. It no longer refits the full radial interval after
adding a local province. Conservative bounds include local relief. Structural
Rock restoration uses the same displacement; native VPP retains meshing, LOD,
materials, collision and edits. Existing authored base geology can still produce
large hills when Rift is hidden; those independent inputs were preserved.

## Checks

`final-validation.json` records 16 passing tests, including the native landscape
capture. `labels-validation.json` checks the final row caption/tooltip and three
Character captures after that text change. `initial-validation.json` retains the
first 15-test run. `disclosure-and-caves.json`
retains the subsequent native disclosure/preview and CurrentRift qualification.
CurrentRift passed: 30 native domains, 12,669 visible debug pixels, zero pending
native tasks; close detail residual 0.217652 m at native cell size 0.6 m.
The previous readiness timing failures and close-navigation limitation remain
recorded in `conformance/PlanetLandforms`; this successful run does not erase them.

`staged-source.json` records exact source/example/tool parity with the built host.
`stock-vpp.json` rechecks the four frozen VPP source hashes. Native build logs are
retained. Tests and editor captures use full recipes with Caves; the isolated
height-delta test disables Caves and Watershed only to measure the terrain field.
That test samples 1,024 directions at eleven Character positions on the current
review base, both in normal placement and whole-planet Solo. Maximum changes were
4.199412 m and 4.795877 m respectively, below the 34 m physical envelope check.
Noise and structural contribution changes, Rock restoration, source preservation,
transaction undo/redo, placement relaxation, serialization, cave placement and the
enabled cave minimum are checked independently.

## Captures

`planet-0.png`, `planet-1.png`, `planet-2.png` are actual assembled Planet renders
at Character 0, 0.5 and 1, with unchanged base geology and Caves. The middle capture
opens the disclosure. `landscape.png` is a closer native viewport capture
at Character 0.5 on that same assembled planet. It shows a still smooth surface;
it is not a claim of finished reference-quality Rift anatomy. `planet-3.png` shows whole-planet Solo; `planet-4.png` shows
Rift hidden. These use the ordinary orbit camera, not a separate demonstration.

The existing review asset is retained at
`/Game/LandformDesigner/Provinces/DA_ContinentalRiftFamilyReview`. Its explicit
adoption of the curated physical ranges preserves Character, seed, noise,
placement, guides, Caves and all other settings. Its exact JSON field diff and
other authoring-asset checksums are retained with this checkpoint. No commercial
stamp packages are included.

The live saved Planet row was also opened by keyboard, Maximum Throw changed
from 28 m to 40 m, and the native editor transaction undone back to 28 m. The
complete live recipe then matched the expected curated snapshot exactly. The
review package was explicitly saved (including when its dirty flag was false);
all other 107 authoring assets retain their original hashes. The editor is left
on Planet → Landforms with Rift expanded and no pending authoring changes.
`live-review-check.json` records the retained Character and seed.
