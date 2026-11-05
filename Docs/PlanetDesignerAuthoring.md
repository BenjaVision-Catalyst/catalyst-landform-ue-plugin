# Planet Designer authoring contract

5 November 2026. This corrects the UI accepted by the earlier scale/watershed
technical qualifications. Passing generator tests did not establish compliance
with the world-builder workflow. Earlier screenshots remain historical evidence,
not the current authoring contract.

Planet owns body configuration, radial references and **Surface Distribution**.
The latter is an optional planet-wide height-distribution constraint, not a
geological process. It has no Target selector. It controls how much area occupies
low, middle and high parts of SurfaceMin/Max; its independent rare-tail controls
remain under Advanced. Opening an asset never changes its saved recipe.

Geology lists **Tectonics, Continents, Rock and Watershed**, in that order. Each layer is one
native expandable category header: chevron, Outliner eyeball, indicator toggle, name, Character
slider/value and reseed icon. All Character readouts use the same native property
font and fixed width, showing rounded 0–100 values without a percent sign. This
also applies to Planet Surface Distribution; stored values remain normalized. The eyeball has the same accepted enable/disable
semantics as the former checkbox. The slider and reseed remain accessible while
the category is collapsed. Opening the header reveals its ordinary native
property rows directly underneath. There is no separate Character row, Advanced
button, nested Details container or extra panel for geology.

Continents, Tectonics and Rock retain their own reseed/Undo behavior. Watershed
currently has no independent seeded variation; its native reseed icon is disabled.
The user selected watershed-local variation as the next separate task. It must not
reseed the planet or upstream layers. This UI change adds no generation algorithm.
Planet Surface Distribution retains its existing one-knob/Advanced layout.

Landforms owns terrain sources and placed/explicit regional definitions. Noise
wavelengths, stamp footprints and feature densities remain in their existing
native per-layer/source controls. Regional elevation distribution is edited in
the owning region's Advanced property tree. Planet Surface Distribution always
edits the global profile and cannot redirect itself into a region. The former
Planet Terrain detail dashboard and bulk size/add-layer shortcuts are removed.
The existing physical-scale model and saved dimensions remain unchanged.

Layer indicator toggles sit immediately beside the eye and use a native UE wireframe
icon with the standard checked highlight. They are independent display preferences,
initially off, scoped to the open editor; they never edit the recipe, dirty the
asset or request generation. Watershed toggles its globe drainage/channel splines,
transparent connected ocean and shore-bounded potential-lake surfaces. Tectonics, Continents and Rock have no
separate indicator overlays today, so their icons are disabled. Rock strata are
terrain/material output, not an inspection overlay. No new indicators are generated.

Preview retains channel focus and surface walking. The former Drainage overlay
checkbox is removed; there is one owner for the layer display preference. Optional
surface-area and rock-thickness plots live under Preview Advanced. Numeric
watershed qualification records remain in conformance artifacts. VPP rendering
LOD is independent of all Character/physical-feature controls and is unchanged.

[Background evaluation](BackgroundEvaluation.md) now keeps derived field work off
the UI thread, with stage progress in Preview and the last successful terrain
retained during edits. Native Undo fences superseded work. Derived errors leave
the authored settings editable and label the displayed terrain as stale; native
input errors still reject before mutation.

The geology-row layout change is an editor-only correction: no new geological producer, recipe migration,
asset regeneration or runtime generation/streaming change. The [native UI record](../conformance/GeologyRows/qualification-mac-arm64.json)
records actual collapsed/expanded views, tab ownership, shared four-layer layout,
Undo, region/global isolation and preserved inputs. Domain and cooked qualifications
remain those of the owning scale/rock/watershed profiles.

The [layer indicator qualification](../conformance/LayerIndicators/qualification-mac-arm64.json)
records the updated native row layout and display-only toggle checks.

The [Watershed debug contract](WatershedDebugView.md) specifies native reuse,
fill-to-spill meaning, background derivation and visibility during navigation.
