# Balanced province allocation — 13 November 2026

The previous selector compared candidates independently at each cell. A source
with consistently better scores could take every cell, excluding another source
that the user had included. Selection now reserves equal cell shares first and
optimizes the geological fit of the complete assignment within those shares.

Every included source gets at least one cell. Without manual assignment overrides,
counts differ by at most one. A single source fills the planet; Rift plus Volcanic
arc divides 24 cells 12/12, 25 cells 12/13, and 128 cells 64/64. Character does not
control coverage. Strict matches are preferred, then earlier relaxation tiers,
then the existing terrain preferences, with deterministic seed-based tie breaking.
This changes allocation, not the underlying geological evidence or cave shapes.

Manual overrides stay locked. Excess locked coverage is respected while the
remaining capacity is shared fairly. Contradictory overrides that leave no room
for an included source are rejected. If 17 sources are selected with an authored
count of 8 cells, generation resolves to 17 cells, one for each source; the authored
setting is retained.

`functional-result.json` records native UE 5.7 Mac Development regressions in the
normal workspace LandformLab project. They include deterministic replay/source
reordering, another seed, odd and maximum cell counts, all 17 selected sources,
locked overrides, full coverage, unchanged rock identity, geological placement
preferences, native field parity and enabled cave minima.

`rendered-result.json` records the actual Planet row-control test on a transient
copy of the current unsaved `DA_BasePlanet`. `rift-only.png` shows Rift on all
24 cells. `rift-plus-volcanic.png` shows the same planet after including Volcanic
arc, with 12 Arc cells highlighted and 12 retained Rift cells. The test verifies
both counts and exact info-overlay membership against the published native plan.
`cases.json` also includes mixed placement, info off/on, Solo and Arc alone.
These captures demonstrate allocation and highlighting, not visual approval of
the landform morphology.

`build.log` records the successful native build. `preservation.json` records exact
unsaved-authoring restoration and unchanged project Content files. No planet,
guide-library or source assets were saved during testing.
