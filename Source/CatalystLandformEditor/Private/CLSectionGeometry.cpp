#include "CLSectionGeometry.h"

double FCLSectionGeometry::DefaultWidth(const FCLCompiledPlanetRecipe&)
{ return 500; }

double FCLSectionGeometry::ReferenceSurfaceRadius(const FCLCompiledPlanetRecipe& Plan)
{ return FMath::Max(.001, Plan.RadiusMeters + (Plan.Coordinates!=ECLTerrainCoordinates::Sphere ? Plan.BaseHeightMeters : Plan.SampleHeightMeters(FVector::ZAxisVector))); }

namespace
{
FVector SectionDirection(double X, double Y, double ReferenceRadius, const FQuat& Rotation)
{
    // Exponential map: centre-axis distances are metres on the reference sphere.
    const double Distance = FMath::Sqrt(X * X + Y * Y), Angle = Distance / ReferenceRadius;
    const double S = Distance > .000001 ? FMath::Sin(Angle) / Distance : 0;
    return Rotation.RotateVector(FVector(X * S, Y * S, FMath::Cos(Angle)));
}
}

FVector FCLSectionGeometry::SourceSurfacePoint(const FCLCompiledPlanetRecipe& Plan, double X, double Y, double Heading)
{
    if(Plan.Coordinates==ECLTerrainCoordinates::LocalPatch)
    {
        const FVector P(X,Y,0);TArray<double> Heights;Plan.SampleLocalHeightsMeters(MakeArrayView(&P,1),Heights);
        return Plan.TerrainDirection(FVector2D(X,Y))*(Plan.RadiusMeters+Heights[0])*100;
    }
    const FVector Direction = SectionDirection(X, Y, ReferenceSurfaceRadius(Plan), FQuat(FVector::ZAxisVector, Heading));
    return Direction * (Plan.RadiusMeters + Plan.SampleHeightMeters(Direction)) * 100;
}

FVector FCLSectionGeometry::RadialGuidePoint(const FVector2D& Edge, double Normalized)
{
    // Unwrap radial height onto a flat reference plane. Terrain relief remains
    // in the sampled height; no planetary sag is added to the Designer display.
    return FVector(Edge.X, Edge.Y, FMath::Max(0.0, DisplaySize * Normalized)) * 100;
}

double FCLSectionGeometry::RadialGuideAtScreenY(const FVector2D& Edge, const FMatrix& Projection, double Y)
{
    const double NdcY = 1 - 2 * Y;
    const FVector4 Base = Projection.TransformFVector4(FVector4(Edge.X * 100, Edge.Y * 100, 0, 1));
    const FVector4 Axis = Projection.TransformFVector4(FVector4(0, 0, 1, 0));
    const double Denominator = NdcY * Axis.W - Axis.Y;
    if (FMath::Abs(Denominator) < 1.e-9) return 0;
    const double TargetZ = (Base.Y - NdcY * Base.W) / Denominator;
    return FMath::Clamp(TargetZ / (DisplaySize * 100), 0.0, 1.0);
}

FVector FCLSectionGeometry::SurfacePoint(const FCLCompiledPlanetRecipe& Plan, double X, double Y, double Heading, double WidthMeters)
{
    const FVector Source = SourceSurfacePoint(Plan, X, Y, Heading);
    const double Width = FMath::Clamp(WidthMeters > 0 ? WidthMeters : DefaultWidth(Plan), 100.0, 1000.0);
    return RadialGuidePoint(FVector2D(X, Y) * (DisplaySize / Width), Source.Size() / (100 * Plan.RadiusMeters));
}

FCLSectionGeometry FCLSectionGeometry::Build(const FCLCompiledPlanetRecipe& Plan, double Heading, double WidthMeters)
{
    FCLSectionGeometry G;
    G.HalfWidthMeters = FMath::Clamp(WidthMeters > 0 ? WidthMeters : DefaultWidth(Plan), 100.0, 1000.0) * .5;
    // Detailed stamp gullies need denser sampling than the base-noise overview.
    // Retain the fixed presentation frame and cap work at 513x513 top vertices.
    const bool bStamps = Plan.GetDistributedStampCount() > 0;
    const double Spacing = bStamps ? 1.0 : 5.0;
    const int32 N = FMath::Clamp(FMath::CeilToInt(G.HalfWidthMeters * 2 / Spacing), 48, bStamps ? 512 : 200);
    auto Add = [&](const FVector& P, FLinearColor C) { G.Vertices.Add(P); G.Colors.Add(C); };
    // Arguments use mathematical outward CCW; UE renders the reverse winding.
    auto Triangle = [&](int32 A, int32 B, int32 C) { G.Triangles.Append({A, C, B}); };
    TArray<FVector> Directions;
    Directions.Reserve((N + 1) * (N + 1));
    const double ReferenceRadius = ReferenceSurfaceRadius(Plan);
    const FQuat Rotation(FVector::ZAxisVector, Heading);
    for (int32 Y = 0; Y <= N; ++Y)
        for (int32 X = 0; X <= N; ++X)
        {
            const double PX=G.HalfWidthMeters*(2.0*X/N-1),PY=G.HalfWidthMeters*(2.0*Y/N-1);
            Directions.Add(Plan.Coordinates==ECLTerrainCoordinates::LocalPatch ? FVector(PX,PY,0) : SectionDirection(PX,PY,ReferenceRadius,Rotation));
        }
    TArray<double> Heights;
    if(Plan.Coordinates==ECLTerrainCoordinates::LocalPatch) Plan.SampleLocalHeightsMeters(Directions,Heights);
    else Plan.SampleHeightsMeters(Directions, Heights);
    for (int32 Y = 0; Y <= N; ++Y)
        for (int32 X = 0; X <= N; ++X)
        {
            const FVector2D XY(G.HalfWidthMeters * (2.0 * X / N - 1), G.HalfWidthMeters * (2.0 * Y / N - 1));
            const int32 Index = Y * (N + 1) + X;
            const FVector Source = (Plan.Coordinates==ECLTerrainCoordinates::LocalPatch ? Plan.TerrainDirection(XY) : Directions[Index]) * (Plan.RadiusMeters + Heights[Index]) * 100;
            G.SourceSurfacePoints.Add(Source);
            const FVector2D DisplayXY = XY * (DisplaySize / (G.HalfWidthMeters * 2));
            const FVector P = RadialGuidePoint(DisplayXY, Source.Size() / (100 * Plan.RadiusMeters));
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
    // supplies readable mass without inventing geological radialGuides or core materials.
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
