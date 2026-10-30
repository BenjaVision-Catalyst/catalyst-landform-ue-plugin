#include "CLSectionGeometry.h"

double FCLSectionGeometry::HalfWidth(const FCLCompiledPlanetRecipe& Plan)
{ return FMath::Min(Plan.RadiusMeters * .42, (Plan.RadiusMeters + Plan.MinimumHeightMeters) * .6); }

FVector FCLSectionGeometry::DisplaySurfacePoint(const FVector& P)
{
    // The lab's tiny walkable sphere exaggerates curvature for a Designer section.
    // Keep its sampled relief and horizontal footprint; flatten only radial sag.
    return FVector(P.X, P.Y, P.Size() + (P.Z - P.Size()) / 3);
}

FVector FCLSectionGeometry::StratumPoint(const FCLCompiledPlanetRecipe& Plan, const FVector2D& Edge, double Normalized)
{
    const double R = Plan.RadiusMeters * Normalized;
    const double Transition = Edge.Size() * 1.05;
    if (Transition > 0 && R < Transition)
    {
        const FVector P = DisplaySurfacePoint(FVector(Edge.X, Edge.Y, FMath::Sqrt(Transition * Transition - Edge.SizeSquared())) * 100);
        return FVector(Edge.X * 100, Edge.Y * 100, P.Z * FMath::Max(0.0, R) / Transition);
    }
    return DisplaySurfacePoint(FVector(Edge.X, Edge.Y, FMath::Sqrt(FMath::Max(0.0, R * R - Edge.SizeSquared()))) * 100);
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
    // Intersect a vertical section ray with the same radial field VPP uses.
    // Cartesian cut planes remain straight even though the terrain is spherical.
    double Low = 0, High = Plan.RadiusMeters + Plan.MaximumHeightMeters;
    const FQuat Rotation(FVector::ZAxisVector, Heading);
    for (int32 I = 0; I < 32; ++I)
    {
        const double Z = (Low + High) * .5;
        const FVector P(X, Y, Z);
        const double Field = P.Size() - Plan.RadiusMeters - Plan.SampleHeightMeters(Rotation.RotateVector(P));
        if (Field > 0) High = Z; else Low = Z;
    }
    return FVector(X, Y, (Low + High) * .5) * 100;
}

FCLSectionGeometry FCLSectionGeometry::Build(const FCLCompiledPlanetRecipe& Plan, double Heading)
{
    FCLSectionGeometry G;
    G.HalfWidthMeters = HalfWidth(Plan);
    const int32 N = 48;
    auto Add = [&](const FVector& P, FLinearColor C) { G.Vertices.Add(P); G.Colors.Add(C); };
    // Arguments use mathematical outward CCW; UE renders the reverse winding.
    auto Triangle = [&](int32 A, int32 B, int32 C) { G.Triangles.Append({A, C, B}); };
    for (int32 Y = 0; Y <= N; ++Y)
        for (int32 X = 0; X <= N; ++X)
        {
            const FVector P = SurfacePoint(Plan, G.HalfWidthMeters * (2.0 * X / N - 1), G.HalfWidthMeters * (2.0 * Y / N - 1), Heading);
            const double H = P.Size() / 100 - Plan.RadiusMeters;
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
