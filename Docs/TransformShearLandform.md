# Transform and shear belt

The accepted family has four anatomically distinct examples. Character has no
privileged midpoint. Each position must produce useful terrain, with a stronger
overall expression toward the right. Individual values rise and fall independently.

| Character | Expression | Anatomy |
| --- | --- | --- |
| 0 | Linear shear valley | Broad weathered trough, rolling shoulders, low uneven ridges and saddles; Olema Valley reference. |
| 30 | Offset ridge landscape | Laterally displaced banks and ridges, shutter ridges and broken gullies; Carrizo Plain / Wallace Creek reference. |
| 65 | Stepover basin landscape | Overlapping strands around asymmetric elongated depressions, low dividing ridges and saddles. |
| 100 | Pressure ridge landscape | Raised asymmetric, dissected ridges at constraining bends; Dragon’s Back reference. |

The three halfway review positions are 15, 47.5 and 82.5. Quintic interpolation
blends each physical value independently. Valley depth decreases from the first
to third anchor; lateral offset and ridge height rise, fall, then rise. Basin
depth peaks at the third anchor. Opening and constraining bends occupy different
positions, so blending does not subtract one whole landscape from another.

`FCLTransformLandform` stores the four editable examples in metres, their fixed
positions, a local seed and feature length. Details expose the physical values;
Character selects within the edited family without overwriting those values.
Widths are half widths. Source terrain schema 13 adds the typed profile; inactive
profile zero preserves existing snapshots. The portable source is
`Examples/TerrainTypes/TransformShearFamily.json`, with the existing canonical
Transform Belt ID. Existing applied placeholders are not silently regenerated on load. A deliberate
Character, reseed or re-include gesture adopts the implemented source, preserving
the planet’s authored strict placement preferences and existing local Character/seed.
Matching legacy strict requirements gain the source’s recorded relaxation policy;
their authored strict coverage is retained. Unrelated custom requirements remain pinned.

Finite seeded fault corridors use the strongest available plate shear trend and
handedness. If none is available, they use a deterministic local orientation.
Strict Shear suitability is evaluated first, followed by recorded sparse-shear
and local-expression relaxations; relaxed allocation remains capped at two
regions. Missing geological evidence remains unknown. This is a gameplay
placement preference, not a fabricated tectonic history.

Point queries normalize the incoming body-space position before applying the
sideways warp; unit-direction generation queries and native VPP position queries
therefore share the same mapping. The field restores sideways terrain coordinates before sampling the base
landscape and adds local metre relief after base calibration. Canonical Rock
uses exactly the same horizontal mapping and structural radial displacement.
Rock identity, beds and geological materials remain owned by the existing rock
system; water remains owned by the planet water system. Native VPP owns meshing,
LOD, rendering, collision and edits. No vendor code is changed.

The existing Planet row supplies checkbox, Character, eye, reseed, Solo and
disclosure. Reseed changes layout; Character preserves it. Source and applied
snapshot remain separate. `Tools/create-transform-family-review.py` creates a
separate reusable review and candidate and adopts only the byte-identical shipped
catalogue placeholder. Existing authored source/review assets are preserved.
The review recipe is a normal assembled planet with tectonics, canonical rock
and enabled caves. The seven comparison captures use its actual Planet Solo
preview, the same seed and camera, and the surface checkpoint before caves.
The ordinary assembled path is separately checked for placement and rock identity.

Visual acceptance remains with the user. Native field/mesh tests and captures
prove delivery and provide review evidence; they do not establish artistic approval.

The seven actual Planet review views and validation record are in
[Transform conformance](../conformance/TransformShear/README.md). Uniform palette
queries skip redundant regional weighting while cell inspection retains the
original partition. The comparison camera stays fixed through native publication.
