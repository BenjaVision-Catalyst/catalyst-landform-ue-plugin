#pragma once
#include "CoreMinimal.h"

struct FCLSectionCamera
{
    FVector Eye, Target;
    FRotator Rotation;
};

/** Object navigation only. Rotation-independent zoom with centred framing and crop-limited panning. */
struct CATALYSTLANDFORMEDITOR_API FCLSectionNavigation
{
    static constexpr double DefaultYaw = -51.34, DefaultElevation = 32;
    static constexpr double FitFrame = .85; // at least 7.5% viewport padding per edge
    double Yaw = DefaultYaw, Elevation = DefaultElevation, Zoom = 1.0;
    FVector2D Pan = FVector2D::ZeroVector; // fraction of the currently clipped range
    bool bCanPanX = false, bCanPanY = false;
    void Orbit(FVector2D Pixels);
    void PanBy(FVector2D Pixels);
    void Dolly(double Steps);
    FCLSectionCamera Camera(double TanHorizontal, double TanVertical);
};
