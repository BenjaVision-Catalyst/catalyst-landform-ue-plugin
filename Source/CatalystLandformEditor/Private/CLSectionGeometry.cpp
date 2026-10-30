#include "CLSectionGeometry.h"

double FCLSectionGeometry::DefaultWidth(const FCLCompiledPlanetRecipe& Plan)
{
    const double OldHalfWidth = FMath::Min(Plan.RadiusMeters * .42, (Plan.RadiusMeters + Plan.MinimumHeightMeters) * .6);
    return FMath::Clamp(FMath::RoundToDouble(OldHalfWidth * 4 / 10) * 10, 100.0, 1000.0);
}

double FCLSectionGeometry::ReferenceSurfaceRadius(const FCLCompiledPlanetRecipe& Plan)
{ return FMath::Max(.001, Plan.RadiusMeters + Plan.SampleHeightMeters(FVector::ZAxisVector)); }

FVector FCLSectionGeometry::SourceSurfacePoint(const FCLCompiledPlanetRecipe& Plan, double X, double Y, double Heading)
{
    // Exponential map at the north pole: distance along either centre axis is
    // exactly the requested number of metres on the reference surface sphere.
    const double Distance = FMath::Sqrt(X * X + Y * Y);
    const double Angle = Distance / ReferenceSurfaceRadius(Plan);
    const double S = Distance > .000001 ? FMath::Sin(Angle) / Distance : 0;
    const FVector Local(X * S, Y * S, FMath::Cos(Angle));
    const FVector Direction = FQuat(FVector::ZAxisVector, Heading).RotateVector(Local);
    return Direction * (Plan.RadiusMeters + Plan.SampleHeightMeters(Direction)) * 100;
}

FVector FCLSectionGeometry::StratumPoint(const FCLCompiledPlanetRecipe& Plan, const FVector2D& Edge, double Normalized)
{
    const double Reference = ReferenceSurfaceRadius(Plan);
    const double Sag = Reference * (1 - FMath::Cos(Edge.Size() / Reference)) / 3;
    const double Height = FMath::Max(0.0, Plan.RadiusMeters * Normalized);
    // Apply the same additive sag to terrain and guides, preserving radial relief.
    // Deep references continue schematically to the centre plane, rather than
    // disappearing where their small concentric spheres cannot reach the edge.
    const double Blend = Sag > .000001 ? FMath::Min(1.0, Height / (2 * Sag)) : 1;
    return FVector(Edge.X, Edge.Y, Height - Sag * Blend) * 100;
}

double FCLSectionGeometry::StratumAtScreenY(const FCLCompiledPlanetRecipe& Plan, const FVector2D& Edge, const FMatrix& Projection, double Y)
{
    const double NdcY = 1 - 2 * Y;
    const FVector4 Base = Projection.TransformFVector4(FVector4(Edge.X * 100, Edge.Y * 100, 0, 1));
    const FVector4 Axis = Projection.TransformFVector4(FVector4(0, 0, 1, 0));
    const double Denominator = NdcY * Axis.W - Axis.Y;
    if (FMath::Abs(Denominator) < 1.e-9) return 0;
    const double TargetZ = (Base.Y - NdcY * Base.W) / Denominator;
    double Low = 0, High = 1;
    for (int32 I = 0; I < 48; ++I)
    {
        const double Mid = (Low + High) * .5;
        if (StratumPoint(Plan, Edge, Mid).Z < TargetZ) Low = Mid; else High = Mid;
    }
    return (Low + High) * .5;
}

FVector FCLSectionGeometry::SurfacePoint(const FCLCompiledPlanetRecipe& Plan, double X, double Y, double Heading)
{
    const FVector Source = SourceSurfacePoint(Plan, X, Y, Heading);
    return StratumPoint(Plan, FVector2D(X, Y), Source.Size() / (100 * Plan.RadiusMeters));
}

FCLSectionGeometry FCLSectionGeometry::Build(const FCLCompiledPlanetRecipe& Plan, double Heading, double WidthMeters)
{
    FCLSectionGeometry G;
    G.HalfWidthMeters = FMath::Clamp(WidthMeters > 0 ? WidthMeters : DefaultWidth(Plan), 100.0, 1000.0) * .5;
    // Keep spacing at most 5m as the viewed area grows.
    const int32 N = FMath::Clamp(FMath::CeilToInt(G.HalfWidthMeters * 2 / 5), 48, 200);
    auto Add = [&](const FVector& P, FLinearColor C) { G.Vertices.Add(P); G.Colors.Add(C); };
    // Arguments use mathematical outward CCW; UE renders the reverse winding.
    auto Triangle = [&](int32 A, int32 B, int32 C) { G.Triangles.Append({A, C, B}); };
    for (int32 Y = 0; Y <= N; ++Y)
        for (int32 X = 0; X <= N; ++X)
        {
            const FVector2D XY(G.HalfWidthMeters * (2.0 * X / N - 1), G.HalfWidthMeters * (2.0 * Y / N - 1));
            const FVector Source = SourceSurfacePoint(Plan, XY.X, XY.Y, Heading);
            G.SourceSurfacePoints.Add(Source);
            const FVector P = StratumPoint(Plan, XY, Source.Size() / (100 * Plan.RadiusMeters));
            const double H = Source.Size() / 100 - Plan.RadiusMeters;
            const double Shade = FMath::Clamp((H - Plan.MinimumHeightMeters) /
                FMath::Max(.001, Plan.MaximumHeightMeters - Plan.MinimumHeightMeters), 0.0, 1.0);
            Add(P, FLinearColor::LerpUsingHSV(FLinearColor(.19, .24, .11), FLinearColor(.63, .59, .36), Shade));
            if (X < N && Y < N)
            {
                const int32 A = Y * (N + 1) + X;
                Triangle(A, A + 1, A + N + 2); Triangle(A, A + N + 2, A + N + 1);
            }
        }
    G.SurfaceVertexCount = G.Vertices.Num();
    TArray<int32> Rim;
    for (int32 X = 0; X <= N; ++X) Rim.Add(X);
    for (int32 Y = 1; Y <= N; ++Y) Rim.Add(Y * (N + 1) + N);
    for (int32 X = N - 1; X >= 0; --X) Rim.Add(N * (N + 1) + X);
    for (int32 Y = N - 1; Y > 0; --Y) Rim.Add(Y * (N + 1));
    // Separate face vertices preserve crisp vertical section edges. A depth tint
    // supplies readable mass without inventing geological strata or core materials.
    const FLinearColor Top(.38, .28, .17), Bottom(.12, .085, .055);
    const int32 BottomCentre = G.Vertices.Num(); Add(FVector::ZeroVector, Bottom);
    for (int32 I = 0; I < Rim.Num(); ++I)
    {
        const FVector A = G.Vertices[Rim[I]], B = G.Vertices[Rim[(I + 1) % Rim.Num()]];
        const int32 K = G.Vertices.Num();
        Add(A, Top); Add(B, Top); Add(FVector(A.X, A.Y, 0), Bottom); Add(FVector(B.X, B.Y, 0), Bottom);
        Triangle(K, K + 2, K + 1); Triangle(K + 1, K + 2, K + 3);
        Triangle(BottomCentre, K + 3, K + 2);
    }
    return G;
}
