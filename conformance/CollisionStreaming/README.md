# Collision publication witness

Captured 7 November 2026. See [the findings](../../Docs/CollisionStreaming.md).
This is one instrumented causal check, not a performance A/B comparison.

- `native-result.json`: successful 100.04 m native walk, every route frame grounded.
- `witness.json`: 25 observations, including 20 held snapshots and five releases.
- `summary.json`: episode/state correlations and all-snapshot predicates.
- `command.json`: actual launch arguments; adjust temporary paths for a new host.
- `environment.json`: baseline commits, settings and installed vendor-source hashes.
- `diagnostic-source-sha256.json`: compiled source manifest for the observer build.
- `observer/`: our temporary read-only observer and its two-line gated hook.
- `verify.py`: validates the retained result and publication correlations.

The observer samples the same short forward radial physics trace as the movement
guard. It checks VPP's newest subsystem's computed keys and samples the passive
original generated field to establish that a floor crossing exists in the trace
range. The native sculpt is empty in this entrance proof. This direct field
check is not a general replacement for final edited-layer queries.

To reproduce, use an independent physical Lab project at the recorded baseline
with the pinned plugin and VPP installed. Copy the observer header/source to its
Lab module's `Private` directory, apply `observer/hook.diff` to that module's
`CLGeneratedCaveProof.cpp`, and build. Replay the recorded command with adjusted
engine/project/recipe/report/log paths and `-CLCollisionResearch`. Keep the native
voxel and invoker settings fixed. The observer writes `collision-witness.json`
next to the requested report. Remove the hook and observer files afterward and
rebuild; the captured host was restored to all 278 published source hashes and
its restored build succeeded.

The old-state `HasCollision` API is intentionally not called: once a new state
loads from it, it is a previous subsystem. The observer uses the newest subsystem,
whose check includes retained previous computed references. Physics readiness is
measured independently with an actual world trace.

No vendor source, substitute floor, movement predicate or native collision
setting was modified. Raw editor logs remain local diagnostic output.
