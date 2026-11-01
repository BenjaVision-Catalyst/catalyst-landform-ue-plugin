# Globe-to-ground inspection

In Planet Builder’s left **Surface inspection** panel, enable **Choose walking
location**, click terrain, then **Walk here**. Picking does not edit Landform
placements, regions, guides or geometry. Empty space clears the destination.
Normal viewport orbit remains available.

LandformLab registers one host through `FCLPlanetInspection::Launch`. A request
contains a portable JSON recipe snapshot, a planet-local unit direction, an
identity and a weak editor callback. The host starts one standalone PIE instance
using Engine Entry and the runtime recipe game mode. The open map, authoring
assets and project play settings are preserved. Overlapping launches are refused.
Closing or cancelling PIE retires the request. Return callbacks are fenced to the
requesting editor identity. Other projects can register their own walking host.

## Landing

The shared field searches the selected point, then 16 points on each 2, 5, 10 and
20 metre ring. The first outward slope below 38 degrees becomes a candidate.
The host reports any adjustment; it refuses a location without suitable footing.

After possession, the character is held with movement disabled above the candidate.
Its VPP collision invoker brings in local collision. Release requires VPP’s current
state to finish, a walkable native floor belonging to the explicit planet within
one voxel of the recipe, and clearance for the held capsule. Ordinary falling
establishes ground contact. Controls are enabled after stable native contact.
A 120 second collision limit or 8 second grounding limit stops the character with
an error. No per-frame relocation is used after release.

**WASD**, mouse and **Space** use normal local-gravity movement. The fixed proof
auto-route is disabled. **Tab** frames the globe around the current location with
a gold marker; Tab again returns to the character. **Esc** ends PIE and marks the
last walked location in Planet Builder. Walking uses a fixed snapshot; start a
new inspection to see subsequent authoring changes.

## Qualification

- `Catalyst.Landform.Inspection.RadialLanding`: radial positions and normals at
  poles, equator and a patch boundary, plus invalid centre/local-patch requests.
- `Catalyst.Landform.PlanetBuilder.WalkingLocationPicking`: routed Slate clicks
  through the rendered viewport, recipe-space destination and empty-space clear.
- `LandformLab.Inspection.EditorRoundTrip`: real PIE launch from the source planet,
  overlapping launch refusal, collision walking, globe transition, exactly one
  return callback with the final location, unchanged source recipe.
- `verify-packaged-runtime-recipe.py --inspection`: the same landing gates without
  editor code, at both poles, four equatorial locations, a patch boundary, one cell
  per terrain type and the authored canyon. Every location must walk at least one
  metre, remain grounded on native collision and agree with the field within one
  voxel.

This is a bounded 1 km proof, not qualification of unlimited planet scale,
arbitrary cliffs or long-distance streaming. See the conformance record for
actual platform results.
