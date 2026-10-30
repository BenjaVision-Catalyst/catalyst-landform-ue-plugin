#pragma once
#include "CoreMinimal.h"

struct FCLSectionCamera
{
    FVector Eye, Target;
    FRotator Rotation;
};

/** Object navigation only. Full core-to-sky bounds stay inside a reserved frame. */
struct CATALYSTLANDFORMEDITOR_API FCLSectionNavigation
{
    double Yaw = -51.34, Elevation = 32, Zoom = 1.08;
    FVector2D Pan = FVector2D::ZeroVector;
    void Orbit(FVector2D Pixels);
    void PanBy(FVector2D Pixels);
    void Dolly(double Steps);
    FCLSectionCamera Camera(double TanHorizontal, double TanVertical) const;
};
