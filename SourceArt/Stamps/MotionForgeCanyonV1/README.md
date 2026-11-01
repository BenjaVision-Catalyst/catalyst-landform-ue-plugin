# Motion Forge river canyon, revision 1

Source: [Motion Forge height maps](https://www.motionforgepictures.com/height-maps/),
Chris J Mitchell / Motion Forge Pictures, CC0-1.0. Download: Canyon and River Height
Map, page download ID 10152. `sources.json` records the original archive/member
hashes and every preparation parameter.

`MFC_RiverCanyonDepth.png` is linear uint16 **positive depth**, not ordinary
positive elevation. Use a negative signed relief in an editorial heightmap
feature to incise the planet. Interior source relief is retained below a chosen
plateau contour; tributaries are connected to the main river incision. The
quintic contour toe closes the banks and selected ends into a zero moat. The
native import uses bilinear sampling with 25 cm source pixels and 100 m reference
relief; placement supplies actual angular extent and guide-relative depth.

`MFC_RiverMask.png` follows the same crop/padding/resampling. It is retained for
later drainage/water experiments and does not generate water in this proof.
These source elevations have no claimed georeferenced distance or height scale.

Reproduce with `Tools/prepare-canyon.py --archive <canyon-river.zip>` using
`Tools/motion-forge-requirements.txt`, as for the mountain extraction.
