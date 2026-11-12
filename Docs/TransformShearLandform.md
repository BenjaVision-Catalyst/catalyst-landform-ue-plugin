# Transform and shear belt

Transform uses the same authoring pattern as tectonics, continents and rock:
`ApplyProgression(Character)` writes the current physical settings through
independent internal curves. Character has no privileged midpoint. Valley depth,
width, ridge height, lateral offset, basin depth, strand overlap and dissection
can rise and fall at different points while overall expression strengthens.
The geological reference landscapes guide these curves internally.

The Planet Landforms row retains its checkbox, Character, eye, reseed, Solo and
disclosure. Disclosure exposes the current physical values directly. Manual edits
are consumed by generation as stored; compilation does not reapply Character.
A subsequent knob gesture recalculates the mapped values. Seed, feature length,
placement preferences, rock identity and planet guides remain independently
stored. Undo/Redo and save/reload use the existing Planet operations.

The canonical source is `Examples/TerrainTypes/Provinces/TransformBelt.json`,
matching the existing catalogue asset and stable Transform Belt identity. Terrain
schema 13 carries the typed physical settings. There is no serialized collection
of named landscapes, preset interpolation, fixed-position validation or special
row-adoption mechanism. The ordinary source/snapshot workflow remains in use.

Finite deterministic shear corridors follow available plate shear direction and
handedness, with a seeded local orientation when that context is absent. Placement
starts with strict Shear metadata and may use its recorded relaxation tiers.
Missing geological evidence stays unknown. Underlying geology remains owned by
the planet's Geology layer.

Surface and canonical rock sampling share the same sideways and vertical
mapping, including body-space queries. Native VPP owns meshing, LOD, rendering,
collision and edits. The current cave anatomy is unchanged and has no new visual
acceptance from this work.
