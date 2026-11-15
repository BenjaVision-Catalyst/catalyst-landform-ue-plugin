# Planetary PCG integration

Voxel Plugin supplies the terrain sampler and dependency tracking. Unreal PCG's
Runtime Generation system owns generation sources, hierarchical partition cells,
priorities, cancellation, pooling and cleanup. Landform does not implement a
second terrain sampler, spherical tile generator or generation queue.

Configure native PCG graphs with hierarchical generation and a 3D grid. In-world
components use Generate at Runtime and Is Partitioned. Register a native PCG
Generation Source on the player. A PCGWorldActor must exist in the running world;
Ecosystems supplies that native service when activating its configured content.
Use VPP's Voxel Sampler on the Planet's volume layer with Track Layer Changes
on. It samples the actual composed VPP field, including cuts and cave surfaces,
and VPP requests native PCG regeneration when queried terrain dependencies change.

`UCLPlanetPCGLibrary::BindSource` attaches `UCLPlanetSurfaceRegion` to an explicit
original PCG execution source. This component contains the Planet reference and
optional native wind transform provider, not generation bounds or a tile index.
Partition components resolve it through PCG's `GetOriginalComponent()`. There is
no first-Planet lookup and no Ecosystem dependency in Landform.

`Landform Radial Orientation` preserves native point positions, seeds and
metadata while aligning upright trees to the bound Planet centre. Ground scatter
can retain VPP's sampled surface normal. An optional classifier supplies biome
rules, current planetary context and support-slope filtering.

Landform observes native PCG completion and cleanup to attach/retire curved
rendering and restore native animation providers. The observer never calls
Generate, Cleanup or Cancel. Native PCG settings determine task concurrency and
generation distances; distant Nanite trees do not require a separate scheduler.

## Walking and reopening

The Planet asset stores `WalkingMaterial`, `WalkingFrame` and `WalkingVoxelSize`
outside its portable recipe. The exact bound Planet actor is reused when present.
Otherwise, the host creates a fully configured transient native Planet in the
existing world. It validates optional content references and compiles presentation
materials without generating a forest in the editor.

The running world starts its own native runtime PCG. Existing editor-generated
scenery is excluded from PIE duplication. Walking waits for collision and local
native PCG readiness, not a complete planet. Regions outside the player's support
are neither pre-generated nor saved as world products. Missing configured art or
materials fails explicitly. A recovery world or Python deployment script is not a
runtime input.

Walking explicitly clears PIE spectator-only mode and checks both possession
links before admitting the player. Holding Control while starting PIE must not
turn a player-marker request into an unpossessed character. EndPIE drains queued
Slate rendering before the floating game viewport loses its game-thread owner.

New native PCG products receive transient projection material copies and share
the same curved presentation as terrain. Ground appearance receives transient
texture parameters, allowing Ecosystems to update coverage without rebaking a
planet texture. Source materials and licensed meshes remain untouched. Accepted
recipe updates preserve opaque client links and active material composition;
the presenter rebinds to the new accepted terrain rather than retaining a frozen
sampling snapshot.

## Walking Nanite capacity

Dense native canopy exceeded the engine's default 2,097,152 hierarchy traversal
nodes, with 3,308,649 observed. Walking reserves at least 4,194,304 nodes through
`r.Nanite.MaxNodes`. This is independent of the existing tessellation candidate
patch capacity. The extra node-buffer allocation is 40–56 MiB on the qualified
UE 5.8 path, depending on native assembly support.

The budget does not reduce canopy coverage or change native PCG generation.
A larger host value is retained. The scoped render-settings owner restores its
prior value on return, unless another owner changed it during walking. The
presentation report includes the actual `naniteNodeCapacity`. Qualify native
feedback while walking/turning; absence of one screenshot warning is not a
universal workload-capacity guarantee.

## Authoring assignment queries

`FCLGroundSurfaceSampler` and `ACLPlanet::SampleGroundSurface` remain available for
bounded, immutable authored-recipe queries such as the ecosystem overview map.
They resolve cave-aware ground in the compiled recipe. They are not the runtime
PCG surface source and do not replace VPP's tracking of live volume edits.

Native source-context and recipe-resource tests cover explicit binding, radial
orientation, preserved native roots/seeds and atomic live replacement. Rendered
qualification must additionally exercise a fresh project open, the actual Planet
marker, movement into new native cells and a live VPP terrain change. Counts alone
are not rendered acceptance. Historical custom-tile reports describe the removed
implementation and do not qualify the native runtime path.

References: [VPP PCG setup](https://docs.voxelplugin.com/knowledgebase/foliage/configuring-pcg),
[Epic runtime generation](https://dev.epicgames.com/documentation/unreal-engine/using-pcg-generation-modes-in-unreal-engine).
