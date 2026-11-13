# Cave strata material bindings

11 November 2026. The current spatial passage/chamber appearance was accepted by
the user, with a request to restore the previously generated rock strata.

The geological model was still generating physical beds, dip, deformation and
stable unit/stratum identities. `Examples/SpatialCave.json` bound limestone,
dolostone and carbonate interbeds to the same Rock surface, so crossing a bed
contact did not change the native material.

The fixture now reuses the established `RockBedsExcavation.json` unit bindings:

| Geological unit | Existing native surface | Revision |
| --- | --- | ---: |
| Limestone succession | Rock | 2 |
| Dolostone succession | Scree | 2 |
| Carbonate interbeds | Soil | 2 |

These are the earlier provisional artwork bindings, not a claim that dolostone
is loose scree or buried carbonate interbeds are topsoil. Unit lithology,
resistance, erodibility, dissolution, metre-scale thickness and stable identities
retain their values. The only recipe changes are the latter two surface IDs.
The canonical geological classifier drives the material transitions; no shader
bands, substitute geology, density edits or new materials were introduced.
VPP still owns surface blending, rendering, terrain, collision and native edits.
Profile 7 remains the existing UI default; this changes only the opt-in profile-8
fixture loaded by the cave walkthrough. Previously saved review assets are not
silently rewritten.

Qualification results and native captures are recorded in
[the strata evidence](../conformance/CaveStrata/README.md).

The current [rock-material authoring](RockMaterials.md) adds an explicit solid-rock
rebind in Planet. The original study recipes and authored review assets keep the
bindings above until explicitly edited. Its new artwork does not amend the
approved cave anatomy or claim visual acceptance of a later cave family.
