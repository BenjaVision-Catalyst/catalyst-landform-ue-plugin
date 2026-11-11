# Cave placement and surface openings

12 November 2026. Cave-shape refinement is paused. This change retains the current
spatial family and its scalar/SIMD morphology. Its appearance remains unapproved.

## Editable radial bounds

`CavesMin` and `CavesMax` are normal radial guides, edited, renamed and exported
through the existing guide editor. The cave settings retain their named bindings
as `minimumGuide` and `maximumGuide`.

Missing canonical guides are initialized from the planet's current guide values:

- `CavesMin`: half the initial `SurfaceMin` radius, deep inside the body.
- `CavesMax`: 5 m below the initial `SurfaceMax` radius. Very small bodies use a
  proportional clearance so their enabled cave minimum still fits.

Initialization preserves existing guides. Subsequent surface-guide changes do
not move either cave guide. Like the other radial guides, their stored values
are normalized to the outer planet radius. The current `SurfaceMax - CavesMax`
distance supplies the minimum local surface clearance for covered systems.
`CavesMin < CavesMax` is required when generating caves. Named bindings follow
guide renames; edited values and references survive recipe JSON round trips.

Legacy shared catalogs receive the cave defaults in the resolved planet snapshot
without overwriting catalog authoring. Repeated catalog synchronization preserves
the planet's cave-guide edits until an explicit style selection or catalog edit.

## Deterministic depth and entrances

Each network receives an independent, seeded placement fraction across the radial
band. Its entire level stack and rolling floors move together. Physical fit adapts
the existing anatomy to the available band and body; neither guide clips the field
with a flat floor or roof. A covered network fits under its local terrain envelope,
rather than being placed under the planet-wide minimum surface radius.

With surface entrances enabled, some proposals instead place an existing upper
chamber against the actual terrain surface. This can expose a broad side opening
on a slope or a skylight in flatter terrain. The native terrain union produces the
opening; no entrance tail, capsule, replacement chamber or additional mesh is
constructed. A published opening must cut a broad patch of previously solid
terrain, connect to covered cave air and retain the existing walking foundation.
Entrance mouths can intersect the surface and are exempt from underground cover.
Deep systems remain fully covered. Disabling surface entrances retains caves;
disabling Caves requests none.

The frozen profile-8 visual comparison is retained unchanged. Ordinary family
generation uses the editable placement bounds. Canonical geological identity,
surface bindings, native VPP meshing, LOD, rendering, collision and edits keep
their existing owners.

Native inspection grouping uses padded joint supports rather than overlapping
whole-network boxes. Those supports also tighten finite exterior distance
estimates. Radial placements can reject certified empty joint space before
geological inversion; bedding-following geometry retains exact evaluation.
Floor and wall-contact calculations reuse the same restored coordinate when
they share canonical rock. The existing resolution policy, native preview
quality and chunk size are retained. The close-up qualification camera uses a
physical 40 m distance, with the existing 1 m cell and 0.5 m field budgets.

The UE host builds with stock VPP. Optional projection-prototype lifecycle hooks
in three lab proofs are guarded in the workspace; the plugin's optional
capability macro defaults to absent. The projection prototype is not required
by cave generation or the editor terrain presenter.

## Checks

`Catalyst.Landform.Caves.Placement` covers guide defaults and edits, persistence,
renaming, shared catalog synchronization, depth spread, exact regeneration,
unchanged local shape under guide edits and actual saved-Rift terrain openings.
The existing family editor capture also records outside and inside views of a
published surface opening. Enabled-minimum, guide-editor, current-Rift native
preview and family-floor regressions remain required.

The saved review planet resolves `CavesMin` to 499.181 m radius and `CavesMax`
to 1581.855 m, 5 m below its `SurfaceMax`. Its 16 generated systems have mean
node radii from 562.521 to 1151.441 m and one to three levels. One existing
upper chamber makes a verified surface opening; its recorded 6.30 m width is the
minimum sampled aperture, not a measurement of the entire exposed chamber.

Placement review captures use the real `SCLPlanetPreview` and native geological
terrain presenter. `-CLSpatialFamilyPlacementOnly` selects the two entrance
cameras from the existing family capture; the full anatomy capture remains the
default. Capture checks also reject a frame containing only background sky.
The saved recipe's Caves character remains 0.348. These images document placement,
not acceptance of the paused cave shapes.

Native persistence tests allow exterior walking samples only in networks with
verified surface openings, and probe their natural solid footing at the anatomy's
walking scale. Bulk/sparse field parity, canonical geological identity, exact
regeneration and subsequent earthworks/backfill assertions remain unchanged.

The final Mac arm64 editor build succeeds with stock VPP, and all 24 selected
automation checks pass, including the targeted native persistence rerun.
CurrentRift publishes 16 networks in 12 native inspection domains. Its physical
40 m close-up measures 0.480 m maximum cells and 0.177186 m maximum field
residual, within the unchanged qualification budgets. The
[placement evidence](../conformance/CavePlacement/README.md) contains actual editor
entrance captures, guide/depth data, commands, results and staged-source hashes.
