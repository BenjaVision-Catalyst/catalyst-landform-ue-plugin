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
New planets select the current guided model before adding their surface settings;
the factory regression now compiles the stored recipe directly.
Measured rasters use VPP's exported native float bilinear ISPC kernel after the
existing identity-scaling preflight. Translation units compile independently so
file-local helpers cannot collide in installed unity builds. The optional native
projection path remains unavailable where stock VPP lacks its lifecycle capability;
the ordinary Planet editor does not require it.

There is one current Planet editor: Planet, Geology, Provinces and Preview.
Optional plugins append their own sections through the generic extension registry.
Ecosystem supplies its own Climate/Biomes panels; disabling Ecosystem removes its
section. No Ecosystem dependency or climate/material implementation was added to
Landform. [Material ownership](LandformDeposits.md) is unchanged.

## Checks and limits

- The final 20-test selection passed with Ecosystem enabled: packaged-install rendering,
  17 catalogue rows and balanced Rift/Volcanic Arc assignment; Planet row controls
  and persistence; new-planet guides; four measured-raster checks; five deposit
  checks including rendered review; rock bindings; five Ecosystem model/PCG/UI checks.
- The final selection includes GPU material-coordinate parity and enabled-cave
  minima across 72 scale/profile/access cases, including missing geology and
  planets from 10 m to 10,000 km radius.
- A separate Ecosystem-disabled launch checks the native installation review,
  new-planet factory and GPU material-coordinate parity in the same host.
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


## Provinces workflow — 13 November 2026

The current UE 5.8.4 Mac arm64 Planet editor has Provinces with Placement, Types
and Landforms pages. Nine rendered native checks pass, including added-instance
asset-picker selection, duplicate instances, list/globe selection, deletion,
undo/redo, source preservation, placement-frame mathematics, native VPP parity,
real HWidgetAxis move/rotate/scale drags, province row controls, fresh installation,
new-planet creation and Ecosystem integration. The installer suite also passes
all three checks. Reports are retained in VPPTest58 Saved/ProvincesEditor.

UE 5.8's actor-oriented ITF gizmo path suppressed the preview's HWidgetAxis
handles. This private viewport explicitly selects Unreal's axis widget path on
5.8; the level editor's gizmo policy is untouched. All seven supported handle
gestures commit once on mouse release, and Escape cancels a draft. No test or
review authoring assets are saved.

## Placed-landform editing — 13 November 2026

The final 16-test rendered selection passes on UE 5.8.4 Mac arm64. It covers
standard keyboard Undo/Redo for move/rotation/scale; one commit for each of seven
routed HWidgetAxis gestures at 1200 × 800 without camera orbit; list
Backspace/deletion, complete source-link restoration, native VPP parity,
source ownership, assembly/structure bindings, fresh-project installation,
province rows, new planets and Ecosystem's optional editor/asset selector.
The enabled-cave minimum and scale/profile/access cases pass. Three installer
checks pass independently. Reports are in VPPTest58 Saved/PlacedLandformFix.

Mountain relief adds to the finished geological surface: 51 feature samples
rise without excavation, all 2,595 outside-footprint samples retain the exact
base height, the boundary fades continuously, and the VPP zero-set residual is
less than 0.02 cm. Signed subtractive source relief still makes valleys.

The initial selection also attempted the historical CurrentRift rendered test;
its required /Game/LandformDesigner/Provinces/DA_ContinentalRiftReview fixture
is absent in this host. It was excluded from the final selection, not counted as
a pass. This update does not qualify a new cave anatomy or delivery backend.
No source asset or authoring experiment was saved.

The Landforms page now contains only its asset selector and instance list. The
extra Move/Rotate/Scale buttons are removed; viewport W/E/R shortcuts remain.
This follow-up passes the two existing placed-instance/native-handle editor
checks and all three installer checks, with the current unsaved planet restored.
Evidence is in VPPTest58 Saved/LandformToolbarRemoval.

## Signed local terrain blending — 13 November 2026

Placed landforms reference absolute carrier noise to its deterministic local
footprint mean, excluding additive detail and heightmaps. This removes the
artificial elevated oval introduced by the prior minimum-height datum. The
resulting signed displacement adds ridges or removes valleys from the already
calibrated geology. Heightmap relief, seeds, placement IDs and source assets are
retained. There is no absolute-surface replacement or whole-planet refitting.

The independent dense-grid regression measures -0.0443 m carrier mean change,
-21.081..16.561 m local relief and 2,018 exact exterior samples. Positive and
negative constant brushes retain the complete varying ground height beneath
them. Native VPP before/after/ground captures cover a portable reference and the
current unsaved review placement, using the same camera. The native source's
steep crags remain; this change removes its inherited elevation pedestal rather
than changing the heightmap anatomy. Evidence: VPPTest58 Saved/LandformTerrainFit.

The final 19-test rendered selection and three installer checks pass on UE
5.8.4 Mac arm64. The unsaved recipe, guides, Ecosystem settings and source links
are restored exactly. The historical CurrentRift fixture exclusion recorded
above remains; no cave anatomy or preview-delivery backend was changed.
