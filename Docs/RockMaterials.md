# Rock material bindings

Rock's expanded details in the Planet editor own the material for each geological
unit. **Exposed rock material** is a native dropdown populated from the configured
surface library. Selecting another material commits its ID and revision together.
**Use rock materials** explicitly binds the strong, mixed and weak unit roles to
`StrongRock`, `MixedRock` and `WeakRock`. The action is one Undo step.

These are solid-rock appearance classes, not newly asserted lithologies. They
reuse the packaged rock-face scan with different tint, roughness, detail and
physical texture scale. Weak buried beds no longer need to look like loose soil
in newly created planets. These starter materials are not claimed to be measured
limestone, dolostone or shale scans. Geological identity remains in each unit.

New planets require these bindings in the configured library; a missing binding
rejects creation instead of silently falling back to another model. Opening a
planet does not apply an editing action. `Rock`, `Scree` and `Soil` are current
fallback ground surfaces used when Rock is disabled, not compatibility aliases
for the three bedrock materials. A project-owned library may provide the current
required bindings with different artwork. Explicit rebinding is validated before
mutation.

There is one current metric rock profile. Retired profile tags are rejected; no
older geology implementation or automatic migration is provided. Product version
1 will mark the first supported release. Current authoring work is preserved,
including the user's open unsaved editor session.

Rebinding changes only surface IDs/revisions. Bed contacts, seeds, unit/stratum
GUIDs, lithology, fracture/dissolution, resistance and erodibility remain exact.
Native VPP Surface Types/MegaMaterial receive the same canonical classification.
The existing finite metric strata evaluator is unchanged. Slope/height/noise
fallback rules formerly shown in Surface are retained in Planet Advanced and
apply when Rock is disabled. They do not implement biome cover.

The accepted [ownership/composition contract](GeologyAndMaterials.md) records the
remaining landform-deposit, biome-cover and mining/resource stages. Ordinary
review/test planets remain transient and unsaved. The material-building script
only saves the explicitly named plugin product assets.
