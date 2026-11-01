# River canyon with rising end beds

Derived from the CC0 [Motion Forge Canyon and River Height Map](https://www.motionforgepictures.com/height-maps/),
Chris J Mitchell / Motion Forge Pictures. Version 1 is retained unchanged.

The positive-depth uint16 source raises the floor gradually into the surrounding
terrain along the first and last 32% of the river path. A depth ceiling meets the
existing canyon walls; central relief and shallow bank detail are preserved.
The source still has one connected incision, closed ends and a zero margin.

Use Canyons collection revision 2, source `MFC_RiverCanyonRampDepth`, with negative
signed relief in an editorial heightmap feature. The native import is bilinear.
The river mask is retained for future work; it does not create water.

Reproduce with `Tools/prepare-canyon-ramps.py` using the pinned numerical tools in
`Tools/motion-forge-requirements.txt`. `sources.json` records parent/output hashes,
profile dimensions, independent native sample coordinates and preservation checks.
The canonical 156° × 48° footprint is an editorial scale, not georeferenced data.
