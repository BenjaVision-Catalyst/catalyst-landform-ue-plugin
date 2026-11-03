# Body coordinates and geometric datum proof

4 November 2026. This is a bounded P2 coordinate proof, before measured terrain
adoption. It does not change existing recipes, guides, terrain geometry, previews,
physics or presentation. VPP remains the sole voxel generation/presentation
platform, including layers, stamps, LOD, native material composition and moving
collision invokers.

## Native audit and ownership

UE 5.7.4 includes GeoReferencing, backed by bundled PROJ 9.1.1. Its native
`AGeoReferencingSystem` handles Earth geographic/ECEF coordinates, engine-unit and
handedness mapping and tangent frames. Its pinned ECEF/engine methods use
zero-origin engine coordinates: compose native `UGameplayStatics` rebase helpers
at a live-origin boundary, as checked below. Reuse that actor when the
consumer has its Earth/world contract; do not replace it with another Earth
world-georeferencing service.

The installed `GeoReferencingSystem.cpp` fixes the geocentric target to
`EPSG:4978`, including `ApplySettings` and the geographic-to-ECEF path. The
`NativeGeoAudit` test first checks that the native Earth actor agrees with direct
PROJ, checks native handedness/units and explicit native origin-helper
composition, then configures the actor with a Mars-sized custom source and
reproduces its two fixed-target refusals. A custom body is not the WGS84 Earth.
The audited native
VPP source exposes terrain/stamp transforms, not a scientific body CRS/datum
conversion service. No missing terrain-generation feature is claimed here.

`FCLBodyCoordinates` therefore selects PROJ's existing `cart` operation with
explicit figure axes. It adds no geodetic conversion solver. Surface normals
reuse UE's `FEllipsoid::GeodeticSurfaceNormal(FGeographicCoordinates)`. The
geographic overload matters: dividing an arbitrary elevated position by squared
axes is the quadratic-gradient normal there, not the geodetic normal at its
ellipsoid footpoint. Private runtime native dependencies match UE's
GeoReferencing
modules: GeoReferencing, PROJ and SQLiteCore. The plugin enables GeoReferencing;
no vendor or engine source is changed.

References: [UE 5.7 GeoReferencing](https://dev.epicgames.com/documentation/en-us/unreal-engine/georeferencing-a-level-in-unreal-engine?application_version=5.7),
[PROJ cart conversion](https://proj.org/en/stable/operations/conversions/cart.html)
and [PROJ figure definitions](https://proj.org/en/stable/usage/ellipsoids.html).
The installed source/library fingerprints and executed results own this pinned
qualification; newer online documentation does not upgrade its native version.

The actor audit also measured the raw zero/local-origin difference at
`(50000,-30000,10000)` cm: 59,160.797831 cm. The pinned ECEF/engine methods do
not consume `UWorld::OriginLocation`. Composing UE's existing
`RebaseZeroOriginOntoLocal` and `RebaseLocalOriginOntoZero` closes this boundary;
these helpers are gated by `p.EnableMultiplayerWorldOriginRebasing`. The audit
checks their disabled identity behavior and enables/restores that native policy
only within the test. No saved config or production replication policy changes.
There is no replacement origin algorithm or GeoReferencing actor patch.

The initial synthetic oblate profile (1,000 / 980 m, +/-98 m) kept native inverse
position within 0.209022 mm but reached 1.657612e-7 normal-vector error, beyond
the declared 1e-9 target. That profile is not admitted by version 1. Rather than
add another inverse solver, the admitted ratio is >=0.99 and the oblate height
band is reduced to 2% of polar radius (still capped at 10 km). The original
failed fixture/report remain in the qualification evidence. The target is
unchanged; broader figures/heights need a separately qualified native profile.

## Version 1 boundary

- Explicit body, frame and geometric datum IDs, plus equatorial/polar radii in
  metres. Identity is supplied by the caller, never guessed from matching axes.
  These are geometric reference labels, not EPSG datum/epoch transformations.
- Double body-fixed Cartesian metres: X at longitude zero, Y at 90 degrees east,
  Z north. Latitude is geodetic, longitude east-positive in [-180, 180], angles
  in degrees, height along the ellipsoid normal in metres.
- The admitted near-surface band is +/-min(10 km, 2% polar radius) for oblate figures;
  spheres use 10%. The centre,
  nonfinite inputs, unsupported figures and points outside that band refuse
  explicitly. Failure leaves outputs unchanged.
- Areoid heights, geoid/orthometric heights, radial distances and normalized
  guide values are not interchangeable with ellipsoid height. Unsupported
  height kinds refuse; no implicit zero correction or spherical fallback exists.
- One independent native context per owner thread. Queries on another thread
  refuse. Native contexts and operations retire through RAII. There is no
  singleton, world lookup, demand manager, network delivery or grid operation.
- No handedness flip is hidden in a quaternion. This service returns mathematical
  body coordinates. UE engine/world georeferencing and a future measured-data
  ingestion boundary must explicitly account for parity, units and frame identity.
  The existing `FCLBodyFrame` checks here test numeric rigid-frame continuity;
  they do not claim scientific CRS-to-UE orientation integration.

The accepted figure domain is finite spheres/oblate figures with polar radius
at least 1 m, equatorial radius at most 1e9 m and polar/equatorial ratio at least
0.99. Admission is not qualification of every intermediate or extreme figure.
The six measured profiles below own the actual error envelope. Triaxial/prolate
figures and full-body/deep-interior inverse coordinates require another profile.

## Independent qualification

`Tools/generate-ellipsoid-oracle.py` uses Python Decimal at 70 digits and a
quadratic supporting-plane construction, independent of PROJ and runtime code.
Only the final reference values are rounded to double. The six figures are
100 m and 1 km spheres, a 1 km oblate test body, a Mars-sized test ellipsoid,
a 6,371 km sphere and WGS84's geometric figure. The Mars-sized axes are test
inputs (3,396,190 / 3,376,200 m), not a selected scientific Mars datum or areoid.

Fifteen locations include both exact/near poles, both longitude seam sides,
cardinal equatorial axes and middle latitudes. Seven heights include zero,
+/-1 mm, half-band depths/altitudes and points 1 mm inside the declared band.
The proof compares native forward position, inverse height, inverse/forward
normal and independent-point reconstruction, then exercises three numeric local
frames/origin translations. Exact-pole longitude is nonunique, so inverse
longitude is assessed through recovered position and normal instead of an
arbitrary pole angle. Budgets are 1 mm for position/height/frame and 1e-9 for
normal-vector difference. Counts: 630 reference points and 1,890 frame checks.

The same host checker runs in editor automation and a cooked native GameMode.
It creates no terrain mesh or replacement renderer. The cooked numerical run
uses NullRHI; native rendered terrain is protected by separate existing gameplay
regressions. Results and exact limits are recorded in
[`conformance/body-coordinates-mac-arm64.json`](../conformance/body-coordinates-mac-arm64.json).

The final editor and cooked numerical runs agree on these measured maxima:

| Quantity | Maximum error | Budget |
| --- | ---: | ---: |
| Forward body position | 1.475599e-9 m | 0.001 m |
| Inverse ellipsoid height | 9.167172e-6 m | 0.001 m |
| Reconstructed body position | 1.296448e-5 m | 0.001 m |
| Numeric frame continuity | 1.475633e-9 m | 0.001 m |
| Normal vector difference | 6.404647e-10 | 1e-9 |

These are arithmetic errors against analytic references, not the accuracy of a
scientific dataset or a rendered/collidable surface. Inverse admission uses the
native approximate height; points within a numerical budget of a band cutoff
may refuse. The qualified oracle keeps a 1 mm margin inside that cutoff.

If UE later exposes an owner-scoped custom-body conversion context with the
needed error/height contract, reuse that backend through this boundary. Body
identity and data semantics remain Landform responsibilities; another solver
or a custom terrain manager is not the removal path.

## Replay

From the host directory, with all package/output paths absolute:

```sh
python3 scripts/stage-plugins.py
# Build LandformLabEditor before a fresh cook.
python3 scripts/package-runtime-recipe.py --scale-lab --archive "$PWD/Saved/Packages/BodyCoordinates"
python3 scripts/qualify-datum.py --archive "$PWD/Saved/Packages/BodyCoordinates" --output "$PWD/Saved/Verification/BodyCoordinates/repeat"
```

The runner requires the initialized LandformLab macOS app container; the
existing interactive recipe launcher initializes it on first use.

Editor filters: `Catalyst.Landform.Coordinates` and
`LandformLab.Coordinates`. The native Earth/custom-body audit is an editor
check; the 630-point geometry oracle executes in both editor and cooked builds.
Regenerate the reference JSON into a temporary path and compare it byte-for-byte
before replacing any oracle.

## Open gates

This does not complete P2 or adopt ellipsoid terrain into the spherical recipe
schema. Measured ingestion still needs a selected scientific CRS/datum, datum
resource identity and uncertainty, any geoid/areoid grid/provider, height-policy
conversion, and a qualified axis/handedness boundary. Projection/collision on a
rendered ellipsoid, triaxial bodies, epoch transforms, rotating frames, gravity
models, fine global noise and resource delivery remain separate work. The next
bounded architectural proof is two-body transfer: native demand/collision and
explicit local gravity, heading and velocity continuity. Real Mars resource
adoption remains gated on its actual datum/provider profile.
