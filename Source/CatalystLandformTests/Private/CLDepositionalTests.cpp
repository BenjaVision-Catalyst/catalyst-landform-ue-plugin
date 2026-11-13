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
    FCLRockLayer L;L.bEnabled=true;L.ApplyProgression(.8);L.AlgorithmVersion=5;L.DepositionalVariation=1;
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
    int32 YoungCount=0;
    for(int32 I=Basin->GetOlderBedCount();I<Basin->GetBedCount();++I)
    {
        const auto Bed=Basin->GetBed(I);const double Mid=(Bed.LowerMeters+Bed.UpperMeters)*.5;
        if(Mid<B.YoungerFloorRadiusMeters)continue;
        ++YoungCount;
        const auto S=Basin->Sample(FVector::ZAxisVector*Mid);
        TestEqual(TEXT("Younger beds retain their physical thickness and canonical identity"),S.StratumIndex,int64(I));
        const FVector D=FVector(.015,0,1).GetSafeNormal();
        if(Mid>Basin->SampleColumn(D).YoungerFloorRadiusMeters)
            TestEqual(TEXT("A contact keeps the same identity across the basin"),Basin->Sample(D*Mid).StratumId,S.StratumId);
    }
    TestTrue(TEXT("Subsidence contains finite metric beds"),YoungCount>0);
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
            const auto S=Models[K]->Sample(Point);
            const int32 Unit=R.Rock.Units.IndexOfByPredicate([&](const FCLRockUnit& V){return V.Id==S.UnitId;});
            const int32 Age=S.StratumIndex<0?-1:Models[K]->GetBed(S.StratumIndex).bYounger?1:0;
            CSV+=FString::Printf(TEXT("%.6f,%.6f,%d,%lld,%d,%d\n"),PX,PZ,Unit,S.StratumIndex,Point.Length()<=800?1:0,Age);
        }
        TestTrue(TEXT("Regional section exports canonical C++ samples"),FFileHelper::SaveStringToFile(CSV,*(Folder/(FString(Names[K])+TEXT(".csv")))));
    }
    AddInfo(FString::Printf(TEXT("2048 shared-crust probes; basement %.3f..%.3f m; %d onlap pinchout probes; %.3f m filled accommodation; %d younger episodes"),MinBasement,MaxBasement,Pinched,B.YoungerThicknessMeters,YoungCount));
    return true;
}

#endif
