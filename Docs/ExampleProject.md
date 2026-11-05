# User-facing example boundary

The workspace's `Landform/Projects/LandformExamples` owns the initial library and
one globe-to-ground level. The existing asset editors are World Builder.
`LandformLab` retains qualification fixtures, reproductions and research. Its
assets are absent from the example project's Content tree. Existing local edits
remain in place. The workspace's example asset-disposition record assigns an
explicit role to every pre-cleanup package, including dynamically resolved native
catalog resources.

## Native ownership

- VPP owns volume generation, native stamp scaling, independent layer stacks,
  LOD, render/collision meshes and moving collision invokers.
- UE owns the camera and `APlayerController::HiddenActors` per-view filtering.
  This covers future native mesh components without hiding source actors, which
  would unregister their stamps. Gameplay bodies keep their runtime and fixed
  canonical frame when the camera returns to the gallery.
- Landform owns spherical geology/recipe evaluation and the existing radial
  gravity adapter. The example adds selection, camera transitions and landing
  orchestration around those existing features.
- `DisplayRadiusMeters` scales the native terrain stamp, leaving recipe units and
  physical gameplay bodies unchanged. Zero preserves ordinary actor behavior.
  Invalid scales and combinations with sampling-window coordinates are rejected.
- Material specialization is limited to the existing high/low coordinate
  metadata. The authored surface shader previously assumed a display globe at
  world origin; it now subtracts UE's native actor-origin data before decoding
  native stamp scale. Its Eye graph already supplies actor-relative coordinates.
  No origin manager or streaming manager is introduced.

Planet sizes follow the user-selected recipe/sky-envelope radius convention:
25 m asteroid, 100 m, 1 km, 10 km and the existing measured Mars recipe. Caves are
disabled because their last morphology failed visual acceptance. Physics records
remain historical evidence and do not establish acceptable cave appearance.

Qualification tests load explicitly in LandformLab. Consumer editors do not
implicitly load fixture-dependent tests. Shared dependency scripts live under
workspace `Landform/Tools`; old Lab entry points delegate there.

The editor-binary `-game` harness exposes a native VPP viewport assumption when
UE's LevelEditor module is absent. Loading that module in `-game` mode in turn
triggers UE's MainFrame ensure. That harness is rejected; use actual editor PIE
and the cooked game, recorded separately. No vendor patch or alternate product
generation path was added for the unsupported harness.

## Demonstrated gaps corrected

The 25 m starter exposed a Landform normal-calculation defect: normalizing a
cross product in square metres with UE's default tolerance returned zero for a
valid small spherical surface. Normalize the tangent vectors before their cross
product. Native floor queries then accept the asteroid's original preferred
landing point. No global landing-search policy or terrain change was needed.
The regression spans six radii, two directions, spherical floors and rescaled
relief slopes.

The first cooked launch exposed the catalog's missing cook root. The existing
native PrimaryAssetLabel now explicitly includes that path-selected catalog as
well as dynamic terrain/material catalogs. This is asset configuration, not a
second loading or streaming system.

The gallery uses the native VPP Nanite path on both sides of the camera handoff.
Procedural physical bodies request the same precise material mapping at unit
stamp scale; measured Mars retains its native body-relative material path.
Display globes do not cast shadows onto neighboring display globes. The authored
material's ordinary debug-grid parameter defaults off; dedicated preview grid
materials and explicit preview overrides remain available.

## Approach presentation remains pending

The user clarified during this work that the approach must progressively blend
the existing curve-to-flat presentation and reverse that blend on departure.
The recorded five-planet run qualifies the canonical spherical baseline only.
It is not evidence of this presentation integration. The existing projection
math/interaction API lives in the component, while the bounded native Nanite
binding and gameplay camera fixture remain Lab-owned. Reuse and promote that
path with matching camera/terrain state; keep canonical VPP collision, physical
recipes and gameplay actor frames unchanged. The new host must not create an
alternate projection or streaming implementation.
