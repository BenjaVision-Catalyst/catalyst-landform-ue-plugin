#pragma once

#include "CoreMinimal.h"

/** A bounded presentation chart. All distances are Unreal centimetres in one common frame.
 * ReferenceRadiusCm is a physical reference surface, not the recipe's outer sky radius.
 * Curvature 1 preserves the sphere; 0 maps arc distance onto its anchor tangent plane.
 */
struct CATALYSTLANDFORM_API FCLPlanetProjectionSettings
{
    FVector Center = FVector::ZeroVector;
    FVector AnchorDirection = FVector::ZAxisVector;
    double ReferenceRadiusCm = 10000;
    double Curvature = 1;
    double MaxArcRadians = UE_PI / 3;
    double MinAltitudeCm = -1000;
    double MaxAltitudeCm = 1000;
};

/** Immutable after construction, allocation-free CPU reference for the GPU presentation contract.
 * Invalid settings/queries fail without changing output. This chart deliberately cannot unwrap
 * an entire sphere. It neither changes terrain/collision nor creates repeated charts.
 */
class CATALYSTLANDFORM_API FCLPlanetProjection
{
public:
    static bool TryCreate(const FCLPlanetProjectionSettings& Settings, FCLPlanetProjection& Out);
    bool TryProjectPosition(const FVector& Source, FVector& Out) const;
    bool TryUnprojectPosition(const FVector& Presented, FVector& Out) const;
    bool TryProjectVector(const FVector& Source, const FVector& Vector, FVector& Out) const;
    bool TryProjectNormal(const FVector& Source, const FVector& Normal, FVector& Out) const;

    /** Conservative bounds for this entire chart, useful for contract checks and chart admission.
     * Do not assign this box to every Nanite cluster: renderer integration needs per-cluster bounds.
     */
    bool TryGetChartBounds(FBox& Out) const;
    const FCLPlanetProjectionSettings& GetSettings() const { return Settings; }

private:
    struct FDifferential
    {
        FVector Position, SourceRadial, SourceMeridian, Azimuth, PresentedRadial, PresentedMeridian;
        double MeridianScale, AzimuthScale;
    };
    bool Evaluate(const FVector& Source, FDifferential& Out) const;
    FCLPlanetProjectionSettings Settings;
    bool bValid = false;
};
