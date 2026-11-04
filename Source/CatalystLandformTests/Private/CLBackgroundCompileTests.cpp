#include "Misc/AutomationTest.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLCompileProgress.h"
#include "Model/CLSphericalPatches.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Tasks/Task.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLBackgroundCompileParity,"Catalyst.Landform.Background.NativeFieldParity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLBackgroundCompileParity::RunTest(const FString&)
{
    const auto Root=IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir();
    const TCHAR* Examples[]={TEXT("Watershed"),TEXT("WatershedContinental"),TEXT("WatershedExcavation"),TEXT("SurfaceScale10"),TEXT("SurfaceScaleRegional"),TEXT("GeneratedRegions"),TEXT("RockBedsExcavation"),TEXT("SurfaceScale10Face"),TEXT("LocalPatch"),TEXT("Mars")};
    FString CSV=TEXT("profile,preparation_ms,reused_preparation_ms,completion_seconds,max_height_error_m\n");
    for(const TCHAR* Name:Examples)
    {
        FString J,E;TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>());
        if(FString(Name)==TEXT("LocalPatch"))
        {
            const auto* Source=LoadObject<UCLPlanetRecipeAsset>(nullptr,TEXT("/Game/LandformDesigner/DA_MountainRidgesLocalPlanet.DA_MountainRidgesLocalPlanet"));
            if(!Source){AddError(TEXT("Local native fixture is missing"));return false;}A->Recipe=Source->Recipe;
        }
        else
        {
            const FString File=FString(Name)==TEXT("Mars")?FPaths::ProjectContentDir()/TEXT("RuntimeRecipes/Mars/planet.json"):Root/TEXT("Examples")/((FString(Name)==TEXT("SurfaceScale10Face")?FString(TEXT("SurfaceScale10")):FString(Name))+TEXT(".json"));
            FFileHelper::LoadFileToString(J,*File);if(!A->ImportRecipeJson(J,E)){AddError(E);return false;}
        }
        auto Recipe=A->Recipe;if(FString(Name)==TEXT("SurfaceScale10Face")){Recipe.SchemaVersion=18;Recipe.ElevationDistribution=FCLSurfaceDistributionSettings();}
        const int32 Patch=FString(Name)==TEXT("SurfaceScale10Face")?0:INDEX_NONE;
        const FBox2D Bounds(FVector2D(-250),FVector2D(250));const auto* Region=FString(Name)==TEXT("LocalPatch")?&Bounds:nullptr;
        if(FString(Name)==TEXT("SurfaceScale10")){FCLNoiseLayer L;L.Name=TEXT("Background native stamps");L.Operation=ECLTerrainOperation::StampDistribution;L.Distribution.DensityPerSquareKilometer=20;L.Distribution.SizeMode=ECLStampSizeMode::Absolute;L.Distribution.MinimumSizeMeters=80;L.Distribution.MaximumSizeMeters=120;Recipe.NoiseLayers.Add(L);}
        const auto Sync=FCLPlanetCompiler::Compile(Recipe,E,nullptr,Region,Patch);if(!Sync){AddError(E);return false;}const double Start=FPlatformTime::Seconds();const auto Prepared=FCLPlanetCompiler::PrepareBackground(Recipe,E,Region,Patch);const double Preparation=(FPlatformTime::Seconds()-Start)*1000;if(!Prepared){AddError(E);return false;}
        const double ReuseStart=FPlatformTime::Seconds();const auto Reused=FCLPlanetCompiler::PrepareBackground(Recipe,E,Region,Patch,Prepared.Get());const double ReuseMs=(FPlatformTime::Seconds()-ReuseStart)*1000;if(!Reused){AddError(E);return false;}
        struct FResult{TSharedPtr<const FCLCompiledPlanetRecipe> Plan;FString Error;double Seconds;};
        auto Control=MakeShared<FCLCompileProgress>();auto Job=UE::Tasks::Launch(UE_SOURCE_LOCATION,[Prepared=Reused,Control]{const double T=FPlatformTime::Seconds();FString Error;auto P=FCLPlanetCompiler::CompleteBackground(*Prepared,Control,Error);return FResult{P,Error,FPlatformTime::Seconds()-T};});
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);Job.Wait();const auto Result=Job.GetResult();if(!Result.Plan){AddError(FString(Name)+TEXT(": ")+Result.Error);return false;}
        TArray<FVector> D;for(int32 I=0;I<2048;++I){const double Z=1-2*(I+.5)/2048,Angle=I*2.399963229728653;D.Add(FVector(FMath::Cos(Angle)*FMath::Sqrt(1-Z*Z),FMath::Sin(Angle)*FMath::Sqrt(1-Z*Z),Z));}
        TArray<double> H1,H2;Sync->SampleHeightsMeters(D,H1);Result.Plan->SampleHeightsMeters(D,H2);double Worst=0;for(int32 I=0;I<D.Num();++I)Worst=FMath::Max(Worst,FMath::Abs(H1[I]-H2[I]));TestTrue(FString(Name)+TEXT(" native field matches synchronous compiler"),Worst<1.e-7);
        TestTrue(TEXT("Completion retains its original recipe"),Result.Plan->SourceRecipe&&FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Recipe,Result.Plan->SourceRecipe.Get(),0));
        if(Sync->Watershed){TestEqual(TEXT("Worker drainage retains native topology"),Result.Plan->Watershed->GetChannels().Num(),Sync->Watershed->GetChannels().Num());TestTrue(TEXT("Worker runoff closes"),FMath::Abs(Result.Plan->Watershed->InputDischarge-Result.Plan->Watershed->TerminalDischarge)<1.e-10);}
        auto Stop=MakeShared<FCLCompileProgress>();Stop->Cancelled=true;FString CancelError;TestFalse(TEXT("Superseded work refuses before computing a field"),FCLPlanetCompiler::CompleteBackground(*Prepared,Stop,CancelError).IsValid());
        CSV+=FString::Printf(TEXT("%s,%.6f,%.6f,%.6f,%.12g\n"),Name,Preparation,ReuseMs,Result.Seconds,Worst);
    }
    const FString Dir=FPaths::ProjectSavedDir()/TEXT("Verification/background");IFileManager::Get().MakeDirectory(*Dir,true);FFileHelper::SaveStringToFile(CSV,*(Dir/TEXT("parity.csv")));return true;
}
#endif
