# Cave generation contract

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

Keep VPP stock and the existing UI.
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
