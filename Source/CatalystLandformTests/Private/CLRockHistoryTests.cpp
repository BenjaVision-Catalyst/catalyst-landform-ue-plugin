#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLTectonics.h"

namespace
{
// Controlled two-plate cross section: the same compiled tectonic model supplies
// both the terrain displacement and bedding restoration. No alternate generator.
TSharedPtr<const FCLCompiledTectonics> SectionPlates(bool Compression,FString& Error)
{
    FCLTectonicPreset P;P.ApplyProgression(.8);P.BoundaryWarp=0;P.InteriorRelief=0;
    P.BeltWidthDegrees=10;P.Motion=1;P.Relief=.7;P.CollisionStrength=P.RiftStrength=1;
    const auto Model=FCLCompiledTectonics::Compile(P,1729,160,Error);
    if(!Model)return nullptr;
    auto T=ConstCastSharedPtr<FCLCompiledTectonics>(Model);
    const double Sign=Compression?1.:-1.;
    T->Lower.Plates={{FGuid(1,0,0,1),FVector::XAxisVector,FVector(0,-Sign,0)},
                     {FGuid(1,0,0,2),-FVector::XAxisVector,FVector(0,Sign,0)}};
    FCLCompiledTectonics::FBoundary B;B.Id=FGuid(1,0,0,3);B.A=0;B.B=1;
    B.Normal=FVector::XAxisVector;B.Mid=FVector::ZAxisVector;
    B.Start=FVector(0,-.7071067811865475,.7071067811865475);
    B.End=FVector(0,.7071067811865475,.7071067811865475);
    B.HalfCos=.7071067811865475;B.CapCos=0;
    T->Lower.Boundaries={B};T->Upper={};T->Controls.PlateTransition=0;
    return Model;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLRockHistoryTest,"Catalyst.Landform.Rock.FiniteHistoryAndPlateSections",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLRockHistoryTest::RunTest(const FString&)
{
    FString Error;auto R=FCLPlanetCompiler::ContinentalBase();R.UseGuidedGeology();
    const auto Planet=FCLPlanetCompiler::Compile(R,Error);if(!Planet){AddError(Error);return false;}
    FCLRockLayer L;L.bEnabled=true;L.ApplyProgression(.8);L.CrustDepthRadiusFraction=.15;
    L.BedThicknessRadiusFraction=.022;L.FoldAmplitudeRadiusFraction=.025;
    L.FoldWavelengthRadiusFraction=.14;L.DipDegrees=10;L.TectonicResponse=.4;
    L.FaultThrowRadiusFraction=.018;L.YoungerCoverRadiusFraction=0;
    const auto Flat=FCLCompiledRock::Compile(L,1729,1000,640,800,Planet->Surfaces.Get(),nullptr,Error);
    const auto Compression=SectionPlates(true,Error),Extension=SectionPlates(false,Error);
    if(!Flat||!Compression||!Extension){AddError(Error);return false;}
    const auto Folded=FCLCompiledRock::Compile(L,1729,1000,640,800,Planet->Surfaces.Get(),Compression,Error);
    const auto Basin=FCLCompiledRock::Compile(L,1729,1000,640,800,Planet->Surfaces.Get(),Extension,Error);
    L.YoungerCoverRadiusFraction=.04;
    const auto Young=FCLCompiledRock::Compile(L,1729,1000,640,800,Planet->Surfaces.Get(),Compression,Error);
    if(!Folded||!Basin||!Young){AddError(Error);return false;}
    const auto& Beds=Flat->GetBeds();TestTrue(TEXT("The deposited history has finite individual beds"),Beds.Num()>10&&Beds.Num()<100);
    double Previous=490;TSet<FGuid> Ids;
    for(int32 I=0;I<Beds.Num();++I)
    {
        const auto& B=Beds[I];TestEqual(TEXT("Deposition has no gap or overlap"),B.LowerMeters,Previous);
        TestTrue(TEXT("Every finite bed has positive thickness"),B.UpperMeters>B.LowerMeters);
        const double Mid=(B.UpperMeters+B.LowerMeters)*.5;const auto S=Flat->Sample(FVector(0,0,Mid));
        TestEqual(TEXT("Independent interval midpoint selects its own stratum"),S.StratumIndex,int64(I));Ids.Add(S.StratumId);Previous=B.UpperMeters;
        for(const FVector D:{FVector::XAxisVector,-FVector::XAxisVector,FVector::YAxisVector,-FVector::YAxisVector,-FVector::ZAxisVector})
            TestEqual(TEXT("Undeformed deposition is radial at both poles and all meridians"),Flat->Sample(D*Mid).StratumId,S.StratumId);
    }
    TestEqual(TEXT("Deposition ends at the declared datum"),Previous,800.);
    TestEqual(TEXT("Finite beds have distinct identities"),Ids.Num(),Beds.Num());
    TestEqual(TEXT("The core cannot repeat sediment cycles"),Flat->Sample(FVector(0,0,100)).StratumIndex,int64(-1));
    TestEqual(TEXT("No deformation in a plate interior"),Folded->RestoredRadiusMeters(FVector(700,0,0)),700.);
    const auto C=Compression->SampleRockDeformation(FVector::ZAxisVector,.14,0,.002);
    const auto E=Extension->SampleRockDeformation(FVector::ZAxisVector,.14,0,.002);
    TestTrue(TEXT("Compression buckles beds; extension does not invent compression folds"),C.Fold>.5&&E.Fold==0);
    const FVector Side=FVector(.07,0,1).GetSafeNormal();
    TestTrue(TEXT("The same plate boundary reverses fault throw for extension"),Compression->SampleRockDeformation(Side,.14,0,.002).Fault>0&&Extension->SampleRockDeformation(Side,.14,0,.002).Fault<0);
    const auto AtPole=Young->Sample(FVector(0,0,780));
    for(double Angle:{-.4,-.2,0.,.2,.4})
    {
        const FVector D(FMath::Sin(Angle),0,FMath::Cos(Angle));
        TestEqual(TEXT("Younger deposition stays undeformed above the truncation surface"),Young->Sample(D*780).StratumId,AtPole.StratumId);
    }
    int32 FoldContacts=0,BasinContacts=0;
    for(int32 I=0;I<200;++I)
    {
        const double A=-.4+.8*I/199.;const FVector D(FMath::Sin(A),0,FMath::Cos(A));
        FoldContacts+=Folded->Sample(D*700).StratumId!=Flat->Sample(D*700).StratumId;
        BasinContacts+=Basin->Sample(D*700).StratumId!=Flat->Sample(D*700).StratumId;
    }
    TestTrue(TEXT("Deformation exposes different pre-existing beds in compression and a subsiding basin"),FoldContacts>20&&BasinContacts>20);
    // Reject unbounded histories rather than silently coarsening an authored bed.
    L.BedThicknessRadiusFraction=.00001;TestFalse(TEXT("Excessive finite bed counts fail closed"),FCLCompiledRock::Compile(L,1729,1000,640,800,Planet->Surfaces.Get(),Compression,Error).IsValid());
    const FString Folder=FPaths::ProjectSavedDir()/TEXT("Verification/rock-history");IFileManager::Get().MakeDirectory(*Folder,true);
    const TSharedPtr<const FCLCompiledRock> Models[]={Flat,Folded,Basin,Young};
    const TCHAR* Names[]={TEXT("radial"),TEXT("compression"),TEXT("subsiding-basin"),TEXT("unconformity")};
    for(int32 K=0;K<4;++K)
    {
        FString CSV=TEXT("x_m,z_m,unit,stratum,solid\n");
        for(int32 Z=0;Z<=160;++Z)for(int32 X=0;X<=320;++X)
        {
            const FVector P(-380+760*X/320.,0,480+320*Z/160.);const auto S=Models[K]->Sample(P);
            const int32 Unit=R.Rock.Units.IndexOfByPredicate([&](const FCLRockUnit& U){return U.Id==S.UnitId;});
            CSV+=FString::Printf(TEXT("%.6f,%.6f,%d,%lld,%d\n"),P.X,P.Z,Unit,S.StratumIndex,P.Length()<=790?1:0);
        }
        TestTrue(TEXT("Canonical geological cross section exports"),FFileHelper::SaveStringToFile(CSV,*(Folder/(FString(Names[K])+TEXT(".csv")))));
    }
    AddInfo(FString::Printf(TEXT("Four canonical sections; %d finite older beds; %d compression and %d basin exposure probes"),Beds.Num(),FoldContacts,BasinContacts));
    return true;
}
#endif
