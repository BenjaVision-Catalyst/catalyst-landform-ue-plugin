# Spatial cave family generation

11 November 2026. This is a procedural family candidate for visual review. The
previous editor output and fixed-composition placement were rejected. Neither
those reports nor this change's automated checks establish visual acceptance.
The approved visual reference remains the frozen
[spatial study](SpatialCavePrototype.md#shape-and-reference-comparison), its four
original captures and the [canonical strata bindings](CaveStrata.md).

## Generation contract

A recipe and its seeds reproduce the same network placement, topology, joint
footprints, chambers, levels, floor relief and wall variation. Each candidate
receives a distinct bounded random stream. Random draws are sequenced explicitly,
rather than relying on C++ argument evaluation order.

Networks vary independently. The initial size distribution has a small tail
(18%, scale 0.28–0.55), a broad middle (70%, 0.65–1.35) and a large tail
(12%, 1.5–2.8). These are anatomical scale factors relative to the approved metre
scale, before physical fit. The initial depth distribution is one to five levels
(34%, 34%, 19%, 9%, 4%). Placement, crowding and available body volume constrain
the realized population; these percentages are construction priors, not measured
planet-wide frequencies or a geological simulation.

Each level builds its own branching joint graph, independent strike and unequal chamber footprints.
Chamber faces, recesses, roof pockets, shoulders and surviving roof masses vary
independently. Passages compile broad, independently varying wall footprints.
There is no nearest-spline query, capsule cutter, ring gallery or copied
passage/chamber composition. Traversal records describe the generated field;
they do not generate it. The frozen profile-8 composition remains unchanged.

The scalar and native SIMD paths extend the same spatial morphology kernel used
by that study. Broad spatial wall warp, contact-local recesses, undercuts,
independent roof pockets and retained rock remain the common anatomy.

## Floors and canonical rock

All regions on a level share one rolling sediment/bedrock relief field, including
banks and a shallow channel. Level datums differ by physical depth. Connecting
passages join the adjoining datum over broad, smooth ramps; their attachment
aprons retain the exact adjoining floor before the rise begins. Successive ramps are routed clear of neighbouring floor foundations. No floor plane
clips a chamber mouth.

Restored bedding may guide the placement when its route is walkable. If a steep
site requires radial floor placement, canonical rock still supplies the wall
contact coordinates. It also continues to supply all material, stratum and
geological-property queries. Radial placement does not invent carbonate metadata,
replace a unit or repaint its surface. The current Rift recipe's authored palette
therefore remains visible; it is not replaced with the carbonate study's recipe.

Physical fit includes the generated roof envelope and all connected levels.
Ordinary small caves retain walking clearance. Small bodies adapt the complete
spatial anatomy and scale; they do not select a simpler generator. Enabled caves
have a nonempty minimum even at character or density zero and without known rock,
province or drainage metadata. Disabling Caves requests none.

Generation validates the actual full-width head and foot field before publishing a network, and rejects overlaps that would remove another network’s walking foundation. Unsafe proposals are retried within the same family; no partial network or alternate generator is published.

## Native editor delivery

The existing UI and VPP remain in charge of meshing, camera LOD, materials,
collision and ordered native edits. There is no cave mesh actor or substitute
collider. Diagnostic inspection uses the existing native presenter per joined
support domain, with native SetupForPreview quality and chunk size.

Inspection resolution now follows the selected cave's physical minimum aperture.
Its exterior distance remains finite for stock coarse-cell interpolation.
Complete-planet evaluation excludes certified cave-free supports from cavity
composition, so an empty network envelope cannot manufacture terrain refinement.

The editor interior review uses the actual saved
`/Game/LandformDesigner/Provinces/DA_ContinentalRiftReview` recipe. It renders the
same `SCLPlanetPreview` and native geological terrain presenter used by the
Designer, with its existing camera-aimed studio lights. It records passages,
thresholds, chambers, small and large examples and a deep upper level after native
publication and tasks settle. It does not rewrite the review asset.

## Review and evidence

The new `ProceduralFamily` tests check seed variation, single and deep systems,
size spread, exact regeneration, graph connectivity, a full-width clearance band,
continuous footing, grades, scalar/SIMD agreement, actual scene air, canonical
geological identities and material bindings. The frozen spatial study, enabled
minimum (including missing/unknown geology and small planets), native distance
and persistence checks remain separate regressions.

`PlanetWideNativeDebugAndNavigation.CurrentRift` qualifies actual visible cave
pixels, native publication/task readiness and close-up detail. Its established
half-native-cell overview and 0.5 m close-up field budgets remain in force.
Field tests and cave counts are not a substitute for comparing these editor
captures with the approved images. User visual review remains an explicit gate.

The [review evidence](../conformance/SpatialFamilyGeneration/README.md) includes
unedited native editor captures from the saved Rift recipe, camera positions,
field-measured openings, the build result, all 14 passing regression checks and
source hashes matching the staged plugin. The representative passage measures
34 m wide and 9.25 m high; its chamber measures 64.5–75 m across and 34.5 m high.
The captured Rift population contains 25 networks with one to four levels.
Three additional seed runs include five-level systems and independently varied
chambers, with maximum tested route grades of 27.5–30.4%.

The editor images retain the Rift's authored pale/dark strata and studio lights.
They therefore differ from the warm, nearly uniform original study. The broad
passage and separate high chamber can be compared in physical scale, but these
differences and the visible floor transitions must be judged from the captures.
No visual equivalence or acceptance is asserted here.

The same saved Rift snapshot also passed the existing full-planet native walking
and edit proof: 78.79 m at 600 cm/s, all 1,079 route frames grounded, zero collision
waits, stable runtime/invoker owner, four contacts, mining, placement and compressed
sculpt save. Its warm walking-review captures are labelled separately from the
editor images. The save proof did not include a cold reader process.
