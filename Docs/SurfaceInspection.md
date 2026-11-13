# Globe-to-ground inspection

The Planet globe shows a **player location** icon by default, initially at the
north pole. Drag the icon onto native terrain to move it. Release places it;
clicking the icon without dragging starts walking. Drag elsewhere to orbit.
Preview’s **Player location** checkbox controls visibility. Native Landform
transform handles retain precedence where they overlap the icon. There is no picking
mode or Walk button. The icon remains discoverable in a muted colour on the far
side and at the viewport edge when its projection is outside the view.

Dragging never launches PIE, or edits Landform placements, regions, guides or
geometry. Off-surface drag motion keeps the last valid point. Esc or lost capture
restores the drag start. Empty-space clicks retain the marker. Authoring rebuilds
retain its body-space direction and invalidate an obsolete exact surface point.
The returned walked position updates the same marker.

The Landform plugin registers its walking host through `FCLPlanetInspection::Launch`.
A request contains the explicit source asset, a portable JSON recipe snapshot,
a planet-local unit direction, an identity and a weak editor callback. The host
starts one standalone PIE instance by duplicating the **current world**, including
its existing biome instances. It never replaces the map with Engine Entry or
requires LandformLab code/content. The existing actor using that source asset is
reused in PIE; multiple matching actors are refused. If no actor is placed, only
the PIE world receives an ephemeral actor for the same source Planet. The source
world and authoring assets are untouched. Play settings are duplicated, not saved.
Overlapping launches are refused.
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

**WASD**, mouse and **Space** use local-gravity movement at **20 m/s** (2,000 cm/s).
**Esc** ends PIE and
marks the last walked surface in Planet. Walking uses a fixed snapshot; start a
new inspection to see subsequent authoring changes. The reusable player, native
collision invokers, HUD and inspection game mode belong to the runtime Landform
module; PIE admission and return belong to its editor module. No host-project
adapter or content is needed. This native walking host requires unit actor scale
and native canonical terrain, rather than a projected sampling window.
Walking renders a flat local chart through the shared
[terrain, scenery and camera presentation](WalkingPresentation.md); collision and
radial gravity remain spherical.

## Qualification

- `Catalyst.Landform.Inspection.RadialLanding`: radial positions and normals at
  poles, equator and a patch boundary, plus invalid centre/local-patch requests.
- `Catalyst.Landform.Inspection.PluginWalkingHost`: host registration and reusable
  player ownership without a project adapter.
- `Catalyst.Landform.PlanetBuilder.PlayerLocationMarker`: routed Slate click/drag
  separation, north-pole default, cancellation, hiding, far-side visibility,
  retained location after empty-space clicks and unchanged camera during drag.
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
