# Rock Character in Planet

The current Rock model uses finite metric beds, shared plate/province deformation
and independent geological identity, physical properties and artwork bindings.
There is one current development evaluator. Retired profile tags are rejected;
there are no historical runtime alternatives or migration promises. Product
version 1 will be the first supported release.

## Ownership and authoring

Expand Rock in the Planet editor's Geology section. The header retains its eye,
Character slider and reseed. Details expose structure, units, mining properties,
physical bed distribution and the unit's **Exposed rock material** dropdown.
Material ID/revision are committed together; **Use rock materials** is one Undo
step. See [Rock materials](RockMaterials.md) and the accepted
[geology/material contract](GeologyAndMaterials.md).

Opening Planet does not invoke an editing action. User gestures update the
explicit current recipe; tests use transient planets and never save authoring
experiments. The user's live unsaved authoring session must not be closed or
replaced as part of testing.

## Curated family

Character follows independent smooth tracks rather than increasing all controls
in parallel. Its quiet end still contains physical beds. It traverses rhythmic
beds and modest deformation, more weak units, stronger shortening/dip in existing
compression, sharper bounded fault transitions and basin-rich expressions where
deformation recedes. Peak/spread vary within the normal physical-bed range;
rare-tail controls, palette IDs/properties, seed, competent unit, viscosity
contrast, crust depth and contact feather remain independently authored.

Character does not add a separate terrain-height contribution. Existing plate
and province structure guides deformation. The finite history and the same
structural coordinates drive bed contacts, cave exposure, material transitions
and mining properties. The current implementation is a bounded implicit model,
not a time-resolved sediment-transport or mechanical simulator.

## Physical history and mining

[Physical bed thickness](RockBedThickness.md) defines the current metre-scale
population, compact contacts, younger basin sequence, limits and qualification.
The typical distribution is a gameplay-tuned authoring profile, not a universal
geological law. Deformation restores all contacts through one shared coordinate;
excavation never moves the reference beds to follow the newly exposed surface.

Unit GUIDs identify geological prototypes. Stratum GUIDs additionally identify
individual beds for the accepted immutable recipe. Artwork may be shared by many
beds. Material rebinding changes neither those identities nor mining resistance,
erodibility, lithology, fracture/dissolution or contact geometry. Resource deposits
and rewards remain subsequent work under the shared contract.

`MD_RockProperties` supplies native VPP full-float resistance, erodibility, valid
property contribution and placed-fill contribution. GUIDs are not interpolated
colour channels. `UCLRockQueryLibrary::QueryRock` / `QueryExposedRock` establishes
live VPP solid/contact and accepted snapshot/pose, then obtains geological
identity from the canonical field. `FCLScene::SampleGeology` can inspect the
immutable geology even where excavation has made air. Gameplay must use live
native queries to decide what is currently mineable.

The current query resolves exposed rock and placed fill. Finite biome cover,
cover-specific identity, resource yield/accounting and a complete composition
query are explicitly pending. A dominant visible material must not become the
future authority for substrate identity. Backfill must explicitly paint its
material/properties through native VPP tools.

## Evidence and limits

The current first material slice checks exact bed contacts/IDs/properties across
rebinding, native weighted Surface Type delivery, native dropdown and Undo, and
rendered quarry output in Planet. Human-scale native captures establish what was
rendered; technical tests do not establish user visual acceptance.

Earlier research and diagnostic sections remain in [rock history](RockStratigraphy.md),
[regional deposition](RockDeposition.md) and [folding](RockFolding.md). They describe
historical study milestones and their assumptions, not supported runtime profile
choices. No full plate history, lateral fault solver, compaction, sediment mass
budget, intrusion/resource system or production multiplayer mining service is
claimed by this slice. VPP remains the sole meshing, LOD, rendering, collision,
sculpting and sculpt-persistence platform.
