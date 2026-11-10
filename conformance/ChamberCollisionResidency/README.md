# Chamber collision residency qualification

Physical macOS arm64 / UE5.7.4 / VPP2.0p8. Client checkpoint: 11 November 2026.
[Diagnosis and policy](../../Docs/ChamberCollisionResidency.md).

The failed user session and broad reproduction supersede the earlier short-walk
claim in `CaveWalkingFix`. They are retained, not converted into passing results.

- `failed-session.json`: fresh-launch user telemetry, 54.11 s admission waits.
- `baseline.json`: previous moving demand, three chamber laps at 600 cm/s.
- `cache-only.json`: exact base-field reuse, still insufficient moving demand.
- `publication-witness.log`: generated floor crossing beyond active native
  collider bounds, computed key present, pending state not ready to publish.
- `worker-field-stacks.txt`: relevant literal sampled stack paths; qualification
  metadata retains the full raw sample's SHA256. No exclusive CPU percentages
  are inferred from this selected call-tree excerpt.
- `residency-initial.json`: first full-floor native demand trial, 3,000 cm/s.
- `wall.json`: ordinary full-speed input against native steep walls and a
  reversal; blocking contact must not create a generation-admission hold.
- `chamber.json`: final-build three-lap test at the same requested speed. It
  requires 1 km travel, full peak speed, every frame grounded, zero admission
  waits, no 100 ms stopped-input interval and no 100 ms moving frame.
- `review.json` / `reader.json`: native excavation/backfill, unchanged control,
  sculpt save, independent-process reload of 14 probes with zero field error,
  and a short native grounded reload traversal.
- `headroom.json`: the existing four stationary render-only ecosystem proxy
  levels. These are not a populated ecosystem simulation or a moving GPU trace.
- `resident-before-wall-fix-*`: real-input evidence that identified short false
  holds on already generated, unwalkable physics; retained before correction.
- `real-input.json`: final manual process, ordinary WASD forward, reverse and
  sideways input, including sustained input into obstacles. This free-input
  record checks admission waits/frame stalls; airborne frames are retained.
  Exact grounded continuity is required separately on the kilometre chamber
  route and timed wall fixture. `manual-ready.json` identifies this process
  and its speed.
- `cache-parity.json`: exact cold/warm/mixed/concurrent field parity, bounded
  allocation, and coverage of 1,821 generated floor sites.
- `native-tests.json`: eight native regressions, zero warnings/errors.
- `native-source-sha256.json`: 283 native source files from the qualified host;
  covers the component and Lab harness, not generated binaries/assets.

Commands alongside reports retain the actual launch settings. `--manual-settings`
removes benchmark timing/quality overrides and unattended mode. Automated routes
use ordinary movement input and reduce speed near waypoints to turn, while the
long crossings must reach the full 3,000 cm/s cap. They never reposition the pawn
while walking. The safety guard and native foot readiness check stay enabled.

Run `python3 verify.py --host /path/to/LandformLab` to check retained gates and
source parity. Reproduce the broader walk with
`Tools/qualify-spatial-cave.py --phase chamber --manual-settings --project ... --output ...`.
The existing manual launcher uses the same speed/render settings and records
bounded hold/release diagnostics alongside its ready and movement telemetry.

The final host rebuild used `-SkipRulesCompile` with a generated rules cache
containing one definition per installed Marketplace module. Newly installed
Ascent versions had duplicate rule types and UBT's failed compile left its DLL
empty. Rule compilation selected one original source per module, matching UBT's
first module-map selection. No Marketplace, engine or VPP source was edited.
This is build-environment provenance, not part of the terrain implementation.

This policy requests the finite active spatial cave floor. It does not preload
planet-wide caves or claim a performance fix for the historical spline provider.
