# Volcanic arc province

Volcanic arc uses the existing Planet row and `ApplyProgression(Character)`.
Independent curves write current physical values; generation consumes those
stored values directly. Disclosure edits survive compilation and save/reload.
Moving Character again recalculates the mapped values. There is no privileged
midpoint, serialized set of landscapes, selector or additional editor.

The progression develops weathered volcanic uplands into coherent composite
cones, overlapping massifs and strongly dissected asymmetric centres. Incision
falls as coherent flanks emerge, then strengthens. Secondary summits become
prominent through the compound terrain. Lava shoulders peak before the rugged
right end. Crater dimensions, apron reach and breaches have independent curves.
Only a seeded subset of vents develops a substantial open sector.

References informing the shape, rather than a geological age chronology:

- GNS Taranaki fact sheet: intact composite cone, secondary cone, surrounding
  ring plain, and the eroded Kaitake/Pouakai predecessors.
  https://www.gns.cri.nz/assets/Our-Science/Subfiles/Natural-Hazards-and-Risks/Subfiles/Volcanoes/Volcano-Fact-Sheet-Taranaki.pdf
- DOC Ngauruhoe and Tongariro: coherent cone beside older compound terrain.
  https://www.doc.govt.nz/parks-and-recreation/places-to-go/central-north-island/places/tongariro-national-park/about-tongariro-national-park/central-north-island-volcanoes/
- Smithsonian Tongariro: overlapping composite cones and varied craters.
  https://volcano.si.edu/volcano.cfm?vn=241080
- USGS Mount St Helens: open sector, asymmetric flanks and depositional fans.
  https://www.usgs.gov/geology-and-ecology-of-national-parks/geology-mount-st-helens-national-volcanic-monument

`FCLCompiledVolcanicArcLandform` builds seeded local groups of vents with varied
proportions and angular structure. Their locations persist across Character;
secondary prominence blends continuously. Convergent plate belts can guide the
local group trend. Geology continues to own the planet-scale arc. Radial channels
branch down the flanks, lava shoulders interrupt them, and aprons taper into the
existing landscape. Smooth union retains saddles between overlapping edifices.
Subordinate additive noise supplies weathered mantle, lava/tephra texture and
small rock/debris detail. No external stamps are required by this pass.

The physical family uses tens to low hundreds of metres of local prominence
and several hundred metres of basal radius for the current small review planets;
manual values remain available for larger or more extreme authored terrain.
Small planets scale the whole local anatomy consistently when it cannot fit.
The family does not change planetary guides or calibrate the entire landscape
around its peaks. Snow, vegetation, glacier erosion and eruption activity have
no Character binding in this implementation.

The canonical asset/JSON identity is the existing VolcanicArc catalogue entry.
Terrain schema 14 carries its typed values; absent values in older assets are
inactive. Planet assembly retains the ordinary source/snapshot workflow.
Strict metadata still records subduction and volcanic history. Version-2 ordered
relaxations first permit declared subduction without eruption history, then a
bounded selected local expression. Compression is a soft placement preference,
including during fallback; an inherited arc does not require current compression.
Missing history remains unknown and is never manufactured by this landform.

Metre relief reaches the calibrated surface, canonical rock deformation and
native VPP through the existing generated-province path. Rock identities and
bindings are retained. VPP remains stock and owns meshing, LOD, rendering,
collision and edits. Existing caves and surface-first preview scheduling retain
their owners. Rendered Planet captures are review evidence, not user approval.
