#include "CLSectionNavigation.h"
#include "CLSectionGeometry.h"

FVector FCLSectionNavigation::Centre()
{ return FVector(0, 0, FCLSectionGeometry::DisplaySize * 50); }
FCLSectionNavigation::FCLSectionNavigation() : Pivot(Centre()) {}

void FCLSectionNavigation::Orbit(FVector2D Pixels)
{
    if (Pixels.ContainsNaN()) return;
    Yaw = FMath::UnwindDegrees(Yaw - Pixels.X * .3);
    Elevation = FMath::Clamp(Elevation + Pixels.Y * .3, 15.0, 75.0);
    // Pivot is deliberately unchanged, including after a zoomed-in pan.
}
void FCLSectionNavigation::PanBy(FVector2D Pixels)
{
    if (Pixels.ContainsNaN()) return;
    const double X = bCanPanX ? FMath::Clamp(-Pixels.X * PanScale.X, PanMin.X, PanMax.X) : 0;
    const double Y = bCanPanY ? FMath::Clamp(Pixels.Y * PanScale.Y, PanMin.Y, PanMax.Y) : 0;
    Pivot += PanRight * X + PanUp * Y;
    const double S = FCLSectionGeometry::DisplaySize * 100;
    Pivot.X = FMath::Clamp(Pivot.X, -.5 * S, .5 * S);
    Pivot.Y = FMath::Clamp(Pivot.Y, -.5 * S, .5 * S);
    Pivot.Z = FMath::Clamp(Pivot.Z, 0.0, S);
}
void FCLSectionNavigation::Dolly(double Steps)
{
    if (!FMath::IsFinite(Steps)) return;
    const double Previous = Zoom;
    Zoom = FMath::Clamp(Zoom * FMath::Exp(FMath::Clamp(-Steps * .12, -10.0, 10.0)), .3, 1.0);
    if (Zoom >= 1) { Pivot = Centre(); bCanPanX = bCanPanY = false; }
    else if (Zoom > Previous && Previous < 1)
        Pivot = Centre() + (Pivot - Centre()) * ((1 - Zoom) / (1 - Previous));
}
FCLSectionCamera FCLSectionNavigation::Camera(double TanHorizontal, double TanVertical)
{
    const double S = FCLSectionGeometry::DisplaySize * 100;
    if (Zoom >= 1) Pivot = Centre();
    const double Y = FMath::DegreesToRadians(Yaw), E = FMath::DegreesToRadians(Elevation);
    const FVector Back(FMath::Cos(E) * FMath::Cos(Y), FMath::Cos(E) * FMath::Sin(Y), FMath::Sin(E));
    FCLSectionCamera C; C.Rotation = (-Back).Rotation();
    const FRotationMatrix Axes(C.Rotation);
    const FVector Forward = Axes.GetUnitAxis(EAxis::X), Right = Axes.GetUnitAxis(EAxis::Y), Up = Axes.GetUnitAxis(EAxis::Z);
    const double H = FMath::Max(.01, TanHorizontal), V = FMath::Max(.01, TanVertical);
    // Fit about the real centre axis at the default pose. Orbit never refits.
    const FRotationMatrix DefaultAxes(FRotator(-DefaultElevation, DefaultYaw + 180, 0));
    const double NearDistance = S * (FMath::Sqrt(3.0) * .5 + .05);
    double FitDistance = NearDistance;
    for (double X : {-.5 * S, .5 * S}) for (double YPos : {-.5 * S, .5 * S}) for (double Z : {-.5 * S, .5 * S})
    {
        const FVector P(X, YPos, Z);
        const double Depth = FVector::DotProduct(P, DefaultAxes.GetUnitAxis(EAxis::X));
        FitDistance = FMath::Max(FitDistance, FMath::Abs(FVector::DotProduct(P, DefaultAxes.GetUnitAxis(EAxis::Y))) / (FitFrame * H) - Depth);
        FitDistance = FMath::Max(FitDistance, FMath::Abs(FVector::DotProduct(P, DefaultAxes.GetUnitAxis(EAxis::Z))) / (FitFrame * V) - Depth);
    }
    const double Distance = FMath::Max(FitDistance * Zoom, NearDistance);
    C.Target = Pivot; C.Eye = Pivot + Back * Distance;

    // Determine crop limits from the current view without moving its pivot.
    // Close inspection may clip parts of the box behind the camera's near plane.
    FVector Corners[8]; int32 Count = 0;
    for (double X : {-.5 * S, .5 * S}) for (double YPos : {-.5 * S, .5 * S}) for (double Z : {0.0, S})
    {
        const FVector P = FVector(X, YPos, Z) - C.Eye;
        Corners[Count++] = FVector(FVector::DotProduct(P, Right), FVector::DotProduct(P, Up), FVector::DotProduct(P, Forward));
    }
    constexpr double ClipDepth = 1;
    TArray<FVector, TInlineAllocator<20>> Visible;
    for (const FVector& P : Corners) if (P.Z >= ClipDepth) Visible.Add(P);
    for (int32 I = 0; I < 8; ++I) for (int32 Bit : {1, 2, 4})
    {
        const int32 J = I ^ Bit; if (J <= I) continue;
        const FVector A = Corners[I], B = Corners[J];
        if ((A.Z < ClipDepth) != (B.Z < ClipDepth))
            Visible.Add(FMath::Lerp(A, B, (ClipDepth - A.Z) / (B.Z - A.Z)));
    }
    auto PanRange = [&](int32 Axis, double Tangent, double& Min, double& Max, bool& Enabled)
    {
        double Start = TNumericLimits<double>::Max(), End = -Start;
        for (const FVector& P : Visible)
        {
            Start = FMath::Min(Start, P[Axis] + P.Z * Tangent);
            End = FMath::Max(End, P[Axis] - P.Z * Tangent);
        }
        Min = Max = 0;
        if (Zoom < 1 && !Visible.IsEmpty())
        {
            if (Start <= End) { Min = FMath::Min(0.0, Start); Max = FMath::Max(0.0, End); }
            else if (End > 0) Max = End;
            else if (Start < 0) Min = Start;
        }
        Enabled = Min < -.001 || Max > .001;
    };
    PanRange(0, H, PanMin.X, PanMax.X, bCanPanX);
    PanRange(1, V, PanMin.Y, PanMax.Y, bCanPanY);
    PanRight = Right; PanUp = Up; PanScale = FVector2D(Distance * H, Distance * V) * .002;
    return C;
}
