# UE 5.8.4 Planet installation — Mac arm64

13 November 2026. The current GitHub plugin was installed into VPPTest58 with
`Tools/install-plugin.py`, using its packaged content and existing licensed VPP.
No LandformLab asset, host module, Connect runtime or retired editor was required.
The source baseline was `bf0de052f1e77fd67886d0dbb1faae16b6090ebc`.

## Build and integration

UE 5.8.4 CL 58902052, macOS arm64, SDK 26.5, ISPC 1.31 and stock VPP development
revision `58909934d4b680910cf1edc5114eee3183ccd263`: Editor target passed with
`-NoHotReloadFromIDE -NoUBA`. VPP source was unchanged.

The adapter handles UE's gesture phase and JSON shared-string keys, VPP's new
bounds/volume-transform signatures and renamed surface-detection property.
Measured rasters use VPP's exported native float bilinear ISPC kernel after the
existing identity-scaling preflight. Translation units compile independently so
file-local helpers cannot collide in installed unity builds. The optional native
projection path remains unavailable where stock VPP lacks its lifecycle capability;
the ordinary Planet editor does not require it.

There is one current Planet editor: Planet, Geology, Landforms and Preview.
Optional plugins append their own sections through the generic extension registry.
Ecosystem supplies its own Climate/Biomes panels; disabling Ecosystem removes its
section. No Ecosystem dependency or climate/material implementation was added to
Landform. [Material ownership](LandformDeposits.md) is unchanged.

## Checks and limits

- 18 applicable tests passed with Ecosystem enabled: packaged-install rendering,
  17 catalogue rows and balanced Rift/Volcanic Arc assignment; Planet row controls
  and persistence; new-planet guides; four measured-raster checks; five deposit
  checks including rendered review; rock bindings; five Ecosystem model/PCG/UI checks.
- Two tests passed with Ecosystem disabled: the same native installation review
  and GPU material-coordinate parity. This adds one distinct successful check,
  for 19 distinct applicable checks across the two launches.
- The initial 19-case selection also ran the Lab-only AssignedCellHighlights test.
  It failed because `/Game/LandformDesigner/DA_BasePlanet` is absent in this host.
  That project-specific case is not an end-user installation requirement; the
  packaged-install test independently verifies assigned-cell highlights.
- All 1,020 authored Content/Config files were unchanged. VPP updated only its
  generated `Content/Developers/jupiter/MegaMaterialCache.uasset` among 1,021 files.
  Test planets were transient and no authoring experiment was saved.
- The existing VPP UE 5.8 Nanite shading-pipeline ensure and asset-registry GUID-tag
  warnings remain upstream qualification limits. This is not qualification of
  packaged gameplay, planetary wind, biome ground-cover materials or streaming.

Evidence is retained in VPPTest58 `Saved/CurrentPlanetQualification`; its workspace
conformance record contains individual test states and screenshot paths. The
Ecosystem-enabled capture shows the current Planet editor with its climate panel;
the disabled capture shows only the four built-in sections.
