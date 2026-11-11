# Cave placement review

UE 5.7.4 / Mac arm64. The actual saved `DA_ContinentalRiftReview` recipe is used,
including its authoring Caves character of 0.348. Cave-shape refinement is paused;
these images do not establish visual approval of the current morphology.

- [Outside entrance](editor/entrance-outside.png): an existing upper chamber
  intersects real terrain. No separate entrance cutter or mesh.
- [Inside entrance](editor/entrance-inside.png): the connected exposed chamber.
- [Native cave overview](overview/all-caves.png) and
  [native close-up](overview/zoomed-cave.png): stock VPP inspection delivery.
- [Guide radii and network distribution](editor/review.json),
  [network depths](network-depths.csv), [automation results](automation.json),
  [measurements](measurements.log), [staged source hashes](manifest.json).

`CavesMin` is 499.181 m radius. `CavesMax` is 1581.855 m, 5 m below
`SurfaceMax`. Sixteen systems occupy mean node radii of 562.521–1151.441 m
and one to three levels. One surface opening has a minimum verified aperture
of 6.30 m; the exposed chamber visible in the image is larger.

The CurrentRift native scenario has 12 inspection domains and 8829 published
overview vertices, with 4823 visible cave pixels. Close-up maximum cell size is
0.480 m and maximum field residual is 0.177186 m. Its physical camera distance
is 40 m. Native SetupForPreview quality/chunk defaults and the existing 1 m cell /
0.5 m close-up residual budgets are retained.

The entrance images use `SCLPlanetPreview`, its stock native terrain presenter,
and canonical geological materials. The placement-only flag selects the two
entrance cameras in the existing editor capture. Full anatomy capture remains
the default. Both frames must contain lit terrain, rejecting sky-only images.

Automation combines the placement/current-Rift run with the final regression
run, replacing the first entrance-camera run with the corrected camera run.
The command files preserve the exact filters. Native field/persistence results
come from the targeted rerun after replacing its obsolete all-underground
assertion with verified entrance ownership and solid walking-footing checks. The build and stock-VPP
hash manifest are included. Full authoring JSON remains in the task scratch
location recorded by the manifest, rather than being added as a recipe fixture.
