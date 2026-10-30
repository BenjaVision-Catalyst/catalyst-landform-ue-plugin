#include "CLSectionNavigation.h"
#include "CLSectionGeometry.h"

void FCLSectionNavigation::Orbit(FVector2D Pixels)
{
    if (Pixels.ContainsNaN()) return;
    Yaw = FMath::UnwindDegrees(Yaw - Pixels.X * .3);
    Elevation = FMath::Clamp(Elevation + Pixels.Y * .3, 15.0, 75.0);
}
void FCLSectionNavigation::PanBy(FVector2D Pixels)
{
    if (Pixels.ContainsNaN()) return;
    if (bCanPanX) Pan.X = FMath::Clamp(Pan.X - Pixels.X * .004, -1.0, 1.0);
    if (bCanPanY) Pan.Y = FMath::Clamp(Pan.Y + Pixels.Y * .004, -1.0, 1.0);
}
void FCLSectionNavigation::Dolly(double Steps)
{
    if (!FMath::IsFinite(Steps)) return;
    Zoom = FMath::Clamp(Zoom * FMath::Exp(FMath::Clamp(-Steps * .12, -10.0, 10.0)), .3, 2.2);
    if (Zoom >= 1) { Pan = FVector2D::ZeroVector; bCanPanX = bCanPanY = false; }
}
FCLSectionCamera FCLSectionNavigation::Camera(double TanHorizontal, double TanVertical)
{
    const double S = FCLSectionGeometry::DisplaySize * 100;
    const double Y = FMath::DegreesToRadians(Yaw), E = FMath::DegreesToRadians(Elevation);
    const FVector Back(FMath::Cos(E) * FMath::Cos(Y), FMath::Cos(E) * FMath::Sin(Y), FMath::Sin(E));
    FCLSectionCamera C; C.Rotation = (-Back).Rotation();
    const FRotationMatrix Axes(C.Rotation);
    const FVector Forward = Axes.GetUnitAxis(EAxis::X), Right = Axes.GetUnitAxis(EAxis::Y), Up = Axes.GetUnitAxis(EAxis::Z);
    const double H = FMath::Max(.01, TanHorizontal), V = FMath::Max(.01, TanVertical);
    TArray<FVector, TInlineAllocator<8>> Points;
    double FitDistance = S, NearDistance = 0;
    for (double X : {-.5 * S, .5 * S}) for (double YPos : {-.5 * S, .5 * S}) for (double Z : {-.5 * S, .5 * S})
    {
        const FVector P(X, YPos, Z);
        const FVector Q(FVector::DotProduct(P, Right), FVector::DotProduct(P, Up), FVector::DotProduct(P, Forward));
        Points.Add(Q);
        NearDistance = FMath::Max(NearDistance, -Q.Z + S * .05);
        FitDistance = FMath::Max(FitDistance, FMath::Abs(Q.X) / (.65 * H) - Q.Z);
        FitDistance = FMath::Max(FitDistance, FMath::Abs(Q.Y) / (.85 * V) - Q.Z);
    }
    const double Distance = FMath::Max(FitDistance * 1.04 * Zoom, NearDistance);
    auto Offset = [&](int32 Axis, double Tangent, double& PanValue, bool& CanPan)
    {
        // Centre the projected bounds, including perspective asymmetry.
        double Low = TNumericLimits<double>::Max(), High = -Low;
        for (const FVector& P : Points) { Low = FMath::Min(Low, P[Axis]); High = FMath::Max(High, P[Axis]); }
        for (int32 I = 0; I < 48; ++I)
        {
            const double Mid = (Low + High) * .5;
            double Min = TNumericLimits<double>::Max(), Max = -Min;
            for (const FVector& P : Points)
            {
                const double Ndc = (P[Axis] - Mid) / ((Distance + P.Z) * Tangent);
                Min = FMath::Min(Min, Ndc); Max = FMath::Max(Max, Ndc);
            }
            if (Min + Max > 0) Low = Mid; else High = Mid;
        }
        const double Centre = (Low + High) * .5;
        // The permitted range keeps both clipped edges at or beyond the viewport
        // edge. If the entire axis fits, panning on that axis is disabled.
        double Start = TNumericLimits<double>::Max(), End = -Start;
        for (const FVector& P : Points)
        {
            const double Extent = (Distance + P.Z) * Tangent;
            Start = FMath::Min(Start, P[Axis] + Extent);
            End = FMath::Max(End, P[Axis] - Extent);
        }
        CanPan = Start < Centre - .001 && End > Centre + .001;
        if (!CanPan) { PanValue = 0; return Centre; }
        PanValue = FMath::Clamp(PanValue, -1.0, 1.0);
        return Centre + PanValue * (PanValue < 0 ? Centre - Start : End - Centre);
    };
    const double XOffset = Offset(0, H, Pan.X, bCanPanX), YOffset = Offset(1, V, Pan.Y, bCanPanY);
    C.Target = FVector(0, 0, S * .5) + Right * XOffset + Up * YOffset;
    C.Eye = C.Target + Back * Distance;
    return C;
}
