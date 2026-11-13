#include "Model/CLRockLayer.h"
#include "Model/CLCompileProgress.h"
#include "Model/CLTectonics.h"
#include "Model/CLRegionGeneration.h"
#include "Model/CLContinents.h"
#include "Surface/CLSurfaceCompiler.h"

FCLRockLayer::FCLRockLayer()
{
    Units.SetNum(3);
    const TCHAR* Names[]={TEXT("Competent unit"),TEXT("Mixed unit"),TEXT("Weak interbed")};
    const TCHAR* Surfaces[]={TEXT("Rock"),TEXT("Scree"),TEXT("Soil")};
    for(int32 I=0;I<3;++I){auto& U=Units[I];U.Id=FGuid(0x434c524b,1,0,I+1);U.Name=Names[I];U.SurfaceId=Surfaces[I];U.MiningResistance=I==0?.85:I==1?.55:.25;U.Erodibility=1-U.MiningResistance;}
    // Current starting structure; independent of the Character progression.
    InterbedFraction=.39;
    DipDegrees=8.518518518518519;FoldAmplitudeRadiusFraction=.006;
    TectonicResponse=.32592592592592595;
    FaultThrowRadiusFraction=.001056000000000001;FaultZoneRadiusFraction=.006;
    FoldCompetentUnit=Units[0].Id;
}
const TCHAR* FCLCompiledRock::PropertiesAssetPath(){return TEXT("/CatalystLandform/Rock/MD_RockProperties.MD_RockProperties");}
bool FCLRockUnit::Validate(FString& Error)const
{
    auto Normal=[](double V){return FMath::IsFinite(V)&&V>=0&&V<=1;};
    if(!Id.IsValid()||Name.IsNone()||!Normal(MiningResistance)||!Normal(Erodibility)||uint8(Lithology)>uint8(ECLRockLithology::Ice)||!Normal(FracturePotential)||!Normal(DissolutionPotential)||(DissolutionPotential>0&&Lithology!=ECLRockLithology::Carbonate)||(Lithology==ECLRockLithology::Unknown&&FracturePotential>0)||SurfaceId.IsNone()||SurfaceRevision<1)
    {Error=TEXT("Geological materials require identity, name, material revision and bounded physical properties");return false;}
    return true;
}
bool FCLRockLayer::BindBedrockMaterials(const FCLCompiledSurfaceLibrary& Library,FString& Error)
{
    static const FName Materials[]={TEXT("StrongRock"),TEXT("MixedRock"),TEXT("WeakRock")};
    if(Units.Num()!=UE_ARRAY_COUNT(Materials)){Error=TEXT("Rock material bindings require the three geological units");return false;}
    int32 Indices[UE_ARRAY_COUNT(Materials)];
    for(int32 I=0;I<Units.Num();++I)
    {
        Indices[I]=Library.Ids.IndexOfByKey(Materials[I]);
        if(Indices[I]==INDEX_NONE){Error=TEXT("The configured surface library has no rock material: ")+Materials[I].ToString();return false;}
    }
    for(int32 I=0;I<Units.Num();++I){Units[I].SurfaceId=Materials[I];Units[I].SurfaceRevision=Library.Revisions[Indices[I]];}
    return true;
}
FLinearColor FCLCompiledRock::FillProperties(double Resistance,double Erosion)
{return FLinearColor(FMath::Clamp(Resistance,0.,1.),FMath::Clamp(Erosion,0.,1.),1,1);}
TSharedPtr<const FCLCompiledRock> FCLCompiledRock::Compile(const FCLRockLayer& Input,int32 Seed,double R,double Min,double Max,const FCLCompiledSurfaceLibrary* Surfaces,TSharedPtr<const FCLCompiledTectonics> T,FString& Error,TSharedPtr<const FCLCompiledContinents> Crust)
{
    const auto& L=Input;
    auto In=[](double V,double A,double B){return FMath::IsFinite(V)&&V>=A&&V<=B;};
    if(L.AlgorithmVersion!=5||!In(L.Character,0,1)||!In(R,10,1.e7)||!In(Min,0,R)||!In(Max,Min,R)||L.Units.Num()!=3||
       !In(L.InterbedFraction,0,.8)||!In(L.DipDegrees,0,25)||!In(L.FoldAmplitudeRadiusFraction,0,.05)||!In(L.TectonicResponse,0,1)||!In(L.FaultThrowRadiusFraction,0,.05)||!In(L.FaultZoneRadiusFraction,.0001,.1)||!In(L.CrustDepthRadiusFraction,0,1)||!In(L.ContactFeatherFraction,0,.1)||!In(L.DepositionalVariation,0,1))
    {Error=TEXT("Rock requires the current metric profile, three units and finite bounded structural controls");return nullptr;}
    if(!L.BedDistribution.Validate(Error))return nullptr;
    auto Out=MakeShared<FCLCompiledRock>();Out->Layer=L;Out->Radius=R;Out->Datum=Max;Out->Basement=FMath::Max(0.,Min-L.CrustDepthRadiusFraction*R);Out->Tectonics=MoveTemp(T);Out->Crust=MoveTemp(Crust);
    TSet<FGuid> Ids;
    for(const auto& U:L.Units)
    {
        if(!U.Validate(Error))return nullptr;
        if(Ids.Contains(U.Id)){Error=TEXT("Rock units require unique identities");return nullptr;}Ids.Add(U.Id);
        const int32 Index=Surfaces?Surfaces->Find(U.SurfaceId,U.SurfaceRevision):INDEX_NONE;
        if(Index==INDEX_NONE){Error=TEXT("Rock unit requires a resolved native client surface: ")+U.SurfaceId.ToString();return nullptr;}Out->SurfaceIndices.Add(Index);
    }
    if(FCLCompileProgress::IsPreparing())
    {
        {FCLRockFolding F;const int32 Competent=L.Units.IndexOfByPredicate([&](const auto& U){return U.Id==L.FoldCompetentUnit;});if(Competent==INDEX_NONE){Error=TEXT("Folding requires an explicit competent unit from the accepted rock palette");return nullptr;}if(!FCLRockFolding::Resolve(3,L.FoldViscosityContrast,L.FoldShorteningStrain,L.FoldAmplitudeRadiusFraction*R,F,Error))return nullptr;}
        return Out;
    }
    FRandomStream Random(int32(uint32(Seed)^uint32(L.Seed)^0x434c5231U));Out->Phase=Random.FRand();
    Out->Namespace=FGuid(0x434c5231,uint32(Seed),uint32(L.Seed),L.AlgorithmVersion);
    // Current metric deposition.
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
    // A single material-aware fold coordinate.
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
    return ClassifyFinite(P);
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
    const double Wavelength=Folding.WavelengthMeters>0?Folding.WavelengthMeters/FMath::Max(Folding.ReferenceRadiusMeters,1.e-6):1.;
    const auto S=Tectonics->SampleRockDeformation(D,Wavelength,Phase*2*PI,Layer.FaultZoneRadiusFraction);
    if(Extension)*Extension=S.Extension;
    return Regional+Layer.TectonicResponse*S.UpliftMeters+Radius*(FMath::Tan(FMath::DegreesToRadians(Layer.DipDegrees))*S.Tilt+Layer.FaultThrowRadiusFraction*S.Fault)+Folding.AmplitudeMeters(S.Compression)*S.FoldWave;

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
    if(Direction.ContainsNaN()||Direction.IsNearlyZero())return C;
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
    if(GetBedCount()==0)return Out;
    const double Distance=P.Length();if(Distance<1.e-6)return Out;
    bool Young=false;double Coordinate=0;
    const auto C=SampleColumn(P/Distance);
    Young=C.YoungerThicknessMeters>1.e-9&&Distance>=C.YoungerFloorRadiusMeters;
    Coordinate=Young?Distance:Distance-C.DeformationMeters;
    if(!Young&&Coordinate<C.RestoredBasementRadiusMeters)return Out;
    // Binary lookup within each finite sequence; a surface edit never moves a bed.
    int32 Begin=0,End=GetBedCount();
    {
        if(Young)Begin=OlderBedCount;else End=OlderBedCount;
    }
    if(Begin==End)return Out;
    const int32 First=Begin,Last=End-1;
    while(Begin<End){const int32 Mid=(Begin+End)/2;if(Coordinate>=GetBed(Mid).UpperMeters)Begin=Mid+1;else End=Mid;}
    const int32 Index=FMath::Clamp(Begin,First,Last);const auto B=GetBed(Index);Out.Unit=Out.Other=B.Unit;Out.Bed=Index;
    const double Feather=FMath::Min(Layer.ContactFeatherFraction*(B.UpperMeters-B.LowerMeters),(B.UpperMeters-B.LowerMeters)*.2);
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
    const auto C=Classify(P);const auto& U=Layer.Units[C.Unit];const auto& V=Layer.Units[C.Other];S.bValid=true;S.Origin=ECLMaterialOrigin::Rock;S.UnitId=U.Id;S.StratumIndex=C.Bed;
    S.StratumId=FGuid::Combine(FGuid::Combine(Namespace,U.Id),FGuid(0x53545241,0,uint32(uint64(C.Bed)>>32),uint32(C.Bed)));
    S.SubstrateUnitId=S.UnitId;S.SubstrateStratumId=S.StratumId;
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
    if(GetBedCount()==0)return false;
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
{return Layer.Units.Num()==3&&!Layer.Units.ContainsByPredicate([](const auto& U){return U.Lithology!=ECLRockLithology::Carbonate;});}
bool FCLCompiledRock::HasKarstHosts()const
{return HasCarbonateSequence()&&!Layer.Units.ContainsByPredicate([](const auto& U){return U.DissolutionPotential<=0;});}
bool FCLCompiledRock::HasFractureHosts()const
{return Layer.Units.Num()==3&&Layer.Units.ContainsByPredicate([](const auto& U){return U.Lithology==ECLRockLithology::Ice;})==false&&Layer.Units.ContainsByPredicate([](const auto& U){return U.Lithology==ECLRockLithology::Unknown||U.FracturePotential<=0;})==false;}
double FCLCompiledRock::FracturePotential()const
{double V=1;for(const auto& U:Layer.Units)V=FMath::Min(V,U.FracturePotential);return V;}
