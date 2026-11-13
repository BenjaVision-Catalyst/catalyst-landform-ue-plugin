#if WITH_DEV_AUTOMATION_TESTS
#include "Model/CLPlanetRecipe.h"
#include "Model/CLSurfaceRange.h"
#include "Model/CLSphericalPatches.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Serialization/JsonSerializer.h"
#include "Async/Async.h"
#include "CLPlanetStamp.h"
#include "VoxelLayers.h"
#include "VoxelQuery.h"
#include "Surface/VoxelSurfaceTypeTable.h"
namespace
{
TArray<FVector> Directions(int32 N)
{
    TArray<FVector> D;
    for(int32 I=0;I<N;++I){const double Z=1-2*(I+.5)/N,A=I*2.399963229728653;D.Emplace(FMath::Cos(A)*FMath::Sqrt(1-Z*Z),FMath::Sin(A)*FMath::Sqrt(1-Z*Z),Z);}
    return D;
}
FCLPlanetRecipe Character(int32 Seed,double Position,double Tectonic)
{
    auto R=FCLPlanetCompiler::ContinentalBase();R.UseGuidedGeology();R.Seed=Seed;
    R.Continents.bEnabled=true;R.Continents.ApplyProgression(Position);R.Tectonics.bEnabled=Tectonic>=0;R.Tectonics.ApplyProgression(FMath::Max(0.,Tectonic));return R;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLContinentalCharacterVersionTest,"Catalyst.Landform.ContinentalCharacter.VersionAndZero",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLContinentalCharacterVersionTest::RunTest(const FString&)
{
    FString Error;const auto D=Directions(256);
    for(int32 Seed:{1729,817,529})for(double Tectonic:{-1.,.45,.85})
    {
        auto R=Character(Seed,0,Tectonic);const auto Zero=FCLPlanetCompiler::Compile(R,Error);R.Continents.bEnabled=false;const auto Off=FCLPlanetCompiler::Compile(R,Error);
        if(!Zero||!Off){AddError(Error);return false;}TestFalse(TEXT("Zero has no latent continental datum/shape"),Zero->Continents->HasEffect());
        TArray<double> A,B;Zero->SampleHeightsMeters(D,A);Off->SampleHeightsMeters(D,B);double Worst=0;for(int32 I=0;I<A.Num();++I)Worst=FMath::Max(Worst,FMath::Abs(A[I]-B[I]));
        TestTrue(TEXT("Zero matches disabled continents, including tectonic classification and final fitting"),Worst<1.e-9);
        if(Tectonic<0)TestFalse(TEXT("A sole zero layer keeps the empty sphere"),Zero->SurfaceRange.IsValid());
    }
    FCLContinentalLayer Small;Small.ApplyProgression(.0001);
    const auto Entering=FCLCompiledContinents::Compile(Small,1729,140,Error,true);if(!Entering){AddError(Error);return false;}
    for(int32 Version:{2,4,5,6})
    {
        FCLTectonicPreset T;T.ApplyProgression(.45);T.AlgorithmVersion=Version;
        const auto Base=FCLCompiledTectonics::Compile(T,1729,140,Error,true,nullptr,true);
        const auto Near=FCLCompiledTectonics::Compile(T,1729,140,Error,true,Entering,true);
        if(!Base||!Near){AddError(Error);return false;}double Change=0;
        for(const auto& Q:D)Change=FMath::Max(Change,FMath::Abs(Base->SampleDeltaMeters(Q)-Near->SampleDeltaMeters(Q)));
        TestTrue(TEXT("Continental entry blends from each pinned tectonic version's own neutral crust"),Change<.001);
    }
    auto R=Character(1729,.55,.45);auto L=R.Continents;L.bEnabled=true;L.Seed=817;L.ApplyProgression(1);
    TestTrue(TEXT("Knob preserves seed and enabled state"),L.bEnabled&&L.Seed==817);
    TestTrue(TEXT("Endpoint fragments established continental masses"),L.DomainCount>R.Continents.DomainCount&&L.Fragmentation>R.Continents.Fragmentation&&L.Extent<R.Continents.Extent);
    L.DomainCount=8;L.DomainTransition=.2;TestFalse(TEXT("No hidden ninth domain"),FCLCompiledContinents::Compile(L,1729,140,Error,true).IsValid());
    L=R.Continents;L.AlgorithmVersion=99;TestFalse(TEXT("Unknown versions fail closed"),FCLCompiledContinents::Compile(L,1729,140,Error,true).IsValid());
    L=R.Continents;L.Fragmentation=2;TestFalse(TEXT("Invalid shape settings fail closed"),FCLCompiledContinents::Compile(L,1729,140,Error,true).IsValid());
    R.SchemaVersion=16;TestFalse(TEXT("New profile cannot enter legacy guide semantics"),FCLPlanetCompiler::Compile(R,Error).IsValid());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLContinentalCharacterSweepTest,"Catalyst.Landform.ContinentalCharacter.DenseContinuity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLContinentalCharacterSweepTest::RunTest(const FString&)
{
    FString Error;const auto D=Directions(256);double Worst=0,CrustStep=0;int32 Samples=0,WorstPosition=0,WorstSeed=0,WorstDirection=0;
    for(int32 Seed:{1729,817,529})
    {
        TArray<double> Previous,PreviousCrust;
        for(int32 Step=0;Step<=1000;++Step)
        {
            FCLContinentalLayer L;L.ApplyProgression(Step/1000.);const auto P=FCLCompiledContinents::Compile(L,Seed,140,Error,true,17);if(!P){AddError(Error);return false;}
            TArray<double> H,C;
            for(int32 I=0;I<D.Num();++I)
            {
                const double V=P->SampleDeltaMeters(D[I]),Crust=P->CrustContext(D[I],.75);
                if(!FMath::IsFinite(V)||!FMath::IsFinite(Crust)||Crust<0||Crust>1){AddError(TEXT("Nonfinite or unbounded continental field"));return false;}
                if(!Previous.IsEmpty()){if(FMath::Abs(V-Previous[I])/140>Worst){Worst=FMath::Abs(V-Previous[I])/140;WorstPosition=Step;WorstSeed=Seed;WorstDirection=I;}CrustStep=FMath::Max(CrustStep,FMath::Abs(Crust-PreviousCrust[I]));}
                H.Add(V);C.Add(Crust);++Samples;
            }
            Previous=MoveTemp(H);PreviousCrust=MoveTemp(C);
        }
    }
    TestTrue(TEXT("No raw height pop at domain count transitions"),Worst<.015);TestTrue(TEXT("No latent crust pop as continental shape enters"),CrustStep<.02);
    AddInfo(FString::Printf(TEXT("1001 positions / 3 seeds / %d probes: maximum 0.1%% height step %.8f of span, crust step %.8f, worst at seed %d step %d direction %d"),Samples,Worst,CrustStep,WorstSeed,WorstPosition,WorstDirection));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLContinentalCharacterPortfolioTest,"Catalyst.Landform.ContinentalCharacter.FinalPortfolioAndNativeParity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLContinentalCharacterPortfolioTest::RunTest(const FString&)
{
    FString Error,CSV=TEXT("seed,tectonic,position,shape_rms_span,land_fraction,slopes_p95,domains,next_domain,extent,elongation,fragmentation\n");const auto D=Directions(2048);double MinimumChange=1,MaxNativeError=0;int32 Cases=0;
    for(int32 Seed:{1729,817,529})for(double Tectonic:{-1.,.45,.85})
    {
        TArray<double> Previous;
        for(double Position:{0.,.15,.30,.45,.60,.75,.90,1.})
        {
            auto R=Character(Seed,Position,Tectonic);const auto P=FCLPlanetCompiler::Compile(R,Error);if(!P){AddError(Error);return false;}
            auto Other=R;Other.Continents.Seed+=817;const auto Vary=FCLPlanetCompiler::Compile(Other,Error);if(!Vary){AddError(Error);return false;}
            if(P->Tectonics)for(int32 I=0;I<P->Tectonics->Lower.Plates.Num();++I)TestTrue(TEXT("Continental variation retains plate identity and motion"),P->Tectonics->Lower.Plates[I].Id==Vary->Tectonics->Lower.Plates[I].Id&&P->Tectonics->Lower.Plates[I].Site==Vary->Tectonics->Lower.Plates[I].Site&&P->Tectonics->Lower.Plates[I].AngularVelocity==Vary->Tectonics->Lower.Plates[I].AngularVelocity);
            TArray<double> H;P->SampleHeightsMeters(D,H);TArray<FVector> Neighbors;double Difference=0;int32 Land=0;
            const auto* Low=P->Guides.FindByPredicate([](const auto& G){return G.Name==TEXT("SurfaceMin");});const auto* High=P->Guides.FindByPredicate([](const auto& G){return G.Name==TEXT("SurfaceMax");});const auto* Sea=P->Guides.FindByPredicate([](const auto& G){return G.Name==TEXT("OceanHeight");});const double Span=High->HeightMeters-Low->HeightMeters;
            for(int32 I=0;I<D.Num();++I){if(!Previous.IsEmpty())Difference+=FMath::Square((H[I]-Previous[I])/Span);Land+=H[I]>Sea->HeightMeters;FVector X,Y;D[I].FindBestAxisVectors(X,Y);Neighbors.Add((D[I]+X*.002).GetSafeNormal());}
            const double RMS=Previous.IsEmpty()?0:FMath::Sqrt(Difference/D.Num());if(!Previous.IsEmpty()){MinimumChange=FMath::Min(MinimumChange,RMS);TestTrue(TEXT("Each reviewed interval changes final shape after guide fitting"),RMS>.003);}
            TArray<double> NH,Slopes;P->SampleHeightsMeters(Neighbors,NH);for(int32 I=0;I<H.Num();++I)Slopes.Add(FMath::RadiansToDegrees(FMath::Atan(FMath::Abs(NH[I]-H[I])/(.002*(P->RadiusMeters+H[I])))));Slopes.Sort();
            if(P->SurfaceRange){TestEqual(TEXT("Minimum guide remains exact"),P->SampleHeightMeters(P->SurfaceRange->MinimumDirection),Low->HeightMeters);TestEqual(TEXT("Maximum guide remains exact"),P->SampleHeightMeters(P->SurfaceRange->MaximumDirection),High->HeightMeters);}
            const auto& L=R.Continents;CSV+=FString::Printf(TEXT("%d,%.2f,%.2f,%.8f,%.6f,%.4f,%d,%.6f,%.6f,%.6f,%.6f\n"),Seed,Tectonic,Position,RMS,double(Land)/H.Num(),Slopes[FMath::FloorToInt(Slopes.Num()*.95)],L.DomainCount,L.DomainTransition,L.Extent,L.Elongation,L.Fragmentation);
            if(Position==.60&&Tectonic==.45)
            {
                FCLPlanetStamp Stamp;Stamp.bUseRecipe=true;Stamp.Radius=R.RadiusMeters*100;Stamp.Recipe=R;const auto Ref=FVoxelStampRef::New(Stamp);const auto Native=StaticCastSharedPtr<FCLPlanetStampRuntime>(FVoxelStampRuntime::Create(nullptr,Ref,nullptr));if(!Native){AddError(TEXT("Native stamp did not compile"));return false;}
                FVoxelDoubleVectorBuffer Points;Points.Allocate(D.Num());for(int32 I=0;I<D.Num();++I)Points.Set(I,D[I]*(R.RadiusMeters+H[I])*100);
                FVoxelDependencyCollector Dependencies(STATIC_FNAME("CL.ContinentalCharacter"));const auto Table=FVoxelSurfaceTypeTable::Get();FVoxelQuery Query(0,FVoxelLayers::Empty(),*Table,Dependencies);const auto Field=Native->SampleDistances(Query,Points);for(int32 I=0;I<D.Num();++I)MaxNativeError=FMath::Max(MaxNativeError,FMath::Abs(double(Field[I])));
                const auto Worker=Async(EAsyncExecution::ThreadPool,[P,D]{TArray<double> Out;P->SampleHeightsMeters(D,Out);return Out;});const auto Values=Worker.Get();TestEqual(TEXT("Immutable worker matches the accepted native field"),Values[97],H[97]);
            }
            Previous=MoveTemp(H);++Cases;
        }
    }
    const FString Folder=FPaths::ProjectSavedDir()/TEXT("Verification/continental-character");IFileManager::Get().MakeDirectory(*Folder,true);FFileHelper::SaveStringToFile(CSV,*(Folder/TEXT("portfolio.csv")));
    TestTrue(TEXT("Native VPP zero set remains within 0.02 cm"),MaxNativeError<.02);AddInfo(FString::Printf(TEXT("%d final cases; smallest shape RMS %.8f span; native residual %.12g cm"),Cases,MinimumChange,MaxNativeError));return true;
}
#endif
