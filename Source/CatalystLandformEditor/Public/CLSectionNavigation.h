#pragma once
#include "CoreMinimal.h"

struct FCLSectionCamera
{
    FVector Eye, Target;
    FRotator Rotation;
};

/** Orbit a persistent point, never an angle-dependent projected silhouette. */
struct CATALYSTLANDFORMEDITOR_API FCLSectionNavigation
{
    static constexpr double DefaultYaw = -51.34, DefaultElevation = 32;
    static constexpr double FitFrame = .85; // at least 7.5% viewport padding per edge
    FCLSectionNavigation();
    static FVector Centre();
    double Yaw = DefaultYaw, Elevation = DefaultElevation, Zoom = 1.0;
    FVector Pivot;
    bool bCanPanX = false, bCanPanY = false;
    void Orbit(FVector2D Pixels);
    void PanBy(FVector2D Pixels);
    void Dolly(double Steps);
    FCLSectionCamera Camera(double TanHorizontal, double TanVertical);
private:
    FVector PanRight = FVector::ZeroVector, PanUp = FVector::ZeroVector;
    FVector2D PanMin = FVector2D::ZeroVector, PanMax = FVector2D::ZeroVector, PanScale = FVector2D::ZeroVector;
};
