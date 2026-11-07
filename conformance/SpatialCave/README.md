# Spatial cave evidence

Native UE 5.7.4 / VPP 2.0p8, Mac arm64, 7 November 2026. See
[the prototype and reference comparison](../../Docs/SpatialCavePrototype.md).

- `qualification.json`: platform, native settings and scope.
- `automation-summary.json`: 17 passing native geometry, cave, UI/undo, scene
  and volume tests, with zero errors. The historical profile-7 relief test
  captured one unrelated UE HTTP connectivity timeout warning, retained verbatim
  in the summary; both new prototype tests have zero warnings.
- `geometry.json`: floor continuity, dimensions and field/delivery parity.
- `coarse-work.json`: two-site native invoker coarse-selection replay, including
  every chunk's field extrema, fine selection count and timing.
- `walk-250.json`: pristine shape review, native walking and sculpt writer.
- `walk-600.json`: normal-speed review at 100% render resolution, active-walking
  frame timings, floor penetration checks and strict collision-wait gate.
- `reload.json`: fresh-process native sculpt save reader; all 14 field, surface
  and geological probes match the 250 cm/s writer.
- `headroom-100.json`: settled native ISM rendering proxy at explicit 100%
  screen percentage. This does not include ecosystem simulation or collision.
- `commands/`: exact native invocations. The temporary Lab path is an isolated
  physical build host, not an alternate product owner.
- `native-source-sha256.json`: 282 exact native source files; 272 baseline files
  remain unchanged. Six existing owned files change and four new files are added.
- `capture-sha256.json`: hashes of the unedited native viewport captures.

The separate `DA_SpatialCave` review asset was created through Unreal and loaded
by a second commandlet process with profile 8 preserved; its hash is retained in
`qualification.json`. Existing review assets were not rewritten.

Final images are from the 600 cm/s pristine review before native mining/filling.
The first-iteration chamber is retained separately to show the rejected column
shape. It is not the final geometry or a performance control.

`verify.py` checks the retained reports and capture hashes. With `--host` it also
checks the physical Lab sources against the captured native source manifest.
`Tools/qualify-spatial-cave.py` runs a fresh native phase. Close other Unreal
review processes when collecting GPU measurements and run phases sequentially.

Coarse replay times are synchronous CPU time, not native walking latency.
The cave geometry differs from the previous spline family; no identical-geometry
speedup is inferred. The moving review includes reference illumination. Active
walking metrics exclude deliberate still capture and settlement stops; the full
review timing retains them. The 100% table must not be mixed with the earlier
auto-resolution diagnostic run.
