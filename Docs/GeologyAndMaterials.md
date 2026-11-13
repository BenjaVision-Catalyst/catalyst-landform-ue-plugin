# Geological substrate, deposits and ground cover

Accepted scope: Planet authoring, 13 November 2026. This is the ownership and
composition contract. The implementation stages below distinguish available
behaviour from work still to come. Version 1 will be the first supported product
release. Development has one current model, without backward-compatible
algorithms, migrations or frozen-default guarantees. Internal schema/profile tags
are validation identifiers, not supported historical product versions.

## Ownership

| Owner | Authored data | Planet editor location |
| --- | --- | --- |
| Geology / Rock | Finite rock history, unit/stratum identities, physical properties and exposed-rock material bindings | Expanded Rock details |
| Landform | Local geological bodies and deposits, their extent/depth, properties and materials | That landform's expanded details |
| Ecosystem / Biome | Ground-cover material, coverage, thickness and habitat preferences | That biome's expanded details |
| Planet | Shared material-library/renderer configuration and fallback surface rules | Planet Advanced; project defaults may supply the shared library |
| Preview | Inspect visible contribution, cover depth and underlying geology | Preview inspection |
| Gameplay | Excavation, placed material, resource accounting and persistent edits | Native VPP tools and explicit gameplay services |

There is one Planet editor. A separate Surface tab does not own these models.
Landform remains usable without Ecosystem. Ecosystem contributes through an
explicit optional integration; Landform does not depend on Ecosystem or PCG.

## Composition and precedence

Evaluate geological substrate, then local landform bodies/deposits, then biome
cover. Explicit player-painted/placed material is applied through native VPP edit
ordering. Each contribution has a finite volume or coverage mask; no contribution
preserves the input beneath it. Priority resolves overlaps within the same stage
and must be explicit, deterministic and independent of row order.

A landform changes subsurface identity only where it declares an actual body,
deposit or structural operation. A province-cell label alone cannot replace a
whole underground column. Structural displacement maps the shared geological
field consistently for materials, properties and identity. Geology supplies
placement preferences with declared relaxation; game composition is not blocked
by scientific prerequisites.

Soil, sand, peat and other diggable cover have finite depth and their own physical
identity/properties. Thin litter, moss or wetness may be visual cover. Cover is
anchored to the accepted generated terrain/deposit, not the latest excavated
surface: digging must not continually regenerate soil onto exposed rock. Cave
deposits require explicit local rules. Terrain slope is relative to radial up,
including at poles and after a planet transform.

Changing visible artwork does not change lithology, unit/stratum identity,
resistance, resource content or bed contacts. Biome ground cover does not erase
the underlying geology. Excavation exposes the fixed underlying sequence.
Player fill records its own material/properties and does not masquerade as a
natural stratum. A material colour or VPP rendering index is never a mining ID.

## Deterministic rock and gameplay tuning

Keep one canonical, finite implicit geological history. Sedimentary packages,
volcanic flows, massive basement and intrusive bodies require appropriate domain
rules; a repeated palette of concentric stripes is not a complete rock model.
Use shared structural coordinates for deformation and contacts. Stable identities
belong to the accepted immutable recipe and survive cave subtraction, excavation,
material rebinding and native save/reload.

Physical bed thickness, structural variation, mining resistance and resource
abundance are separately tunable. Character traverses a curated family; extreme
controls remain in expanded details. Resource seams, veins, lenses and contact
deposits should have readable continuity and clues, with explicit host preferences
and gameplay relaxation. Resource generation/accounting is a separate subsequent
stage; no material binding introduces resources or changes their yield.

Mining queries must establish current native solid/contact and placed/cover
contributions, then return geological identity and physical properties. They must
not infer substrate from the dominant visible Surface Type. Rewards need bounded
accounting of actual removed material, including overlap between mining actions.
Rendering resolution/LOD must not redefine authoritative geological contacts.

## Native material delivery

Use stock VPP 2.0p8 Surface Types and MegaMaterial. Portable recipes store stable
surface IDs/revisions; the client library binds them to native Surface Types and
Unreal materials. Resolve native bindings once per immutable compilation snapshot.
Internal VPP indices are runtime details and must not become persistent IDs.

Batch requested positions and emit native weighted Surface Types. Reuse the
canonical classification for requested properties. Ordinary contacts should use
few materials (target two to four); distinguish the library size from the number
of contributors at a position. Many beds may reuse one material. Missing bindings
fail before accepting an edit; there is no silent lithology substitution.

Prefer native Smart Surface Types/mesh normals for appropriate appearance-only
slope blending. Keep gameplay metadata query-driven; shader metadata textures
must be justified by visual need and measured VRAM. VPP owns meshing, LOD,
material blending/rendering, collision and sculpt persistence. Surface edits must
not add a second mesher, renderer, geology classifier or sculpt format.

References: [material workflow](https://docs.voxelplugin.com/knowledgebase/materials/working-with-materials/),
[Smart Surface Types](https://docs.voxelplugin.com/knowledgebase/materials/working-with-materials/smart-surface-types),
[metadata costs](https://docs.voxelplugin.com/knowledgebase/materials/working-with-metadata),
[runtime edits and gameplay limits](https://docs.voxelplugin.com/knowledgebase/blueprints/runtime-edits-and-sculpting).

## Implementation stages and acceptance

1. **Rock bindings, current slice:** keep metric bed generation and geological
   identity unchanged. Provide dedicated rock artwork bindings and native material
   choices under Geology / Rock. Existing assets retain their authored bindings;
   new planets require rock-only starter bindings. Explicit rebinding is one Undo step.
   Planet Advanced retains the fallback rules previously edited in Surface.
2. **Composition:** introduce the single substrate/deposit/cover evaluator shared
   by preview, native terrain and gameplay queries. The existing enabled Rock path
   currently owns exposed materials and bypasses fallback surface rules. That path
   does not yet implement biome cover. The current eight-entry client-library
   budget is separate from VPP's per-position blend budget and must be revisited
   before adding broad biome/rock catalogues.
3. **Landform deposits:** add explicit bodies/volumes and their identity/properties;
   preserve underlying strata outside them. Validate cross-region continuity.
4. **Biome cover:** add finite-depth cover through optional Ecosystem contributions.
   The present Ecosystem classifier/PCG bridge does not supply terrain materials.
5. **Mining/resources:** add controlled deposits and actual-removal accounting,
   then qualify native excavation, fill, cold reload and gameplay behaviour.

Use unsaved transient planets for tests/review. Never save authoring experiments
or migrate existing planets merely by opening them. Verify actual Planet/VPP
rendering at human scale alongside identity/property/native query checks. Test
quarry walls, covered slopes and cave transitions as their stages are introduced.
Technical checks do not replace visual review. Publish the complete plugin and
dependencies after confirmation, with component commits preceding workspace links.
