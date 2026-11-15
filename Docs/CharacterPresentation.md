# Character presentation

`FCLPlanetCurvature` evaluates the accepted recipe's resolved `SurfaceMax` and
outer Sky boundary in canonical metres. At or below SurfaceMax it returns the
host's ground curvature (ThatGame uses 0.15); at or above Sky it returns 1.
Between them it applies quintic smoothstep, with zero first and second derivatives
at both boundaries. Descent and ascent use exactly the same function. There is no
time filter that could carry curvature beyond either boundary. Missing/invalid
boundaries fail rather than manufacturing an altitude range.

The host publishes one `FCLPlanetProjection` for terrain, camera and characters.
The chart reference follows the physical ground chosen at entry, independently
of the guides that control blending. Using SurfaceMax as that reference would
magnify low-lying terrain and change near-camera scale.
`FCLRigidActorPresentation` creates a transient, nonreplicated actor containing
render-only copies of the source actor's visible skeletal meshes, static meshes,
grooms and attached equipment. Skeletal copies follow their original mesh's
leader pose; they do not run a second animation graph. Groom copies attach to the
corresponding copied skeletal component. One projected position/orientation for
the entire actor preserves rigid body/clothing/hair/equipment relationships.
Character size and animation stay unchanged; this is a local rigid approximation
rather than per-vertex curvature of a character.

Original animation, component/socket transforms, collision and actor replication
remain canonical. The binding suppresses original main/depth passes and shadows,
while keeping source skeletal animation refreshed. It samples moving source
transforms on each camera update. Attached equipment is discovered at most every
250 ms, with a 256-component limit per actor. Destroyed sources retire their
copies. Retirement restores original render flags and animation tick policy.
Out-of-chart copies are hidden; this does not add a far-field character renderer.

One presentation owner is supported per local view/world. Network clients have
independent worlds and charts. Same-world split-screen requires per-view primitive
visibility and is not supported by this owner. Native Nanite terrain continues to
use the existing isolated UE 5.8.4 shader overlay and stock VPP generation,
collision and materials. The existing direct-light presentation limitations and
finite chart remain; this adapter does not qualify whole-planet chart coverage,
Lumen, arbitrary character material displacement or production multiplayer load.

Focused checks are `Catalyst.Landform.Presentation.GuideCurvature` and
`Catalyst.Landform.Presentation.RigidActors`. The consuming game's retained play
qualification owns actual MetaHuman, ACF, camera and network evidence.

UE 5.8.4 Mac arm64 qualification in ThatGame58 passes all fourteen presentation
editor tests, including CPU/GPU parity, guide easing and moving rigid copies.
The consuming game's `conformance/curvature-mac-arm64.json` records actual Shaki
body/clothing/groom/equipment, side/underside alignment, reversible flight and
two PIE network clients. The space/ground case retains different curvatures in
each world; the distant player follows native relevancy and is presented again
when replicated back into range. This is a bounded functional profile, not a
production load or dedicated-server performance qualification.
