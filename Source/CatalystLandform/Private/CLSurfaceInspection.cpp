#include "CLSurfaceInspection.h"
#include "Model/CLPlanetRecipe.h"
#include "Surface/CLSurfaceCompiler.h"
#include "Scene/CLScene.h"
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

bool FCLSurfaceInspection::FindLandingPoint(const FCLScene& Scene,const FVector& Requested,FCLSurfaceLanding& Out)
{
    Out={};if(Requested.ContainsNaN()||Requested.Length()<1||Requested.Length()>Scene.OuterRadiusMeters())return false;
    const FVector Up=Requested.GetSafeNormal();FVector X,Y;Up.FindBestAxisVectors(X,Y);
    auto Value=[&](const FVector& P,double& F)
    {TArray<FCLSceneShapeSample> S;FString Error;if(!Scene.SampleShape({MakeArrayView(&P,1)},S,Error)||S[0].State!=ECLSceneSampleState::Known)return false;F=S[0].FieldMeters;return true;};
    for(double Ring:{0.,1.,2.})for(int32 I=0;I<(Ring==0?1:8);++I)
    {
        const double Angle=I*UE_DOUBLE_PI/4;const FVector Q=Requested+(X*FMath::Cos(Angle)+Y*FMath::Sin(Angle))*Ring;
        double Previous;if(!Value(Q+Up*3,Previous))return false;
        for(int32 K=1;K<=24;++K)
        {
            const double T=3-K*.25;double F;if(!Value(Q+Up*T,F))return false;
            if(Previous>0&&F<=0)
            {
                double Low=T,High=T+.25;
                for(int32 J=0;J<20;++J){const double Mid=(Low+High)*.5;double V;if(!Value(Q+Up*Mid,V))return false;if(V>0)High=Mid;else Low=Mid;}
                const FVector P=Q+Up*((Low+High)*.5);FVector N;
                for(int32 Axis=0;Axis<3;++Axis){FVector Delta=FVector::ZeroVector;Delta[Axis]=.02;double A,B;if(!Value(P+Delta,A)||!Value(P-Delta,B))return false;N[Axis]=A-B;}
                N.Normalize();double Head;if(!Value(P+Up*2.,Head))return false;
                if(FVector::DotProduct(N,Up)<FMath::Cos(FMath::DegreesToRadians(38.))||Head<=0)break;
                Out.PositionCm=P*100;Out.Direction=P.GetSafeNormal();Out.Normal=N;Out.AdjustmentMeters=Ring;return true;
            }
            Previous=F;
        }
    }
    return false;
}
