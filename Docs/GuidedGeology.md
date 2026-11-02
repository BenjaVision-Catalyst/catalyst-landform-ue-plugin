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
| Plate count | 2 | 4 | 7 | 9 | 12 |
| Motion | .12 | .70 | 1.15 | .95 | 1.85 |
| Relief contribution | .12 | .40 | .65 | .82 | 1 |
| Belt width (degrees) | 18 | 14 | 10 | 8 | 12 |
| Crust thickness ratio | .012 | .020 | .028 | .045 | .070 |
| Boundary warp | .12 | .27 | .38 | .30 | .52 |
| Roughness | .08 | .20 | .38 | .58 | .90 |
| Collision strength | .18 | .30 | .65 | .90 | 1.30 |
| Rift strength | .08 | .75 | .45 | .30 | .75 |
| Transform strength | .05 | .35 | .50 | .35 | .80 |
| Interior relief | .15 | .10 | .08 | .055 | .035 |

Motion and belt width reverse direction along this path. Each field participates
in the compiled geometry: topology, deformation, broad collision shoulders,
boundary shape, surface texture, boundary-type balance or quiet interior relief.
Quiet retains structure. Extreme introduces more fragmented, sharper terrain.
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
plate network (algorithm 4). Old controlled-boundary recipes and their tests keep
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
Legacy override flags remain serialized for older recipes, with no editor or
Blueprint exposure. They are ignored by the current resolved-value algorithms.

[GuidedGeology.json](../Examples/GuidedGeology.json) is the current portable
example. Existing LayeredGeology, ContinentalBase and TectonicBase examples remain
pinned historical fixtures. Landform assets remain terrain-type configurations;
this final planet range is not saved into them.
