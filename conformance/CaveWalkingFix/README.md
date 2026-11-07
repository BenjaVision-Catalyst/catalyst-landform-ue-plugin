# Cave walking correction evidence

See [the cause, correction and results](../../Docs/CaveWalkingFix.md).

- `baseline.json`: the original 25 m configuration and distant guard. The old
  quarter-second acceptance threshold passes this run despite a 151 ms stop.
- `step-only.json`: corrected actual-step checking at 25 m; captures a remaining
  70.8 ms coverage wait without losing grounded contact.
- `fixed.json`: 308.62 m loop under manual settings, native 32 m collision lead,
  3,727 grounded frames, no admission or input-progress stops. A passive legacy
  probe rejects two frames where actual native-supported movement continues.
- `real-input.json`: manual W/D/A/S verification, 2,320 input frames grounded,
  zero waits, maximum moving frame 45.86 ms. This is real window input, not the
  route controller. The walkthrough remains available for further exploration.
- `legacy-entrance.json`: historical spline control, 100.06 m with all 3,356
  frames grounded; its 17.23 s generation waits remain outside the prototype fix.
- `native-tests.json`: seven passing native geometry, scene, UI/undo, delivery
  and gravity tests, without test warnings or errors.
- `native-source-sha256.json`: 282 exact sources of the final native build. Only
  movement admission and three Lab source files differ from the spatial
  prototype checkpoint; all geometry/geology sources remain unchanged.
- `commands/`: exact invocations for the baseline, step correction, final
  manual-settings route and independent checks.

`verify.py` validates these retained records and optionally checks physical-host
sources with `--host`. The final gate requires no admission waits, no 100 ms input
stop and no 100 ms moving frame on the route. It does not classify native wall
contact as a stall in manual play. No VPP or engine source was changed.
