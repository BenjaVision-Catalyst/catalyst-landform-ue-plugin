# Revised volcanic flanks — awaiting visual review

The preceding range was rejected for repeating, steep radial slots. This revision
uses unequal catchments, merging tributaries, broad rounded banks and tapered
incision at the depositional foot. Three independent lava shoulders replace the
second periodic petal pattern. The Character curves and geological bindings are
unchanged. No test or experiment values are saved to planet/source assets.

These are actual stock VPP renders in the normal workspace LandformLab Planet
editor, from transient copies of the user’s current unsaved DA_BasePlanet recipe.
All seven captures use the same seeded group and camera orientation; the target
height is updated with each surface. The viewport remains in ordinary Lit mode.
The original 153 project .uasset files are byte-identical. Both unsaved models
were restored and verified against the captured struct text, including the
radial-guide catalogue.

- [Character 0](planet-00.png)
- [Character 17](planet-17.png)
- [Character 34](planet-34.png)
- [Character 50](planet-50.png)
- [Character 67](planet-67.png)
- [Character 84](planet-84.png)
- [Character 100](planet-100.png)

Focused native sampling: 320,000 probes over four seeds and four Character
positions, approximately 359 ns/probe in this local run, at most 20 overlapping
segment candidates, and 21,851 mean cached bytes per primary centre. This is a
local field benchmark, not a full-planet frame-time guarantee. Six focused native
checks pass, plus the seven-position rendered review in wide and closer views.
They cover continuity, determinism, manual values, assembly/rock identity,
geological fallback, enabled caves, row controls and catalogue availability.

These captures show the implementation for user review; passing tests do not
establish visual acceptance. VPP code, quality and LOD/chunk defaults are unchanged.
