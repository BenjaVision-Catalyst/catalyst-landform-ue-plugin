# Continental Rift family — first review candidate

## Current authoring decision — 12 November 2026

The user considers Continental Rift complete for now. Historical open visual
checks below describe earlier checkpoints; they do not reopen that decision.
Transform and shear belt is the next family, with four accepted reference
expressions and seven Planet views requested for visual review. See
[Transform family](TransformShearLandform.md). Caves retain their existing
shape; this province work does not claim cave visual approval.

12 November 2026. The user chose a blend across Character: weathered Basin-and-Range
terrain through sharper, younger fault scarps. This is the first candidate in the
province-completion workflow; visual acceptance remains open.

## Current direction

Province placement is a gameplay preference with explicit relaxation metadata.
The strict geological definition remains recorded. Generation starts strict and
can relax it where needed. Geology owns planet-scale plates, belts and structure;
a Landform supplies its local expression while respecting that context. Impact
and icy crust are planet-scoped Geology features implemented with the same kind
of Landform machinery, rather than a separate class of extension.

This supersedes the assumption that missing geological history must prevent a
selected province from appearing. It does not invent missing history. The Caves
layer retains its separate enabled-minimum contract and its current shapes.

## References and visible anatomy

- [NPS Basin and Range / Death Valley](https://npgallery.nps.gov/AssetDetail/9769a60c-02a3-4c4a-b321-49e043762548):
  unequal mountain fronts, broad basins and broken slopes. NPS/Dale Pate.
- [USGS range-front study](https://www.usgs.gov/publications/geometry-and-rates-change-fault-generated-range-fronts-north-central-nevada):
  faceted spurs, transverse basins and basal scarps. Used as shape references,
  not as validation of a process simulation.

The underlying tectonic construction stays in Geology. Local faults retain
metre-scale spacing and asymmetric back slopes. Low Character uses broader,
softer, lower blocks and stronger surface weathering; high Character uses closer,
sharper, higher scarps and less weathering. Three authored native VPP noise layers
supply shoulder breakup (90 m wavelength, ±4 m), broken scarp relief (22 m,
−2..0 m) and rock/debris detail (3.5 m, ±0.25 m). Profile 3 keeps their breakup in a gentler band across Character. These are appearance layers, not simulated erosion or
connected drainage. Local reseeding changes both fault structure and the stack.

## Versioned implementation

`Examples/TerrainTypes/ContinentalRiftFamily.json` is the explicit candidate
(schema 12, Rift profile 3, province definition 2). The existing catalogue source,
profile 1 and saved palette snapshots remain frozen pending review.

Province definition 2 records ordered fallback tiers: relaxed requirements,
coverage and ranking weight. A source first seeks strict eligible cells anywhere
on the planet. Only if none exist does it try progressively relaxed tiers. The
candidate's automatic fallback is capped at two regions, avoiding a whole planet
of unsupported Rift merely because it was included. Editorial assignments may
use a permitted tier at the chosen cell. The existing region explanation records
the actual tier. Metadata never changes the underlying evidence bits.

Where real extensional belts exist, they control the structural field. Where
context is weak or missing, an assigned candidate can supply its seeded local
fault-block expression without manufacturing a plate boundary. Surface and Rock
consume the same structural displacement. Surface noise exposes relief without
warping geological units or painting replacement rock.

The Planet editor owns the applied Character and variation in its Landforms row.
Its eye and whole-planet Solo are preview-only. The standalone source editor was
incorrectly used for the first captures and has been retired; those captures are
historical, not current UI qualification. See [Planet Landforms](PlanetLandforms.md).
Both palette compilation and native VPP use the authored noise stack. VPP remains
stock and owns geometry, LOD, materials, collision and edits.

## Megascans stamp candidates

The existing TGCAssetStaging library contains these prepared candidates:

| Candidate | Intended use | Current observation |
| --- | --- | --- |
| [Granite Cliff, rivav](https://www.fab.com/listings/9253e224-5522-4052-82e2-2447e62ff80f) | Broken scarp faces and resistant outcrops | Listing describes an open mesh, 8.04 × 29.49 × 16.27 m; examine relief-oriented baking |
| Rock Plates 00, skEpS | Small exposed shoulders / fractured ledges | Local mesh available; identity and physical scale need target inspection |
| Rock Plates 01, tdqbcacda | Alternate fractured ledges | Local mesh available; same inspection required |
| [Icelandic Rock Formation, sjzbj](https://www.fab.com/listings/54167f2f-e607-4326-915e-7e7f18fd1328) | Later volcanic-context variation | Open mesh, 6.34 × 11.75 × 1.74 m; not the default Basin-and-Range identity |

The library's acquisition/target records remain authoritative. No commercial mesh
or texture is redistributed in the plugin. Private inspection copies preserve
source packages and their checksums. Candidate selection is not a claim that a
mesh has already become a qualified native VPP terrain stamp.

## Historical profile-2 delivery correction

The first effect-on/effect-off test found that a native surface-stack atlas routes
height queries into six spherical fields before sampling region relief attached
only to the atlas root. Profile 2 composes the shared province field into those
raw fields before global height calibration. Tests independently disable noise and
fault throw to prove that each changes the actual final terrain. Rock restoration
uses the same structural displacement. Profile-1 saved output remains unchanged.

Close review captures keep a comparable physical view at each Character setting.
Read-only inspection samples at most 8192 vertices from the stock native meshes,
including views larger than the existing one-million-vertex full-copy limit.
It never supplies a render mesh or changes that limit. Navigation timing starts
after returning to the normal orbit camera and settling native publication.

## Review checkpoint

The three actual native source-editor captures are retained in
[`conformance/RiftFamily`](../conformance/RiftFamily/README.md), at Character 0,
0.5 and 1. They establish delivery, not visual approval. The current surface is
still too smooth relative to the broken range fronts and dissected shoulders in
the references. Next visual work should address that anatomy and evaluate local
stamp placement; it must not promote the present candidate to the catalogue.

`Tools/create-rift-family-review.py` creates a reusable source and a planet review
asset, copying the original review's recipe and retaining its current Caves
settings. Existing candidate assets retain their edits; only the Planet editor opens.
The original `DA_ContinentalRiftReview` and radial-guide library stay untouched.

Private native VPP mesh bakes now exist for Granite Cliff and both Rock Plates.
Their measured dimensions, bake resolution and field occupancy are recorded in
the conformance report. This proves conversion to stock native distance fields;
visual stamp qualification, placement/blending, material bindings and packaged
asset distribution remain open. None of their commercial package bytes is
included in this repository.

## Curated profile 3 and inline values

The current Planet row adds a disclosure for editable physical ranges, noise and
placement values. Character now restores a curated range: spacing 180–440 m,
throw 8–28 m and scarp width 8–26 m. High Character retains sharper structure
without the former extreme endpoint. Larger deliberate ranges can be edited in
the disclosure; another knob gesture restores the curated ranges. Existing saved
profiles remain pinned until explicitly edited.

Profile 3 adds physical relief after base landscape calibration and elevation
distribution. It does not stretch a local fault block to occupy the full planet
radial interval. Its Rock restoration continues to follow structural displacement.
The current authored base relief remains independent and can be substantial even
with Rift hidden. See `conformance/LandformCuration` for actual Planet captures
and the measured change against the same base at eleven slider positions. These
are review evidence, not visual approval or completion of Rift anatomy/stamps.
