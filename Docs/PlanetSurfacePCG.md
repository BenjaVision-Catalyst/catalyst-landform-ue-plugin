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
are owned by Ecosystem/the host fixture, not this geometry adapter. Automatic
world deployment, profile-driven regeneration and packaged streaming remain
separate integration work.
