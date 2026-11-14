#include "Scene/CLGroundSurface.h"
#include "Scene/CLScene.h"
#include "Scene/CLVolumes.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLCaves.h"
#include "Model/CLSpatialCave.h"

namespace
{
bool RayBox(const FVector& D,const FBox& B,double& Exit)
{
    double Low=0,High=MAX_dbl;
    for(int32 K=0;K<3;++K)
    {
        if(FMath::Abs(D[K])<1.e-12){if(B.Min[K]>0||B.Max[K]<0)return false;continue;}
        const double A=B.Min[K]/D[K],C=B.Max[K]/D[K];Low=FMath::Max(Low,FMath::Min(A,C));High=FMath::Min(High,FMath::Max(A,C));
        if(Low>High)return false;
    }
    Exit=High;return High>0;
}
}
TSharedPtr<const FCLGroundSurfaceSampler> FCLGroundSurfaceSampler::Make(TSharedPtr<const FCLCompiledPlanetRecipe> P,FString& Error)
{
    auto S=MakeShared<FCLGroundSurfaceSampler>();S->Plan=P;S->Scene=FCLScene::FromPlanet(P,Error);if(!S->Scene)return nullptr;
    if(P->Caves)
    {
        if(P->Caves->Spatial)P->Caves->Spatial->GetCellSupportBounds(S->CutBounds);
        else if(P->Caves->Cutters)S->CutBounds.Add(P->Caves->Cutters->Bounds());
    }
    if(P->Volumes)for(const auto& O:P->Volumes->Operations)
    {
        const FBox B(O.Bounds.Min,O.Bounds.Max);
        if(O.Source.Composition==ECLSolidComposition::Union)S->AddBounds.Add(B.ExpandBy(.5));
        else S->CutBounds.Add(B);
    }
    for(auto& B:S->CutBounds)B=B.ExpandBy(.5);
    return S;
}
bool FCLGroundSurfaceSampler::Sample(TConstArrayView<FVector> Input,TArray<FCLGroundSurfaceSample>& Out,FString& Error)const
{
    Out.Reset();Error.Reset();TArray<FVector> D,HeightDirections;D.Reserve(Input.Num());HeightDirections.Reserve(Input.Num()*3);
    for(const auto& V:Input)
    {
        if(V.ContainsNaN()||!FMath::IsFinite(V.SizeSquared())||V.IsNearlyZero()){Error=TEXT("Ground directions must be finite and nonzero");return false;}
        const FVector N=V.GetSafeNormal(),T=FVector::CrossProduct(N,FMath::Abs(N.Z)<.9?FVector::ZAxisVector:FVector::XAxisVector).GetSafeNormal(),B=FVector::CrossProduct(N,T);
        D.Add(N);HeightDirections.Add(N);HeightDirections.Add((N+T*.001).GetSafeNormal());HeightDirections.Add((N+B*.001).GetSafeNormal());
    }
    TArray<double> H;Plan->SampleHeightsMeters(HeightDirections,H);Out.SetNum(Input.Num());
    struct FRay {int32 Index;double Air,Solid=0,Start;int32 SolidRun=0;bool bBracket=false,bDone=false;};TArray<FRay> Rays;
    for(int32 I=0;I<D.Num();++I)
    {
        const int32 K=I*3;const double R=Plan->RadiusMeters+H[K];const FVector P=D[I]*R,A=HeightDirections[K+1]*(Plan->RadiusMeters+H[K+1]),B=HeightDirections[K+2]*(Plan->RadiusMeters+H[K+2]);
        auto& O=Out[I];O.PositionMeters=P;O.Normal=FVector::CrossProduct(A-P,B-P).GetSafeNormal();O.bSupported=R>0;
        bool Affected=CutBounds.ContainsByPredicate([&](const FBox& Bounds){return Bounds.IsInsideOrOn(P);});double Start=R+.25;
        for(const auto& Bounds:AddBounds){double Exit;if(RayBox(D[I],Bounds,Exit)&&Exit>R-.5){Affected=true;Start=FMath::Max(Start,Exit+.25);}}
        if(Affected){O.bSupported=false;Rays.Add({I,Start,0,Start,0});}
    }
    auto Fields=[&](const TArray<FVector>& Points,TArray<FCLSceneShapeSample>& Values)
    {
        if(!Scene->SampleShape({Points},Values,Error))return false;
        for(const auto& V:Values)if(V.State!=ECLSceneSampleState::Known||!FMath::IsFinite(V.FieldMeters)){Error=TEXT("Ground query requires known finite solid fields");return false;}
        return true;
    };
    // March in fixed, resolution-qualified increments. No SDF/Lipschitz claim.
    TArray<FVector> Points;TArray<int32> Indices;TArray<FCLSceneShapeSample> Values;
    for(int32 Step=0;Step<=MaximumProbeSteps;++Step)
    {
        Points.Reset();Indices.Reset();
        for(int32 J=0;J<Rays.Num();++J)if(!Rays[J].bDone)
        {
            const double R=Rays[J].Start-Step*ProbeStepMeters;
            if(R<=0){Rays[J].bDone=true;continue;}
            Points.Add(D[Rays[J].Index]*R);Indices.Add(J);
        }
        if(Points.IsEmpty())break;if(!Fields(Points,Values))return false;
        for(int32 K=0;K<Indices.Num();++K)
        {
            auto& Ray=Rays[Indices[K]];const double R=Ray.Start-Step*ProbeStepMeters;
            if(Values[K].FieldMeters>0){Ray.Air=R;Ray.SolidRun=0;}
            else if(Step==0)Ray.bDone=true;
            else
            {
                if(Ray.SolidRun++==0)Ray.Solid=R;
                // A sub-voxel sliver is not a stable root support. Keep marching
                // through air after a thin roof, rather than anchoring above the
                // cave floor represented by the native 0.5 m detail budget.
                if(Ray.Solid-R>=MinimumSupportDepthMeters-1.e-6){Ray.bDone=true;Ray.bBracket=true;}
            }
        }
    }
    for(int32 Iteration=0;Iteration<10;++Iteration)
    {
        Points.Reset();Indices.Reset();
        for(int32 J=0;J<Rays.Num();++J)if(Rays[J].bBracket){Points.Add(D[Rays[J].Index]*((Rays[J].Air+Rays[J].Solid)*.5));Indices.Add(J);}
        if(Points.IsEmpty())break;if(!Fields(Points,Values))return false;
        for(int32 K=0;K<Indices.Num();++K){auto& R=Rays[Indices[K]];const double Mid=(R.Air+R.Solid)*.5;if(Values[K].FieldMeters>0)R.Air=Mid;else R.Solid=Mid;}
    }
    Points.Reset();Indices.Reset();constexpr double E=.05;
    for(int32 J=0;J<Rays.Num();++J)if(Rays[J].bBracket)
    {
        auto& O=Out[Rays[J].Index];O.PositionMeters=D[Rays[J].Index]*((Rays[J].Air+Rays[J].Solid)*.5);Indices.Add(J);
        for(const FVector Axis:{FVector::XAxisVector,FVector::YAxisVector,FVector::ZAxisVector}){Points.Add(O.PositionMeters+Axis*E);Points.Add(O.PositionMeters-Axis*E);}
    }
    if(!Points.IsEmpty())
    {
        if(!Fields(Points,Values))return false;
        for(int32 K=0;K<Indices.Num();++K)
        {
            auto& O=Out[Rays[Indices[K]].Index];const int32 B=K*6;
            O.Normal=FVector(Values[B].FieldMeters-Values[B+1].FieldMeters,Values[B+2].FieldMeters-Values[B+3].FieldMeters,Values[B+4].FieldMeters-Values[B+5].FieldMeters).GetSafeNormal();
            O.bSupported=!O.Normal.IsNearlyZero()&&FVector::DotProduct(O.Normal,D[Rays[Indices[K]].Index])>.0001;
        }
    }
    return true;
}
