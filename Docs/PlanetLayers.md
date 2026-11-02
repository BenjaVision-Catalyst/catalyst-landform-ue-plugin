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
editable. Advanced pins keep their selected values as the knob changes. Unpinned
Advanced fields display resolved extent, motion, relief and plate count while a
knob is being dragged. The draft does not silently enable any pin.

Horizontal globe dragging follows the grabbed surface. The Preview tab's Ocean
checkbox adds a transient blue translucent sphere at `OceanHeight`. It is off by
default, follows guide edits even when rings are hidden, has no collision, and
hides if the guide is removed. It is absent from recipes and exported JSON.
It is a visual reference, with no waves, fluid simulation or walking collision.

Each accepted globe edit first publishes a 64-cell draft, then refines the same
immutable plan to 512 cells. Revision and cancellation fences keep stale jobs
from replacing newer edits. Variation changes broad continental geography and
controlled boundary orientation as well as local texture.

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

## Calibrated one-knob mapping (schema 16)

The continental foundation uses seeded spherical domains with continuous
basin-to-upland elevation and low-amplitude interior relief. It avoids hard
plateaus from saturating the crust mask. Continental strength drives extent
0.7–1.2 and relief 0.05–0.22 unless pinned. `OceanHeight`, when present, is the
continental datum; without it the neutral base surface is the datum. The ocean
checkbox never changes this field.

Tectonic activity coordinates the plate-count progression, motion 0–1.1,
relief 0.03–0.25 and belt width 6–12 degrees. Controlled boundaries use a smooth
crust-side transition; choosing one side abruptly introduced a height jump.
Both layers cap their effective guide interval at a quarter of the neutral
surface radius. Large guide gaps therefore do not turn default relief into
planet-scale walls. These bounds tune an artistic construction; they do not
constitute a geological evolution or erosion simulation.

Continental margins and tectonic deformation have distinct responsibilities.
Real shelves, slopes, rises and basins are the reference rather than stacked
terraces; see [NOAA's ocean-floor overview](https://www.noaa.gov/education/resource-collections/ocean-coasts/ocean-floor-features)
and [USGS's plate/crust explanation](https://pubs.usgs.gov/gip/dynamic/tectonic.html).
The exact coefficients above are Landform's artistic calibration, not numbers
claimed by those sources.

## Compatibility

Pinned schema 14 recipes keep their combined semantics and existing algorithms.
Schema 15 retains the first separated field and its original knob mapping.
Opening a saved recipe neither converts it nor marks it dirty. The inspector
projects its implicit crust and tectonics into the two control groups. The first
geology edit adopts schema 16 in the same Undo transaction. This deliberately
changes the old combined construction into the separated model; Undo restores
the exact prior recipe. It is not a geometry-preserving automatic migration.
The historical C++ name `FCLTectonicPreset` remains for serialized compatibility;
its editor display name and controls identify it as a tectonic layer.

The portable [LayeredGeology.json](../Examples/LayeredGeology.json) demonstrates
both enabled layers. Earlier examples and saved assets remain pinned. Scientific
crust evolution, erosion and geological guidance of regional assignment remain
separate future work. Shared curvature inspection retains its existing material,
local-chart and open performance limits.
