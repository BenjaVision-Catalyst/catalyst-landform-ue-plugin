# Planet layers and inspector

Continents and Tectonics are independent layers in one planet. A preset is a
mapping from a layer's one-knob control to its underlying values. It is not a
choice between continental and tectonic planets.

## Editing

Planet Builder uses scoped native Unreal Details views in workflow order:

| Tab | Owns |
| --- | --- |
| Planet | Name, outer radius, shared Radial Guide set; advanced neutral surface and world seed |
| Geology | Independently enabled Continents and Tectonics layers |
| Landforms | Broad editorial structures, base terrain, placed Landforms and regional distribution |
| Surface | Ground material stack |
| Preview | Globe/cutaway display, guide visibility and surface inspection |

Geology is the initial working tab. Each layer presents one slider and a compact
variation action. Native Advanced disclosures reveal its seed and detailed
values. Instructional prose, generated counts, raw recipe wrappers and schema
fields are absent from the normal inspector. Validation remains available through
the toolkit's Window menu; generation errors appear only when present.

Dragging publishes live draft previews, releases to full quality, and creates one
Undo transaction. A variation changes only that layer's seed, preserving knob
values and the other layer's seed. The planet's world seed remains separately
editable. Advanced pins keep their selected values as the knob changes.

## Compilation

Schema 15 adds `Continents` alongside `Tectonics`. The continental layer owns
broad crust domains, shelves and oceanic/continental elevation contrast. Its
strength mapping adjusts extent and relief unless pinned in Advanced. Tectonics
uses that shared continental context to form deformation; it contributes no
second implicit continental elevation. With Continents disabled, the tectonic
layer uses oceanic crust context over the neutral base.

Continental and tectonic seeds are independently combined with the planet seed.
Plate IDs depend on the tectonic seed; changing continental variation retains
those plate identities while their crust-dependent deformation can respond.
The moving-plate solver samples crust context at 17 positions along each boundary
when compiled, then interpolates during evaluation. The continental field itself
remains analytical. The bounded cache avoids repeatedly evaluating coast noise
for every boundary at every terrain vertex. Native VPP, globe and cutaway consume
the same compiled field. Regional child fields clear both geological layers so
the parent adds their contributions once.

The default Tectonics control mapping coordinates plate count, motion, belt width
and relief. Advanced also offers the controlled-boundary model retained from the
earlier proof. Preset mapping is distinct from the layer's enabled state, seed and
composition. This task does not add a preset-library UI or geological simulation.

## Compatibility

Pinned schema 14 recipes keep their combined semantics and existing algorithms.
Opening a saved recipe neither converts it nor marks it dirty. The inspector
projects its implicit crust and tectonics into the two control groups. The first
geology edit adopts schema 15 in the same Undo transaction. This deliberately
changes the old combined construction into the separated model; Undo restores
the exact prior recipe. It is not a geometry-preserving automatic migration.
The historical C++ name `FCLTectonicPreset` remains for serialized compatibility;
its editor display name and controls identify it as a tectonic layer.

The portable [LayeredGeology.json](../Examples/LayeredGeology.json) demonstrates
both enabled layers. Earlier examples and saved assets remain pinned. Scientific
crust evolution, erosion and geological guidance of regional assignment remain
separate future work. Shared curvature inspection retains its existing material,
local-chart and open performance limits.
