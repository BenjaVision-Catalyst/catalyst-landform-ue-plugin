# Mountain surface scans

The default Rock, Scree and Soil materials use Poly Haven's CC0 scan maps:

- [Rock Face 03](https://polyhaven.com/a/rock_face_03), 2.7 m tile.
- [Gravelly Sand](https://polyhaven.com/a/gravelly_sand), 2.5 m tile, used as fine scree.
- [Dirt](https://polyhaven.com/a/dirt), 2 m tile.

[Poly Haven's license](https://polyhaven.com/license) permits commercial use and redistribution under CC0. `sources.json` records exact download URLs and SHA-256 hashes for the 2K colour, DirectX normal and roughness PNGs. Raw downloads are ignored to avoid duplicating the texture sources embedded in the imported UE assets. To regenerate them, run LandformLab's `scripts/download-surface-scans.py`, then run `create-surface-library.py` through editor Python. The imported assets work without downloading again.

Surface revisions are now 2. Revision 1 was the procedural placeholder; the new client artwork does not silently resolve a revision-1 recipe to revision-2 materials. The supplied examples and proof inputs are explicitly upgraded.
