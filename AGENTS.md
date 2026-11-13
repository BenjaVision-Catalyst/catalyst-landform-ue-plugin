# Cave generation contract

## Publication and end-user installation

Publish the complete current plugin to GitHub `main` and verify a clean-project
installation when the user requests publication/install verification, or before
moving to a new feature. These checks are not required for every individual
change during work on the same feature. Reconcile concurrent `main` changes;
never force-push over another contributor's work. Publish the component before
the workspace pointer. Keep ordinary authoring/test experiments unsaved.

The repository is the end-user source of truth. Installation must use this
repository and documented dependencies, with no LandformLab assets, private
temporary checkout paths, retired editor, or superseded generator required.
Keep one Planet editor. Remove obsolete implementation paths rather than
retaining alternative editor/algorithm dispatch under compatibility aliases.

## Development format

The first supported product release will be version 1. During development there
is one current model: do not retain old algorithms, compatibility aliases,
migrations or frozen defaults for backward compatibility. Internal schema/profile
tags identify current data; they are not promises to execute earlier models.
Update maintained fixtures explicitly when formats change. Preserve the user's
current authoring work and never save testing experiments.

## Cave generation

When Caves is enabled, every valid planet recipe must produce at least one real,
nonempty cave in the shared terrain volume. Only disabling Caves requests zero.
Character zero and density zero retain the enabled minimum.

Rock identity, fracture/dissolution potential, plates, provinces, drainage and
formation history may guide morphology and preferred placement. They must never
be prerequisites for the existence of enabled caves. Adapt placement and physical scale within the established spatial cave family
when preferred construction cannot fit; do not invent or rewrite geological metadata.
Never substitute capsules, stretched chambers or a simpler cave generator to
satisfy existence. Acceptance must retain natural passages, varied chambers and
continuous walkable floors. Check the current review asset as well as synthetic
inputs, and assert spatial-family identity before accepting a generated result.

The only visually approved anatomy is the profile-8 Mammoth-style passage and
Carlsbad-style chamber in CLSpatialCave.cpp / CLSpatialMorph.ispc. Generation
must call that shared evaluator. The ring galleries and long entrance tails were
rejected; never reintroduce them or substitute another layout on the strength of
connectivity, clearance, counts or performance tests. Procedural extensions need
a concrete comparison and user visual acceptance before replacing this anatomy.
Keep the frozen profile-8 review field available as the comparison baseline.

Keep VPP stock. Planet is the only editor; assemble and review Landforms in its
Landforms tab, never a separate source/terrain editor. Each row has a name, brief
description, generation checkbox, one Character slider, preview eye, reseed and
whole-planet Solo and a disclosure triangle for underlying values. Character
must stay within a curated, landscape-fitting family across its full travel;
extreme values belong in expanded details. An unchecked generation checkbox disables the other controls.
Eye and Solo are preview-only; Unsolo restores the previous eye state. Keep this
interface and the planet's authoring edits. See Docs/PlanetLandforms.md.
Any change to cave eligibility, placement, fit or recipe validation must run the
enabled-minimum regression tests, including missing/unknown geology and small
planets. Cancellation and invalid input remain normal pipeline validation, not
geological eligibility rules.

Preview-delivery changes must qualify the rendered CurrentRift scenario, including
visible cave pixels, native publication/task readiness and native zoom detail.
Field-only checks do not qualify a usable preview. Keep inspection at VPP's native
SetupForPreview quality and chunk-size defaults unless a measured comparison
establishes a specific benefit. Overview accuracy follows native cell size;
close-up accuracy retains the 0.5 m field budget.
