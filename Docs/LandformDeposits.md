# Landform deposits over rock

Planet → Landforms → expand the landform row → Deposits. Each deposit declares a
stable identity, material ID/revision, mining resistance and erodibility, depth,
thickness, coverage, patch size, slope limit, priority and seed. The generation
checkbox disables the whole row. Eye/Solo continue to control the preview only.
Adding/duplicating a deposit assigns a new identity on that explicit edit;
selecting its material resolves the configured revision in the same transaction.
Opening Planet never rewrites an applied snapshot or its source definition.

The current packaged starters are Rift basin alluvium, Transform fault-slope
colluvium and a Volcanic Arc ash mantle. They use patch and original-slope masks,
not drainage transport/sedimentation simulation. They are editable initial
geological domains, not claims of complete depositional history. Alluvium and
VolcanicAsh are artwork profiles derived from the installed gravel/dirt scans;
they are not independently measured lithology scans.

## Physical contract

`FCLLandformDeposit` describes finite material **inside existing terrain solid**;
it adds no terrain height. The shared implicit field remains the source for VPP
meshing, collision, edits and LOD. A deposit starts `TopDepthMeters` below the
accepted generated surface and ends at its locally varied thickness. Footprint
and coverage taper that thickness. The bottom contact feathers within the body;
the top feather extends by at most `ContactFeatherMeters` for native interpolation.
The authoring limits permit at most eight deposits per landform.

Generated provinces reuse the accepted palette weights, including softened cell
boundaries. Placed landforms reuse their spherical footprint. A landform cannot
paint every underground column just because it owns a province cell. Deposits
stop outside their footprint and below their finite depth. Surface depth and
slope are anchored before cave subtraction, authored cuts and live excavation.
Excavation therefore exposes the pre-existing underlying sequence. It does not
regrow deposits onto a new quarry or cave floor. This slice does not place local
cave-floor sediments, veins, resource yields or intrusive bodies.

Coverage/variation use canonical body coordinates in metres and explicit seeds.
There is no world-Z dependence or pole seam. Higher deposit priority applies
last; source priority and stable source/deposit GUIDs resolve ties independently
of array order. Different physical deposits may reuse the same artwork.

## One composition and native delivery

`FCLScene::SampleMaterials` returns `FCLMaterialSample` for an immutable whole-body
recipe, before live native edits. It evaluates substrate, landform deposits, then
explicit authored material paint. `SampleSurfaces` adapts that same result. The
native stamp requests the result once when both weighted Surface Types and
`MD_RockProperties` are needed. Bindings are resolved during compilation; missing
IDs/revisions reject the change before accepting a new planet snapshot.

The result keeps `Substrate` independently of the current physical `Material`.
Physical identity is the dominant **contribution**, not the dominant merged
artwork index. Resistance/erodibility blend across contacts with the same weights
as appearance. The physical sample reports those composed contact properties.
`Properties` is (resistance, erodibility, known physical fraction,
placed-fill fraction). Unknown substrate retains zero known fraction; an isolated
deposit becomes fully known only where it completely occupies the sample.

`UCLRockQueryLibrary::QueryRock` checks current native solid, accepted snapshot and
native metadata. It returns the physical occupant, substrate unit/stratum IDs,
source/deposit IDs and bounded deposit depth/thickness. `SurfaceId/Revision` belong
to that physical occupant; `VisibleSurfaceId/Revision` report native visible paint.
Paint does not relabel geology. Placed fill has its own origin/identity and native
properties. The query refuses unknown/partially unknown physical metadata rather
than inferring geology from a colour. Removed air is not a geological reward.

Native player edit ordering/persistence remains entirely VPP-owned. Resource
accounting and cold-reload mining acceptance remain separate gameplay work.

## Ecosystem handoff

Ecosystem has no dependency in Landform. Its classifier must supply optional
cover to the same immutable composition boundary, using canonical body metres
and fixed generated-surface/deposit depth. It must not sample the excavated floor
as a new cover anchor. Use finite physical domains with stable IDs and properties
for soil/sand/peat; use `FCLMaterialSample::OverlayAppearance` for visual litter,
wetness or moss that must preserve physical identity/properties. `Overlay` is the
physical compositor. Keep substrate identity independent throughout.

This change provides the shared result/compositor and deposit stage; **it does not
register an Ecosystem provider or implement biome materials**. The next integration
must define snapshot ownership, cancellation, batching and explicit deterministic
cover priority, then insert cover after deposits and before authored/native paint.
Do not attach UObjects, per-point world lookups or a dependency on PCG to workers.
Native queries must consume the same accepted cover snapshot as rendering.

The current client library has an eight-entry dense adapter limit, now occupied
by Rock/Scree/Soil revision 2 and StrongRock/MixedRock/WeakRock/Alluvium/VolcanicAsh
revision 1. This is **not VPP's catalogue or per-position blend limit**. Before
expanding the Ecosystem catalogue, remove that adapter constraint coherently,
including flattened diagnostic preview packing, rather than increasing a constant
and leaving truncated previews. Aim for few contributors at each contact.

## Verification checkpoint

UE 5.7 macOS arm64 with stock VPP 2.0p8: 27 focused native checks passed across
this slice and its final parity rerun, plus three installer and three workspace
staging tests. They include finite depth after excavation, generated/placed
footprints, deterministic reseeding, independent physical identities sharing
artwork, native Surface Type/property parity with and without Rock, Planet
addition/duplication/revision/Undo, current recipe/native scene parity, existing
volume paint/metadata, balanced assignments, packaged assets, standalone install,
enabled cave minima and the rendered CurrentRift regression. The 512-point patch
fixture measured 0.283 ms for its material composition query (not a general native
rendering or full default-province performance claim).

The scene suite now uses maintained current recipes instead of requiring retired
serialized fields. Historical binary captures remain historical records; they do
not require the product to import old formats.

The controlled unsaved Planet quarry shows a 4 m deposit cap over the actual
packaged Rift geometry and the same excavation with that deposit disabled:
[deposit over rock](../conformance/LandformDeposits/deposit-over-rock.png),
[exposed rock](../conformance/LandformDeposits/rock-without-deposit.png).
[Recorded scope and results](../conformance/LandformDeposits/verification.json).
These captures are review evidence, not user visual approval. No experimental
planet was saved. All 186 normal-project Content files remained unchanged; the
user's unsaved authoring editor was left running on its already loaded modules.
