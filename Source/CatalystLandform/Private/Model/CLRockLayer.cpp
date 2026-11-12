#include "Model/CLRockLayer.h"
#include "Model/CLCompileProgress.h"
#include "Model/CLTectonics.h"
#include "Model/CLRegionGeneration.h"
#include "Model/CLContinents.h"
#include "Surface/CLSurfaceCompiler.h"
#include "CLNativeNoiseKernel.h"

FCLRockLayer::FCLRockLayer()
{
    Units.SetNum(3);
    const TCHAR* Names[]={TEXT("Competent unit"),TEXT("Mixed unit"),TEXT("Weak interbed")};
    const TCHAR* Surfaces[]={TEXT("Rock"),TEXT("Scree"),TEXT("Soil")};
    for(int32 I=0;I<3;++I){auto& U=Units[I];U.Id=FGuid(0x434c524b,1,0,I+1);U.Name=Names[I];U.SurfaceId=Surfaces[I];U.MiningResistance=I==0?.85:I==1?.55:.25;U.Erodibility=1-U.MiningResistance;}
    // Frozen published defaults: UE serialization may omit values equal to the
    // default struct. Retuning authoring must not reinterpret those saved recipes.
    BedThicknessRadiusFraction=.04990661443148686;InterbedFraction=.39;
    DipDegrees=8.518518518518519;FoldAmplitudeRadiusFraction=.006;
    FoldWavelengthRadiusFraction=.42777777777777776;TectonicResponse=.32592592592592595;
    FaultThrowRadiusFraction=.001056000000000001;FaultZoneRadiusFraction=.006;
    FoldCompetentUnit=Units[0].Id;
}
const TCHAR* FCLCompiledRock::PropertiesAssetPath(){return TEXT("/CatalystLandform/Rock/MD_RockProperties.MD_RockProperties");}
FLinearColor FCLCompiledRock::FillProperties(double Resistance,double Erosion)
{return FLinearColor(FMath::Clamp(Resistance,0.,1.),FMath::Clamp(Erosion,0.,1.),1,1);}
TSharedPtr<const FCLCompiledRock> FCLCompiledRock::Compile(const FCLRockLayer& L,int32 Seed,double R,double Min,double Max,const FCLCompiledSurfaceLibrary* Surfaces,TSharedPtr<const FCLCompiledTectonics> T,FString& Error,TSharedPtr<const FCLCompiledContinents> Crust)
{
    auto In=[](double V,double A,double B){return FMath::IsFinite(V)&&V>=A&&V<=B;};
    if((L.AlgorithmVersion<1||L.AlgorithmVersion>5)||!In(L.Character,0,1)||L.Orientation.ContainsNaN()||!In(R,10,1.e7)||!In(Min,0,R)||!In(Max,Min,R)||L.Units.Num()!=3||
       !In(L.BedThicknessRadiusFraction,.00001,1)||!In(L.InterbedFraction,0,.8)||!In(L.DipDegrees,0,25)||!In(L.FoldAmplitudeRadiusFraction,0,.05)||(L.AlgorithmVersion<4&&!In(L.FoldWavelengthRadiusFraction,.01,2))||!In(L.TectonicResponse,0,1)||!In(L.FaultThrowRadiusFraction,0,.05)||!In(L.FaultZoneRadiusFraction,.0001,.1)||!In(L.CrustDepthRadiusFraction,0,1)||!In(L.ContactFeatherFraction,0,.1)||!In(L.YoungerCoverRadiusFraction,0,.1)||!In(L.DepositionalVariation,0,1))
    {Error=TEXT("Rock Character requires algorithm 1..5, three units and finite bounded structural controls");return nullptr;}
    if(L.AlgorithmVersion>=5&&!L.BedDistribution.Validate(Error))return nullptr;
    auto Out=MakeShared<FCLCompiledRock>();Out->Layer=L;Out->Radius=R;Out->Datum=Max;Out->Basement=FMath::Max(0.,Min-L.CrustDepthRadiusFraction*R);Out->Tectonics=MoveTemp(T);Out->Crust=MoveTemp(Crust);
    TSet<FGuid> Ids;
    for(const auto& U:L.Units)
    {
        if(!U.Id.IsValid()||Ids.Contains(U.Id)||U.Name.IsNone()||!In(U.MiningResistance,0,1)||!In(U.Erodibility,0,1)||uint8(U.Lithology)>uint8(ECLRockLithology::Ice)||!In(U.FracturePotential,0,1)||!In(U.DissolutionPotential,0,1)||(U.DissolutionPotential>0&&U.Lithology!=ECLRockLithology::Carbonate)||(U.Lithology==ECLRockLithology::Unknown&&U.FracturePotential>0))
        {Error=TEXT("Rock units require unique identities, names and normalized resistance/erodibility");return nullptr;}Ids.Add(U.Id);
        const int32 Index=Surfaces?Surfaces->Find(U.SurfaceId,U.SurfaceRevision):INDEX_NONE;
        if(Index==INDEX_NONE){Error=TEXT("Rock unit requires a resolved native client surface: ")+U.SurfaceId.ToString();return nullptr;}Out->SurfaceIndices.Add(Index);
    }
    if(FCLCompileProgress::IsPreparing())
    {
        if(L.AlgorithmVersion>=4){FCLRockFolding F;const int32 Competent=L.Units.IndexOfByPredicate([&](const auto& U){return U.Id==L.FoldCompetentUnit;});if(Competent==INDEX_NONE){Error=TEXT("Folding requires an explicit competent unit from the accepted rock palette");return nullptr;}if(!FCLRockFolding::Resolve(3,L.FoldViscosityContrast,L.FoldShorteningStrain,L.FoldAmplitudeRadiusFraction*R,F,Error))return nullptr;}
        return Out;
    }
    if(L.AlgorithmVersion==1){Out->NativeNoise=FCLNativeNoiseKernel::Create(Error);if(!Out->NativeNoise)return nullptr;}
    FRandomStream Random(int32(uint32(Seed)^uint32(L.Seed)^0x434c5231U));Out->NoiseSeed=int32(Random.GetUnsignedInt());Out->Phase=Random.FRand();
    Out->Frame=L.Orientation.Quaternion()*FRotator(Random.FRandRange(-45,45),Random.FRandRange(-180,180),Random.FRandRange(-180,180)).Quaternion();
    Out->Namespace=FGuid(0x434c5231,uint32(Seed),uint32(L.Seed),L.AlgorithmVersion);
    if(L.AlgorithmVersion>=5)
    {
        if((Max-Out->Basement+Max)/L.BedDistribution.MaximumMeters>MaximumMetricBeds)
        {Error=TEXT("Metric rock history exceeds 4194304 contacts even at maximum thickness; no coarsening was applied");return nullptr;}
        const auto Curve=L.BedDistribution.QuantileCurve();
        // One physical sample per bed. Basin fill shares a radial sequence clipped
        // by its floor; it never stretches a short packet across accommodation.
        // Compact immutable contacts replace the old 1024 expanded-bed limit.
        auto Deposit=[&](double Bottom,double Top)
        {
            int32 Previous=INDEX_NONE;
            while(Bottom<Top)
            {
                if(!FCLCompileProgress::Check(Error))return false;
                if(Out->MetricUnits.Num()>=MaximumMetricBeds)return false;
                const double Thickness=FMath::Clamp(double(Curve.Eval(Random.FRand())),L.BedDistribution.MinimumMeters,L.BedDistribution.MaximumMeters);
                const double End=FMath::Min(Top,Bottom+Thickness);
                int32 Unit=Random.FRand()<L.InterbedFraction?2:Random.FRand()<.65?0:1;
                // Adjacent beds should remain readable even with provisional art.
                // This conditions lithology only, never the thickness draw.
                if(Unit==Previous)Unit=Unit==2?(Random.FRand()<.65?0:1):1-Unit;
                Out->MetricUppers.Add(End);Out->MetricUnits.Add(uint8(Unit));Previous=Unit;Bottom=End;
            }
            return true;
        };
        if(!Deposit(Out->Basement,Max)){Error=TEXT("Metric rock history exceeds 4194304 contacts; no coarsening was applied");return nullptr;}
        Out->OlderBedCount=Out->MetricUnits.Num();
        if(!Deposit(0,Max)){Error=TEXT("Metric rock history exceeds 4194304 contacts; no coarsening was applied");return nullptr;}
    }
    else if(L.AlgorithmVersion>=2)
    {
        const double ReferenceThickness=L.AlgorithmVersion>=3?FMath::Max(L.YoungerCoverRadiusFraction,L.BedThicknessRadiusFraction*2)*R:L.YoungerCoverRadiusFraction*R;
        Out->Unconformity=FMath::Max(Out->Basement,Max-ReferenceThickness);
        // V2 uses a fixed truncation/cover level. V3 stores the younger reference
        // interval here; SampleColumn supplies its actual basin floor/thickness.
        auto Deposit=[&](double Bottom,double Top,bool Young)
        {
            int32 PreviousUnit=INDEX_NONE,RunLength=0;
            while(Bottom<Top)
            {
                if(!FCLCompileProgress::Check(Error))return false;
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
        if((L.AlgorithmVersion>=3||L.YoungerCoverRadiusFraction>0)&&!Deposit(Out->Unconformity,Max,true))
        {Error=TEXT("Finite rock history exceeds 1024 beds; increase bed thickness");return nullptr;}
    }
    if(L.AlgorithmVersion>=4)
    {
        const int32 Competent=L.Units.IndexOfByPredicate([&](const FCLRockUnit& U){return U.Id==L.FoldCompetentUnit;});
        if(Competent==INDEX_NONE){Error=TEXT("Folding requires an explicit competent unit from the accepted rock palette");return nullptr;}
        double Thickness=0,Centers=0;int32 Count=0;
        for(int32 I=0;I<Out->OlderBedCount;++I)
        {if((I&1023)==0&&!FCLCompileProgress::Check(Error))return nullptr;const auto B=Out->GetBed(I);if(B.Unit==Competent){Thickness+=B.UpperMeters-B.LowerMeters;Centers+=(B.UpperMeters+B.LowerMeters)*.5;++Count;}}
        // One effective thickness/wavelength for the whole conformable sequence.
        // Independent per-bed modes would make shared contacts cross.
        if(!FCLRockFolding::Resolve(Count?Thickness/Count:0,L.FoldViscosityContrast,L.FoldShorteningStrain,L.FoldAmplitudeRadiusFraction*R,Out->Folding,Error))return nullptr;
        Out->Folding.ReferenceRadiusMeters=Count?Centers/Count:Max;
    }
    return Out;
}
FCLCompiledRock::FPoint FCLCompiledRock::Classify(const FVector3d& P)const
{
    if(ProvinceBase)return ProvinceBase->Classify(ProvinceReferencePosition(P));
    if(Layer.AlgorithmVersion>=2)return ClassifyFinite(P);
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
TSharedPtr<const FCLCompiledRock> FCLCompiledRock::WithProvinceDeformation(TSharedPtr<const FCLCompiledRegionGeneration> P)const
{auto Out=MakeShared<FCLCompiledRock>(*this);if(P&&P->bHasDirectionalDeformation)Out->ProvinceBase=MakeShared<FCLCompiledRock>(*this);Out->Province=MoveTemp(P);return Out;}
FVector3d FCLCompiledRock::ProvinceReferencePosition(const FVector3d& P)const
{const double R=P.Length();if(R<1.e-6)return P;const FVector D=P/R;return Province->ProvinceReferenceDirection(D)*FMath::Max(0.,R-Province->ProvinceDeformationMeters(D));}
double FCLCompiledRock::DeformationMeters(const FVector3d& D,double* Extension) const
{
    if(Extension)*Extension=0;
    const double Regional=Province?Province->ProvinceDeformationMeters(D):0;
    if(!Tectonics)return Regional;
    const double Wavelength=Layer.AlgorithmVersion>=4?(Folding.WavelengthMeters>0?Folding.WavelengthMeters/FMath::Max(Folding.ReferenceRadiusMeters,1.e-6):1.):Layer.FoldWavelengthRadiusFraction;
    const auto S=Tectonics->SampleRockDeformation(D,Wavelength,Phase*2*PI,Layer.FaultZoneRadiusFraction);
    if(Extension)*Extension=S.Extension;
    if(Layer.AlgorithmVersion>=4)
        return Regional+Layer.TectonicResponse*S.UpliftMeters+Radius*(FMath::Tan(FMath::DegreesToRadians(Layer.DipDegrees))*S.Tilt+Layer.FaultThrowRadiusFraction*S.Fault)+Folding.AmplitudeMeters(S.Compression)*S.FoldWave;
    return Regional+Layer.TectonicResponse*S.UpliftMeters+
        Radius*(FMath::Tan(FMath::DegreesToRadians(Layer.DipDegrees))*S.Tilt+
        Layer.FoldAmplitudeRadiusFraction*S.Fold+Layer.FaultThrowRadiusFraction*S.Fault);
}
double FCLCompiledRock::RestoredRadiusMeters(const FVector3d& P) const
{
    if(ProvinceBase)return ProvinceBase->RestoredRadiusMeters(ProvinceReferencePosition(P));
    const double R=P.Length();
    return R<1.e-6?R:R-DeformationMeters(P/R);
}
FCLRockColumn FCLCompiledRock::SampleColumn(const FVector3d& Direction) const
{
    if(ProvinceBase&& !Direction.ContainsNaN()&&!Direction.IsNearlyZero()){const FVector D=Direction.GetSafeNormal();auto C=ProvinceBase->SampleColumn(Province->ProvinceReferenceDirection(D));const double Delta=Province->ProvinceDeformationMeters(D);C.DeformationMeters+=Delta;C.YoungerFloorRadiusMeters+=Delta;return C;}
    FCLRockColumn C;
    if(Layer.AlgorithmVersion<3||Direction.ContainsNaN()||Direction.IsNearlyZero())return C;
    const FVector D=Direction.GetSafeNormal();C.bValid=true;
    C.DeformationMeters=DeformationMeters(D,&C.Extension);
    C.Continentalness=Crust?Crust->CrustContext(D,.75):Tectonics?Tectonics->Continentalness(D):.75;
    const double CrustValue=FMath::Clamp(C.Continentalness,0.,1.);
    // Artistic palaeo-accommodation prior: broad margin basins, thin oceanic
    // cover and inherited extensional lows. These are the existing crust/stress
    // fields, not a new province/noise generator or a reconstructed history.
    const double Potential=FMath::Clamp(4*CrustValue*(1-CrustValue)+.08*(1-CrustValue)+C.Extension,0.,1.);
    C.RestoredBasementRadiusMeters=FMath::Lerp(Basement,Datum-(Datum-Basement)*Potential,Layer.DepositionalVariation);
    // Older deposition ends at Datum before deformation. Positive displacement
    // is truncated there; negative displacement creates local accommodation.
    // Sufficient sediment fills that space to the same fixed reference level.
    C.YoungerFloorRadiusMeters=FMath::Clamp(Datum+C.DeformationMeters,0.,Datum);
    C.YoungerThicknessMeters=Datum-C.YoungerFloorRadiusMeters;
    return C;
}
FCLCompiledRock::FPoint FCLCompiledRock::ClassifyFinite(const FVector3d& P) const
{
    FPoint Out;Out.Bed=-1;
    if((Layer.AlgorithmVersion<5&&Layer.InterbedFraction==0)||GetBedCount()==0)return Out;
    const double Distance=P.Length();if(Layer.AlgorithmVersion>=3&&Distance<1.e-6)return Out;
    bool Young=false;double Coordinate=0;
    if(Layer.AlgorithmVersion>=3)
    {
        const auto C=SampleColumn(P/Distance);
        Young=C.YoungerThicknessMeters>1.e-9&&Distance>=C.YoungerFloorRadiusMeters;
        if(Young)
        {
            // One finite set of episode identities spans each basin. Mapping
            // thickness, rather than restarting a cycle, makes every contact
            // converge at the basin edge and leaves dry columns untouched.
            const double Fraction=FMath::Clamp((Distance-C.YoungerFloorRadiusMeters)/C.YoungerThicknessMeters,0.,1.);
            Coordinate=Layer.AlgorithmVersion>=5?Distance:FMath::Lerp(Unconformity,Datum,Fraction);
        }
        else
        {
            Coordinate=Distance-C.DeformationMeters;
            if(Coordinate<C.RestoredBasementRadiusMeters)return Out;
        }
    }
    else
    {
        const double Restored=RestoredRadiusMeters(P);
        Young=Layer.YoungerCoverRadiusFraction>0&&Distance>=Unconformity;
        Coordinate=Young?Distance:Restored;
        if(!Young&&Coordinate<Basement)return Out;
    }
    // Binary lookup within each finite sequence; a surface edit never moves a bed.
    int32 Begin=0,End=GetBedCount();
    if(Layer.AlgorithmVersion>=3||Layer.YoungerCoverRadiusFraction>0)
    {
        if(Young)Begin=OlderBedCount;else End=OlderBedCount;
    }
    if(Begin==End)return Out;
    const int32 First=Begin,Last=End-1;
    while(Begin<End){const int32 Mid=(Begin+End)/2;if(Coordinate>=GetBed(Mid).UpperMeters)Begin=Mid+1;else End=Mid;}
    const int32 Index=FMath::Clamp(Begin,First,Last);const auto B=GetBed(Index);Out.Unit=Out.Other=B.Unit;Out.Bed=Index;
    const double Feather=FMath::Min(Layer.ContactFeatherFraction*(Layer.AlgorithmVersion>=5?B.UpperMeters-B.LowerMeters:Layer.BedThicknessRadiusFraction*Radius),(B.UpperMeters-B.LowerMeters)*.2);
    // Keep the angular unconformity sharp: only conformable neighbours feather.
    if(Feather>0&&Coordinate>=B.LowerMeters&&Coordinate-B.LowerMeters<Feather&&Index>First)
    {Out.Other=GetBed(Index-1).Unit;const double T=(Coordinate-B.LowerMeters)/Feather;Out.Alpha=.5+.5*T*T*(3-2*T);}
    else if(Feather>0&&Coordinate<=B.UpperMeters&&B.UpperMeters-Coordinate<Feather&&Index<Last)
    {Out.Other=GetBed(Index+1).Unit;const double T=(B.UpperMeters-Coordinate)/Feather;Out.Alpha=.5+.5*T*T*(3-2*T);}
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


bool FCLCompiledRock::TryStableSurfaces(const FVector3d& P,double Deformation,double BasementRadius,double Tolerance,FCLSurfaceWeights& W)const
{
    if(ProvinceBase)return false; // Curved shear contacts require exact mapped classification.
    if(Layer.AlgorithmVersion<5||GetBedCount()==0)return false;
    const double Distance=P.Length();if(Distance<1.e-6)return false;
    // The compiler's directional input tolerance is expanded before testing
    // discontinuities. Never approximate a contact blend or a bed identity.
    // Bedding deformation retains its 25 micrometre input budget.
    // Tolerance describes the separately prepared basement input only.
    const double Guard=.000025*4+1.e-7;
    const double BasementGuard=Tolerance*4+1.e-7;
    const double Floor=FMath::Clamp(Datum+Deformation,0.,Datum);
    bool Young=false;
    if(Deformation<-Guard&&Distance>Floor+Guard)Young=true;
    else if(!(Deformation>Guard||Distance<Floor-Guard))return false;
    const double Coordinate=Young?Distance:Distance-Deformation;
    int32 Unit=0;
    if(!Young&&Coordinate<BasementRadius-BasementGuard){}
    else
    {
        if(!Young&&Coordinate<BasementRadius+BasementGuard)return false;
        int32 Begin=Young?OlderBedCount:0,End=Young?GetBedCount():OlderBedCount;
        if(Begin==End){}
        else
        {
            const int32 First=Begin,Last=End-1;
            while(Begin<End){const int32 Mid=(Begin+End)/2;if(Coordinate>=GetBed(Mid).UpperMeters)Begin=Mid+1;else End=Mid;}
            const int32 Index=FMath::Clamp(Begin,First,Last);const auto B=GetBed(Index);
            const double Feather=FMath::Min(Layer.ContactFeatherFraction*(B.UpperMeters-B.LowerMeters),(B.UpperMeters-B.LowerMeters)*.2);
            if((Index>First&&Coordinate<B.LowerMeters+Feather+Guard)||(Index<Last&&Coordinate>B.UpperMeters-Feather-Guard))return false;
            Unit=B.Unit;
        }
    }
    for(double& V:W.Values)V=0;W.Values[SurfaceIndices[Unit]]=1;return true;
}

bool FCLCompiledRock::HasCarbonateSequence()const
{return Layer.AlgorithmVersion>=3&&Layer.Units.Num()==3&&!Layer.Units.ContainsByPredicate([](const auto& U){return U.Lithology!=ECLRockLithology::Carbonate;});}
bool FCLCompiledRock::HasKarstHosts()const
{return HasCarbonateSequence()&&!Layer.Units.ContainsByPredicate([](const auto& U){return U.DissolutionPotential<=0;});}
bool FCLCompiledRock::HasFractureHosts()const
{return Layer.Units.Num()==3&&Layer.Units.ContainsByPredicate([](const auto& U){return U.Lithology==ECLRockLithology::Ice;})==false&&Layer.Units.ContainsByPredicate([](const auto& U){return U.Lithology==ECLRockLithology::Unknown||U.FracturePotential<=0;})==false;}
double FCLCompiledRock::FracturePotential()const
{double V=1;for(const auto& U:Layer.Units)V=FMath::Min(V,U.FracturePotential);return V;}
