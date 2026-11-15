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
are refused while uploading. A retained native Dynamic Mesh uploads only when
the payload changes; per-frame mesh construction is avoided. The material is
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
