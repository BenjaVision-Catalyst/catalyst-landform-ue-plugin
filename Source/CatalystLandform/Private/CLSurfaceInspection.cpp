#include "CLSurfaceInspection.h"
#include "Model/CLPlanetRecipe.h"
#include "Surface/CLSurfaceCompiler.h"
bool FCLSurfaceInspection::FindLanding(const FCLCompiledPlanetRecipe& Plan,const FVector& Requested,FCLSurfaceLanding& Out)
{
    Out={};if(Requested.ContainsNaN()||Requested.IsNearlyZero()||Plan.Coordinates==ECLTerrainCoordinates::LocalPatch)return false;
    const FVector D=Requested.GetSafeNormal();const double Radius=Plan.RadiusMeters+Plan.SampleHeightMeters(D);if(Radius<=1)return false;
    const FVector X=FVector::CrossProduct(D,FMath::Abs(D.Z)<.9?FVector::ZAxisVector:FVector::XAxisVector).GetSafeNormal(),Y=FVector::CrossProduct(D,X);
    TArray<FVector> Samples;TArray<double> Distances;
    for(double Ring:{0.,2.,5.,10.,20.})for(int32 I=0;I<(Ring==0?1:16);++I)
    {
        const double Angle=2*UE_DOUBLE_PI*I/16;const FVector V=(D*FMath::Cos(Ring/Radius)+(X*FMath::Cos(Angle)+Y*FMath::Sin(Angle))*FMath::Sin(Ring/Radius)).GetSafeNormal();
        CLSurfaceProbe(V,Samples);Distances.Add(Ring);
    }
    TArray<double> H;Plan.SampleHeightsMeters(Samples,H);
    for(int32 I=0;I<Distances.Num();++I)
    {
        const int32 K=I*5;const FVector N=CLSurfaceNormal(Samples,H,Plan.RadiusMeters,K);
        if(FVector::DotProduct(N,Samples[K])<FMath::Cos(FMath::DegreesToRadians(38.)))continue;
        Out.Direction=Samples[K];Out.PositionCm=Samples[K]*(Plan.RadiusMeters+H[K])*100;Out.Normal=N;Out.AdjustmentMeters=Distances[I];return true;
    }
    return false;
}
