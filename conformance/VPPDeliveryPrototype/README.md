# VPP delivery prototype evidence

Qualification captured on 7 November 2026 for the opt-in comparison described in
[the report](../../Docs/VPPDeliveryPrototype.md).

- `environment.json` records engine, VPP, hardware, native settings and baseline revisions.
- `native-source-sha256.json` identifies every compiled plugin and Lab source file.
  Paths are relative to the isolated Lab host: `Plugins/CatalystLandform/Source/`
  maps to this component, and `Source/LandformLab/` maps to the workspace Lab module.
- `native-tests.json` retains the 29 native automation results, including exact
  field, surface, geological and authored-operation parity.
- `precision-regression.json` records the saved-position cache regression and
  the exact-packet fix covered by the native parity test.
- `query-benchmark.json` retains balanced native bulk and sparse query timings
  and matching output checksums.
- `accepted-results.json` retains all accepted standalone trial results and their
  original command arguments. Temporary paths describe the captured run; use
  [the portable runner](../../Tools/compare-vpp-delivery.py) for a new checkout.

Only final frozen-source `runs-v3` measurements are accepted. Six entrance,
six render-headroom and eight native edit/save/reload runs were captured. The
performance sample is two runs per backend and scenario; reported ranges are
observations, not statistical confidence intervals. The object fixture measures
opaque instanced-sphere rendering, not complete ecosystem capacity.

Native owners, recipe digest, object transform digest, voxel settings and settled
render windows are checked before results are accepted. Native readers run in
fresh processes; their sampled distances must match their saved probes exactly,
and their material/geological identities must also match. Raw editor logs,
screenshots and native save files remain diagnostic local output, not source.
