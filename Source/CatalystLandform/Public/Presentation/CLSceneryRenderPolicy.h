#pragma once
#include "CoreMinimal.h"
class FCLPlanetProjection;

/** Physical-size budgets, independent of biome names, tags and asset libraries. */
struct CATALYSTLANDFORM_API FCLSceneryRenderPolicy
{
    int32 FadeStartCm=0,DrawEndCm=0,WpoEndCm=15000,ShadowEndCm=18000;
    bool bDistantCanopy=true;
    static FCLSceneryRenderPolicy ForSize(double MaximumDimensionCm);
    /** Lipschitz enclosure of the complete source box, including curved interiors.
     * Projecting only the eight corners does not bound a nonlinear chart. */
    static bool ProjectBounds(const FBox& Source,const FCLPlanetProjection& Projection,double Stretch,FBox& Out);
    /** Scale required when stock static-instance bounds must keep their canonical centre. */
    static double CenteredBoundsScale(const FBox& Source,const FBox& Presented);
};
