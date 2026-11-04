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

Geology owns **Continents, Tectonics, Rock and Watershed**. Each native layer
header keeps its enable checkbox. Collapsed layer content is one Character knob
and its value, followed by **Advanced**. Native external property rows appear
directly below Advanced; there is no nested Details container. Existing variation
actions live inside Advanced. Model/status prose, topology counts, plots and
inspection actions do not belong in the authoring layers.

Landforms owns terrain sources and placed/explicit regional definitions. Noise
wavelengths, stamp footprints and feature densities remain in their existing
native per-layer/source controls. Regional elevation distribution is edited in
the owning region's Advanced property tree. Planet Surface Distribution always
edits the global profile and cannot redirect itself into a region. The former
Planet Terrain detail dashboard and bulk size/add-layer shortcuts are removed.
The existing physical-scale model and saved dimensions remain unchanged.

Preview owns drainage overlays, channel focus and surface walking. Optional
surface-area and rock-thickness plots live under Preview Advanced. Numeric
watershed qualification records remain in conformance artifacts. VPP rendering
LOD is independent of all Character/physical-feature controls and is unchanged.

This is an editor-only correction: no new geological producer, recipe migration,
asset regeneration or runtime generation/streaming change. The [native UI record](../conformance/Authoring/qualification-mac-arm64.json)
records actual collapsed/expanded views, tab ownership, shared four-layer layout,
Undo, region/global isolation and preserved inputs. Domain and cooked qualifications
remain those of the owning scale/rock/watershed profiles.
