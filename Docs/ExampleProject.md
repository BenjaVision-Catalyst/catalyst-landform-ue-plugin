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
- Native VPP MegaMaterial `AttributePostProcess` applies one shared
  `MF_PlanetPresentation` function after ordinary native surface selection and PBR
  shading. The original materials sample UE's world position excluding offsets,
  preserving canonical texture/geology coordinates while their completed position
  and normal are projected. Primitive data without the presentation packet is
  unchanged. Native slot bindings are retained, with zero presentation overrides.
- `FCLNativePlanetPresentation` is the promoted Lab binding, now maintained in the
  component. Lab delegates to this same implementation. It retains native mesh
  lifecycle subscriptions, bounds admission, material identity guards and detach
  restoration. UE's native camera-modifier hook projects the final camera view;
  gameplay actors, collision, invokers and gravity remain canonical.

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

## Curve-to-flat approach

The example uses the existing spherical-to-flat map, becoming flat near the
ground and reversing on ascent. Its reference radius is the physical landing
surface, independent of the recipe's sky-envelope naming convention. The blend
starts within `min(0.25 * reference radius, 1 km)` above that surface, with at least
a metre between its start and grounded eye height. A smoothstep maps altitude to
curvature; the grounded camera uses curvature zero. Native full-sphere visibility
is retained at curvature one, with the existing 60-degree chart admission during
flattening. The anchor follows the canonical camera's radial direction.

The existing origin-centred/local-window packets cannot describe a translated
whole planet. Packet 9150 transports current/previous world anchors as UE
high/low coordinates. It reuses `CLAnchoredPosition`/`CLAnchoredNormal` and native
UE double-float arithmetic, rather than introducing another projection or origin
manager. Current terrain/camera state is published after physics and checked
against the current render frame. The camera inverse is checked independently.

The first rendered runs demonstrated two integration gaps. UE's camera actor
path bypasses actor `CalcCamera`; its native camera modifier now applies the
shared frame to both flight and walking views. The previous Lab binding's
512-mesh metadata limit rejected Mars. Whole-planet binding now admits up to
2,048 native meshes (the qualified Mars run peaks at 822); the legacy Lab profile
retains its limit of 512. This changes metadata capacity only: VPP owns every
mesh, LOD decision, ready/retire event and collision update.

[The approach qualification](../conformance/example-approach-mac-arm64.json)
records the cooked journeys, spherical control, GPU parity and preservation
checks. The previous canonical spherical record is retained as a baseline.
The `projectionPrototype` report key/profile label is retained for older Lab
consumers; native mesh replacement during normal VPP LOD is expected and reported.
This record does not claim fixed generated meshes or collision identities.

The 25 m asteroid visibly exposes the finite chart boundary as a jagged outer
edge. The local flattening/walking mechanism passes; an edge-free whole-planet
illusion is not established. A future far-field presentation decision must keep
the same canonical VPP source and camera/interaction contract.

The qualified profile remains direct-lit, without authored WPO/tessellation or
Lumen/distance-field lighting. The presentation scope restores its captured
lighting CVars when detached. General third-person/ACF actors, combat/building
visuals, arbitrary materials and performance are separate qualifications. The
example adds no actor-moving workaround or custom streaming manager. Existing
inverse projection/trace APIs remain the interaction boundary; no mining tool
or alternate interaction path is introduced by this change.
