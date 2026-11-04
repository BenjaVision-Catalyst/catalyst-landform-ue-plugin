#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Async/Async.h"
#include "Model/CLPlanetRecipe.h"
#include "CLRockSectionFixtures.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLRockMetricDistributionTest,"Catalyst.Landform.Rock.MetricBedDistribution",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLRockMetricDistributionTest::RunTest(const FString&)
{
    FString Error;FCLRockBedDistribution D;TestTrue(TEXT("Physical defaults validate"),D.Validate(Error));
    const auto Q=D.QuantileCurve();int32 Thin=0,Core=0,Thick=0,NearMinimum=0;double Mean=0,Previous=0;
    FString CSV=TEXT("probability,thickness_m\n");
    for(int32 I=0;I<65536;++I)
    {
        const double P=(I+.5)/65536,T=Q.Eval(P);
        TestTrue(TEXT("Native UE quantile curve is bounded and monotone"),T>=Previous&&T>=1&&T<=10);Previous=T;
        Thin+=T<2;Core+=T>=2&&T<=5;Thick+=T>=6;NearMinimum+=T<1.1;Mean+=T;
        if(I%64==0)CSV+=FString::Printf(TEXT("%.9f,%.9f\n"),P,T);
    }
    TestTrue(TEXT("About 1% thin beds, 5% thick beds and at least 88% between 2 and 5 metres"),FMath::Abs(Thin/65536.-.01)<.001&&FMath::Abs(Thick/65536.-.05)<.001&&Core/65536.>.88);
    TestTrue(TEXT("Beds near the 1 m minimum are especially rare"),NearMinimum<20);
    auto Shift=D;Shift.PeakMeters=4.5;const auto ShiftQ=Shift.QuantileCurve();TestTrue(TEXT("An authored peak moves the central population"),ShiftQ.Eval(.5)>Q.Eval(.5)+1);
    auto Broad=D;Broad.SpreadMeters=1.5;const auto BroadQ=Broad.QuantileCurve();TestTrue(TEXT("Spread independently broadens the middle half"),BroadQ.Eval(.75)-BroadQ.Eval(.25)>Q.Eval(.75)-Q.Eval(.25));
    auto Tails=D;Tails.ThinBedPercent=10;Tails.ThickBedPercent=15;const auto TailQ=Tails.QuantileCurve();TestTrue(TEXT("Tail probabilities are by bed count"),FMath::IsNearlyEqual(TailQ.Eval(.1),2.,1.e-5)&&FMath::IsNearlyEqual(TailQ.Eval(.85),6.,1.e-5));
    D.PeakMeters=7;TestFalse(TEXT("Peak outside the core fails closed"),D.Validate(Error));D=FCLRockBedDistribution();D.SpreadMeters=0;TestFalse(TEXT("Zero spread fails closed"),D.Validate(Error));
    const FString Folder=FPaths::ProjectSavedDir()/TEXT("Verification/rock-beds");IFileManager::Get().MakeDirectory(*Folder,true);TestTrue(TEXT("Native quantiles export"),FFileHelper::SaveStringToFile(CSV,*(Folder/TEXT("requested.csv"))));
    AddInfo(FString::Printf(TEXT("65536 native quantile probes: thin %.3f%%, 2–5 m %.3f%%, thick %.3f%%, mean %.3f m"),100*Thin/65536.,100*Core/65536.,100*Thick/65536.,Mean/65536));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLRockMetricScaleTest,"Catalyst.Landform.Rock.MetricScaleAndCompactHistory",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLRockMetricScaleTest::RunTest(const FString&)
{
    FString Error;auto R=FCLPlanetCompiler::ContinentalBase();R.UseGuidedGeology();const auto P=FCLPlanetCompiler::Compile(R,Error);if(!P){AddError(Error);return false;}
    FCLRockLayer L;L.bEnabled=true;L.ApplyProgression(.58);L.BedDistribution=FCLRockBedDistribution();L.DepositionalVariation=0;
    TArray<double> First;FString CSV=TEXT("radius_m,older_beds,total_beds,history_bytes,mean_m,thin_percent,core_2_5_percent,thick_percent\n");
    for(double Radius:{1000.,10000.,6371000.})
    {
        const auto C=FCLCompiledRock::Compile(L,1729,Radius,Radius*.9,Radius*.98,P->Surfaces.Get(),nullptr,Error);if(!C){AddError(Error);return false;}
        if(First.IsEmpty())for(int32 I=0;I<32;++I){const auto B=C->GetBed(I);First.Add(B.UpperMeters-B.LowerMeters);}
        else for(int32 I=0;I<32;++I){const auto B=C->GetBed(I);TestTrue(TEXT("Physical beds retain the same seeded thickness as radius changes"),FMath::IsNearlyEqual(B.UpperMeters-B.LowerMeters,First[I],1.e-8));}
        const auto Stats=C->ThicknessStatistics();int32 Thin=0,Core=0,Thick=0;
        for(int32 I=0;I<C->GetOlderBedCount()-1;++I)
        {
            const auto B=C->GetBed(I);const double T=B.UpperMeters-B.LowerMeters;Thin+=T<2;Core+=T>=2&&T<=5;Thick+=T>=6;
            TestTrue(TEXT("Individual metric beds are not split into thinner interbeds"),T>=1-1.e-8&&T<=10+1.e-8);
            if(I)TestTrue(TEXT("Physical contacts join and adjacent material identities remain visible"),C->GetBed(I-1).UpperMeters==B.LowerMeters&&C->GetBed(I-1).Unit!=B.Unit);
        }
        if(Radius>1000000)
        {
            TestTrue(TEXT("Earth-radius finite history passes the old 1024 limit with bounded compact storage"),C->GetBedCount()>1000000&&C->HistoryBytes()<64*1024*1024);
            TestTrue(TEXT("Stored physical beds retain the requested count distribution"),FMath::Abs(Thin/double(Stats.Count)-.01)<.003&&FMath::Abs(Thick/double(Stats.Count)-.05)<.003&&Core/double(Stats.Count)>.88);
        }
        CSV+=FString::Printf(TEXT("%.0f,%d,%d,%llu,%.9f,%.5f,%.5f,%.5f\n"),Radius,C->GetOlderBedCount(),C->GetBedCount(),C->HistoryBytes(),Stats.TotalMeters/Stats.Count,100.*Thin/Stats.Count,100.*Core/Stats.Count,100.*Thick/Stats.Count);
        const auto Point=FVector(0,0,Radius*.85);const auto A=C->Sample(Point);const auto Future=Async(EAsyncExecution::ThreadPool,[C,Point]{return C->Sample(Point);});TestEqual(TEXT("Worker lookup retains immutable stratum identity"),Future.Get().StratumId,A.StratumId);
    }
    auto TooMany=L;auto& D=TooMany.BedDistribution;D.MinimumMeters=.1;D.ThinTailUpperMeters=.2;D.PeakMeters=.3;D.SpreadMeters=.1;D.ThickTailLowerMeters=.4;D.MaximumMeters=.5;
    TestFalse(TEXT("Unrepresentable history fails explicitly without enlarging the physical beds"),FCLCompiledRock::Compile(TooMany,1729,1.e7,9.e6,9.8e6,P->Surfaces.Get(),nullptr,Error).IsValid());TestTrue(TEXT("Contact budget rejection identifies the limit"),Error.Contains(TEXT("4194304")));
    const FString Folder=FPaths::ProjectSavedDir()/TEXT("Verification/rock-beds");IFileManager::Get().MakeDirectory(*Folder,true);TestTrue(TEXT("Physical scale evidence exports"),FFileHelper::SaveStringToFile(CSV,*(Folder/TEXT("scale.csv"))));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLRockMetricBasinTest,"Catalyst.Landform.Rock.MetricBasinAndFoldContacts",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLRockMetricBasinTest::RunTest(const FString&)
{
    FString Error;auto R=FCLPlanetCompiler::ContinentalBase();R.UseGuidedGeology();const auto P=FCLPlanetCompiler::Compile(R,Error);if(!P){AddError(Error);return false;}
    FCLRockLayer L;L.bEnabled=true;L.ApplyProgression(.78);L.DepositionalVariation=0;L.DipDegrees=L.FoldAmplitudeRadiusFraction=L.FaultThrowRadiusFraction=0;L.TectonicResponse=1;
    const auto T=CLRockSectionPlates(false,Error,2,1,2);
    const auto C=FCLCompiledRock::Compile(L,1729,1000,640,800,P->Surfaces.Get(),T,Error);if(!C){AddError(Error);return false;}
    int32 Checked=0;
    for(double A:{-.12,-.06,0.,.06,.12})
    {
        const FVector D(FMath::Sin(A),0,FMath::Cos(A));const auto Col=C->SampleColumn(D);
        for(int32 I=C->GetOlderBedCount();I<C->GetBedCount();++I)
        {
            const auto B=C->GetBed(I);if(B.LowerMeters<=Col.YoungerFloorRadiusMeters||B.UpperMeters>=800)continue;
            const auto Low=C->Sample(D*(B.LowerMeters+.1)),High=C->Sample(D*(B.UpperMeters-.1));
            TestTrue(TEXT("Young beds keep their physical radial thickness across differing basin depths"),Low.StratumIndex==I&&High.StratumIndex==I);++Checked;
        }
    }
    AddInfo(FString::Printf(TEXT("Physical younger probes %d; central basin depth %.6f m"),Checked,C->SampleColumn(FVector::ZAxisVector).YoungerThicknessMeters));
    TestTrue(TEXT("The basin fixture actually exercises many younger physical beds"),Checked>=10);
    L.ApplyProgression(.58);L.DepositionalVariation=0;L.TectonicResponse=L.DipDegrees=L.FaultThrowRadiusFraction=0;L.FoldShorteningStrain=.15;L.FoldViscosityContrast=48;
    const auto F=FCLCompiledRock::Compile(L,1729,1000,640,800,P->Surfaces.Get(),CLRockSectionPlates(true,Error,2,1,1),Error);if(!F){AddError(Error);return false;}
    double H=0;int32 N=0;
    for(int32 I=0;I<F->GetOlderBedCount();++I){const auto B=F->GetBed(I);if(B.Unit==0){H+=B.UpperMeters-B.LowerMeters;++N;}}
    TestTrue(TEXT("Physical competent-bed thickness still sets native-context folding wavelength"),N>0&&FMath::IsNearlyEqual(F->GetFolding().WavelengthMeters,2*UE_DOUBLE_PI*(H/N)*2,1.e-8));
    for(int32 I=0;I<F->GetOlderBedCount();++I)
    {
        const auto B=F->GetBed(I);if(B.UpperMeters>760)continue;const double Mid=(B.LowerMeters+B.UpperMeters)*.5;
        for(double A:{-.05,0.,.05}){const FVector D(FMath::Sin(A),0,FMath::Cos(A));const double Shift=700-F->RestoredRadiusMeters(D*700);TestEqual(TEXT("Folded physical contacts preserve ordered canonical identities"),F->Sample(D*(Mid+Shift)).StratumIndex,int64(I));}
    }
    return true;
}
#endif
