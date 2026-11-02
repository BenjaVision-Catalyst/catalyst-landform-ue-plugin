# Planet geological design proposal

2 November 2026. Proposed design and implementation sequence for review. The
existing UE 5.7.4 / VPP 2.0p8 proof remains the executable baseline. This document
adds no geology implementation and changes no saved planets.

Build planets from a shared geological history and structure, refine them through
landscape processes, then use reusable Landforms to express the resulting terrain.
The recommended approach combines bounded tectonic construction, explicit rock
units, connected drainage and accepted erosion products. It retains artistic
control over continents, belts and landmarks, with an efficient immutable field
for Unreal presentation.

## Existing work and what to retain

The current compiler has broad oriented uplift/basin shapes and heightmap
features, structure-guided cellular regions, reusable terrain sources, native VPP
geometry/materials/collision and globe-to-ground inspection. These establish the
presentation and composition path. An uplift currently has no crustal cause,
tectonic age, plate motion, rock sequence or connected drainage model.

The older research workspace contributes useful concepts:

- [Planet descriptions](../../../../landform-workspace/CatalystPlugins/Plugins/CatalystLandform/Docs/PlanetDescriptionAndExample.md): one shared structure, geological types, explicit contacts and accepted derived fields.
- [Watershed and karst family](../../../../landform-workspace/CatalystPlugins/Plugins/CatalystLandform/Docs/WatershedKarstFamily.md): continuous bedding and drainage across provinces; rock properties affect terrain processes.
- [Automatic watersheds](../../../../landform-workspace/CatalystPlugins/Plugins/CatalystLandform/Docs/AutomaticWatershedGeneration.md): spherical analysis, deterministic routing, bounded refinement and qualification gates.
- [Research review](../../../../landform-workspace/research/unreal-landform/README.md): process-based formation families, multiscale refinement and hybrid procedural/derived data.

Those documents distinguish scalar experiments from native execution. Their
prepared C++ and watershed experiments are research inputs, not qualified code in
the current plugin. Reuse selected algorithms only after separate tests. Their
absolute/datum-radius conventions do not supersede the current normalized guides
and outer-sky radius contract.

## Research basis

| Primary source | Finding relevant to this proposal | Proposed use |
| --- | --- | --- |
| [Procedural Tectonic Planets, Cortial et al., 2019](https://perso.liris.cnrs.fr/apeytavi/website/publication/hal-02136820/) | Controllable approximations of plate motion and crust interaction can construct large planetary structures, subsequently amplified with terrain data. | Bounded tectonic construction and editorial motion controls. |
| [USGS, Understanding plate motions](https://pubs.usgs.gov/gip/dynamic/understanding.html) | Different convergent settings, spreading and transform motion have different geological expressions. | Distinct boundary operators, including mixed motion; no generic mountain at every boundary. |
| [NASA, Venus crust study, 2025](https://science.nasa.gov/science-research/astromaterials/nasa-study-reveals-venus-crust-surprise/) | Venus illustrates crustal evolution and volcanism without Earth's moving-plate/subduction pattern. | Explicit geological regimes rather than Earth tectonics on every body. |
| [Tectonic uplift and fluvial erosion, Cordonnier et al., 2016](https://onlinelibrary.wiley.com/doi/10.1111/cgf.12820) | Uplift and erosion together provide a basis for mountain and watershed structure. | Couple generated relief to drainage and erosion. |
| [Terrain Amplification using Multi-scale Erosion, 2024](https://research.adobe.com/publication/terrain-amplification-using-multi-scale-erosion/) | Coarse terrain can be amplified with hydrologically consistent detail at several scales. | Constrained regional refinement rather than unrelated fine noise. |
| [Stochastic Geomorphological Transport, McDonald and Cordonnier, 2026](https://erosiv.studio/publications/stochastic-geomorphological-transport) | Momentum-aware transport models produce meanders, braided channels, fans and deposits. | Evaluate a higher-quality upstream erosion producer after a simpler baseline. |
| [Arenite, Yang et al., 2025](https://jango6324.github.io/arenite/) | Stress and erosion/deposition support sandstone structures such as alcoves, arches and buttes. | Later material-specific Landform generation, with retained accepted outputs. |
| [Priority-Flood, Barnes et al.](https://arxiv.org/abs/1511.04463) | Depression analysis and watershed labelling can operate on irregular meshes. | Adapt to a closed sphere with explicit oceans, lakes and internal terminal basins. |

These inform the design; the stages below are Catalyst proposals. Research
implementations need separate licensing, representation and platform evaluation.
The official [2026 transport implementation](https://github.com/erosiv/geotransport)
is a CUDA core reference, while the [multiscale erosion release](https://github.com/H-Schott/MultiScaleErosion)
is an OpenGL application. Neither is an established Mac/VPP integration.

## Planet inputs and physical regimes

The planet references a geological profile, a Radial Guide Set and existing
celestial/environmental domain inputs. Where available, consume mass, gravity,
rotation, obliquity, heat and water/climate forcing from their owners. Landform
owns their terrain consequences, not a replacement astronomy or weather service.

Initial profile: active tectonic rocky worlds. Subsequent distinct profiles:
ancient eroded rocky worlds, volcanic single-lid worlds, crater-dominated airless
bodies and fractured icy bodies. Each enables relevant processes and records its
assumptions; a dry airless world does not acquire modern river erosion by default.
A body can retain evidence of processes from an earlier declared environment.

Radius remains the outer sky envelope. Guide values remain fractions from centre
0 to that envelope 1. The solid reference surface, sea reference and terrain
limits are separate resolved radii. Guides are references, not geological layers.
Layer thickness, rock properties and structural coordinates belong to geology.

Sea level needs an explicit policy: either a pinned ocean guide, or a water-
inventory calculation that proposes a sea reference for acceptance. Do not claim
both are independently satisfied. A pinned sea level can yield a different land
fraction when geology changes; forcing a target land fraction is an editorial
constraint and should be labelled as such.

Distinguish physical planetary scale from deliberate miniature presentation.
Earth-like tectonics on a 100 m game world is an explicitly scaled artistic
profile, not a prediction for a natural 100 m body. A proportional resize
rebuilds dimensioned fields; a physically different planet requires regeneration
of physical process outputs. Do not infer mass or gravity from the sky envelope.

## Shared spherical geological model

Use an analysis mesh on the sphere, independent of render chunks, PCG points and
Landform regions. Prototype with an icosahedral mesh and explicit spherical areas;
refine by measured error and process needs. About ten thousand vertices is a
starting tectonic experiment, not an established drainage resolution or budget.

Maintain a canonical body frame and seam-free 3D directions. Plate motion uses
an angular velocity and Euler pole: tangent velocity is `omega cross position`.
For adjacent plates, resolve relative motion into boundary-normal convergence or
extension and boundary-tangent shear. A boundary can contain both components.

Plates and continents are separate objects. A plate can contain continental and
oceanic crust; continents can span several plates. Track crustal type, thickness,
density, age, thermal state and inherited structure on the plate material, not
only on an unmoving Voronoi cell. Cellular methods can seed a partition, but the
visible world must not inherit a pattern of independent polygon-shaped hills.

| Setting | Structural products |
| --- | --- |
| Continental collision | Broad mountain belt, thickened crust, elevated interior plateau and adjacent basins |
| Oceanic subduction beneath a continent | Trench, asymmetric margin, volcanic arc and related basins |
| Oceanic subduction beneath oceanic crust | Trench, island arc and associated basins |
| Continental extension | Rift, faulted shoulders, subsiding basin and possible later ocean opening |
| Oceanic spreading | Ridge, newly formed crust and age-dependent seafloor depth |
| Predominantly transform motion | Fault/displacement corridor; local relief only where the selected mixed-motion model supports it |
| Interior plume or hotspot | Broad dome and volcanic province; chains require a motion/history model |
| Stable continental interior | Old basement, subdued inherited relief and sedimentary cover where supported |
| Impact event | Basin, rim/rings and ejecta; later deformation, deposition and resurfacing change their preservation |

Boundary polarity and structure depend on the crust and process profile. They
must not be chosen from plate ID or speed alone. Broad support includes simplified
isostatic response and flexural basin effects, calibrated as approximations.
An uplift becomes a product of these processes or an explicit editorial override.

Preserve a bounded geological history: ordered rifting, collision, volcanism,
subsidence, sedimentation, deformation and erosion phases. Young and old belts
must have different exposure and morphology. The first tectonic milestone can
show current forcing with a declared maturity approximation; it must not claim
to have simulated continental drift or a full geological history.

## Geological units and landscape processes

Rock units carry identity, composition, strength/erodibility, thickness and a
structural coordinate field. Sedimentary bedding can be tilted, folded, faulted
or exposed by incision. Contacts, intrusions, erosion surfaces and deposits have
explicit process order. Continuous surface relief need not imply a blurred rock
contact; a deliberate fault must remain a fault.

An interior/geological section in Planet Builder can inspect this model. The
Landform Designer keeps its flat, fixed-size core-to-sky terrain preview and uses
a disposable context snapshot; it does not become a second planet-history editor.

Compile landscape construction in this order:

1. Planet figure, envelope, profile, reference levels and environmental forcing.
2. Crust and tectonic structure, including authored constraints and inherited events.
3. Geological provinces and initial Landform assignment.
4. All major Landform relief and macro stamps, producing the combined coarse surface.
5. Connected drainage, erosion and deposition on that combined surface, with
   rock resistance and climatic forcing. Coarse climate supplies rainfall,
   temperature and rain-shadow context; ecology remains a downstream consumer.
6. Fine Landform detail and PCG stamps constrained by accepted ridges, channels,
   shorelines and contacts. Validate that this refinement preserves the drainage.
7. Material exposure, regolith/deposits and the shared native presentation field.

Preliminary drainage may help assignment, but it cannot be treated as final
before major Landforms are applied. Macro edits invalidate dependent watersheds
and erosion products; this dependency can extend far beyond the edited footprint.
Use a bounded rebuild sequence with explicit convergence checks rather than an
unlimited circular solve.

Derive drainage globally across province boundaries. Distinguish an ocean outlet,
a real closed basin, a lake spill connection and a numerical depression. Priority-
Flood is an analysis tool, not an instruction to erase every lake or cut channels
through lake floors. All-dry, all-ocean and internally draining worlds need explicit
valid policies; the older mixed-land/ocean draft's rejection rule is not universal.

Start with stream-power incision and hillslope/talus transport. Track sediment
sources, storage and exports, including declared approximations. Advance to
momentum-aware transport for fans, braided channels and deltas after measured
quality gains. Glacial, aeolian and karst processes follow as separate qualified
families. Surface water rendering remains outside this milestone, while terrain
still needs sensible bathymetry and drainage.

## Landforms and regional assignment

A geological province, tectonic plate, drainage basin, biome and VPP chunk are
different partitions. Regions may be guided by cellular structure, but coherent
belts, basin outlines, terraces and drainage divides should shape their domains.
Shared ridges, rivers and faults have identities independent of those regions.

Extend Landform suitability with geological setting, lithology, relative age,
uplift/subsidence, erosion/deposition state, ridge/fold direction, climate context
and characteristic scale. Pass resolved context into an immutable field. The
Landform describes how to express a local terrain family; it does not choose the
planet's plates or own their geological history.

A mountain source follows an orogenic axis; a plateau source expresses an elevated
block or resistant cap; alluvial terrain follows depositional lows. Canyon and
river networks are shared structural features, not independently repeated
rectangular stamps in every cell. Detail stamps remain PCG candidates conditioned
on those constraints, with their accepted deterministic placements shared by the
Designer and native planet.

Preserve palette semantics: one selected Landform fills every generated Landform
region over the common base. Report unsuitable combinations without silently
substituting another source. With several selected Landforms, use contextual
suitability and deterministic variation, provide an explanation and allow manual
overrides. Do not silently introduce unselected terrain types.

## Editorial controls

Planet Builder owns the globe, plates, continents, broad process features,
history, global guides, palette and accepted generation. Keep controls in the left
panel and make diagnostic views selectable there: elevation, crust, plate motion,
boundaries, age, uplift, lithology, drainage, sediment and Landform assignments.

The creator can sketch or move continents, edit boundary curves and Euler motion,
place a hotspot or impact, draw a desired mountain belt and pin important basins
or landmarks. Constrained generation adjusts unpinned content and reports conflicts.
An arbitrary requested mountain range is either reconciled with the structure or
retained as an explicit override; the UI must not imply a solved inverse geology
problem when it has merely added relief.

Separate cheap draft preview from explicit quality generation and acceptance.
Dragging changes draft constraints; expensive history/erosion is coalesced and
cancellable. Idle frames never rerun generation. Stable IDs, transactions and
seed scopes preserve unaffected editorial work. Walk here inspects a frozen
accepted snapshot and returns to the same authored planet.

## Compiler and backend boundary

Extend the existing compiler with a structural producer and typed compiled
products. Retain one composition/presentation path. Preserve old oval/heightmap
recipes through a versioned legacy lowering; do not silently reinterpret them as
tectonic events or add generated uplift twice.

A saved generative description contains exact profile/operator/library versions,
seeds, guides, body/frame conventions, editorial constraints, ordered history and
resource references. Accepted derived products contain spherical crust and
process fields, feature graphs, topology and sparse refinement data where needed.
The planet references their exact content digests and dependency closure.

The backend eventually owns generation and accepted planet revisions. Local
Builder authoring first exercises the same inputs and outputs without inventing
a new backend service or Connect protocol. Connect delivers the agreed recipe and
resources through its existing architecture. A seed alone cannot reproduce an
unversioned numerical simulation.

The client resolves installed operator/asset IDs, validates the snapshot and
resources, and compiles immutable native evaluators. Expensive global geology,
erosion and routing run upstream or in authoring. A voxel query reads accepted
fields and nearby indexed features; it does not solve the planet again. No mutable
UObjects enter worker evaluation. Native VPP noise does not establish cross-platform
bitwise agreement; accept exact derived artifacts when regeneration could differ.

Hybrid resources are legitimate: procedural operators for compact structure,
coarse fields and sparse residuals for irreducible process output. A whole-planet
SDF/streamed surface remains a separate measured experiment. A radial sign field
must not be labelled an exact signed-distance field without establishing that
property. Navigation is a consumer of the resulting geometry, not the geological
analysis mesh.

Every generation job has explicit time, memory, resolution and feature budgets,
progress, cancellation and a generation fence. Failed or unfinished products
never replace the last accepted snapshot. A quality failure is recorded with its
recipe; do not reroll seeds or relax tolerances invisibly.

## Implementation sequence and acceptance

| Milestone | Work | Evidence required before advancing |
| --- | --- | --- |
| 1. Spherical tectonic base | Explicit body basis, crust/plate model, Euler motion, boundary classification, curved structural belts and draft controls; retain existing Landforms downstream | A global continent/subduction case, a collision plateau, a rift/spreading case and a transform case; native globe and walking output; canonical IDs and coordinate compatibility |
| 2. Inherited structure and rock | Bounded history, crust age/thickness response, lithology, bedding, faults, basins and hotspots | Young versus eroded belts, exposed units in sections, rock continuity across regional boundaries and explicit material contacts |
| 3. Watersheds and aging | Global routing over the composed macro surface, explicit lakes/closed basins, incision, talus and sediment baseline | Downstream connectivity, conservative accounting under the selected model, strictly graded channels where required, resolution sensitivity and recorded failures |
| 4. Contextual Landforms | Geological suitability, ribbon/basin domains, conditioned stamp distribution and protected feature corridors | Selected palette semantics, stable placements, Designer/native parity and no blocked drainage after detail |
| 5. Quality and regime expansion | Compare multiscale/2026 transport producers; add single-lid and cratered profiles, then icy/glacial/karst families | Visible and measured improvement on reference classes, bounded generation/runtime cost and platform qualification for each producer |
| 6. Backend activation and scale | Deliver exact accepted resources, exercise missing/stale dependencies and extend beyond the small proof | Server/local snapshot parity, atomic activation, deterministic conformance within declared profiles, multi-scale error and streaming budgets |

Each milestone keeps the existing end-to-end proof working. Backend data contracts
are designed from milestone 1; actual backend activation waits for the relevant
owner/integration work, not a speculative parallel service.

### First implementation scope

Implement milestone 1 next. Use a small explicit Earth-like test setup with
continental and oceanic crust distributed independently of its plate partition.
Compare convergent, divergent, transform and stationary versions of the same
constraints. Produce real curved belts and basin/trench structure before adding
fine terrain. Existing Mountain ridges and Plateaus then express the appropriate
regions; the existing canyon remains an editorial feature until drainage-derived
canyons are qualified.

The current `FCLCompiledPlanetFeature::Frame` uses a latitude convention with +Y
as its pole, while the geographic material grid uses +Z as north and +X as zero
longitude. Introduce a declared authoring basis and versioned conversion before
tectonic editing. Preserve existing 3D feature frames and placements while
converting their displayed coordinates. A camera rotation is not a migration.

Tests must cover closed spherical topology and area, tangential velocity,
relative-motion signs, boundary orientation/polarity, degenerate cases, input
reordering, pole/dateline/cube-edge continuity and native field/collision parity.
Movement reversal must change the structural setting; zero relative motion must
not create a fresh boundary mountain by default. Pin stable feature IDs and test
that unedited accepted instances retain identity.

Use the existing globe-to-ground workflow for visual and collision inspection.
Repeat native walking at poles, equator, patch boundaries and selected belts.
Validate publication/cancellation and preserve old saved planets byte-for-byte
unless an explicit migration is committed.

## How to judge realism

Use a suite of comparable terrain classes and parameter ranges, not one attractive
seed. Compare reference hypsometry, slope/curvature distributions, ridge and valley
orientation, drainage organization, relief by scale, sediment placement and
lithological exposure. Compare ocean-floor patterns separately from continents.
Use class-appropriate references; lunar or miniature artistic profiles should not
be penalized for differing from Earth.

Measure graph/field convergence, shared-feature continuity, generation time,
peak memory, voxel query cost and native render/collision error. Record numeric
thresholds after establishing baseline cases, before tuning the generator. Keep
expert visual review alongside metrics: a smooth seam or a plausible histogram
alone does not establish believable geology.
