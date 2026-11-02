# Guided planet geology and final surface heights

Schema 17 replaces intensity-only geology authoring with a guided character
progression. A preset is the authored relationship between a knob and its
advanced data. Continents and Tectonics remain composable layers.

## One knob

The tectonic preset follows five authored configurations: broad quiet structure,
rifting, mixed margins, collision belts, then fragmented extreme terrain. These
are useful artistic configurations, not stages of a physical time simulation.
Intermediate settings interpolate the curves smoothly; plate count is discrete.

| Resolved setting | 0% | 25% | 50% | 75% | 100% |
| --- | ---: | ---: | ---: | ---: | ---: |
| Plate count | 2 | 3 | 5 | 7 | 10 |
| Motion | .10 | .24 | .42 | .36 | 1.40 |
| Relief contribution | .08 | .12 | .20 | .36 | .75 |
| Belt width (degrees) | 18 | 15 | 12 | 9 | 10 |
| Crust thickness ratio | .012 | .017 | .023 | .030 | .050 |
| Boundary warp | .10 | .16 | .24 | .22 | .36 |
| Roughness | .06 | .10 | .17 | .30 | .65 |
| Collision strength | .10 | .16 | .24 | .40 | .95 |
| Rift strength | .06 | .24 | .18 | .16 | .60 |
| Transform strength | .04 | .12 | .20 | .24 | .55 |
| Interior relief | .16 | .16 | .15 | .14 | .14 |

Motion and belt width reverse direction along this path. Each field participates
in the compiled geometry: topology, deformation, broad collision shoulders,
boundary shape, surface texture, boundary-type balance or quiet interior relief.
Algorithm 5 retains broad interior structure throughout the range, while young
boundary deformation stays small. It also reserves more unsaturated range for
that background. This prevents final height fitting from promoting small young
ridges to full-height walls. The last quarter develops pronounced, rougher belts;
100% is an authored usable extreme, with further exaggeration available in
Advanced. Quiet retains structure. Extreme introduces more fragmented, sharper terrain.
The chosen guide heights determine its absolute vertical extent at every setting.

Continental character grows then fragments domains, rather than expanding a
fixed continent mask monotonically:

| Resolved setting | 0% | 25% | 50% | 75% | 100% |
| --- | ---: | ---: | ---: | ---: | ---: |
| Domain count | 1 | 2 | 3 | 5 | 8 |
| Extent | .70 | .95 | 1.25 | .95 | .70 |
| Relief contribution | .25 | .45 | .60 | .80 | 1 |
| Coast warp | .06 | .16 | .28 | .40 | .60 |
| Interior roughness | .010 | .020 | .035 | .055 | .080 |
| Coast sharpness | .8 | 1 | 1.4 | 1.7 | 2.2 |

Advanced shows resolved, directly editable values. There are no override
checkboxes. Advanced edits persist until the next knob gesture reapplies the
chosen preset. Variation retains those values and changes the layer seed.
The recipe stores resolved settings, position and algorithm versions; workers do
not depend on editor widgets to reconstruct a planet.

## One tectonic model in the authoring workflow

The old Moving plates and Controlled boundary options were implementation
experiments. Controlled boundary isolates a two-plate test configuration; it is
not a second observed category of planet. Current authoring uses the planet-wide
plate network (algorithm 5). Old controlled-boundary recipes and their tests keep
their original algorithms, without a model selector in the current inspector.

Convergent, divergent and transform boundaries coexist within a plate network;
see [USGS, Understanding plate motions](https://pubs.usgs.gov/gip/dynamic/understanding.html).
Landform's curves and coefficients above are authored controls, not empirical
values claimed by that source. Crust thickness is a ratio used for deformation
shoulders; this is not a mantle, erosion or geological-history simulation.

## Exact final SurfaceMin and SurfaceMax

These are planet-global extrema of the completed generated surface. They are
not merely limits or an amplitude scale for individual layers. For an outer
radius of 1000 m, guides at .3 and .6 mean the actual lowest generated surface
radius is 300 m and the actual highest is 600 m.

The compiler first composes the complete raw field: noise operations, continental
and tectonic layers, editorial structures, native stamps, regional Landforms,
generated regions and their blends. One final monotone range mapping then places
its lowest and highest points at the two guides. Intermediate raw construction
defers the old outer-radius clipping, so that it does not flatten features before
this final mapping. The final field remains inside both guides. Schema 17 also removes the old
quarter-radius cap on the raw geological contribution; the final surface mapping
owns the height range.

The range is compiled from deterministic spherical probes, known stamp
footprints and refined extrema. The final map clamps any unsampled outliers to
the same exact guide bounds. It is shared by all patches; patches never fit
independent ranges. Dense qualification checks look for unwanted clipped terraces.
There is no universal claim that every possible asset/function has its exact raw
extremum located by this numerical search. The mapped field nevertheless reaches
both guide values and cannot exceed them.

VPP, globe, planet cross-section and walking collision query that same final
field. Tessellation/LOD approximates the field; extrema guarantees describe the
procedural field rather than each finite triangle mesh. The Ocean display remains
a sphere at OceanHeight and never changes generation settings.

A newly created empty planet remains a perfect sphere. A constant configured
field cannot reach two distinct requested extrema: compilation reports that
conflict rather than silently violating the guides. Removing/reversing required
surface guides also reports an error.

## Version boundary

Schemas 14–16 and earlier algorithms retain their previous generation. Opening
an old asset does not mutate it. The current inspector projects a guided draft;
the next explicit geology edit adopts schema 17 in one Undo transaction.
Algorithm 4 recipes keep their previous resolved geometry; the next explicit
knob gesture applies the new algorithm 5 progression in one Undo transaction.
Variation alone does not change a saved recipe's algorithm.
Legacy override flags remain serialized for older recipes, with no editor or
Blueprint exposure. They are ignored by the current resolved-value algorithms.

[GuidedGeology.json](../Examples/GuidedGeology.json) is the current portable
example. Existing LayeredGeology, ContinentalBase and TectonicBase examples remain
pinned historical fixtures. GuidedGeologyV1 pins the former algorithm 4 example. Landform assets remain terrain-type configurations;
this final planet range is not saved into them.

## Radial Guide measurements

The shared annotation overlay shows every guide's radial distance from Core in
metres, with Core at 0 and Sky boundary at the planet's outer radius. Only the
name is editable in place; measurements are derived display values. They update
as guides move or the preview radius changes, in both Designer and Planet Builder.

A right-hand bracket measures SurfaceMin to SurfaceMax. When OceanHeight is
strictly inside that range it also marks the two ocean intervals, preserving the
total above the bracket. Callout leaders locate exact render heights; bracket
endpoints use those aligned label rows so short projected intervals stay readable.
Missing or reversed surface guides omit the bracket; an absent/outside ocean
omits only its subdivision. These annotations never change recipe data.
