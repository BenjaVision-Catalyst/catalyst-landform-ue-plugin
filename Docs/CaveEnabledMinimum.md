# Enabled caves are unconditional

Caves is an authoring and exploration feature. Enabling it promises a real cave
layer; it must not require the user to prepare a compatible three-rock palette,
select a province or supply a drainage history. Geological metadata influences
preferred morphology and location. It never grants permission for caves to exist.
A hidden geological prerequisite also invalidates the preview, making unrelated
terrain editing appear broken. That was the defect, entirely in Landform code.

The compiler preserves a successful preferred construction verbatim. When it
cannot supply a complete network, it fits one deterministic general network into
the conservative radial interior, using overlapping native vault/lobe kernels.
Surface access, when requested, adds a connected opening. This minimum is actual
terrain subtraction delivered by the existing scene and VPP stamp, with canonical
rock queries retained. It does not fabricate rock identity or carbonate history.
Character zero, density zero, unknown/missing host data and preferred-placement
budget exhaustion cannot switch caves off. The eye toggle requests zero.
Subsequent authored backfill and native sculpting retain their existing ordering.

The invariant is recorded in the component [AGENTS.md](../AGENTS.md) and
[Caves contract](Caves.md#generation-and-editing-contract), with executable
regressions in `Catalyst.Landform.Caves.AlwaysEnabledMinimum`. Future cave changes
must retain this invariant instead of introducing a geological admission rule.
VPP source/configuration and the existing UI are unchanged by this change.

## Qualification

Native UE 5.7 Mac arm64 editor build; five focused automation tests passed:

- AlwaysEnabledMinimum: 72 combinations of profiles 1–9, radii 10/100/1,000/10,000,000 m,
  and surface access off/on, with missing geological context, zero character and
  density, oversized authored dimensions, actual positive-air and connected-field
  probes, deterministic construction and finite inspection resolution.
- EligibilityAndVersions: unknown and mixed hosts, missing/unselected provinces,
  older spherical recipe schema and invalid dimensions.
- NativeFieldsAndPersistence: existing native bulk/sparse parity, strata, recipe
  round trip and later authored backfill.
- KnobAndBackground: existing progression, reseeding, preferred placement budget
  and background parity.
- SmallPlanetMinimumAndScale: existing saved 500 m planet, seeds, zero density,
  eye off, absent rock and deterministic reload.

The minimum tests establish existence and connected field geometry, not visual
acceptance or walkability of every fitted shape. Preferred family geometry code
is unchanged. Full procedural-family walkthrough/performance qualification is
outside this change; no freeze or ecosystem-headroom result is claimed.
