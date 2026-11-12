# Assigned-cell info overlay — 13 November 2026

Native UE 5.7 Mac Development build and Planet editor captures in the normal
workspace `Landform/Projects/LandformLab`, using a transient copy of the current
unsaved `DA_BasePlanet` authoring. No planet, guide library or source asset was
saved. The original planet and guide values were restored in memory, including
full-precision floats and source links.

The instruction banner and Preview's “Province cells” checkbox are removed.
Each included Landform row has an info toggle immediately after its eye.
Gold boundaries and centre markers identify its actual assigned cells; blue
globe lines remain independent. Excluded rows disable the info toggle.

| Capture | Highlighted cells |
| --- | --- |
| `info-off.png` | 0 / 24 |
| `mixed-info.png` | 9 / 24, Volcanic arc among the current included sources |
| `solo-info.png` | 24 / 24, Volcanic arc Solo |
| `single-info.png` | 24 / 24, only Volcanic arc included |

`cases.json` also records toggling info off again (0 / 24). The rendered test
checks exact cell membership, unchanged terrain evaluation count, native world
identity and mesh-copy count for info toggles, and unchanged original authoring.
These captures establish the overlay's rendered behavior; they do not constitute
user approval of any landform's visual morphology.

Placement now assigns every generated cell to an included source. A single
source covers the planet. Multiple sources prefer strict geological fits, then
ordered source-declared relaxations, then a closest-fit coverage fallback.
Relaxed-region budgets remain soft preferences. Source geological requirements,
evidence and canonical rock identity are retained. Invalid manual assignments to
the internal neutral entry are rejected.

`functional-result.json` records 15 passing regressions covering placement,
determinism, controls, installed Planet integration, source metadata, existing
landform families and enabled cave minima. `rendered-result.json` records the
passing native overlay test. `final-result.json` repeats the placement regression
after adding neutral-override rejection. `build.log` records the final successful
build. `preservation.json` records unchanged project Content files and exact
unsaved-authoring restoration. No morphology refinement was attempted here.
