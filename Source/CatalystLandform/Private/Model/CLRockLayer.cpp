#include "Model/CLRockLayer.h"
#include "Model/CLTectonics.h"
#include "Surface/CLSurfaceCompiler.h"
#include "CLNativeNoiseKernel.h"

FCLRockLayer::FCLRockLayer()
{
    Units.SetNum(3);
    const TCHAR* Names[]={TEXT("Competent unit"),TEXT("Mixed unit"),TEXT("Weak interbed")};
    const TCHAR* Surfaces[]={TEXT("Rock"),TEXT("Scree"),TEXT("Soil")};
    for(int32 I=0;I<3;++I){auto& U=Units[I];U.Id=FGuid(0x434c524b,1,0,I+1);U.Name=Names[I];U.SurfaceId=Surfaces[I];U.MiningResistance=I==0?.85:I==1?.55:.25;U.Erodibility=1-U.MiningResistance;}
    ApplyProgression(Character);DipDegrees=8.518518518518519;YoungerCoverRadiusFraction=0;AlgorithmVersion=1; // omitted serialized version retains the published v1 model
}
const TCHAR* FCLCompiledRock::PropertiesAssetPath(){return TEXT("/CatalystLandform/Rock/MD_RockProperties.MD_RockProperties");}
FLinearColor FCLCompiledRock::FillProperties(double Resistance,double Erosion)
{return FLinearColor(FMath::Clamp(Resistance,0.,1.),FMath::Clamp(Erosion,0.,1.),1,1);}
TSharedPtr<const FCLCompiledRock> FCLCompiledRock::Compile(const FCLRockLayer& L,int32 Seed,double R,double Min,double Max,const FCLCompiledSurfaceLibrary* Surfaces,TSharedPtr<const FCLCompiledTectonics> T,FString& Error)
{
    auto In=[](double V,double A,double B){return FMath::IsFinite(V)&&V>=A&&V<=B;};
    if((L.AlgorithmVersion!=1&&L.AlgorithmVersion!=2)||!In(L.Character,0,1)||L.Orientation.ContainsNaN()||!In(R,10,1.e7)||!In(Min,0,R)||!In(Max,Min,R)||L.Units.Num()!=3||
       !In(L.BedThicknessRadiusFraction,.00001,1)||!In(L.InterbedFraction,0,.8)||!In(L.DipDegrees,0,25)||!In(L.FoldAmplitudeRadiusFraction,0,.05)||!In(L.FoldWavelengthRadiusFraction,.01,2)||!In(L.TectonicResponse,0,1)||!In(L.FaultThrowRadiusFraction,0,.05)||!In(L.FaultZoneRadiusFraction,.0001,.1)||!In(L.CrustDepthRadiusFraction,0,1)||!In(L.ContactFeatherFraction,0,.1)||!In(L.YoungerCoverRadiusFraction,0,.1))
    {Error=TEXT("Rock Character requires algorithm 1 or 2, three units and finite bounded structural controls");return nullptr;}
    auto Out=MakeShared<FCLCompiledRock>();Out->Layer=L;Out->Radius=R;Out->Datum=Max;Out->Basement=FMath::Max(0.,Min-L.CrustDepthRadiusFraction*R);Out->Tectonics=MoveTemp(T);
    TSet<FGuid> Ids;
    for(const auto& U:L.Units)
    {
        if(!U.Id.IsValid()||Ids.Contains(U.Id)||U.Name.IsNone()||!In(U.MiningResistance,0,1)||!In(U.Erodibility,0,1))
        {Error=TEXT("Rock units require unique identities, names and normalized resistance/erodibility");return nullptr;}Ids.Add(U.Id);
        const int32 Index=Surfaces?Surfaces->Find(U.SurfaceId,U.SurfaceRevision):INDEX_NONE;
        if(Index==INDEX_NONE){Error=TEXT("Rock unit requires a resolved native client surface: ")+U.SurfaceId.ToString();return nullptr;}Out->SurfaceIndices.Add(Index);
    }
    if(L.AlgorithmVersion==1){Out->NativeNoise=FCLNativeNoiseKernel::Create(Error);if(!Out->NativeNoise)return nullptr;}
    FRandomStream Random(int32(uint32(Seed)^uint32(L.Seed)^0x434c5231U));Out->NoiseSeed=int32(Random.GetUnsignedInt());Out->Phase=Random.FRand();
    Out->Frame=L.Orientation.Quaternion()*FRotator(Random.FRandRange(-45,45),Random.FRandRange(-180,180),Random.FRandRange(-180,180)).Quaternion();
    Out->Namespace=FGuid(0x434c5231,uint32(Seed),uint32(L.Seed),L.AlgorithmVersion);
    if(L.AlgorithmVersion==2)
    {
        Out->Unconformity=FMath::Max(Out->Basement,Max-L.YoungerCoverRadiusFraction*R);
        // Ordered history: finite older deposition -> plate deformation ->
        // truncation at Unconformity -> undeformed younger deposition.
        auto Deposit=[&](double Bottom,double Top,bool Young)
        {
            int32 PreviousUnit=INDEX_NONE,RunLength=0;
            while(Bottom<Top)
            {
                if(Out->Beds.Num()>1022)return false;
                const double Thickness=L.BedThicknessRadiusFraction*R*Random.FRandRange(.65f,1.35f);
                const double Contact=FMath::Min(Top,Bottom+Thickness*(1-L.InterbedFraction));
                // Bound lithology runs without imposing a periodic material cycle.
                // A developed finite sequence must not lose an authored rock type
                // just because several independent random draws agreed.
                const int32 Candidate=Random.FRand()<.65?0:1;
                const int32 Unit=RunLength>=2?1-PreviousUnit:Candidate;
                RunLength=Unit==PreviousUnit?RunLength+1:1;PreviousUnit=Unit;
                if(Contact>Bottom)Out->Beds.Add({Unit,Bottom,Contact,Young});
                const double End=FMath::Min(Top,Bottom+Thickness);
                if(End>Contact)Out->Beds.Add({2,Contact,End,Young});
                Bottom=End;
            }
            return true;
        };
        if(!Deposit(Out->Basement,Max,false))
        {Error=TEXT("Finite rock history exceeds 1024 beds; increase bed thickness");return nullptr;}
        Out->OlderBedCount=Out->Beds.Num();
        if(L.YoungerCoverRadiusFraction>0&&!Deposit(Out->Unconformity,Max,true))
        {Error=TEXT("Finite rock history exceeds 1024 beds; increase bed thickness");return nullptr;}
    }
    return Out;
}
FCLCompiledRock::FPoint FCLCompiledRock::Classify(const FVector3d& P)const
{
    if(Layer.AlgorithmVersion==2)return ClassifyFinite(P);
    FPoint Out;const double Distance=P.Length();
    if(Distance<=Basement||Layer.InterbedFraction==0)return Out;
    const FVector D=Distance>1.e-6?P/Distance:FVector::ZAxisVector;
    const FVector Local=Frame.UnrotateVector(P),Direction=Frame.UnrotateVector(D);
    const double Tilt=Local.X*FMath::Tan(FMath::DegreesToRadians(Layer.DipDegrees));
    const double Fold=Radius*Layer.FoldAmplitudeRadiusFraction*NativeNoise->Sample(Direction/Layer.FoldWavelengthRadiusFraction,NoiseSeed);
    const double Inherited=Tectonics?Layer.TectonicResponse*Tectonics->SampleDeltaMeters(D):0;
    const double F=FMath::Clamp(.5+Local.Y/(Layer.FaultZoneRadiusFraction*Radius),0.,1.);
    const double Fault=Radius*Layer.FaultThrowRadiusFraction*(F*F*(3-2*F)-.5);
    const double Coordinate=(Distance-Datum+Tilt+Fold-Inherited-Fault)/(Radius*Layer.BedThicknessRadiusFraction)+Phase;
    const double Cycle=FMath::FloorToDouble(Coordinate),X=Coordinate-Cycle;const int64 Bed=int64(Cycle);
    const double Ends[]={1-Layer.InterbedFraction,1-Layer.InterbedFraction*.35,1.};
    const double Starts[]={0.,Ends[0],Ends[1]};
    for(int32 I=0;I<3;++I)if(X<Ends[I]||I==2)
    {
        Out.Unit=Out.Other=I;Out.Bed=Bed*3+I;
        const double Width=FMath::Min(Layer.ContactFeatherFraction,(Ends[I]-Starts[I])*.2);
        if(Width>0&&X-Starts[I]<Width){Out.Other=(I+2)%3;const double T=(X-Starts[I])/Width;Out.Alpha=.5+.5*T*T*(3-2*T);}
        else if(Width>0&&Ends[I]-X<Width){Out.Other=(I+1)%3;const double T=(Ends[I]-X)/Width;Out.Alpha=.5+.5*T*T*(3-2*T);}
        return Out;
    }
    return Out;
}
double FCLCompiledRock::RestoredRadiusMeters(const FVector3d& P) const
{
    const double R=P.Length();
    if(!Tectonics||R<1.e-6)return R;
    const auto S=Tectonics->SampleRockDeformation(P/R,Layer.FoldWavelengthRadiusFraction,Phase*2*PI,Layer.FaultZoneRadiusFraction);
    // This is a kinematic radial restoration, not a displacement of UE actors.
    // Local dip and folds vanish outside actual plate belts and with zero drive.
    const double Displacement=Layer.TectonicResponse*S.UpliftMeters+
        Radius*(FMath::Tan(FMath::DegreesToRadians(Layer.DipDegrees))*S.Tilt+
        Layer.FoldAmplitudeRadiusFraction*S.Fold+Layer.FaultThrowRadiusFraction*S.Fault);
    return R-Displacement;
}
FCLCompiledRock::FPoint FCLCompiledRock::ClassifyFinite(const FVector3d& P) const
{
    FPoint Out;Out.Bed=-1;
    if(Layer.InterbedFraction==0||Beds.IsEmpty())return Out;
    const double Distance=P.Length(),Restored=RestoredRadiusMeters(P);
    const bool Young=Layer.YoungerCoverRadiusFraction>0&&Distance>=Unconformity;
    const double Coordinate=Young?Distance:Restored;
    if(!Young&&Coordinate<Basement)return Out;
    // Binary lookup within each finite sequence; a surface edit never moves a bed.
    int32 Begin=0,End=Beds.Num();
    if(Layer.YoungerCoverRadiusFraction>0)
    {
        if(Young)Begin=OlderBedCount;else End=OlderBedCount;
    }
    if(Begin==End)return Out;
    const int32 First=Begin,Last=End-1;
    while(Begin<End){const int32 Mid=(Begin+End)/2;if(Coordinate>=Beds[Mid].UpperMeters)Begin=Mid+1;else End=Mid;}
    const int32 Index=FMath::Clamp(Begin,First,Last);const auto& B=Beds[Index];Out.Unit=Out.Other=B.Unit;Out.Bed=Index;
    const double Feather=FMath::Min(Layer.ContactFeatherFraction*Layer.BedThicknessRadiusFraction*Radius,(B.UpperMeters-B.LowerMeters)*.2);
    // Keep the angular unconformity sharp: only conformable neighbours feather.
    if(Feather>0&&Coordinate>=B.LowerMeters&&Coordinate-B.LowerMeters<Feather&&Index>First)
    {Out.Other=Beds[Index-1].Unit;const double T=(Coordinate-B.LowerMeters)/Feather;Out.Alpha=.5+.5*T*T*(3-2*T);}
    else if(Feather>0&&Coordinate<=B.UpperMeters&&B.UpperMeters-Coordinate<Feather&&Index<Last)
    {Out.Other=Beds[Index+1].Unit;const double T=(B.UpperMeters-Coordinate)/Feather;Out.Alpha=.5+.5*T*T*(3-2*T);}
    return Out;
}
FCLRockSample FCLCompiledRock::Sample(const FVector3d& P)const
{
    FCLRockSample S;if(P.ContainsNaN()||!FMath::IsFinite(P.SizeSquared())||P.Length()>Radius)return S;
    const auto C=Classify(P);const auto& U=Layer.Units[C.Unit];const auto& V=Layer.Units[C.Other];S.bValid=true;S.UnitId=U.Id;S.StratumIndex=C.Bed;
    S.StratumId=FGuid::Combine(FGuid::Combine(Namespace,U.Id),FGuid(0x53545241,0,uint32(uint64(C.Bed)>>32),uint32(C.Bed)));
    S.Name=U.Name;S.SurfaceId=U.SurfaceId;S.SurfaceRevision=U.SurfaceRevision;S.MiningResistance=FMath::Lerp(V.MiningResistance,U.MiningResistance,C.Alpha);S.Erodibility=FMath::Lerp(V.Erodibility,U.Erodibility,C.Alpha);return S;
}
FLinearColor FCLCompiledRock::Properties(const FVector3d& P)const
{
    const auto C=Classify(P);const auto& U=Layer.Units[C.Unit];const auto& V=Layer.Units[C.Other];
    return FLinearColor(FMath::Lerp(V.MiningResistance,U.MiningResistance,C.Alpha),FMath::Lerp(V.Erodibility,U.Erodibility,C.Alpha),1,0);
}
void FCLCompiledRock::ApplySurfaces(const FVector3d& P,FCLSurfaceWeights& W)const
{
    const auto C=Classify(P);for(double& V:W.Values)V=0;W.Values[SurfaceIndices[C.Unit]]+=C.Alpha;W.Values[SurfaceIndices[C.Other]]+=1-C.Alpha;
}
