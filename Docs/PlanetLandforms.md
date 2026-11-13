# Planet editor — Landforms

## Current authoring decision — 12 November 2026

The user considers Continental Rift complete for now. Historical open visual
checks below describe earlier checkpoints; they do not reopen that decision.
Transform and shear belt uses the established Character progression and direct
physical settings. See [Transform](TransformShearLandform.md) and [Volcanic arc](VolcanicArcLandform.md). Caves retain their existing
shape; this province work does not claim cave visual approval.

12 November 2026. User correction: there is one editor, **Planet**, where the world
is assembled. Standalone Landform/source editors are retired. Terrain assets are
source definitions; opening one focuses Planet's Landforms tab without applying
or replacing it. The obsolete window menu entry and Rift source UI are removed.
Legacy console/API names route into Planet.

Each catalogue or applied Landform occupies one row. The text cell contains its
name and brief description; the same row contains:

| Control | Effect |
| --- | --- |
| Disclosure | Edit applied physical ranges, noise and placement values inline. |
| Checkbox | Include in generation. Off disables disclosure, Character, eye, info, reseed and Solo. Settings are retained for re-enabling. |
| Character | Change this planet's applied family Character, with one Undo per drag. |
| Eye | Include/exclude only in the current preview; generation selection remains saved. |
| Info | Toggle highlighted outlines and centre markers for all currently assigned cells of this Landform. This display action starts no terrain generation and is disabled when the row is excluded. |
| Reseed | Change this planet's deterministic local variation. |
| Solo / Unsolo | Apply this included Landform across every planet region / restore the assembled preview. |

Eye, Info and Solo are editor session state. They do not dirty the planet or mutate
source definitions. Solo temporarily reveals a hidden selected row and restores its eye state on
Unsolo. It temporarily excludes
other Landforms, including manually placed instances. Base geology, planet scale,
radial guides, Rock and Caves remain supplied by the planet. Every generated cell must be assigned to an included Landform. One included
Landform fills every cell. With several, every included Landform receives at
least one cell and their cell counts differ by at most one unless explicit
assignment overrides require otherwise. Two included Landforms share the cells
about 50/50. Character and suitability do not increase a Landform's share.
Within those shares, a deterministic assignment over the complete planet
preserves as many strict geological fits as possible, then prefers the source’s
earlier relaxation tiers and the closest elevation/slope/structural fit.
The source’s strict requirements and known
geological evidence are never rewritten. Preferred relaxed-placement budgets
remain soft during mandatory full coverage. If more Landforms are included than
the requested cell count, generation uses enough cells to represent every one;
the authored count is retained. Explicit assignment overrides stay fixed, but an
override set that leaves no room for an included source is rejected. Solo is
an explicit compiler evaluation option, outside the recipe and JSON format;
it overrides region assignment in the same stock native VPP pipeline. Changing
that option invalidates prepared input reuse where necessary.

The catalogue still contains fourteen neutral province definitions awaiting their
terrain pass. Their controls retain planet-local settings; including or soloing a
placeholder cannot create terrain that has not yet been implemented. Rift, Transform and Volcanic arc use
their typed physical Character and seed. Generic authored noise Landforms use
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


## Disclosure and curated Character

Each included row has a disclosure triangle. Its native property view edits the
planet's applied physical ranges, noise layers and placement settings directly,
with validation and Undo/Redo. Source identity and algorithm fields stay protected.
Collapsed/expanded state is preview UI state. Disabled rows disable the disclosure
and its values as well as the other controls.

Rift profile 3 is the curated family: 180–440 m spacing, 12–42 m throw and 8–26 m
scarp width across Character. The shape expression also avoids the previous
harsh sawtooth endpoint. These defaults are intended to produce usable variants
throughout the slider; visual approval remains open. Details permit deliberate
extremes. Moving Character again restores the curated ranges. Older saved
profiles remain pinned until a slider gesture or explicit physical edit adopts
profile 3; the candidate source uses terrain schema 12.

Local physical relief is added to the already calibrated geological landscape,
rather than refitting the whole planet after introducing a province. Elevation
distribution continues to shape the base; the local metre displacement stays
local. Rock restoration uses that same physical structural displacement. The
planet's authored global guide span, underlying geology and Caves settings remain
independent. A high-relief base can still have large mountains with Rift hidden;
this change curates the Landform contribution rather than rewriting those inputs.

Planet preview publishes the updated surface before generating caves. The first
worker checkpoint retains the complete authored recipe, including enabled caves,
but defers cave construction and diagnostic measurements. VPP publishes that
surface and its collision before the editor starts the cave continuation. Picking
and navigation are available while caves generate. The continuation reuses the
same immutable surface, native world and walk location; it adds the existing cave
family and then its diagnostics. A newer edit cancels either phase and rejects
stale results. Runtime generation still returns the complete planet in one call.

The expanded Rift heading reads “Continental Rift”. Planet opens with province
cell boundaries hidden and a stronger geographic grid, highlighted equator and
labelled north/south poles. Preview → Globe grid controls the geographic grid;
each Landform's Info icon controls its assigned-cell overlay. These are editor
overlays; neither changes saved generation or
VPP geometry. Grid paths are cached from surface heights once per accepted input
and reused when caves arrive and during navigation.

Within a resolved extensional belt, Rift relief varies across the belt and its
ridges run along it. This follows the rift trend rather than the plate's travel
direction. Where that context is absent, relaxed placement uses a seeded local
orientation; not every generated ridge is therefore aligned to a plate boundary.

## Assigned-cell display — 13 November 2026

The instruction banner is removed. Each row has an Info icon immediately after
the preview eye. It toggles gold cell boundaries and centre markers for that
Landform’s actual current preview assignments. Multiple Info toggles may be on.
Solo and a single included Landform therefore highlight every cell. Turning Info
off clears its overlay; excluding the row disables and clears its Info state.
The Preview tab no longer contains a Province cells checkbox. The globe grid
retains its independent default and control. Info changes only display state:
no recipe changes, native-world replacement, mesh extraction or rebuild.


## Planetary base ownership

The planetary base comes from tectonics and geology. The Landforms page does not
provide a separate global terrain-preset selector or base-definition edit action.
Landforms are applied over that base through generation or explicit placement.
Removing the former UI controls does not rewrite saved recipes or their authored
geology, sources, layers or material settings.
