Latest read-only gate correlation: [measured collision gate](../../Docs/ProceduralCaveFamily.md#measured-collision-gate). `collision-gate-audit.json` shows State 47 collision generation finished before the longest hold; its render-generation task context delayed publication and the next state. This finding does not fix the failed walkthrough.

> **Current status: EXPERIMENTAL, not a freeze fix.** The latest integrated
> provider completes the 400 m control grounded but still has a 0.43 s maximum
> stop and 0.90 s total admission holds. The stock scheduler is unchanged and
> game LOD quality is restored to its native default of 1. Existing 20 cm voxels
> and 32 m / 1 m invokers remain explicit nondefault world settings. No full-route,
> native edit/cold-reader or ecosystem-headroom acceptance is claimed for profile 9.
> See [the docs re-audit](../../Docs/ProceduralCaveFamily.md#documentation-re-audit)
> and `docs-reaudit-trials.json`. Earlier results below are revision-scoped history.

# Procedural cave-family qualification

11 November 2026. UE 5.7.4, installed VPP 2.0p8, Mac arm64 M3 Max
(16 CPU cores, 128 GB). See [the family contract](../../Docs/ProceduralCaveFamily.md).
All 11 retained-build geometry, controls, native delivery and legacy regression tests pass; see `retained-automation.json` and `retained-checks-command.json`. Native full-route, persistence and ecosystem qualification remains incomplete.

## Geometry and preservation

`family.json` records three independently compiled seeds: 176, 731 and 2049.
Each fits one complete three-level network, 18 chambers, 22 passage connections
and four alternative ramps. Every sampled route retains head clearance and solid
foundation; the maximum physical grade is 0.391. Each exposes 29 physical strata
and the three preserved appearance bindings. The independent unpruned scalar
oracle loses no void signs on 26,896 grid probes per seed. Maximum scalar/SIMD
error at route/foundation probes is 0.016 mm.

The controls/determinism test covers Character 0.01, 0.5, 0.85 and 1.0, producing
one, two, three and three levels. A larger 1,600 m recipe fits multiple complete
networks, reproduces identical seeded route metadata and same-packet fields,
and preserves independent native inspection selection. Sparse/coherent packet
error is 8.16520078217e-8 m against a 0.1 mm budget. Repeated identical worker
packets remain exact. Profile-7 serialized defaults are unchanged; profile 8
retains its frozen composition. Profile 9 explicitly refuses surface entrances.

`family-automation.json` is the completed real-RHI report for both family tests
after the chart-domain correction and native-float local kernel. `family-command.json` records its exact
arguments. `cpu-test-events.txt` records seven earlier completed CPU test successes: delivery parity,
the two family tests, and the four existing SpatialPrototype tests.
`cpu-tests-command.json` is the exact invocation. That run subsequently attempted
a windowed Designer test under NullRHI and could not create a native window; it
is not counted as a completed automation suite. The separate real-RHI
`ui-and-legacy-automation.json` records all four remaining selected tests passing:
Designer controls/undo, eligibility/version handling, native noise batching and
native scene bulk/sparse parity. Their exact command is retained.

`legacy-spatial-geometry.json` and `legacy-spatial-work.json` retain the old
profile-8 field and coarse-work regression evidence. `native-source-sha256.json`
verifies the changed plugin and Lab source bytes against the physical native
host used for the final runs. No engine or VPP sources were changed.

## Delivery and scheduler investigation

The initial development implementation filled unused network support with a
near-surface distance, wasting native refinement, and compiled an unnecessarily
broad bedding chart. `failed-initial-family.json` retains that failed run;
it is historical development evidence, not a reproducible final-source baseline.
The final supplier uses tight finite cell supports, conservative exterior
distances, native packet candidate queries, one SIMD union and immutable compiled
bedding/surface inputs. It does not preload voxel geometry or memoize point values.

The following same-geometry 1 km trials retained the 20 cm voxel setting,
native quality 2, requested 3,000 cm/s speed, centred 32 m moving invoker and
1 m foot anchor. Exact arguments and unmodified numeric reports are retained.
The old photo-route lighting was present in these short comparisons.

| Native background workers | Guard hold seconds | Longest stop under input | Max moving frame | Outcome |
| ---: | ---: | ---: | ---: | --- |
| 2 (Unreal game defaults) | 65.995 | 7.317 s | 56.75 ms | Failed |
| 6 (8 total workers) | 4.878 | 0.383 s | 126 ms | Failed |
| 14 (16 total workers) | 0.484 | 0.183 s | 52.41 ms | Failed |
| 10 (12 total workers) | 0 | 0 | 42.52 ms | Short route passed |

Unreal's generic game-mode scheduler caps its initial worker pool at four;
two are foreground workers. VPP's supported worker API restarts that shared
scheduler, rather than creating a private terrain pool. These historical worker
trials did not qualify the complete route. The override has been removed, along
with experimental task budgets, collision consumers and radius tuning. Current
launchers retain the native scheduler. Existing project resolution and invoker
settings are not a fully stock-default VPP control.

The first full family benchmark failed at 5.8 km. Its traversal revisits every
edge, but the old photographic lighting code added fills repeatedly along that
entire route. The family roam benchmark now uses the same headlamp and room
lights as manual exploration. `failed-duplicate-photo-lights.json` retains the
failure. `focused-floor-replay.json` crosses the failed connection using original
route indices 995 onward: 299.7 m, 851/851 grounded movement frames, zero holds,
and 27.4 ms maximum frame. This establishes that the native floor can be
traversed; it does not replace the complete-route qualification. Forced-30-FPS
and uncapped focused repeats also retained every grounded frame without holds.

Removing the extra lights alone was insufficient: the full route completed
7,653 m with one ungrounded frame, 41.85 seconds of guard holds and a 127 ms
maximum frame. `failed-unpruned-nearest-full.json` preserves that failed result.
CPU sampling identified the SIMD union as the largest remaining supplier cost.
Exterior rock lanes still compared distances against all 40 cell boxes. A packet-level nearest-support subset was tested, but used 7% more evaluator CPU
per point on the complete route and did not remove the waits. It was reverted;
`failed-nearest-pruning-full.json` retains its result.

A 64 m native moving collision window was also tested on the same late route.
It increased collision memory from 43.5 MB to 163 MB and added 2.68 seconds of
holds; the original 32 m window passed that fresh late-route comparison with
zero holds. The larger window is not the selected solution. Exact commands and
reports are `late-window-*`.

Stack sampling then identified exact tectonic/rock evaluation beneath the family
bedding sampler, despite zero uncertain chart leaves. The aggregate network box
contains positions outside the compiled chart; those positions fall outside all
finite cell supports. The supplier now returns a conservative world-distance
bound there, using its native support index, rather than evaluating irrelevant
tectonic geology. In-domain uncertain leaves retain the exact evaluator. This
changes the conservative exterior field in unsupported rock, while preserving
all zero contours and canonical geological samples. Packet buffers reserve their
known sizes; noise coordinates are built only for packets with candidate cells.

The double local kernel still failed the full-route pause gate after domain
correction (`failed-domain-double-full.json`). Profile 9 now uses native float
arithmetic within its bounded local coordinates, retaining double body/bedding
coordinates and the original double profile-8 kernel. The independent scalar
comparison bounds the resulting numerical difference; no physical scale or
movement gate is reduced.

## Scope

The native C++ supplier is the reviewed walking path. The graph binding also
accepts profile 9, but no new profile-9 graph performance result is claimed.
The numerical tests do not establish arbitrary seeds, every Advanced setting,
other platforms, surface connections or real ecosystem rendering/simulation.
Temporary review lighting and any ISM stress fixture belong only to the Lab.
