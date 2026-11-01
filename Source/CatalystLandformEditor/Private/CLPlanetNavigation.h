#pragma once
#include "CoreMinimal.h"
#include "CLSectionNavigation.h"

/** Bounded globe inspection. The outer sky sphere defines a stable frame. */
struct FCLPlanetNavigation
{
    static constexpr double Radius=100000;
    double Yaw=-45,Elevation=22,Zoom=1;
    FVector Pivot=FVector::ZeroVector;
    FVector2D Pan=FVector2D::ZeroVector;
    FVector Right,Up;
    FVector2D PanLimit=FVector2D::ZeroVector,PanScale=FVector2D::ZeroVector;
    void Orbit(FVector2D Pixels);
    void PanBy(FVector2D Pixels);
    void Dolly(double Steps);
    FCLSectionCamera Camera(double TanHorizontal,double TanVertical);
};
