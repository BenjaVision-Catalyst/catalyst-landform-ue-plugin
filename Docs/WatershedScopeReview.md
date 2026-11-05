# Watershed scope and preview review

**Presentation update:** the later [Watershed debug view](WatershedDebugView.md)
supersedes the old line/rim-only display and fixes camera-LOD suppression below.
It adds editor-only ocean/lake surfaces and splines; runtime hydrology limits remain.

5 November 2026. Review of `a4884ca` against the initial Watershed implementation
`116b788`. This task changes Character typography and Geology row order, and
reviews Watershed; it does not change its generation algorithm or add water.

## Scope finding

There has been no subsequent hydrology/terrain-feature expansion in the watershed
model. The model diff since `116b788` is limited to cancellation checks and native
sampling/progress wrappers. The first implementation already included reference
rain/runoff accounting, lake/ocean classification and declared closed terminals.
That historical fact does not establish that every advanced control is necessary
for the intended Geology authoring workflow.

The minimum useful layer contract is **derive drainage from the composed terrain
and shape coherent valleys/channel beds through the existing VPP planet stamp**.
Reference rain and runoff fraction currently change discharge metadata only;
they do not change valley geometry or routing. These are optional reference
hydrology/diagnostic controls, not more terrain-character controls. Their place
in the artist-facing advanced fields needs review before further hydrology work.
Closed-terminal policy does affect drainage topology and should remain explicit.
No controls or saved fields are silently removed in this review.

## What reaches the preview

| Output | Current behaviour | Meaning |
| --- | --- | --- |
| Valley/channel-bed conditioning | Actual native VPP terrain in globe, section and walking views | Static terrain changes; can cut small humps and fill local lows. No conserved sediment model. |
| Blue drainage lines | Optional globe overlay | Active constructed reaches that remain final drainage receivers; not water meshes. |
| Grey routing lines | Optional globe overlay | Final finite-resolution flow relations; not all are graded constructed rivers. |
| Cyan lake-rim points | Optional globe overlay | Potential depression/spill information; not current lake level or filled lake geometry. |
| Ocean classification | Internal graph policy | Largest connected sub-sea component is the reference terminal ocean. It creates no ocean mesh. |
| Preview “Ocean” | Existing independent translucent sea-level sphere | Predates Watershed; follows OceanHeight, not the watershed ocean mask or lake spill levels. A display reference, not generated water. |
| Focus largest channel / Walk here | Existing camera and native terrain inspection host | Lets authors inspect a generated reach on the same accepted terrain. |

There are no generated UE river/lake/ocean water bodies, flowing water, wetness,
shoreline treatment, water physics or gameplay water volumes in this layer.
The one-knob values do not represent current rainfall or elapsed geological time.
Eye-off restores the upstream surface. Character 0 is still enabled, lightly
conditioned terrain: nodal incision .75 m, largest influence 35 m and channel-area
threshold 12,000 m². At 100 these are 9 m, 100 m and 1,500 m² respectively.
Rock erodibility scales nodal incision. Incision is not maximum measured cut,
and influence width is not a guaranteed excavated cross-section width.

The graph is computed before caves, volume operations and player sculpting. Native
mining and sculpt save/load retain the natural graph; a breach or dam does not
currently reroute it. It must not be presented as live edited-world hydraulics.

## Demonstrated gaps and presentation drift

- A single analysis mesh is capped at 65,538 cells. It refuses unresolved physical
  widths rather than silently coarsening. This does not deliver global metre-scale
  Earth/Mars drainage; measured Mars resources are deliberately source-pinned and
  do not accept this procedural watershed layer.
- The optional overlay is currently hidden during any native readiness/LOD update,
  including camera movement with an unchanged recipe. That is a presentation gap,
  separate from authoring progress, which is now correctly independent of LOD.
- Existing review frames mix overview and focused cameras. They demonstrate native
  presentation, not a controlled same-view comparison across every slider value.
  Topology, grade and persistence tests do not establish convincing visual geology
  at every knob position, seed and body scale.
- Some conditioned corridors can become abandoned after final routing. A visible
  trough need not carry a blue active-channel overlay.
- Earlier text treating the million-vertex inspection limit as a general preview
  guard is obsolete. Inspection is now explicit and lazy; VPP alone owns visible
  terrain/LOD. The existing final/draft preview qualities remain 4/1.

## Recommended next bounded work

Keep the next Watershed task on terrain authoring and presentation: fixed-camera,
overlay-off comparisons for eye-off and Character 0/50/100, across representative
seeds and 5/10/20 km² bodies; verify native valley visibility, geological coherence
and overlay stability while navigating. Review the reference-hydrology controls
as diagnostics rather than expanding the Geology knob into a water simulator.
Water rendering, current lake/flow state, edit invalidation, sediment/climate and
karst remain separate capabilities. Native VPP generation/LOD/collision and UE
features must be audited first for any such capability. Multiplayer stays out.

The [qualification record](../conformance/ReadoutsAndWatershed/qualification-mac-arm64.json)
records the actual layout, native preview cases and scope-boundary checks. The
owning [Watershed contract](WatershedCharacter.md) retains detailed model rules.
