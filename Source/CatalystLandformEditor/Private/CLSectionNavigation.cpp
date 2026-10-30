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
    Pan.X = FMath::Clamp(Pan.X - Pixels.X * .0004, -.08, .08);
    Pan.Y = FMath::Clamp(Pan.Y + Pixels.Y * .0004, -.08, .08);
}
void FCLSectionNavigation::Dolly(double Steps)
{
    if (FMath::IsFinite(Steps)) Zoom = FMath::Clamp(Zoom * FMath::Exp(FMath::Clamp(-Steps * .12, -10.0, 10.0)), 1.0, 2.2);
}
FCLSectionCamera FCLSectionNavigation::Camera(double TanHorizontal, double TanVertical) const
{
    const double S = FCLSectionGeometry::DisplaySize * 100;
    const double Y = FMath::DegreesToRadians(Yaw), E = FMath::DegreesToRadians(Elevation);
    const FVector Back(FMath::Cos(E) * FMath::Cos(Y), FMath::Cos(E) * FMath::Sin(Y), FMath::Sin(E));
    FCLSectionCamera C; C.Rotation = (-Back).Rotation();
    const FRotationMatrix Axes(C.Rotation);
    const FVector Forward = Axes.GetUnitAxis(EAxis::X), Right = Axes.GetUnitAxis(EAxis::Y), Up = Axes.GetUnitAxis(EAxis::Z);
    const double H = FMath::Max(.01, TanHorizontal), V = FMath::Max(.01, TanVertical);
    const double ShiftX = .20 + Pan.X, ShiftY = Pan.Y;
    double Distance = S;
    // Solve each frustum inequality for distance, reserving the right for labels.
    // This covers the entire sky/core box, not only the current surface mesh.
    for (double X : {-.5 * S, .5 * S}) for (double YPos : {-.5 * S, .5 * S}) for (double Z : {-.5 * S, .5 * S})
    {
        const FVector P(X, YPos, Z);
        const double Depth = FVector::DotProduct(P, Forward);
        const double PX = FVector::DotProduct(P, Right) / H, PY = FVector::DotProduct(P, Up) / V;
        Distance = FMath::Max(Distance, -Depth + S * .05);
        Distance = FMath::Max(Distance, (PX - .35 * Depth) / (.35 + ShiftX));
        Distance = FMath::Max(Distance, (PX + .88 * Depth) / (-.88 + ShiftX));
        Distance = FMath::Max(Distance, (PY - .85 * Depth) / (.85 + ShiftY));
        Distance = FMath::Max(Distance, (PY + .85 * Depth) / (-.85 + ShiftY));
    }
    Distance *= 1.04 * Zoom;
    C.Target = FVector(0, 0, S * .5) + Right * (ShiftX * Distance * H) + Up * (ShiftY * Distance * V);
    C.Eye = C.Target + Back * Distance;
    return C;
}
