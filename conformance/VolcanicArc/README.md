# Volcanic arc — original Planet review

**Rejected:** the user identified unnatural repeating radial slots in this range.
The current [revised flank review](Realism/README.md) replaces that drainage pattern.
The captures below are historical evidence, not visual approval.

Actual stock VPP renders inside Planet, using the existing landform row and
independent physical Character curves. Same seed and camera throughout. The
review uses a transient copy of the saved authoring planet and whole-planet Solo;
no named landscape collection, preset assets or new UI are introduced.

- [Character 0%](planet-00.png)
- [Character 17%](planet-17.png)
- [Character 34%](planet-34.png)
- [Character 50%](planet-50.png)
- [Character 67%](planet-67.png)
- [Character 84%](planet-84.png)
- [Character 100%](planet-100.png)

The captures intentionally take the usable surface checkpoint before cave
construction. The normal cave continuation is released when the interactive
review remains open. The original editor session is retained; all 162 original
Game asset files are byte-identical. Only the owned canonical plugin Arc source
asset is updated. These renders await user visual review.

Thirteen unique native checks passed, covering the family, deterministic layout
and reseed, crater-axis continuity, physical overrides, JSON round trips, rock
identity, geological fallback, enabled cave minima, province catalogue and
neutrality, existing Transform, and Planet controls/persistence. The final
oblique seven-position render check passed again. See native-tests.json and
range.json. No VPP source or quality/chunk defaults changed.

Reproduce with the normal UE automation filter
`Catalyst.Landform.Planet.LandformRows.VolcanicArcRange`. Add
`-LandformKeepArcReview` and omit TestExit to retain the interactive review
and continue caves after the captures. The recipe and source are not saved by
this review.
