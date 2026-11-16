# Planet Builder overlays

`FCLPlanetBuilderOverlay` is an optional immutable, metric body-space presentation
payload. It has no weather or other provider semantics. The viewport owns native
mesh/material lifetime and hides overlays until its accepted native planet plan
matches. Provider retirement clears its overlay; viewport teardown removes its
private mesh component. Overlay meshes have no collision, selection or shadows.

Existing point/vector arrays remain supported. Optional vertex-colour indexed
meshes, line segments and labels allow continuous spherical maps. Limits are
8192 vertices / 49152 indices, 16384 segments, 32 labels, and 512 arrows at the
generic renderer boundary. Providers can impose tighter bounds. Invalid indices
are refused while uploading. A retained native Dynamic Mesh for filled maps uploads only when
the payload changes; its mesh is not rebuilt per frame. Screen-sized annotation
quads are batched for each view. The material is
transient, unlit and translucent at 18% opacity; no copied artwork or persistent material asset
is needed. Lines and labels cull the far hemisphere; the heatmap uses back-face
culling and native depth. Labels below the sampled terrain are hidden.

`CLPlanetOverlayRadiusMeters` resolves the named **Overlay** guide from the
compiled plan. If it is absent, the radius is `Plan.RadiusMeters`, the outer sky
boundary. It never substitutes AtmosphereMax. Providers project their geometry
onto that radius and may inset heatmaps slightly behind foreground contours.
Guide height does not implicitly change any domain input or physical altitude.

New built-in guide layouts include Overlay at AtmosphereMax when present, else
at sky. Existing styles remain editable and are not silently upgraded to add a
guide. The ordinary radial-guide authoring controls add, rename, move and remove
Overlay, with their existing shared-catalog and undo behaviour.

Ecosystem's weather provider supplies pressure/heatmap semantics and detects
whether an Overlay edit changes terrain bindings before retaining its bake/time.
Other providers make that choice through their own input contracts. This editor
scene path does not add a second gameplay curvature or weather renderer.


## Compatibility gate found during qualification

The existing `RadialGuides.AuthoringAndResize` test reaches a schema-1 / terrain
algorithm-1 import and fails with `Unsupported recipe schema or algorithm version`.
The rejecting compiler and importer are unchanged from `a92069f`; the guide-style
change does not alter that guard. The test and documented compatibility contract
are retained. Legacy algorithm restoration/migration remains an open existing
gate, outside this editor overlay slice. No legacy profile was silently changed.

## Line quality

Planet Designer renders 3D annotation lines as camera-facing translucent quads
with analytic capsule edge coverage. A one-pixel opacity transition smooths both
edges and round caps, independently of editor MSAA support. Provider contours,
fronts, flow arrowheads, geographic grids, placement outlines and generated
boundaries share this path. Each segment uses four vertices and two triangles;
world and foreground batches retain their existing depth priority. The transient
unlit material belongs to the preview and writes no assets. Native world depth
and the existing hemisphere culling are preserved.

The preview requests 100% primary resolution and high-DPI rendering locally;
it does not rewrite global editor or game rendering preferences. Geometry uses
screen-pixel widths at each endpoint's depth, keeping coverage stable through
orbit and zoom. Domain sample coordinates and timelines are unchanged.

Radial-guide rings, ticks and leaders already use Slate's antialiased line path.
Rings now adapt to projected screen size, with at most eight-pixel chords and
96–2048 segments, rounded to multiples of eight to retain exact diagonal anchors.
The maximum segment bound takes precedence at extreme zoom. Ring tessellation is
presentation only; guide heights, provider samples and terrain are unchanged.

Qualification on UE 5.8.4 / macOS arm64 / Metal (16 November 2026): ten focused
native checks pass, zero warnings/failures/not-run cases. A frame-aware readback
of the same isolated diagonal finds 0 fractional blue edge pixels with direct
primitive lines and 344 with analytic coverage (contrast peaks 122/156). The
comparison disables temporal AA and excludes terrain, text and the heatmap.
Guide projection retains exact diagonal anchors with adaptive tessellation; the
36-orientation lighting sweep and all seven Weather/editor checks pass, including
rendered marching heads with frozen field data and 132 GT/native point cases.
No portable weather program or GT source was changed.

Evidence in ThatGame58: `Saved/PlanetLinesQualification/`, matching log under
`Saved/Logs/`, and `Saved/Verification/planet-lines-{aliased,antialiased}.png`.
Workspace native lease evidence is retained under
`recovery/planet-lines-20261116/native-6/`: released gate, restored original
registries, independent Runtime owner joined through cleanup. The live GT image
remains open. Pixel counts qualify this fixture; they are not a GPU-time claim.

## Marching paths

Optional `Flows` contain closed metric spherical paths with precomputed cumulative
arc lengths, stable keys, signed visual speeds and arrow counts. Up to 256 paths,
16384 edges and 2048 heads (128 per path) are admitted by the weather provider.
The viewport retains only current keyed phases and integrates elapsed real time,
preserving spacing through payload updates. Each arrowhead samples a spherical
arc and its tangent; screen-sized chevrons have no vector shaft. Far-side culling
and native depth apply. Hidden/retired payloads clear their phase state. This
animation never changes a provider's simulation clock or sample values.
