#if WITH_DEV_AUTOMATION_TESTS
#include "Model/CLPlanetRecipe.h"
#include "Model/CLSurfaceRange.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

namespace
{
TArray<FVector> CharacterDirections()
{
    TArray<FVector> D;
    for(int32 I=0;I<2048;++I)
    {
        const double Z=1-2*(I+.5)/2048.,A=I*2.399963229728653;
        D.Emplace(FMath::Sqrt(1-Z*Z)*FMath::Cos(A),FMath::Sqrt(1-Z*Z)*FMath::Sin(A),Z);
    }
    return D;
}
FCLPlanetRecipe CharacterRecipe(int32 Seed,bool Continents)
{
    auto R=FCLPlanetCompiler::ContinentalBase();R.UseGuidedGeology();
    R.Seed=Seed;R.Continents.bEnabled=Continents;R.Continents.ApplyProgression(.55);
    R.Tectonics.bEnabled=true;
    return R;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLTectonicZeroTest,"Catalyst.Landform.TectonicCharacter.ZeroAndChannelTiming",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLTectonicZeroTest::RunTest(const FString&)
{
    FString Error;const auto D=CharacterDirections();
    for(int32 Seed:{1729,817,529})for(bool Continents:{false,true})
    {
        auto R=CharacterRecipe(Seed,Continents);R.Tectonics.ApplyProgression(0);
        const auto Zero=FCLPlanetCompiler::Compile(R,Error);if(!Zero){AddError(Error);return false;}
        R.Tectonics.bEnabled=false;const auto Off=FCLPlanetCompiler::Compile(R,Error);if(!Off){AddError(Error);return false;}
        TestFalse(TEXT("Zero has no latent tectonic contribution"),Zero->Tectonics->HasEffect());
        TArray<double> A,B;Zero->SampleHeightsMeters(D,A);Off->SampleHeightsMeters(D,B);
        double Difference=0;for(int32 I=0;I<A.Num();++I)Difference=FMath::Max(Difference,FMath::Abs(A[I]-B[I]));
        TestTrue(TEXT("Zero is geometrically identical to disabling tectonics, including final range fitting"),Difference<1.e-9);
        if(!Continents)TestFalse(TEXT("A sole zero-effect layer retains the existing empty-sphere behavior"),Zero->SurfaceRange.IsValid());
    }
    FCLTectonicPreset P;P.ApplyProgression(.15);
    TestTrue(TEXT("Early deformation has no premature collision/fold/arc channels"),P.CollisionStrength==0&&P.FoldStrength==0&&P.ArcStrength==0&&P.TransformStrength==0&&P.Roughness==0);
    P.ApplyProgression(.38);const double RiftPeak=P.RiftStrength;
    TestTrue(TEXT("Rift phase precedes fold and trench development"),P.RiftStrength>1&&P.FoldStrength==0&&P.TrenchStrength==0);
    P.ApplyProgression(.6);const double FaultPlateau=P.TransformStrength;
    P.ApplyProgression(.45);TestEqual(TEXT("Fault strength holds through its authored interval"),P.TransformStrength,FaultPlateau);
    P.ApplyProgression(.8);TestTrue(TEXT("Rifts recede while mature folded belts grow"),P.RiftStrength<RiftPeak&&P.FoldStrength>.75);
    P.ApplyProgression(.92);const double ArcPlateau=P.ArcStrength;
    P.ApplyProgression(1);TestEqual(TEXT("Arc strength stops changing before the endpoint"),P.ArcStrength,ArcPlateau);
    TestTrue(TEXT("Endpoint strengthens fold structure independently of capped arc/fault channels"),P.FoldStrength>1.5&&P.Roughness>.7);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLTectonicDensePathTest,"Catalyst.Landform.TectonicCharacter.DensePathContinuity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLTectonicDensePathTest::RunTest(const FString&)
{
    FString Error;const auto AllDirections=CharacterDirections();TArray<FVector> Directions;for(int32 I=0;I<AllDirections.Num();I+=16)Directions.Add(AllDirections[I]);
    double LargestStep=0;int32 Samples=0,WorstSeed=0,WorstCrust=0,WorstStep=0,WorstDirection=0;
    for(int32 Seed:{1729,817,529})for(bool Continents:{false,true})
    {
        auto R=CharacterRecipe(Seed,Continents);R.Tectonics.ApplyProgression(0);
        const auto Base=FCLPlanetCompiler::Compile(R,Error);if(!Base){AddError(Error);return false;}
        TArray<double> Previous;
        for(int32 Step=0;Step<=1000;++Step)
        {
            R.Tectonics.ApplyProgression(Step/1000.);
            const auto T=FCLCompiledTectonics::Compile(R.Tectonics,Seed,300,Error,true,Base->Continents,true);
            if(!T){AddError(Error);return false;}
            TArray<double> Values;
            for(int32 I=0;I<Directions.Num();++I)
            {
                const double H=T->SampleDeltaMeters(Directions[I]);
                TestTrue(TEXT("Every intermediate setting produces finite geometry"),FMath::IsFinite(H));
                if(!Previous.IsEmpty()&&FMath::Abs(H-Previous[I])/300>LargestStep)
                {LargestStep=FMath::Abs(H-Previous[I])/300;WorstSeed=Seed;WorstCrust=Continents;WorstStep=Step;WorstDirection=I;}
                Values.Add(H);++Samples;
            }
            Previous=MoveTemp(Values);
        }
    }
    TestTrue(TEXT("Plate additions and effect entries introduce no raw field jumps"),LargestStep<.025);
    AddInfo(FString::Printf(TEXT("1001 slider positions / 3 seeds / 2 crust contexts / %d raw samples; largest 0.1%% step %.8f of span at seed %d crust %d step %d direction %d"),Samples,LargestStep,WorstSeed,WorstCrust,WorstStep,WorstDirection));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLTectonicPortfolioTest,"Catalyst.Landform.TectonicCharacter.FinalSurfacePortfolio",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLTectonicPortfolioTest::RunTest(const FString&)
{
    FString Error,CSV=TEXT("seed,continents,position,shape_rms_span,slopes_p95_degrees,plates,next_plate_blend,motion,rift,collision,fold,arc,trench\n");
    const auto D=CharacterDirections();double MinimumChange=1;int32 Cases=0;
    for(int32 Seed:{1729,817,529})for(bool Continents:{false,true})
    {
        TArray<double> Previous;
        for(double Position:{0.,.15,.30,.45,.60,.75,.90,1.})
        {
            auto R=CharacterRecipe(Seed,Continents);R.Tectonics.ApplyProgression(Position);
            const auto P=FCLPlanetCompiler::Compile(R,Error);if(!P){AddError(Error);return false;}
            TArray<double> H;P->SampleHeightsMeters(D,H);
            const auto* Low=P->Guides.FindByPredicate([](const auto& G){return G.Name==TEXT("SurfaceMin");});
            const auto* High=P->Guides.FindByPredicate([](const auto& G){return G.Name==TEXT("SurfaceMax");});
            const double Span=High->HeightMeters-Low->HeightMeters;
            double Difference=0;TArray<FVector> Neighbors;
            for(int32 I=0;I<D.Num();++I)
            {
                if(!Previous.IsEmpty())Difference+=FMath::Square((H[I]-Previous[I])/Span);
                FVector X,Y;D[I].FindBestAxisVectors(X,Y);Neighbors.Add((D[I]+X*.002).GetSafeNormal());
                TestTrue(TEXT("Every final sample is finite"),FMath::IsFinite(H[I]));
            }
            const double RMS=Previous.IsEmpty()?0:FMath::Sqrt(Difference/D.Num());
            if(!Previous.IsEmpty())
            {
                MinimumChange=FMath::Min(MinimumChange,RMS);
                TestTrue(TEXT("Each authored interval changes final terrain after normalization, including the first 80 percent"),RMS>.005);
            }
            TArray<double> NH,Slopes;P->SampleHeightsMeters(Neighbors,NH);
            for(int32 I=0;I<H.Num();++I)Slopes.Add(FMath::RadiansToDegrees(FMath::Atan(FMath::Abs(NH[I]-H[I])/(.002*(P->RadiusMeters+H[I])))));
            Slopes.Sort();const double P95=Slopes[FMath::FloorToInt(Slopes.Num()*.95)];
            if(P->SurfaceRange)
            {
                TestEqual(TEXT("Surface minimum remains exact"),P->SampleHeightMeters(P->SurfaceRange->MinimumDirection),Low->HeightMeters);
                TestEqual(TEXT("Surface maximum remains exact"),P->SampleHeightMeters(P->SurfaceRange->MaximumDirection),High->HeightMeters);
            }
            const auto& T=R.Tectonics;
            CSV+=FString::Printf(TEXT("%d,%d,%.2f,%.8f,%.4f,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n"),Seed,int32(Continents),Position,RMS,P95,T.PlateCount,T.PlateTransition,T.Motion,T.RiftStrength,T.CollisionStrength,T.FoldStrength,T.ArcStrength,T.TrenchStrength);
            Previous=MoveTemp(H);++Cases;
        }
    }
    const FString Folder=FPaths::ProjectSavedDir()/TEXT("Verification/tectonic-character");IFileManager::Get().MakeDirectory(*Folder,true);
    FFileHelper::SaveStringToFile(CSV,*(Folder/TEXT("portfolio.csv")));
    AddInfo(FString::Printf(TEXT("%d final terrain cases, 3 seeds with/without continents: smallest consecutive shape change %.6f of guide span"),Cases,MinimumChange));
    return true;
}
#endif
