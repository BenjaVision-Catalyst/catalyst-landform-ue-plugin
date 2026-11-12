# Surface-first Planet preview

Base component: `36fa4000fd1125be446c15d01c815b0234b3bc8b`.
Host: LandformLab, UE 5.7, macOS arm64 Development.

Rift Character now curates 12–42 m throw, retaining 180–440 m spacing and
8–26 m scarps. Eleven assembled/Solo field samples stay at local metre scale;
the recorded assembled maximum rises from 4.199412 m to 5.857777 m.
Older authored physical ranges remain pinned until a knob gesture adopts the
new defaults. The saved FamilyReview adopts only its two throw limits; Character
1, Seed 37 and all other authored fields are preserved.

Planet first compiles its complete surface with the original authored recipe,
then waits for native VPP publication and a drawn viewport frame before starting
caves. The continuation reuses that surface and defers diagnostic measurements.
The independent visible-pixel check verifies an opaque surface and picking while
caves are still absent; field/proxy readiness alone is insufficient. Walk marker
and native owner survive the final cave publication. Ordinary runtime compilation
continues to return a complete planet including enabled caves.

The expanded heading is Continental Rift. Geographic lines, highlighted equator
and labelled N/S poles start visible; province cells start hidden. Both overlays
have independent Preview controls. Grid paths are cached per accepted surface,
with no terrain work during navigation or overlay toggles.

## Qualification limits

`initial-render-validation.json` retains an early native-only readiness result.
Its first surface screenshot was blank despite available geometry; that result
is not visual qualification. The replacement check requires visible pixels and
a surface frame before releasing caves.

Two CurrentRift runs exceeded the old 90-second total deadline during close-up
refinement; both retained visible overview caves (about 21 seconds). The latest
qualification uses the same 300-second total ceiling as other cave fixtures and
records overview/close-up times separately. Native quality and chunk sizes,
visible cave coverage, publication/task readiness and the 0.5 m close-up field
budget remain unchanged. This is not a cave performance or visual approval claim.

Cave anatomy was not changed. Surface and cave images are actual native editor
captures for user review, not approval of the cave shapes or Rift appearance.

## Measured result

Final rendered qualification passed: 39 networks, 30 native inspection domains,
26,275 overview vertices and 12,667 visible cave pixels. Overview readiness was
20.643 s; close refinement took a further 71.766 s, with 53,423 nearby vertices,
0.600 m maximum native cell size and 0.217652 m maximum field residual. The close
phase is still slow; increasing the test ceiling is not a performance improvement.

The final surface-first frame contains 185,911 opaque surface pixels before cave
construction is released. The recorded surface-ready and cave-ready times are in
`surface-timings.json`; the controlled test holds caves for 0.5 s to inspect the
intermediate state. Its capture is `surface-before-caves.png`; the completed frame
is `surface-with-caves.png`. Camera motion performs no authoring evaluation or
mesh copy; `rift-navigation.json` records the final slider navigation measurement.

Field tests cover eleven slider positions, old-profile pinning, geological
restoration, enabled-minimum cave generation, placement and entrances. Controller
tests cover stale acknowledgment, replacement while waiting, cancellation during
caves, undo/redo and row persistence. The continuation is compared against the
normal compiler's surface, cave placement and actual cave cutter field.

`authoring-preservation.json` and `review-changes.json` record the authoring scope.
`staged-source.json` fingerprints the staged code/recipe used in LandformLab.
`stock-vpp.json` retains the four unchanged recorded stock VPP file fingerprints.

Pole captions convert projected physical pixels into canvas DPI coordinates;
the high-DPI desktop render is checked as well as the original no-high-DPI run.
