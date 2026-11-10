# Cave strata qualification

11 November 2026. Mac arm64 / UE 5.7.4 / VPP 2.0p8, M3 Max 128 GB.

The spatial cave had three geological units mapped to one native Rock surface.
The fixture restores the earlier physical-bed palette (Rock / Scree / Soil).
`recipe-comparison.json` records exactly two surface-ID changes; all other recipe
values are unchanged. The 6,673-probe geometry report exactly matches the previous
streaming checkpoint, including floor relief, grades, openings and roofs.
The native source fingerprint matched 289 staged component/Lab files;
no C++, shader, material asset, UI or vendor changes were needed.

Four existing spatial-cave native tests passed with zero errors. Three warnings
are the engine's existing Google connectivity-probe timeouts. `automation.json`
retains their actual results. Native passages/chamber screenshots were inspected
for visible material changes at the canonical bed contacts.

| Native run | Travel | Grounded/input frames | Collision holds |
| --- | ---: | ---: | ---: |
| Chamber, 30 m/s cap | 1041.23 m | 2082/2082 | 0.000 s |
| Passage/threshold review | 78.76 m | 494/494 | 0.000 s |

The chamber moving-frame p95/max were
44.37/68.72 ms.
This is one fixture run, not a controlled GPU performance comparison.
Both walks retained exactly two moving native invokers and zero static demand.
Native excavation/backfill, unchanged control and compressed save passed.
Cold reader PID 85812 differed from writer PID 85708;
14 probes retained fields, surfaces, unit/stratum IDs and
properties with 0 cm maximum reload error.
The existing qualifier enforced the native ownership and walking gates.

Raw reports and exact commands are adjacent. `passage.png`, `threshold.png` and
`chamber.png` are pristine native captures before excavation. The palette uses
provisional artwork; the surface names do not reclassify carbonate units as loose
scree or topsoil. Saved review assets and existing UI defaults remain unchanged.
