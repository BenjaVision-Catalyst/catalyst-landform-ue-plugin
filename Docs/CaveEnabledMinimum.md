# Enabled caves retain the natural family

Caves ON guarantees generation and retains the established natural cave quality.
Rock metadata guides preferred placement and bedding; it cannot switch caves off.
The eye toggle requests zero, and later authored edits retain their existing order.
VPP and the existing UI remain unchanged.

The previous minimum implementation was a regression: three overlapping chamber
lobes and a stretched entrance chamber replaced the natural family when host
checks failed. Its air/connectivity tests did not establish the requested cave
morphology or floor quality. That implementation has been removed.

The enabled minimum now uses the existing spatial family's irregular chambers,
looping galleries and shared rolling floors. General-host placement uses physical
radial coordinates without rewriting geological metadata; canonical strata still
come from the original rock source. Dimensions adapt to small bodies within the
same family. Surface access uses an ordinary passage and continuous gradual floor
from a chamber outward to local terrain, rather than a shaft from the global
minimum surface radius. Existing successful preferred bedding geometry is retained.

The permanent component [contract](../AGENTS.md) forbids replacing this family with
primitive volumes to satisfy existence. The regression suite must assert actual
spatial-family identity and natural-family passage data, alongside positive air.
`Catalyst.Landform.Caves.RiftNaturalFamily` loads the current Continental Rift
review asset and checks chambers/passages, head clearance, foundation, actual
route grade, scalar/native parity and delivery into the real terrain field.
`AlwaysEnabledMinimum` checks the same family across missing geology, zero density,
small/large planets and all saved profile selections. Other focused regressions
retain native field parity, strata, persistence, backfill and background checks.

This correction does not claim a new cave-performance or ecosystem qualification.

## Verified correction

UE 5.7 Mac arm64 native build and all six focused tests passed: AlwaysEnabledMinimum,
EligibilityAndVersions, KnobAndBackground, NativeFieldsAndPersistence,
RiftNaturalFamily and SmallPlanetMinimumAndScale. The 72 fitted cases now assert
spatial-family identity and use freshly compiled physical bodies at each radius.
The test host matched all 275 source/build/configuration/shader inputs.

The saved Rift at Character 1 produced 26 networks, 208 chambers, 234 passages and
26 entrances. Walking probes reported zero clearance failures, zero cutter-floor
failures and no unsupported feet in the final terrain field. Maximum sampled route
grade was 0.30134 (about 17 degrees); native/scalar residual was 0.00001152 m.
These checks establish this saved recipe and the stated synthetic cases. Visual
approval remains the user's review of the restored existing family.
