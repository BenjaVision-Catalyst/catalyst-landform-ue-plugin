# Planet editor — Landforms

12 November 2026. User correction: there is one editor, **Planet**, where the world
is assembled. Standalone Landform/source editors are retired. Terrain assets are
source definitions; opening one focuses Planet's Landforms tab without applying
or replacing it. The obsolete window menu entry and Rift source UI are removed.
Legacy console/API names route into Planet.

Each catalogue or applied Landform occupies one row. The text cell contains its
name and brief description; the same row contains:

| Control | Effect |
| --- | --- |
| Checkbox | Include in generation. Off disables Character, eye, reseed and Solo. Settings are retained for re-enabling. |
| Character | Change this planet's applied family Character, with one Undo per drag. |
| Eye | Include/exclude only in the current preview; generation selection remains saved. |
| Reseed | Change this planet's deterministic local variation. |
| Solo / Unsolo | Apply this included Landform across every planet region / restore the assembled preview. |

Eye and Solo are editor session state. They do not dirty the planet or mutate
source definitions. Solo temporarily reveals a hidden selected row and restores its eye state on
Unsolo. It temporarily excludes
other Landforms, including manually placed instances. Base geology, planet scale,
radial guides, Rock and Caves remain supplied by the planet. Normal generation
continues to use recorded geological placement and relaxation metadata. Solo is
an explicit compiler evaluation option, outside the recipe and JSON format;
it overrides region assignment in the same stock native VPP pipeline. Changing
that option invalidates prepared input reuse where necessary.

The catalogue still contains sixteen neutral province definitions awaiting their
terrain pass. Their controls retain planet-local settings; including or soloing a
placeholder cannot create terrain that has not yet been implemented. Rift uses
its existing physical Character and seed. Generic authored noise Landforms use
Character to scale their local relief around the unchanged default and a local
seed offset; the original noise snapshot remains intact.

`FCLPlanetLandforms` owns row actions, validation and preview composition.
`SCLPlanetLandforms` supplies the native Slate rows inside `FCLRecipeToolkit`'s
Landforms page. No external source editor is used for acceptance. Current review
must show the assembled Planet editor and exercise row disable, Character,
reseed, eye and whole-planet Solo, plus unchanged generation/serialization.

UI qualification captures use the ordinary whole-planet camera, so Solo's global
coverage and the assembly window are visible together. The earlier close-camera
harness exceeded its 180-second limit while cycling the current 2 km review
planet. That remains a close-view rebuild performance limitation; it is not a
claim that the revised UI fixes close-view generation time. CurrentRift's existing
native cave publication/navigation/0.5 m zoom checks retain their own budgets.

Saved Rift profile 1 keeps its normal generation field pinned. An explicit Solo
preview additionally delivers its regional relief into the six spherical fields;
the saved-profile regression disables fault throw to verify actual height changes.

CurrentRift cave inspection passed twice earlier in this change set, but two later
runs exceeded its existing 90-second readiness budget. This remains an unresolved
native readiness/performance gap; the UI work does not claim reliable cave timing.
A closer-view navigation run also measured 72.6 ms p95. Whole-planet UI navigation
uses the same camera as its captures (zoom 0.7) and retains the 50 ms p95 limit.
