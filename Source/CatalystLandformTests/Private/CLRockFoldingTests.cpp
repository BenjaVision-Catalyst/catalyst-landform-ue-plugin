#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Model/CLPlanetRecipe.h"
#include "CLRockSectionFixtures.h"
#include "Interfaces/IPluginManager.h"
#include "JsonObjectConverter.h"
#include "Scene/CLScene.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLRockFoldLawTest,"Catalyst.Landform.Rock.FoldThicknessAndViscosityLaw",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLRockFoldLawTest::RunTest(const FString&)
{
    FString Error;FCLRockFolding Base,Thick,Strong,Neutral;
    TestTrue(TEXT("Reference thin-layer law resolves"),FCLRockFolding::Resolve(10,48,.1,100,Base,Error));
    TestTrue(TEXT("Twice-thick layer resolves"),FCLRockFolding::Resolve(20,48,.1,100,Thick,Error));
    TestTrue(TEXT("Eightfold viscosity contrast resolves"),FCLRockFolding::Resolve(10,384,.1,100,Strong,Error));
    TestTrue(TEXT("Neutral material resolves without an invented instability"),FCLRockFolding::Resolve(10,1,.1,100,Neutral,Error));
    TestTrue(TEXT("Independent 10 m / ratio-48 wavelength oracle is 40 pi metres"),FMath::IsNearlyEqual(Base.WavelengthMeters,125.66370614359172,1.e-9));
    TestTrue(TEXT("Independent amplification-rate oracle is 16"),FMath::IsNearlyEqual(Base.AmplificationRate,16.,1.e-10));
    TestTrue(TEXT("Independent added-growth oracle"),FMath::IsNearlyEqual(Base.AmplitudeMeters(1),1.1859097273185345,1.e-9));
    TestTrue(TEXT("Doubling thickness doubles dominant wavelength"),FMath::IsNearlyEqual(Thick.WavelengthMeters,2*Base.WavelengthMeters,1.e-9));
    TestTrue(TEXT("An eightfold contrast change doubles dominant wavelength"),FMath::IsNearlyEqual(Strong.WavelengthMeters,2*Base.WavelengthMeters,1.e-9));
    TestTrue(TEXT("Stronger contrast increases bounded growth"),Strong.AmplitudeMeters(1)>Base.AmplitudeMeters(1));
    TestTrue(TEXT("Neutral contrast has no buckling wavelength or growth"),!Neutral.bActive&&Neutral.WavelengthMeters==0&&Neutral.AmplitudeMeters(1)==0);
    TestEqual(TEXT("No compression adds no fold"),Base.AmplitudeMeters(0),0.);
    for(double H:{.001,1.,10.,10000.})for(double Contrast:{10.,48.,64.,384.,1000.})
    {
        FCLRockFolding F;TestTrue(TEXT("Bounded physical parameters resolve"),FCLRockFolding::Resolve(H,Contrast,.15,500000,F,Error));
        double Previous=0;
        for(int32 I=0;I<=32;++I)
        {
            const double A=F.AmplitudeMeters(I/32.);TestTrue(TEXT("Growth is finite, monotone and within the early-stage amplitude ceiling"),FMath::IsFinite(A)&&A>=Previous&&A<=F.WavelengthMeters*.05+1.e-9);Previous=A;
        }
    }
    const auto Accepted=Base;TestFalse(TEXT("Unsupported low-contrast thin-layer approximation fails closed"),FCLRockFolding::Resolve(10,5,.1,100,Base,Error));
    TestEqual(TEXT("Rejected mechanics preserve the previous result"),Base.WavelengthMeters,Accepted.WavelengthMeters);
    TestFalse(TEXT("Unbounded shortening is rejected"),FCLRockFolding::Resolve(10,64,.2,100,Base,Error));
    AddInfo(TEXT("Independent Biot wavelength/growth oracles, 660 bounded amplitude samples, thickness and contrast scaling pass"));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLRockFoldVolumeTest,"Catalyst.Landform.Rock.SharedFoldCoordinateAndSections",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLRockFoldVolumeTest::RunTest(const FString&)
{
    FString Error;auto R=FCLPlanetCompiler::ContinentalBase();R.UseGuidedGeology();
    const auto P=FCLPlanetCompiler::Compile(R,Error);if(!P){AddError(Error);return false;}
    FCLRockLayer L;L.bEnabled=true;L.ApplyProgression(.9);L.AlgorithmVersion=5;L.DepositionalVariation=0;
    L.FoldViscosityContrast=48;L.FoldShorteningStrain=.15;
    L.FoldAmplitudeRadiusFraction=.05;L.TectonicResponse=L.DipDegrees=L.FaultThrowRadiusFraction=0;
    const auto Compression=CLRockSectionPlates(true,Error,2,1,1),Extension=CLRockSectionPlates(false,Error,2,1,1);
    const auto Thin=FCLCompiledRock::Compile(L,1729,1000,640,800,P->Surfaces.Get(),Compression,Error);
    auto T=L;T.BedDistribution.MinimumMeters*=2;T.BedDistribution.ThinTailUpperMeters*=2;T.BedDistribution.PeakMeters*=2;T.BedDistribution.SpreadMeters*=2;T.BedDistribution.ThickTailLowerMeters*=2;T.BedDistribution.MaximumMeters*=2;const auto Thick=FCLCompiledRock::Compile(T,1729,1000,640,800,P->Surfaces.Get(),Compression,Error);
    auto S=L;S.FoldViscosityContrast=384;const auto Strong=FCLCompiledRock::Compile(S,1729,1000,640,800,P->Surfaces.Get(),Compression,Error);
    auto N=L;N.FoldViscosityContrast=1;const auto Neutral=FCLCompiledRock::Compile(N,1729,1000,640,800,P->Surfaces.Get(),Compression,Error);
    const auto Rift=FCLCompiledRock::Compile(L,1729,1000,640,800,P->Surfaces.Get(),Extension,Error);
    if(!Thin||!Thick||!Strong||!Neutral||!Rift){AddError(Error);return false;}
    TestTrue(TEXT("Actual finite competent beds set the effective thickness and wavelength"),Thick->GetFolding().CompetentThicknessMeters>1.5*Thin->GetFolding().CompetentThicknessMeters&&Thick->GetFolding().WavelengthMeters>1.5*Thin->GetFolding().WavelengthMeters);
    const auto& Mechanic=Thin->GetFolding();const double AngularPeriod=Mechanic.WavelengthMeters/Mechanic.ReferenceRadiusMeters;
    const FVector D0=FVector::ZAxisVector,D1(FMath::Sin(AngularPeriod),0,FMath::Cos(AngularPeriod));
    const double A0=Mechanic.AmplitudeMeters(Compression->SampleRockDeformation(D0,AngularPeriod,0,.002).Compression);
    const double A1=Mechanic.AmplitudeMeters(Compression->SampleRockDeformation(D1,AngularPeriod,0,.002).Compression);
    const double Mode0=(700-Thin->RestoredRadiusMeters(D0*700))/A0,Mode1=(700-Thin->RestoredRadiusMeters(D1*700))/A1;
    TestTrue(TEXT("The actual restored field repeats after one physical wavelength at the declared competent-bed reference radius"),A0>0&&A1>0&&FMath::IsNearlyEqual(Mode0,Mode1,1.e-6));
    TestTrue(TEXT("Eightfold material contrast changes the shared wavelength, not bed identity"),FMath::IsNearlyEqual(Strong->GetFolding().WavelengthMeters,2*Thin->GetFolding().WavelengthMeters,1.e-9));
    auto Hardness=L;for(auto& U:Hardness.Units){U.MiningResistance=.1;U.Erodibility=.9;}
    const auto OtherCost=FCLCompiledRock::Compile(Hardness,1729,1000,640,800,P->Surfaces.Get(),Compression,Error);if(!OtherCost){AddError(Error);return false;}
    TestEqual(TEXT("Mining hardness is not silently used as viscosity"),OtherCost->GetFolding().WavelengthMeters,Thin->GetFolding().WavelengthMeters);
    int32 Bent=0;
    for(int32 I=0;I<=512;++I)
    {
        const double A=-.6+1.2*I/512.;const FVector D(FMath::Sin(A),0,FMath::Cos(A));
        const double Low=Thin->RestoredRadiusMeters(D*600),High=Thin->RestoredRadiusMeters(D*700);
        TestTrue(TEXT("All horizons share one radial restoration with positive unit derivative; contacts cannot cross"),FMath::IsNearlyEqual(High-Low,100.,1.e-9));
        TestTrue(TEXT("No folding in the extensional control"),FMath::IsNearlyEqual(Rift->RestoredRadiusMeters(D*700),700.,1.e-9));
        TestTrue(TEXT("No buckling in a neutral-rheology sequence"),FMath::IsNearlyEqual(Neutral->RestoredRadiusMeters(D*700),700.,1.e-9));
        TestEqual(TEXT("Changing only mining properties keeps structural deformation exact"),OtherCost->RestoredRadiusMeters(D*700),Thin->RestoredRadiusMeters(D*700));
        Bent+=FMath::Abs(High-700)>.01;
    }
    TestTrue(TEXT("A compressed layered sequence actually folds"),Bent>100);
    const FVector Interior(1,0,0);TestEqual(TEXT("No added fold outside the finite compression belts"),Thin->RestoredRadiusMeters(Interior*700),700.);
    int32 Checked=0;
    for(int32 I=0;I<Thin->GetOlderBedCount();++I)
    {
        const auto Bed=Thin->GetBed(I); if(Bed.UpperMeters>=760)continue;
        const double Mid=(Bed.LowerMeters+Bed.UpperMeters)*.5;
        for(double A:{-.15,0.,.15})
        {
            const FVector D(FMath::Sin(A),0,FMath::Cos(A));const double Shift=700-Thin->RestoredRadiusMeters(D*700);
            const auto G=Thin->Sample(D*(Mid+Shift));TestEqual(TEXT("Each deformed finite bed retains its canonical identity"),G.StratumIndex,int64(I));++Checked;
        }
    }
    auto Bad=L;Bad.FoldCompetentUnit=FGuid(1,2,3,4);TestFalse(TEXT("An unmapped mechanical unit fails closed"),FCLCompiledRock::Compile(Bad,1729,1000,640,800,P->Surfaces.Get(),Compression,Error).IsValid());
    const FString Folder=FPaths::ProjectSavedDir()/TEXT("Verification/rock-folding");IFileManager::Get().MakeDirectory(*Folder,true);
    const TSharedPtr<const FCLCompiledRock> Models[]={Thin,Thick,Strong,Neutral};const TCHAR* Names[]={TEXT("thin"),TEXT("thick"),TEXT("strong"),TEXT("neutral")};
    FString Metrics=TEXT("case,competent_h_m,viscosity_ratio,wavelength_m,amplitude_limit_m,reference_radius_m\n");
    for(int32 K=0;K<4;++K)
    {
        const auto& F=Models[K]->GetFolding();Metrics+=FString::Printf(TEXT("%s,%.12f,%.12f,%.12f,%.12f,%.12f\n"),Names[K],F.CompetentThicknessMeters,F.ViscosityContrast,F.WavelengthMeters,F.AmplitudeLimitMeters,F.ReferenceRadiusMeters);
        FString CSV=TEXT("x_m,z_m,unit,stratum,solid\n");
        for(int32 Z=0;Z<=200;++Z)for(int32 X=0;X<=400;++X)
        {
            const FVector V(-260+520*X/400.,0,540+240*Z/200.);const auto G=Models[K]->Sample(V);
            const int32 Unit=L.Units.IndexOfByPredicate([&](const FCLRockUnit& U){return U.Id==G.UnitId;});
            CSV+=FString::Printf(TEXT("%.6f,%.6f,%d,%lld,%d\n"),V.X,V.Z,Unit,G.StratumIndex,V.Length()<=780?1:0);
        }
        TestTrue(TEXT("Canonical folding section exports"),FFileHelper::SaveStringToFile(CSV,*(Folder/(FString(Names[K])+TEXT(".csv")))));
    }
    TestTrue(TEXT("Resolved fold mechanics export"),FFileHelper::SaveStringToFile(Metrics,*(Folder/TEXT("mechanics.csv"))));
    AddInfo(FString::Printf(TEXT("513 radial-order/neutral/extension probes; %d material-bed probes; effective h %.6f m, wavelength %.6f m, %d bent probes"),Checked,Thin->GetFolding().CompetentThicknessMeters,Thin->GetFolding().WavelengthMeters,Bent));return true;
}

#endif
