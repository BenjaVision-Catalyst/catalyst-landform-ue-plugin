#include "Misc/AutomationTest.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLSphericalPatches.h"
#include "CLPlanetStamp.h"
#include "VoxelLayers.h"
#include "VoxelQuery.h"
#include "Surface/VoxelSurfaceTypeTable.h"
#include "Model/CLRegionGeneration.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLIndependentLayersTest,"Catalyst.Landform.PlanetLayers.CompositionAndSeeds",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLIndependentLayersTest::RunTest(const FString&)
{
    auto R=FCLPlanetCompiler::ContinentalBase();R.SeparateGeologyLayers();R.Tectonics.AlgorithmVersion=2;
    FString Error;auto Both=FCLPlanetCompiler::Compile(R,Error);if(!Both){AddError(Error);return false;}
    TestTrue(TEXT("Both geological layer owners coexist"),Both->Continents&&Both->Tectonics);
    auto C=R;C.Tectonics.bEnabled=false;const auto Crust=FCLPlanetCompiler::Compile(C,Error);
    auto T=R;T.Continents.bEnabled=false;const auto Motion=FCLPlanetCompiler::Compile(T,Error);
    auto N=T;N.Tectonics.bEnabled=false;const auto None=FCLPlanetCompiler::Compile(N,Error);
    TestTrue(TEXT("Each layer can be disabled independently"),Crust&&Crust->Continents&&!Crust->Tectonics&&Motion&&!Motion->Continents&&Motion->Tectonics&&None&&!None->Continents&&!None->Tectonics);
    auto V=R;V.Tectonics.Seed=817;const auto VaryMotion=FCLPlanetCompiler::Compile(V,Error);
    V=R;V.Continents.Seed=529;const auto VaryCrust=FCLPlanetCompiler::Compile(V,Error);
    double Difference=0,CrustVariation=0;int32 Count=0;
    for(int32 Face=0;Face<6;++Face)for(int32 Y=0;Y<=12;++Y)for(int32 X=0;X<=12;++X)
    {
        const FVector D=FCLSphericalPatches::Direction(Face,FVector2D(X/6.-1,Y/6.-1));++Count;
        const double Base=None->SampleHeightMeters(D),Foundation=Both->Continents->SampleDeltaMeters(D),Deformation=Both->Tectonics->SampleDeltaMeters(D);
        TestTrue(TEXT("Continental elevation and deformation are added exactly once"),FMath::IsNearlyEqual(Both->SampleHeightMeters(D),FMath::Clamp(Base+Foundation+Deformation,-Both->RadiusMeters,0.),1.e-6));
        TestEqual(TEXT("Tectonics classifies the actual shared continental crust"),Both->Tectonics->Continentalness(D),Both->Continents->Continentalness(D));
        TestEqual(TEXT("Changing a tectonic seed preserves the continental field"),VaryMotion->Continents->SampleDeltaMeters(D),Foundation);
        TestEqual(TEXT("Changing a continental seed preserves plate identities"),VaryCrust->Tectonics->Lower.Plates[0].Id,Both->Tectonics->Lower.Plates[0].Id);
        Difference+=FMath::Abs(VaryMotion->SampleHeightMeters(D)-Both->SampleHeightMeters(D));CrustVariation+=FMath::Abs(VaryCrust->Continents->SampleDeltaMeters(D)-Foundation);
        TestTrue(TEXT("Combined heights fit conservative bounds"),Both->SampleHeightMeters(D)>=Both->MinimumHeightMeters-1.e-6&&Both->SampleHeightMeters(D)<=Both->MaximumHeightMeters+1.e-6);
    }
    TestTrue(TEXT("Each variation seed changes its own generated feature"),Difference>1&&CrustVariation>1);
    auto Quiet=R;Quiet.Continents.Strength=0;const auto QuietCrust=FCLPlanetCompiler::Compile(Quiet,Error);
    TestTrue(TEXT("The continental knob changes the migrated foundation geometry"),FMath::Abs(QuietCrust->Continents->SampleDeltaMeters(FVector::XAxisVector)-Both->Continents->SampleDeltaMeters(FVector::XAxisVector))>1);
    R.Tectonics.bOverrideMotion=true;R.Tectonics.Motion=0;const auto Still=FCLPlanetCompiler::Compile(R,Error);
    TestTrue(TEXT("Stationary tectonics add no implicit second continent"),FMath::Abs(Still->Tectonics->SampleDeltaMeters(FVector::XAxisVector))<1.e-9);
    AddInfo(FString::Printf(TEXT("Independent geology: %d shared-field probes; separate seed and disable controls"),Count));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLLayerNativeTest,"Catalyst.Landform.PlanetLayers.NativePersistenceAndLegacy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLLayerNativeTest::RunTest(const FString&)
{
    auto Legacy=FCLPlanetCompiler::ContinentalBase();const auto Pinned=Legacy;
    auto R=Legacy;R.SeparateGeologyLayers();TestTrue(TEXT("Preparing layer controls never changes the pinned input"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Legacy,&Pinned,0));
    TestTrue(TEXT("Explicit conversion retains both enabled contributions"),R.SchemaVersion==16&&R.Continents.bEnabled&&R.Tectonics.bEnabled);
    FString Error,Json;const auto Plan=FCLPlanetCompiler::Compile(R,Error);if(!Plan){AddError(Error);return false;}
    auto* A=NewObject<UCLPlanetRecipeAsset>();A->Recipe=R;TestTrue(TEXT("Independent layers export"),A->ExportJsonString(Json,Error));auto* B=NewObject<UCLPlanetRecipeAsset>();TestTrue(TEXT("Independent layers import"),B->ImportRecipeJson(Json,Error));TestTrue(TEXT("All layer controls and seeds persist"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&R,&B->Recipe,0));
    FCLPlanetStamp Stamp;Stamp.bUseRecipe=true;Stamp.Radius=100000;Stamp.Recipe=R;auto Ref=FVoxelStampRef::New(Stamp);const auto Native=StaticCastSharedPtr<FCLPlanetStampRuntime>(FVoxelStampRuntime::Create(nullptr,Ref,nullptr));if(!Native){AddError(TEXT("Native layered field did not compile"));return false;}
    TArray<FVector> D;for(int32 F=0;F<6;++F)for(int32 Y=0;Y<=12;++Y)for(int32 X=0;X<=12;++X)D.Add(FCLSphericalPatches::Direction(F,FVector2D(X/6.-1,Y/6.-1)));
    TArray<double> H;Plan->SampleHeightsMeters(D,H);FVoxelDoubleVectorBuffer Points;Points.Allocate(D.Num());for(int32 I=0;I<D.Num();++I)Points.Set(I,D[I]*(R.RadiusMeters+H[I])*100);
    FVoxelDependencyCollector Dependencies(STATIC_FNAME("CL.PlanetLayers"));auto Table=FVoxelSurfaceTypeTable::Get();FVoxelQuery Query(0,FVoxelLayers::Empty(),*Table,Dependencies);const auto Dist=Native->SampleDistances(Query,Points);
    double Worst=0;for(int32 I=0;I<D.Num();++I)Worst=FMath::Max(Worst,FMath::Abs(double(Dist[I])));
    TestTrue(TEXT("Native VPP and the Designer share the combined zero set"),Worst<.02);
    auto Old=Legacy;Old.SchemaVersion=14;Old.Continents.bEnabled=true;TestFalse(TEXT("Old schemas cannot acquire separated layer semantics"),FCLPlanetCompiler::Compile(Old,Error).IsValid());
    R.RenameGuideReferences(TEXT("SurfaceMin"),TEXT("LowSurface"));TestEqual(TEXT("Guide renames reach continental settings"),R.Continents.LowerGuide,FName(TEXT("LowSurface")));
    FString RegionalJson;FFileHelper::LoadFileToString(RegionalJson,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/GeneratedRegions.json")));
    auto* Regional=NewObject<UCLPlanetRecipeAsset>();if(!Regional->ImportRecipeJson(RegionalJson,Error)){AddError(Error);return false;}
    Regional->Recipe.SeparateGeologyLayers();Regional->Recipe.Continents.bEnabled=true;Regional->Recipe.Tectonics.bEnabled=true;Regional->Recipe.Tectonics.AlgorithmVersion=2;
    const auto Composed=FCLPlanetCompiler::Compile(Regional->Recipe,Error);if(!Composed){AddError(Error);return false;}
    for(const auto& Field:Composed->SurfacePatches[0]->GeneratedRegions->Fields)
        TestTrue(TEXT("Regional child fields cannot inherit a second geological foundation"),!Field->Continents&&!Field->Tectonics);
    AddInfo(FString::Printf(TEXT("Native independent geology: %d directions, %.9g cm residual"),D.Num(),Worst));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLGeologyRangeTest,"Catalyst.Landform.PlanetLayers.CalibratedRange",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLGeologyRangeTest::RunTest(const FString&)
{
    FString Error;double WorstSlope=0,WorstRelief=0,Variation=0;int32 Samples=0;
    for(int32 Model:{2,3})for(double Strength:{0.,.25,.5,.75,1.})for(int32 Seed:{1729,5301,8179})
    {
        auto R=FCLPlanetCompiler::ContinentalBase();R.SeparateGeologyLayers();R.Continents.bOverrideExtent=false;
        R.Tectonics.AlgorithmVersion=Model;R.Continents.Strength=R.Tectonics.Activity=Strength;R.Seed=Seed;
        const auto P=FCLPlanetCompiler::Compile(R,Error);if(!P){AddError(Error);return false;}
        auto Changed=R;Changed.Continents.Seed=529;const auto C=FCLPlanetCompiler::Compile(Changed,Error);
        double Low=1.e9,High=-1.e9;
        for(int32 I=0;I<2048;++I)
        {
            const double Z=1-2*(I+.5)/2048.,A=I*2.399963229728653;const FVector D(FMath::Cos(A)*FMath::Sqrt(1-Z*Z),FMath::Sin(A)*FMath::Sqrt(1-Z*Z),Z);
            const double H=P->SampleHeightMeters(D);Low=FMath::Min(Low,H);High=FMath::Max(High,H);++Samples;
            const FVector E=(D+FVector::VectorPlaneProject(FVector(.7,.2,.5),D).GetSafeNormal()*.0001).GetSafeNormal();
            WorstSlope=FMath::Max(WorstSlope,FMath::Abs(P->SampleHeightMeters(E)-H)/(.0001*(R.RadiusMeters+H)));
            TestTrue(TEXT("Every knob endpoint is finite and inside the sky"),FMath::IsFinite(H)&&H<0&&H>-R.RadiusMeters);
            if(Strength==1)Variation+=FMath::Abs(C->SampleHeightMeters(D)-H);
        }
        WorstRelief=FMath::Max(WorstRelief,(High-Low)/(R.RadiusMeters+R.RadiusMeters*(R.BaseSurfaceNormalized-1)));
    }
    TestTrue(TEXT("Unpinned one-knob range avoids planet-scale cliffs"),WorstSlope<.8&&WorstRelief<.15);
    TestTrue(TEXT("Variation changes broad geometry across seeds"),Variation>1000);
    AddInfo(FString::Printf(TEXT("Calibrated geology: %d probes, worst local slope %.4f, radial relief %.4f"),Samples,WorstSlope,WorstRelief));return true;
}
