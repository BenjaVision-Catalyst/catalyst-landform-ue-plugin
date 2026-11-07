# Solid-rock work audit

Follow-up to [the collision investigation](../../../Docs/CollisionStreaming.md).
These are synchronous native diagnostic replays, not a live traversal comparison.
The production evaluator, morphology, geology and UI were unchanged.

`CLChunkWorkAudit.cpp` runs the published field at the chunk keys returned by
VPP's actual `ComputeInvokerChunks` for a 25 m sphere and 6.4 m chunks. It replays
the installed volume-only cell generator's 11 cubed coarse grid, distance
threshold, sign-neighbour check and nonrecursive propagation. It counts fine
sample positions selected by that mask, without meshing or cooking them.

`phase-replay.json` retains per-chunk extrema, selections and phase timings.
`direct-binding-summary.json` retains the earlier call through the actual binding.
Their checksums, every chunk's field extrema and selection fingerprints agree.
Timing variation prevents interpreting this pair as a speed comparison. The
analytic sphere is a cost control with different geometry, not a proposed
replacement or an equivalent-workload benchmark.

`CLCandidateBoundsAudit.cpp` reconstructs the unchanged cave compiler's exact
centreline subdivision and bounds expressions using native spline/AABB APIs.
It verifies the aggregate bounds against the actual compiled cave bounds, then
counts candidate feature/point pairs at the same coarse probe positions.
`candidate-overlap.json` retains those counts. This is a source-based bounds
reconstruction, not a live callback instrumentation counter. Infill features are
not part of this main passage/chamber candidate count.

To reproduce in an independent physical Lab at workspace `d97775d` / component
`80c7f02`, copy the two diagnostic sources into its Lab `Private` directory and
build. Their input/output paths currently name the captured temporary directory;
adjust them before running. Run automation `Catalyst.Landform.Diagnostics.ChunkWork`
then `Catalyst.Landform.Diagnostics.CandidateBounds`. The latter reads the former's
result. Use the pinned UE5.7.4 / installed VPP2.0p8. The captured selection has
`Tolerance=0.1`, sign checks and propagation enabled, and 20 cm native voxel size.

Both disposable sources were removed after capture, and the host was restored
to the 278 published native source hashes. No diagnostic was installed in the
maintained Lab or plugin runtime.
