# Optional Planet Builder sections

`FCLPlanetBuilderExtensions` is the editor-only registration seam for optional
planet-authoring plugins. Register a unique provider ID, label and session factory
at editor startup. Landform inserts providers after Planet, Geology and Landforms, before the final
Preview section. Provider activation follows the built-in section count. Registry ordering is
stable by ID. Provider labels use the same native property font as built-in
section labels. Internal section indices remain stable. Registration does not open a world or allocate a service/session.

Each toolkit creates and retains its own `ICLPlanetBuilderExtension` session. The
context names the exact planet asset, exposes its accepted immutable terrain plan
and publishes a bounded coloured overlay into that toolkit's native globe scene.
There is no first-world/first-planet lookup. Sessions contribute their owned model
assets to the toolkit's normal Save operation. Opening a planet with no provider
does not create extension assets; soft authoring links survive provider absence.
These links live outside the portable terrain recipe payload.

`SetActive` allows a session to release its overlay when the author leaves its
section. Overlay samples are metric body positions and colours, at most 8,192
samples. Native terrain geometry, cave eligibility and delivery are unchanged.
The globe draws the overlay only once its native preview has accepted the same
terrain plan; it never decorates a different in-flight terrain revision.

Provider modules must disable dynamic reload, unregister on shutdown and keep
worker ownership within the session. Destroy/close must cancel and join any
worker before releasing its inputs; worker code must not reference UObjects or
widgets. Existing toolkits keep their sessions until closed, so providers are
registered before opening those toolkits. Restart after changing reflected native
layouts or the optional provider build capability.

Catalyst Ecosystem is the first consumer, with Climate and Biomes sub-tabs. Landform
has no compile or descriptor dependency on Ecosystem, PCG Biome Core or MegaPlants.
