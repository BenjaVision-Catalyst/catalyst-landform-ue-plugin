# Projected interaction tracing

`FCLProjectionRay` admits the inverse curve of one finite presented ray. `FCLProjectionTrace` queries canonical Unreal collision along conservative envelopes of that curve. It returns an unmodified native `FHitResult`, plus the separately projected hit point, normal, screen-ray distance and ray error. The compiled field, colliders, simulation and terrain rendering are unchanged. The host integration and packaged evidence live in [LandformLab](../../Projects/LandformLab/Docs/ProjectedInteractions.md).

## API and failure contract

Call `FCLProjectionTrace::Trace` synchronously on the game thread with one immutable projection, a finite presented origin/direction, a collision channel, query params and settings. The caller owns collider residency and its collision filtering. Initial blocking overlaps and full narrow phase are mandatory; disabling those query flags is refused before any world query.

Defaults are 20 m range, 0.5 cm presented hit tolerance, at most 64 world queries and 12 subdivision levels. The host uses 19 m within its existing 25 m local collision invoker. One fixed stack holds the pending intervals; no application heap buffer is created per query. World/physics query internals retain engine ownership and cost depends on scene complexity.

Statuses distinguish `Hit`, `Miss`, `InvalidRequest`, `OutsideChart`, `BudgetExceeded` and `UnresolvedContact`. Only exhausted, clear swept envelopes produce `Miss`. An unresolved earlier interval cannot be skipped to return a later hit. A hit is a tolerance-based pick, not an assertion of an exact zero-width ray intersection or exact ordering of sub-tolerance coincident surfaces. It must project within the requested ray distance and lateral tolerance. Starting-penetration contacts cannot be accepted as ordinary hits. Normals use the existing inverse-transpose projector.

`CanonicalHit.Distance` and its `TraceStart`/`TraceEnd` describe the native canonical chord/sweep. Do not replace them with presented values or interpret them as the full curved-path distance. `PresentedDistanceCm` is measured along the finite presented ray. Target/component identities remain canonical. No actor or collision transform is written by the trace.

## Whole-ray admission

Let the presented ray be `p(t)=p0+t*d`, with unit `d`. Write its offset from the anchor as tangent vector `T(t)` and normal coordinate `z(t)`. For physical radius `R` and curvature `c`, let `A=R+c*z`. Endpoints bound affine `z` and convex tangent norm: `zMin`, `zMax`, `X=max(|T0|,|T1|)`, `Amin=R+c*zMin>0`.

Across the entire ray:

- `theta <= atan(c*X/Amin)/c`, using the analytic flat limit and a stable atan-over-argument evaluation.
- `h >= zMin` and `h <= zMax+c*X^2/(2*Amin)`.

These follow from `h=z+c*|T|^2/(sqrt(A^2+c^2*|T|^2)+A)`. The complete ray must fit the chart/altitude band. Admission is conservative and may refuse some otherwise valid rays; it never silently truncates a ray at a chart boundary. Invalid construction/point queries preserve their outputs.

## Conservative inverse-curve bound

For `c>0`, the canonical curve is `(R+h)*Exp_n(a)` about the physical centre, where `y=c*T/A` and `a=atan(|y|)*y/(c*|y|)`. At flat curvature, `a=T/R` and `h=z`. Use the following whole-ray bounds:

```
Y = c*X/Amin
s = sqrt(1+Y^2)
b = 2*Y + 8*Y^3/5
rhoMax = R+hUpper

|a'| <= s/Amin
|a''| <= c*(b*s^2+2*s)/Amin^2
|h'| <= 1
|h''| <= c/Amin

M = (c+2*s)/Amin
  + rhoMax*((1+thetaMax)*s^2+c*(b*s^2+2*s))/Amin^2
```

The atan-vector derivative has norm at most 1. Its second derivative bound `b` follows by differentiating the integral of `y/(1+u^2*|y|^2)` over `u` from 0 to 1. The sphere exponential has derivative norm at most 1 and second derivative norm at most `1+thetaMax` in this chart. Differentiating the canonical curve gives `|q''|<=M`. A curve interval of presented length `L` is therefore at most `M*L^2/8` from its endpoint chord, using the Green-function interpolation bound. Exact natural curvature uses the identity curve and zero mathematical deviation.

The trace sweeps a sphere of that canonical deviation plus `0.01 cm + R*1e-10` numerical slack along the chord. A clear sweep covers the actual curve; a candidate interval splits left-first until its envelope is small enough or the hard budgets are reached. The forward stretch estimate guides refinement; the final projected hit error is checked directly. Natural curvature uses one native line trace. These are collision-query guarantees relative to the engine's loaded geometry/filtering and numerical behavior, not proof of unloaded terrain or arbitrary world-scale physics precision.

## Verification

`InverseRayBounds` checks 24,948 dense samples across three radii, two anchor frames and six curvature values including flat, near-flat and natural. Sampling supplements the derivative proof; it does not establish the bound by itself. `CurvedCollisionTrace` uses an isolated real physics world with tiny boxes: endpoint-only tracing misses the curved target, refinement hits the foreground target, a nearby off-ray collider is rejected, clear rays miss and unsafe flags/budgets report explicit refusal. Natural identity uses one query. World/context cleanup is explicit.

The host separately verifies actual viewport deprojection against marker/native-floor identity, reprojects each hit within 2 pixels, compares normals against finite differences of projected tangent points, checks native source identities and measures packaged query/frame cost. Motion, animation, chart handover, long-range weapons and repeated charts remain separate integrations.
