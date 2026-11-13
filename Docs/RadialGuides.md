# Packaged example radial guides

The plugin contains `DA_ExampleRadialGuides` at
`/CatalystLandform/RadialGuides/DA_ExampleRadialGuides`.
Its sets reproduce the example planets' normalized radii and guide colours
at full stored precision: 100 m, 1 km, 10 km, Mars measured radius bounds and
asteroid. The authored 100 m variant is included alongside the original
100 m example set, preserving both. Each set has a stable catalog identity. Mars's formerly anonymous
embedded snapshot has a persistent identity in the packaged catalog.

New Planet Recipe assets reference the 1 km set. The Planet editor's Radial
Guides menu exposes the library without requiring a project asset. Selecting a
style changes the planet reference and resolved snapshot. Opening a self-contained
or project-library-backed planet does not replace its authored snapshot.

The first edit to a packaged set copies the whole catalog into a unique
`/Game/Landform/DA_RadialGuideLibrary` asset and applies edits there. Planet Save
includes a local catalog but excludes the packaged catalog. Cancel restores the
original reference and snapshot; one Undo reverses a complete guide gesture.
No asset is automatically saved by opening, selecting or editing guides.

Guide positions are fractions of the planet's sky-envelope radius. Resizing a
planet retains these fractions. SurfaceMin/SurfaceMax guide surface composition;
CavesMin/CavesMax guide cave placement independently of surface rendering.

Guide labels, ticks and the add-guide control display without hover tooltips.
