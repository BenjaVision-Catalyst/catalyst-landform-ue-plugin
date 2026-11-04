#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLContinents.h"
#include "CLRockSectionFixtures.h"
#include "Interfaces/IPluginManager.h"
#include "JsonObjectConverter.h"
#include "Scene/CLScene.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLDepositionalTest,"Catalyst.Landform.Rock.RegionalOnlapAndBasinFill",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLDepositionalTest::RunTest(const FString&)
{
    FString Error;auto R=FCLPlanetCompiler::ContinentalBase();R.UseGuidedGeology();
    const auto P=FCLPlanetCompiler::Compile(R,Error);if(!P){AddError(Error);return false;}
    FCLRockLayer L;L.bEnabled=true;L.ApplyProgression(.8);L.DepositionalVariation=1;
    L.BedThicknessRadiusFraction=.018;L.YoungerCoverRadiusFraction=.06;
    L.DipDegrees=L.FoldAmplitudeRadiusFraction=0;L.FaultThrowRadiusFraction=.008;L.TectonicResponse=.9;
    FCLContinentalLayer CL;CL.bEnabled=true;CL.ApplyProgression(.8);
    const auto Crust=FCLCompiledContinents::Compile(CL,1729,160,Error,true);
    const auto Compression=CLRockSectionPlates(true,Error),Extension=CLRockSectionPlates(false,Error,2,1,2);
    if(!Crust||!Compression||!Extension){AddError(Error);return false;}
    const auto Neutral=FCLCompiledRock::Compile(L,1729,1000,640,800,P->Surfaces.Get(),nullptr,Error);
    const auto Onlap=FCLCompiledRock::Compile(L,1729,1000,640,800,P->Surfaces.Get(),nullptr,Error,Crust);
    const auto Basin=FCLCompiledRock::Compile(L,1729,1000,640,800,P->Surfaces.Get(),Extension,Error);
    const auto Uplift=FCLCompiledRock::Compile(L,1729,1000,640,800,P->Surfaces.Get(),Compression,Error);
    if(!Neutral||!Onlap||!Basin||!Uplift){AddError(Error);return false;}
    const auto N=Neutral->SampleColumn(FVector::ZAxisVector);
    TestEqual(TEXT("Declared neutral crust is reused without inventing a new domain generator"),N.Continentalness,.75);
    TestTrue(TEXT("Independent neutral-column basement oracle"),FMath::IsNearlyEqual(N.RestoredBasementRadiusMeters,800-(800-490)*.77,1.e-9));
    TestEqual(TEXT("No subsidence means no younger blanket"),N.YoungerThicknessMeters,0.);
    FVector High=FVector::ZAxisVector,Margin=High;double MaxCrust=-1,BestMargin=2;
    double MinBasement=1000,MaxBasement=0;int32 Pinched=0;
    for(int32 I=0;I<2048;++I)
    {
        const double Z=1-2*(I+.5)/2048.,A=I*2.399963229728653;
        const FVector D(FMath::Cos(A)*FMath::Sqrt(1-Z*Z),FMath::Sin(A)*FMath::Sqrt(1-Z*Z),Z);
        const auto C=Onlap->SampleColumn(D);const double NativeCrust=Crust->CrustContext(D,.75);
        TestEqual(TEXT("Regional model consumes the accepted continental crust field"),C.Continentalness,NativeCrust);
        TestEqual(TEXT("An undeformed crust has no young basin fill"),C.YoungerThicknessMeters,0.);
        if(NativeCrust>MaxCrust){MaxCrust=NativeCrust;High=D;}
        if(FMath::Abs(NativeCrust-.5)<BestMargin){BestMargin=FMath::Abs(NativeCrust-.5);Margin=D;}
        MinBasement=FMath::Min(MinBasement,C.RestoredBasementRadiusMeters);MaxBasement=FMath::Max(MaxBasement,C.RestoredBasementRadiusMeters);
        if(Onlap->Sample(D*650).StratumIndex<0)++Pinched;
    }
    TestTrue(TEXT("Existing crust creates both cover and exposed basement domains"),MaxBasement-MinBasement>150&&Pinched>0&&Pinched<2048);
    const auto AtHigh=Onlap->SampleColumn(High),AtMargin=Onlap->SampleColumn(Margin);
    const double BedRadius=(AtHigh.RestoredBasementRadiusMeters+AtMargin.RestoredBasementRadiusMeters)*.5;
    TestTrue(TEXT("Finite beds onlap basement instead of passing through the regional high"),Onlap->Sample(High*BedRadius).StratumIndex==-1&&Onlap->Sample(Margin*BedRadius).StratumIndex>=0);
    TestEqual(TEXT("Regional basement has one coherent deep identity"),Onlap->Sample(High*100).StratumId,Onlap->Sample(Margin*100).StratumId);
    const auto B=Basin->SampleColumn(FVector::ZAxisVector),U=Uplift->SampleColumn(FVector::ZAxisVector);
    TestTrue(TEXT("Extensional subsidence creates accommodation using the same plate drive"),B.Extension>.5&&B.DeformationMeters<0&&B.YoungerThicknessMeters>1);
    TestTrue(TEXT("Uplift is truncated and receives no invented basin fill"),U.DeformationMeters>0&&U.YoungerThicknessMeters==0);
    TestTrue(TEXT("Independent basin volume identity: fill exactly spans the lowered old top to datum"),FMath::IsNearlyEqual(B.YoungerFloorRadiusMeters,800+B.DeformationMeters,1.e-9)&&FMath::IsNearlyEqual(B.YoungerFloorRadiusMeters+B.YoungerThicknessMeters,800.,1.e-9));
    const auto Dry=Basin->SampleColumn(FVector::XAxisVector);
    TestEqual(TEXT("Young deposits pinch out outside the finite tectonic belt"),Dry.YoungerThicknessMeters,0.);
    TestEqual(TEXT("Fill also ends beyond the actual plate segment endpoint"),Basin->SampleColumn(FVector::YAxisVector).YoungerThicknessMeters,0.);
    int32 YoungCount=0;const auto& Beds=Basin->GetBeds();double YoungLower=800;
    for(const auto& Bed:Beds)if(Bed.bYounger)YoungLower=FMath::Min(YoungLower,Bed.LowerMeters);
    for(int32 I=0;I<Beds.Num();++I)if(Beds[I].bYounger)
    {
        ++YoungCount;const auto& Bed=Beds[I];const double F=((Bed.LowerMeters+Bed.UpperMeters)*.5-YoungLower)/(800-YoungLower);
        const auto S=Basin->Sample(FVector::ZAxisVector*(B.YoungerFloorRadiusMeters+B.YoungerThicknessMeters*F));
        TestEqual(TEXT("Each finite younger episode occupies its own scaled interval"),S.StratumIndex,int64(I));
        const FVector D=FVector(.015,0,1).GetSafeNormal();const auto Side=Basin->SampleColumn(D);
        const auto Other=Basin->Sample(D*(Side.YoungerFloorRadiusMeters+Side.YoungerThicknessMeters*F));
        TestTrue(TEXT("Episode identity continues laterally as thickness changes"),Side.YoungerThicknessMeters>0&&Other.StratumId==S.StratumId);
    }
    TestTrue(TEXT("Basin fill has a finite sequence"),YoungCount>2&&YoungCount<30);
    L.DepositionalVariation=2;TestFalse(TEXT("Unbounded regional controls fail closed"),FCLCompiledRock::Compile(L,1729,1000,640,800,P->Surfaces.Get(),nullptr,Error).IsValid());
    R.SchemaVersion=20;R.Rock.bEnabled=true;R.Rock.ApplyProgression(.8);
    const auto Shared=FCLPlanetCompiler::Compile(R,Error);if(!Shared){AddError(Error);return false;}
    TestEqual(TEXT("The recipe compiler binds the existing crust instance"),Shared->Rock->SampleColumn(Margin).Continentalness,Shared->Continents->CrustContext(Margin,.75));
    const FString Folder=FPaths::ProjectSavedDir()/TEXT("Verification/rock-deposition");IFileManager::Get().MakeDirectory(*Folder,true);
    const TSharedPtr<const FCLCompiledRock> Models[]={Onlap,Basin,Uplift,Basin};
    const TCHAR* Names[]={TEXT("onlap"),TEXT("basin-fill"),TEXT("uplift"),TEXT("basin-edge")};
    const FVector Across=(Margin-High*FVector::DotProduct(High,Margin)).GetSafeNormal();
    for(int32 K=0;K<4;++K)
    {
        FString CSV=TEXT("x_m,z_m,unit,stratum,solid,sequence\n");
        for(int32 Z=0;Z<=160;++Z)for(int32 X=0;X<=320;++X)
        {
            const double PX=-380+760*X/320.,PZ=480+320*Z/160.;
            const FVector Point=K==0?High*PZ+Across*PX:K==3?FVector(0,(PZ+PX)*.7071067811865475,(PZ-PX)*.7071067811865475):FVector(PX,0,PZ);
            const auto S=Models[K]->Sample(Point);const auto& Sequence=Models[K]->GetBeds();
            const int32 Unit=R.Rock.Units.IndexOfByPredicate([&](const FCLRockUnit& V){return V.Id==S.UnitId;});
            const int32 Age=S.StratumIndex<0?-1:Sequence[S.StratumIndex].bYounger?1:0;
            CSV+=FString::Printf(TEXT("%.6f,%.6f,%d,%lld,%d,%d\n"),PX,PZ,Unit,S.StratumIndex,Point.Length()<=800?1:0,Age);
        }
        TestTrue(TEXT("Regional section exports canonical C++ samples"),FFileHelper::SaveStringToFile(CSV,*(Folder/(FString(Names[K])+TEXT(".csv")))));
    }
    AddInfo(FString::Printf(TEXT("2048 shared-crust probes; basement %.3f..%.3f m; %d onlap pinchout probes; %.3f m filled accommodation; %d younger episodes"),MinBasement,MaxBasement,Pinched,B.YoungerThicknessMeters,YoungCount));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLDepositFixtureTest,"Catalyst.Landform.Rock.DepositionalExcavationFixture",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLDepositFixtureTest::RunTest(const FString&)
{
    FString Json,Error;const auto Plugin=IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"));
    TestTrue(TEXT("Resolved deformed fixture exists"),FFileHelper::LoadFileToString(Json,*(Plugin->GetBaseDir()/TEXT("Examples/RockDepositsExcavation.json"))));
    FCLPlanetRecipe R;if(!FJsonObjectConverter::JsonObjectStringToUStruct(Json,&R,0,0)){AddError(TEXT("Invalid fixture input"));return false;}
    TestEqual(TEXT("Fixture uses regional history"),R.Rock.AlgorithmVersion,3);TestTrue(TEXT("Actual native tectonic model is active"),R.Tectonics.bEnabled);
    const auto Full=FCLPlanetCompiler::Compile(R,Error);if(!Full){AddError(Error);return false;}
    int32 Young=0,Old=0;TSet<FGuid> Units,Beds;TArray<FVector> Points;
    for(int32 I=0;I<48;++I)
    {
        const FVector Point(8.05,2,112.5+I*.5);Points.Add(Point);
        const auto G=Full->Rock->Sample(Point);Units.Add(G.UnitId);Beds.Add(G.StratumId);
        if(G.StratumIndex>=0){if(Full->Rock->GetBeds()[G.StratumIndex].bYounger)++Young;else ++Old;}
    }
    TestTrue(TEXT("Cooked wall fixture crosses both older beds and younger basin fill"),Young>=8&&Old>=8&&Units.Num()==3&&Beds.Num()>=12);
    const auto Scene=FCLScene::FromPlanet(Full,Error);if(!Scene){AddError(Error);return false;}
    TArray<FCLSceneShapeSample> Shape;TestTrue(TEXT("Canonical wall shape samples"),Scene->SampleShape({Points},Shape,Error));
    for(const auto& S:Shape)TestTrue(TEXT("Every geology probe is initially in actual solid cave wall"),S.FieldMeters<0);
    AddInfo(FString::Printf(TEXT("Resolved tectonic seed %d: %d older and %d younger wall probes, %d strata; all 48 wall probes begin solid"),R.Tectonics.Seed,Old,Young,Beds.Num()));return true;
}
#endif
