# Continental Rift — first family candidate

12 November 2026, UE 5.7, macOS arm64 Development editor. **Historical standalone-editor captures; superseded for UI review by Planet Landforms. Visual approval open.**
This starts the first province-completion pass; it does not complete Continental
Rift or change the frozen catalogue source.

- [Direction and sources](../../Docs/RiftFamilyReview.md).
- [Candidate recipe](../../Examples/TerrainTypes/ContinentalRiftFamily.json).
- Actual stock native VPP source-editor views: [Character 0](globe-0.png),
  [0.5](globe-1.png), [1](globe-2.png). Matching `controls-*.png` show the editor.
- `native-review.json`: native publication and 60-frame settled navigation p95.
  The measured surface residual is diagnostic, not an asserted close-up accuracy
  qualification. It reaches 13.64 m in the sharpest capture; these pictures must
  not be presented as a sub-metre fidelity result.
- `delivery-result.json` records 13 successful automation cases. Effect-off tests
  prove noise and structural throw independently affect actual final spherical
  height; source JSON round-trip, placement tiers and canonical Rock restoration
  are checked. Render tests assert native vertices and keep the existing 50 ms
  navigation p95 and 180 s timeout limits. The first cold shader run timed out;
  this retained successful run uses the warmed local cache.
- `ubt.log`: successful native build. `preservation.json`: original authoring
  assets and four previously verified stock VPP source-file hashes checked again.

The rendered candidate remains visibly too smooth against the range-front
reference. Noise is not erosion simulation. Prepared private mesh stamps have
not been placed in the province; their conversion report does not establish
visual quality, material binding or packaging rights. No commercial asset bytes
are redistributed here.

`safety-result.json` records six further successful cases: the enabled cave
minimum, three radial-placement/shape/persistence/opening checks, rendered
CurrentRift native navigation and zoom detail, and private native mesh baking.
Together the retained runs contain 19 successful cases. `stamp-native-report.json`
records a 20 cm cliff bake and 5 cm plate bakes. `tested-source-sha256.json`
identifies the staged source and example bytes used for this build.
