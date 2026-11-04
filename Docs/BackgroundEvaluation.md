# Background Planet Designer evaluation

5 November 2026. This changes evaluation scheduling and feedback, with the
existing native VPP preview remaining the sole voxel presenter. It does not add
watershed variation or a streaming manager.

## Authoring contract

Changing a setting updates the authored recipe and UE transaction immediately
after structural/native-input preparation. Expensive derived fields run in a UE
background task. The last successful terrain remains visible. Preview shows a
native indeterminate progress bar and the current stage: geology, elevation,
drainage or native preview update. This is stage/activity feedback, not an
invented percentage or estimated completion time. Camera navigation remains
available; terrain placement, picking and walking wait for a current preview.

The editor admits one running evaluation and one replaceable latest pending
input. New edits cancel superseded field work at bounded checkpoints and replace
the pending input. A serial fence discards old completions, including those
superseded by Undo. The final recipe snapshot alone can publish. There is no
unbounded task per slider event, polling from worker widgets, or worker access to
mutable authoring assets.

Preparation errors reject the edit before mutation, as before. Errors found only
during derived evaluation leave the authored values editable and show an error
over the last successful terrain. That terrain is explicitly labelled as stale.
The author can correct the values or use native Undo. This distinction replaces
the former synchronous full-compile admission for Planet Designer edits. It does
not change the runtime compiler's synchronous fail-without-plan contract. Saving
an authored recipe is not a claim that its derived preview succeeded.

Closing the asset editor removes its ticker and cancels its request without
waiting on the UI thread. The task owns only captured input values, immutable
native plans and atomic progress state; it holds no toolkit, widget or asset
editor. Its result cannot publish after the toolkit is gone. Cancellation is
cooperative and can finish a current bounded native sample batch before exiting.
A module-owned native ticker releases completed job records even after the last
asset editor closes. Module shutdown cancels and joins any remaining native jobs
before the editor code unloads.

## Native audit and demonstrated gap

`FCLNativePreview` already retains a native `AVoxelWorld`, coalesces pending plans,
and observes VPP readiness/task counts and its rendered-state callback. VPP owns
meshes, generation, LOD, collision and material presentation. No replacement of
that mechanism was needed. The pause was earlier: authoring validation and
`Refresh` synchronously compiled the full derived field, followed by synchronous
elevation and bed diagnostics, before submitting anything to VPP.

`FCLPlanetCompiler::PrepareBackground` resolves client assets, surface libraries,
stateless VPP kernel bindings, native stamp/PCG inputs and raw fields on the game
thread. It captures immutable snapshots. Matching raw field inputs are reused
between downstream edits. Any non-recipe UObject modification or property change
invalidates this reuse; upstream recipe values, native resource pointers and
local region/face requests are compared explicitly. The cache is bounded to the
last prepared input per editor. Unchanged measured pins and outer radius also reuse the captured resource. A
changed measured reference or outer radius resolves and validates its pinned
native resource on the game thread. `CompleteBackground` then follows the
same compiler pipeline on a UE task, performing range/CDF fitting, deposited rock
contacts, watershed analysis/conditioning and diagnostic statistics. Generated
regional characterization and contour construction also run there; their possible
native palette children are prepared first. Unused invalid children remain
unselected rather than silently changing normal palette-selection semantics.

The normal synchronous compiler uses the same field algorithms. Preparation-only
placeholders are private and never submitted to a viewport or runtime world.
Whole-sphere, cube-face, local-patch and measured resource paths are covered by
parity checks. Native resource resolution and PCG preparation still have a game
thread boundary. Cold asset loading, arbitrary PCG workloads and every legal
planet size are not established as hitch-free by this bounded qualification.

## Verification and review

The [qualification record](../conformance/BackgroundEvaluation/qualification-mac-arm64.json)
records exact tests, native timings, source hashes, captured preview states and a
cooked watershed regression. Use its recorded workload and hardware when
interpreting timings. It is not a whole-planet performance guarantee.

Run the native editor automation filters `Catalyst.Landform.Background` for field
parity and the actual responsive asset-editor proof. The latter exercises rapid
Watershed edits, latest-result publication, native Undo while work is pending,
a derived failure with retained terrain, recovery and closing with work in flight.
Artifacts are emitted to `Saved/Verification/background`.

The four Geology header rows and Planet Surface Distribution authoring layout
remain those of [Planet Designer authoring](PlanetDesignerAuthoring.md). Independent
watershed-local variation remains the next separate task. Multiplayer remains
out of scope.
