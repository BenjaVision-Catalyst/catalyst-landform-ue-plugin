# Catalyst Landform for Unreal Engine

Catalyst Landform provides deterministic procedural planets and the **Planet**
editor. Planet owns assembly, geology, Landforms, surface authoring, radial guides
and preview. Landform asset actions focus that same editor. Voxel Plugin owns
meshing, LOD, rendering, collision and edits.

This is a development build. Version 1 will be the first supported product release.
Development targets one current model, without backward-compatibility or migration
guarantees. [Geology and material ownership](Docs/GeologyAndMaterials.md) defines
rock, landform-deposit and biome-cover responsibilities.

## Install from source

Use Unreal Engine **5.7**, a C++ toolchain for your platform, Python 3 and a licensed
**Voxel Plugin 2.0p8 for UE 5.7** installation. Start with a C++ project, or add
a C++ class to an existing Blueprint project. This revision is verified on macOS arm64. On that
platform install **ISPC 1.31** on `PATH`, or set `LANDFORM_ISPC` to its executable.
The plugin builds its SIMD kernel automatically. Other platforms use the scalar
field evaluator and require their own build qualification.

Clone this repository's `main` branch, close the destination project's editor,
then run:

```sh
python3 Tools/install-plugin.py --project /absolute/path/MyProject.uproject \
  --voxel /absolute/path/to/Voxel
```

The installer copies the complete plugin into `MyProject/Plugins`, enables its
modules and installs the Foundation model/editor dependency from the pinned
published [CatalystDevKit source](https://github.com/BenjaVisionPro/CatalystDevKit/tree/9610a33966a23fd1a863b0e2256c6255c39a7b8a/CatalystPlugins/Plugins/CatalystFoundation).
It needs Git access to that repository. Supply `--catalystfoundation /path/to/CatalystFoundation`
to use an existing source copy. Existing licensed Voxel installations are retained;
its source and binaries are not redistributed here. CatalystConnect and a Lab
project are not required.

Generate project files and build your project's Editor target, then open the
project. Create a **Planet Recipe**, or open an existing planet asset such as
`DA_BasePlanet`. The **Window → Catalyst → Planet** action opens the same editor.
Use the Landforms tab to include provinces, edit Character or expanded values,
toggle preview visibility, highlight assigned cells, reseed and Solo.

The installer refuses to overwrite an unmanaged or locally edited plugin.
Re-running it can update its own unmodified installation. It records source and
content hashes in `Saved/CatalystLandformInstall.json`. Build products and caches
are generated locally, not committed.

## Included content

The plugin supplies its own province catalogue, surfaces, materials, geological
metadata and reusable radial-guide library. No `/Game/LandformDesigner` asset is
needed for a new project.

`/CatalystLandform/RadialGuides/DA_ExampleRadialGuides` contains the exact guide
sets from the **100 m, 1 km, 10 km, Mars and asteroid** examples, plus the
current authored 100 m variant. New planets start
with the 1 km set. The Radial Guides menu can select another. Editing a packaged
set creates a project-local library, preserving the shipped examples. Existing
planets retain their authored guide values and explicit project libraries.

Portable recipes live in [Examples](Examples). The approved cave comparison
captures remain in [conformance/SpatialCave](conformance/SpatialCave); qualification
records do not constitute visual approval. Cave morphology remains under review.

## Development and verification

See [the current documentation index](Docs/README.md),
[Planet Landforms](Docs/PlanetLandforms.md),
[province placement](Docs/ProvinceLandforms.md) and
[optional Planet Builder sections](Docs/PlanetBuilderExtensions.md).

Run focused tests after a coherent change. Publish the component before updating
workspace pointers. Perform publication and a clean-project installation check
**when requested or before moving to a new feature**, not for every intermediate
change. Never save authoring assets during testing experiments.

A fresh-project verification builds the installed plugin, opens the native Planet
editor, checks its catalogue and balanced province allocation, and captures its
rendered preview without saving a planet:

```sh
python3 Tools/verify-install.py --engine /absolute/path/UE_5.7 \
  --output /absolute/path/new-empty-install-check \
  --catalystfoundation /absolute/path/CatalystFoundation \
  --voxel /absolute/path/Voxel
```

The test host and logs are generated outside the plugin. Runtime, editor and
explicitly loaded automation modules are separate.
