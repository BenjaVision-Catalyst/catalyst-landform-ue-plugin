# Cave generation contract

When Caves is enabled, every valid planet recipe must produce at least one real,
nonempty cave in the shared terrain volume. Only disabling Caves requests zero.
Character zero and density zero retain the enabled minimum.

Rock identity, fracture/dissolution potential, plates, provinces, drainage and
formation history may guide morphology and preferred placement. They must never
be prerequisites for the existence of enabled caves. Fit a general cave when
preferred construction cannot fit; do not invent or rewrite geological metadata.

Preserve successful existing cave fields. Keep VPP stock and the existing UI.
Any change to cave eligibility, placement, fit or recipe validation must run the
enabled-minimum regression tests, including missing/unknown geology and small
planets. Cancellation and invalid input remain normal pipeline validation, not
geological eligibility rules.
