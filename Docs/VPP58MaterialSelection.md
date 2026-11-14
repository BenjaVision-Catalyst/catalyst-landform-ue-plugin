# UE 5.8 VPP material selection

Landform's qualified adapter retains the installed VPP material-selection shader,
surface catalogue, native render indices, texture bindings and world IDs. The
stock pass assumes each material proxy has one Nanite compute shader while
iterating every shading pipeline in a scene. UE 5.8 foliage can have triangle and
voxel shader variants under one unrelated proxy, triggering the compute-shader
identity ensure before terrain material selection.

The adapter builds its table from actual registered native terrain components.
It indexes only their bound terrain proxies and keeps the native identity checks
for those entries. Unrelated foliage pipelines remain owned by Epic's renderer.
Native VPP source/binaries and engine files are not edited. The native selection
scheduler is disabled only while this equivalent Landform pass is active; this
is a replacement material-selection pass, not error-message suppression. An
explicit pre-existing user disable remains respected.

`Tools/stage-vpp-material-bridge.py` qualifies four local VPP source contracts,
then extracts the native pass into ignored Intermediate build output and applies
the filtering adaptation. Licensed source is neither copied into this repository
nor distributed in an installer. Unknown input or edited generated output is
refused. Build rules run the generator and use native private headers. Current
qualification is UE 5.8.4 Mac arm64 Metal SM6; other engines and packaged execution
need their own qualification.

The view-family callback resolves the actual view's world and snapshots its VPP
runtime configuration and component material bindings before rendering. Contexts
are reused while bindings remain equal. Each scene is filtered independently;
world cleanup retires its references. Module shutdown releases contexts/queued
render work and restores the previous scheduler setting.

The fourteen packaged Surface Type/Metadata definitions were re-saved by native
Unreal serialization, pinning the valid identities generated for legacy packages
which had no saved GUID field. The saved GUID tags
are checked by `Catalyst.Landform.Inspection.PackagedVoxelGuids` before loading
the assets. Ordinary authoring packages are not auto-saved on editor startup.
