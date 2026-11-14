# Planetary surface PCG

`Landform Planet Surface` samples the accepted outer-surface plan in canonical
planet coordinates. It does not sample a world-Z rectangle or change the planet.
`UCLPlanetSurfaceRegion` binds a PCG owner to one disjoint cube-sphere tile and
its explicit `ACLPlanet`. `ConfigureRegion` installs that component on an owner.
The same seeded lattice survives changes in tile count without omissions or
repeat candidates. Cube-face area correction keeps reference-area density
consistent around the body. Native height and finite-difference normal queries
match the Planet Ecosystem preview's surface convention.

Feed these points into an optional classifier before asset selection/spawning.
For trees and upright shrubs, put `Landform Radial Orientation` after slope
classification and before local yaw/scale variation. Ground scatter can keep
its surface-normal orientation. Landform has no Ecosystem dependency.

Surface work yields after 512 candidates and publishes data sets of at most
65,536 points. A region refuses more than two million lattice candidates rather
than silently reducing density. The current grid has at most 200,000 cells per
face axis and up to 64 tiles per axis. These are explicit generation limits,
not a production streaming implementation. Regions are native components and
serialize/remap their Planet references with the owning world, including PIE.

The walking presenter accepts up to 32,768 scenery components. Each tile retains
its own canonical component bounds; a single planet-enclosing scenery component
would fail the presenter's finite-chart bounds contract. The chart itself,
terrain coverage limits and native VPP remain unchanged.

Qualification: `conformance/planet-surface-pcg-mac-arm64.json`. The host proof in
VPPTest58 exercises the existing Biome Core LocalBiomeCore generators/spawners in
96 disjoint regions. Forest classification, asset weighting and floor coverage
are owned by Ecosystem/the host fixture, not this geometry adapter. Automatic world deployment, profile-driven regeneration and packaged streaming
remain separate integration work. Accepted terrain/frame edits now regenerate
configured regions automatically as described below.


## Accepted-surface refresh

Landform fingerprints each accepted terrain input and canonical actor frame.
Configured regions store the fingerprint of their generated scenery. In an editor
world, a shared native queue hides stale instanced products immediately, coalesces
successive edits for one second, cleans the old products and regenerates one PCG
component at a time. Completed products retain the region's native animation
provider. The queue never rewrites the recipe, recreates the world, saves assets
or generates a second forest in PIE. Invalid recipe edits retain the last valid
terrain and its products. Native walking refuses stale or still-generating
regions; wait for the scenery update before clicking the player marker again.

`GetSurfaceGenerationKey` exposes the accepted identity for qualification. Region
identities serialize with the world, avoiding regeneration of an unchanged
recovered/saved snapshot. Geometry, frame and guide changes captured by the
accepted terrain input invalidate its regions. This is bounded editor preview
generation, not production runtime streaming. Existing regions are required;
Landform does not invent a biome deployment or depend on Ecosystem. Editing an
external classifier profile alone and rebaking a host-authored forest-floor mask
remain separate from the shape-change queue.

Each surface data batch carries typed, default-valued metadata:

| Attribute | Type | Meaning |
| --- | --- | --- |
| `Planet_ToWorld` | Transform | Canonical body frame in world centimetres |
| `Planet_ReferenceRadiusMeters` | double | Accepted reference surface radius |
| `Planet_Seed` | int32 | Accepted recipe seed |
| `Planet_SeaLevelMeters` | double | OceanHeight relative to reference radius |

An optional classifier can consume this contract without linking to Landform.
These values supersede stale graph-authoring snapshots; they do not modify
classifier assets or geological ownership. Native tests exercise disjoint
sampling, stable/change-sensitive identity, a live accepted radius edit followed
by automatic PCG regeneration, and optional classifier context consumption.
