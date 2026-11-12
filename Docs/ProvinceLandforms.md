# Geological province Landforms

## Current authoring decision — 12 November 2026

The user considers Continental Rift complete for now. Historical open visual
checks below describe earlier checkpoints; they do not reopen that decision.
Transform and shear belt is the next family, with four accepted reference
expressions and seven Planet views requested for visual review. See
[Transform family](TransformShearLandform.md). Caves retain their existing
shape; this province work does not claim cave visual approval.

12 November editor correction: **Planet is the only editor**. Its Landforms tab
uses [one row per Landform](PlanetLandforms.md), with inclusion, Character, eye,
reseed and whole-planet Solo. Earlier standalone source-editor instructions in
historical checkpoints are superseded.

12 November gameplay direction: [the Rift family candidate](RiftFamilyReview.md)
records strict geological preferences with ordered relaxation. The mode-3/4
version-1 hard-gate descriptions below remain the frozen catalogue baseline.
New definition version 2 may relax recorded conditions when needed.

Seventeen native Landform assets define geological province families and the base
geology required to place them. Continental Rift adds local fault-block relief and Transform adds finite shear
corridors, both with matching Rock deformation; fifteen remain neutral placeholders. The catalogue
creates no caves. This establishes
the regional definitions before developing their terrain and underground forms.

## Native ownership and demonstrated gap

The existing `UCLTerrainTypeAsset`, palette snapshots, native UE Details,
`FCLRecipeEvaluation` worker and VPP preview remain the supported path. VPP is the
sole voxel generation and presentation platform, including layers, stamps,
meshing, LOD, collision and edits. Its `FVoxelVolumeLayer`,
`FVoxelVolumeGraphStamp` and typed metadata interfaces were inspected. They already
provide composition, graph execution and metadata transport; none of the inspected
interfaces provides Catalyst's province definitions or the missing geological
history. No vendor source or alternative generation/streaming system is added.

The demonstrated application gap was that existing placement preferences rank
shape, elevation, slope and scale, and a single selected Landform fills all cells.
They cannot distinguish missing lithological evidence from unsuitable shape. The
new specialization is a bounded geological evidence gate in the existing regional
selector. The existing native plate deformation query now also exposes shear;
its previous relief and rock deformation values are unchanged.

## Catalogue

The portable [catalogue](../Examples/ProvinceLandforms.json) identifies all source
JSON, stable IDs and binary asset paths. The first fifteen are core families;
Impact and Icy crust are planetary extensions. Forms and underground potential
are definition metadata for future implementation, not generated results.

| Landform | Required base evidence | Intended regional forms | Future underground potential |
| --- | --- | --- | --- |
| Crystalline shield | ContinentalCrust, CrystallineBasement, AncientBasement | Exposed basement, Weathered uplands, Jointed outcrops | Fissure, Talus |
| Covered continental platform | ContinentalCrust, SedimentaryCover | Broad plains, Layered plateaus, Escarpments | Formation-dependent karst, Fissure |
| Clastic sedimentary basin | ContinentalCrust, ClasticSequence | Layered hills, Escarpments, Valleys, Depositional lowlands | Fracture openings, Weathering shelters |
| Carbonate platform and reef province | CarbonateSequence | Carbonate plateaus, Reef-derived ridges, Karst uplands | Karst, Shafts, Chambers |
| Evaporite basin | EvaporiteSequence | Evaporitic flats, Solution depressions, Subsidence terrain | Dissolution, Collapse |
| Fold and thrust belt | ContinentalCrust, Compression, SedimentaryCover | Structural ridges, Folded valleys, Thrust escarpments | Formation-dependent karst, Fissure |
| Metamorphic mountain core | ContinentalCrust, MetamorphicSequence | Exposed mountain cores, Foliated ridges, Eroded massifs | Marble karst, Fissure, Talus |
| Continental rift and fault-block province | ContinentalCrust, Extension | Fault-block ranges, Rift valleys, Escarpments | Fissure, Formation-dependent caves |
| Transform and shear belt | Shear | Shear valleys, Offset ridges, Pull-apart settings | Open-fracture caves |
| Volcanic arc province | SubductionHistory, VolcanicHistory | Volcanic centres, Arc ridges, Volcaniclastic basins | Flow-dependent lava tubes, Context-dependent hypogene |
| Basaltic volcanic province | MaficFlowHistory | Shield landscapes, Distributed lava fields, Flood-basalt plateaus | Lava tubes, Skylights |
| Silicic volcanic province | SilicicEruptionHistory | Caldera complexes, Lava domes, Ignimbrite plateaus | Fracture openings, Collapse |
| Plutonic and intrusive province | IntrusionHistory | Exposed batholiths, Jointed granite terrain, Intrusive contacts | Fissure, Weathering openings |
| Passive continental margin | PassiveMarginHistory | Coastal plains, Continental shelves, Continental slopes | Coastal erosion, Formation-dependent karst |
| Oceanic crust province | OceanicCrust | Spreading terrain, Ocean-floor basalt settings, Sediment-covered abyssal terrain, Seamount settings | Flow-dependent lava tubes |
| Impact province | ImpactHistory | Impact basins, Crater fields, Ejecta terrain, Impact-melt units | Fracture and collapse potential |
| Icy-crust province | IceCrust | Ice-dominated crust, Fractured ice terrain, Icy depositional units | Ice-hosted cavities |

These families overlap in geological history. An old shield can contain younger
intrusions; a carbonate succession can be folded or covered. The present regional
palette selects one expression per cell and does not claim an exclusive geological
classification. Karst, glaciation, weathering, coastal erosion, talus and hydrothermal
activity are conditional processes/overprints, not random province labels.

The cave associations follow [NPS cave types](https://www.nps.gov/subjects/caves/park-resources.htm),
[NPS solution caves](https://home.nps.gov/subjects/caves/solution-caves.htm),
[USGS evaporite karst](https://www.usgs.gov/publications/evaporite-karst-united-states)
and [USGS lava tubes](https://volcanoes.usgs.gov/volcanic_ash/glossary/lava_tube.html).
Carbonate/evaporite dissolution, suitable lava-flow tubes and ice-hosted cavities
require their host medium and formative processes. A province's potential tag alone
will never justify excavating a cave or guarantee one at every location.

## Physical province checkpoint — 6 November 2026

[Continental Rift](ContinentalRiftLandform.md) now has a physical fault-block
surface and matching restored Rock coordinates. Its source uses schema 10 and
explicit region mode 4; the other sixteen sources retain neutral schema 9.
The placement description below records the original mode-3 placeholder contract.
Mode 4 adds geology-first ordering and local physical host gating, while retaining
Include-only selection. Old snapshots are not silently migrated.

## Placement contract

Terrain schema 9 adds `PlacementSuitability.Province`: type, placeholder flag,
required evidence, minimum area coverage, intended forms and underground potential.
All supplied assets use the existing native spherical algorithm 2, no noise layers
and no surface-stack override. Validation rejects a placeholder carrying either.

Adding or refreshing a neutral province source selects region algorithm 3, unless
the recipe already uses mode 4. Physical Rift selects mode 4. Mode 3 reuses mode
2's equal-area characterization, sites, deterministic ranking and native child
fields. Each sampled point must positively satisfy every prerequisite; its share
of the cell must meet the profile's coverage. Only then do existing soft terrain
preferences rank eligible sources. Missing evidence stays unknown and is not
inferred from altitude, shape, material colour, asset names or rock resistance.

When no source qualifies, the selector retains **Base geology** through an
internal neutral entry. That entry is not a generated or substituted terrain type
and is not offered as a source asset. Mode 3 supports 31 authored sources plus the
neutral entry within the existing 32-entry native scratch limit. A single province
is still gated. Manual cell assignment cannot bypass that gate. Global replacement
and direct geometric-region application reject province assets; use the regional
palette. Opening a neutral source's isolated relief preview strips metadata only from a
validated local preview copy and leaves the original asset intact. Physical Rift
retains metadata and previews through the existing native globe with geological context.

Legacy region algorithms 1 and 2 retain the one-source-fills-all contract for
ordinary Landforms. Mode 3 is explicit and versioned; existing recipes are not
silently migrated. Ordinary Landforms can coexist in a mode-3 palette and retain
their existing soft preferences, without acquiring a province label.

## Available geology and readiness

The resolver reuses existing continentalness and finite warped plate belts.
Continentalness >= 0.6 supplies continental context, <= 0.4 supplies oceanic
context. Compression, extension and shear use a positive structural weight of
0.02. Supplied rift/shear profiles require 35% positive cell coverage; other profiles
require 60%. These are initial versioned placement proxies, not calibrated geological
classifiers or reconstructions of past plate history.

Today Oceanic crust, Continental rift and Transform/shear can satisfy their stated
prerequisites from the existing base. Basement age/composition, specific sedimentary
formations, metamorphic history, volcanism, subduction history, intrusions, passive
margins, impacts and ice crust remain unknown until upstream geology supplies them.
Generic competent/mixed/weak Rock units do not establish carbonate or evaporite
chemistry. Thus most catalogue profiles intentionally remain unassigned.

Cell-wide coverage is a coarse regional placement decision. Before a profile adds
relief, lithology or caves, its physical output must also respect local host units,
bedding, faults, deformation, drainage and contacts. Selection boundaries alone
are not a 3D geological model. No full process simulation, cave generation,
water-table solver or new geological one-knob is delivered here.

## Review and next work

Enable **Show Plugin Content** in the Content Browser and open
`CatalystLandform Content/TerrainTypes/Provinces`. The seventeen
`DA_Province_*` assets use the existing Landform editor. Their native Details expose
`Terrain Type → Placement Suitability → Province`; expand that property for the
definition. Neutral previews are expected for the sixteen placeholders. Rift opens its Character editor and contextual native globe; see the physical checkpoint above.

Open `/Game/LandformDesigner/Provinces/DA_ProvincePlacement` in LandformLab to
inspect a planet with all seventeen selected. Its Landforms page uses the existing
region inspection and explanation controls. The base is the unchanged continental
Watershed example. Opening, orbiting and zooming must not edit the recipe or
reclassify regions. [Tools/create-province-landforms.py](../Tools/create-province-landforms.py)
creates missing source/review assets through native Unreal asset APIs and leaves
existing assets intact.

The next regional task is to review the Rift expression, then implement another
province against positive upstream evidence. Formation-specific carbonate/evaporite
placement continues to require actual host identities, not an artistic surface label.
The [qualification record](../conformance/Provinces/qualification-mac-arm64.json)
records the native checks and their limits.

The default Landforms build page is one native **Include → Landforms** checklist.
Automatic placement chooses only enabled entries from that selection. Existing
manual composition, region tuning and inspection controls remain under the same
native Advanced reveal; geological prerequisite authoring stays in source assets.


Carbonate cave qualification now establishes `CarbonateSequence` only from a
finite Rock history whose entire explicit palette is carbonate. This unlocks the
included Carbonate Platform definition; positive dissolution capability and the
retained Watershed context are additional cave prerequisites. Other unprovided
history remains unknown. See [Caves](Caves.md) for the conservative host rule and
current morphology limits. Province ownership does not clip a cave at an arbitrary
regional boundary; the complete sampled route must retain its actual host and
formation evidence.
