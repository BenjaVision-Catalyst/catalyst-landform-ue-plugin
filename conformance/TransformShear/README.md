# Transform and shear belt — Planet review

12 November 2026 · UE 5.7.4 · Mac arm64 · native VPP 2.0p8.

The four authored expressions and their three halfway blends are implemented in
normal planet generation and the existing Planet Landforms row. These captures
are **for user visual review**, not artistic acceptance.

| Character | Expression | Actual Planet window | Terrain view |
| --- | --- | --- | --- |
| 0 | Linear shear valley | [Planet](planet-0.png) | [Terrain](terrain-0.png) |
| 15 | Valley → offset ridges | [Planet](planet-1.png) | [Terrain](terrain-1.png) |
| 30 | Offset ridge landscape | [Planet](planet-2.png) | [Terrain](terrain-2.png) |
| 47.5 | Offset ridges → stepover basins | [Planet](planet-3.png) | [Terrain](terrain-3.png) |
| 65 | Stepover basin landscape | [Planet](planet-4.png) | [Terrain](terrain-4.png) |
| 82.5 | Stepover basins → pressure ridges | [Planet](planet-5.png) | [Terrain](terrain-5.png) |
| 100 | Pressure ridge landscape | [Planet](planet-6.png) | [Terrain](terrain-6.png) |

Seed 1729, local seed 73, same surface direction, framing scale and studio
exposure. These are native Slate/viewport captures from Planet, with Transform
Solo active. The region-cell and globe overlays are hidden in this local
comparison. The normal editor still defaults to the geographic grid. Comparison
captures hold enabled caves at the existing surface checkpoint. The separate
normal-assembly test generates those caves and checks actual air in the shared
scene. No cave shapes were changed or visually approved.

The candidate is `Examples/TerrainTypes/TransformShearFamily.json`; the normal
assembled review is `Examples/TransformShearProvince.json`.
`Tools/create-transform-family-review.py` creates reusable review assets without
replacing existing authoring assets. The shipped Transform catalogue definition
is now physical; old applied snapshots stay pinned until an explicit row gesture
adopts the implementation. Authored strict placement preferences survive adoption.

## Evidence

[Qualification](qualification-mac-arm64.json) merges 24 distinct successful
checks, with their exact evidence file. The initial combined 21-test run passed
20 checks; province navigation missed 50 ms p95 at 176.795 ms. Its fresh-process
rerun passed at **10.512 ms p95**. Both results are retained. This does not claim
that arbitrary combined editor workloads meet the navigation limit.

The final sampling run passed 12 checks after point normalization, deliberate
row adoption and the uniform-palette optimization. The final native capture test
passed all seven positions, including the legacy-row adoption and unchanged
source/placement assertions, actual visible terrain, stable native owner and
mesh/field agreement within each native cell's budget. The worst sampled global
radial residual was 2.100 m in a coarse overview cell; this is not a close-view
0.5 m claim. CurrentRift independently passed its required cave qualification:
39 networks, 30 native domains, 26,275 overview vertices and 12,667 cyan pixels;
close view 53,423 vertices, maximum cell 0.600 m, residual **0.217652 m**.
Overview readiness took 19.441 s; close refinement took 185.609 s.

Early comparison angles and an initial fixture without enabled caves were
rejected during this task. Point-based queries also exposed a missing direction
normalization in the new sideways warp; the final tests explicitly compare
body-space and unit-direction queries. A profile found redundant regional
weighting in uniform Solo assignments. The shortcut is checked against the
original cell partition. Repeated camera resets in the test fixture caused native
LOD work to keep changing; the final comparison camera is pinned through native
publication. Native preview quality, voxel size and chunk settings are unchanged.

All **108** pre-existing saved Landform authoring assets remain byte-identical.
The four recorded VPP source fingerprints remain stock. Source/asset fingerprints
in `staged-source.json` match the running project's staged plugin. Source,
render, unit and performance evidence establish delivery only; the user retains
visual acceptance of the landform.
