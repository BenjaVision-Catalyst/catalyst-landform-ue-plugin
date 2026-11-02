#include "Misc/AutomationTest.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLSurfaceRange.h"
#include "Model/CLSphericalPatches.h"
#include "CLPlanetStamp.h"
#include "VoxelLayers.h"
#include "VoxelQuery.h"
#include "Surface/VoxelSurfaceTypeTable.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"

namespace
{
FCLPlanetRecipe Guided()
{
    auto R=FCLPlanetCompiler::ContinentalBase();R.UseGuidedGeology();return R;
}
TArray<FVector> Probes(int32 N)
{
    TArray<FVector> D;for(int32 I=0;I<N;++I){const double Z=1-2*(I+.5)/N,A=I*2.399963229728653;D.Emplace(FMath::Cos(A)*FMath::Sqrt(1-Z*Z),FMath::Sin(A)*FMath::Sqrt(1-Z*Z),Z);}return D;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLGuidedProgressionTest,"Catalyst.Landform.GuidedGeology.CharacterProgression",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLGuidedProgressionTest::RunTest(const FString&)
{
    FCLTectonicPreset P;P.bEnabled=true;P.Seed=817;P.LowerGuide=TEXT("CustomLow");P.UpperGuide=TEXT("CustomHigh");
    P.ApplyProgression(.5);const auto Middle=P;P.ApplyProgression(.75);const auto Collision=P;P.ApplyProgression(1);const auto Extreme=P;
    TestTrue(TEXT("Guided motion falls in collision then rises at the extreme"),Middle.Motion>Collision.Motion&&Extreme.Motion>Middle.Motion);
    TestTrue(TEXT("Belt width narrows then broadens"),Middle.BeltWidthDegrees>Collision.BeltWidthDegrees&&Extreme.BeltWidthDegrees>Collision.BeltWidthDegrees);
    TestTrue(TEXT("Guided states retain enabled state, seed and guide references"),P.bEnabled&&P.Seed==817&&P.LowerGuide==TEXT("CustomLow")&&P.UpperGuide==TEXT("CustomHigh"));
    P.ApplyProgression(0);TestTrue(TEXT("Quiet is a configured landscape, not a disabled layer"),P.Motion>0&&P.InteriorRelief>0&&P.PlateCount>=2);
    TestTrue(TEXT("Extreme changes topology, crust, boundary shape and texture"),Extreme.PlateCount>P.PlateCount&&Extreme.CrustThickness>P.CrustThickness&&Extreme.BoundaryWarp>P.BoundaryWarp&&Extreme.Roughness>P.Roughness&&Extreme.CollisionStrength>P.CollisionStrength&&Extreme.RiftStrength>P.RiftStrength);
    auto R=Guided();R.Continents.ApplyProgression(.5);const auto Broad=R.Continents;R.Continents.ApplyProgression(1);
    TestTrue(TEXT("Continental character fragments large domains rather than just increasing amplitude"),R.Continents.DomainCount>Broad.DomainCount&&R.Continents.Extent<Broad.Extent&&R.Continents.CoastWarp>Broad.CoastWarp);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLGuidedExtremaTest,"Catalyst.Landform.GuidedGeology.FinalExtremaAndPatches",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLGuidedExtremaTest::RunTest(const FString&)
{
    FString Error;const auto D=Probes(12000);double WorstSeam=0;int32 Total=0;
    for(double Position:{0.,.25,.5,.75,1.})
    {
        auto R=Guided();R.Continents.ApplyProgression(Position);R.Tectonics.ApplyProgression(Position);
        const double Started=FPlatformTime::Seconds();const auto P=FCLPlanetCompiler::Compile(R,Error);if(!P){AddError(Error);return false;}
        TestTrue(TEXT("Completed planet owns one final range"),P->SurfaceRange&&P->UnmappedSurface);const auto Range=P->SurfaceRange;
        TestEqual(TEXT("The actual minimum reaches SurfaceMin"),P->SampleHeightMeters(Range->MinimumDirection),Range->TargetMin);
        TestEqual(TEXT("The actual maximum reaches SurfaceMax"),P->SampleHeightMeters(Range->MaximumDirection),Range->TargetMax);
        TArray<double> H;P->SampleHeightsMeters(D,H);int32 Clipped=0;
        for(auto V:H){++Total;TestTrue(TEXT("No generated height leaves final surface guides"),V>=Range->TargetMin-1.e-9&&V<=Range->TargetMax+1.e-9);Clipped+=V==Range->TargetMin||V==Range->TargetMax;}
        TestTrue(TEXT("Range fitting does not make broad clipped terraces"),Clipped<120);
        for(const auto& Patch:P->SurfacePatches)TestTrue(TEXT("All patches use the same final mapping"),Patch->SurfaceRange==Range);
        for(int32 I=0;I<6;++I)for(int32 K=0;K<=10;++K)
        {
            const FVector Q=FCLSphericalPatches::Direction(I,FVector2D(1,K/5.-1));
            WorstSeam=FMath::Max(WorstSeam,FMath::Abs(P->SurfacePatches[I]->SampleHeightMeters(Q)-P->SampleHeightMeters(Q)));
        }
        AddInfo(FString::Printf(TEXT("Character %.2f: final %.3f..%.3f m, %d dense probes at bounds, compile %.3fs"),Position,Range->TargetMin,Range->TargetMax,Clipped,FPlatformTime::Seconds()-Started));
    }
    TestTrue(TEXT("Normalization introduces no patch height seam"),WorstSeam<1.e-7);AddInfo(FString::Printf(TEXT("%d final-height probes; seam residual %.9g m"),Total,WorstSeam));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLGuidedCompleteSurfaceTest,"Catalyst.Landform.GuidedGeology.StampsRegionsAndNativeParity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLGuidedCompleteSurfaceTest::RunTest(const FString&)
{
    FString Json,Error;TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>());
    FFileHelper::LoadFileToString(Json,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/GeneratedRegions.json")));
    if(!A->ImportRecipeJson(Json,Error)){AddError(Error);return false;}auto R=A->Recipe;R.UseGuidedGeology();R.Continents.bEnabled=R.Tectonics.bEnabled=true;
    const auto P=FCLPlanetCompiler::Compile(R,Error);if(!P){AddError(Error);return false;}
    TestTrue(TEXT("The fitted field includes regional stamp placements"),P->GetDistributedStampCount()>0&&P->SurfaceRange&&P->UnmappedSurface);
    const auto Range=P->SurfaceRange;const auto D=Probes(2048);TArray<double> Raw,Final;P->UnmappedSurface->SampleHeightsMeters(D,Raw);P->SampleHeightsMeters(D,Final);
    for(int32 I=0;I<D.Num();++I)TestEqual(TEXT("Range applies after regional generation, stamps and blends"),Final[I],Range->Map(Raw[I]));
    FCLPlanetStamp Stamp;Stamp.bUseRecipe=true;Stamp.Radius=R.RadiusMeters*100;Stamp.Recipe=R;const auto Ref=FVoxelStampRef::New(Stamp);
    const auto Native=StaticCastSharedPtr<FCLPlanetStampRuntime>(FVoxelStampRuntime::Create(nullptr,Ref,nullptr));if(!Native){AddError(TEXT("Native guided recipe failed to compile"));return false;}
    FVoxelDoubleVectorBuffer Points;Points.Allocate(D.Num());for(int32 I=0;I<D.Num();++I)Points.Set(I,D[I]*(R.RadiusMeters+Final[I])*100);
    FVoxelDependencyCollector Dependencies(STATIC_FNAME("CL.GuidedGeology"));const auto Table=FVoxelSurfaceTypeTable::Get();FVoxelQuery Query(0,FVoxelLayers::Empty(),*Table,Dependencies);
    const auto Dist=Native->SampleDistances(Query,Points);double Worst=0;for(int32 I=0;I<D.Num();++I)Worst=FMath::Max(Worst,FMath::Abs(double(Dist[I])));
    TestTrue(TEXT("Native VPP uses the final normalized zero set"),Worst<.02);
    AddInfo(FString::Printf(TEXT("Completed regional planet: %d stamps, native residual %.9g cm"),P->GetDistributedStampCount(),Worst));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLGuidedAdvancedTest,"Catalyst.Landform.GuidedGeology.DirectAdvancedAndPersistence",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLGuidedAdvancedTest::RunTest(const FString&)
{
    auto R=Guided();R.Tectonics.Motion=.123;R.Tectonics.PlateCount=5;R.Tectonics.BeltWidthDegrees=19;
    FString Error;FCLTectonicControls C;TestTrue(TEXT("Direct advanced values compile"),FCLTectonicControls::Resolve(R.Tectonics,C,Error));
    TestEqual(TEXT("Motion is editable without an override flag"),C.Motion,.123);TestEqual(TEXT("Plate count is editable without an override flag"),C.LowerPlateCount,5);TestTrue(TEXT("Belt width reaches generation"),FMath::IsNearlyEqual(C.BeltWidth,FMath::DegreesToRadians(19.)));
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>());A->Recipe=R;FString Json;TestTrue(TEXT("Guided fields export"),A->ExportJsonString(Json,Error));
    TStrongObjectPtr<UCLPlanetRecipeAsset> B(NewObject<UCLPlanetRecipeAsset>());TestTrue(TEXT("Guided fields import"),B->ImportRecipeJson(Json,Error));TestTrue(TEXT("Resolved controls roundtrip exactly"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&R,&B->Recipe,0));
    R.Tectonics.ApplyProgression(.75);TestTrue(TEXT("The next knob gesture replaces prior advanced edits"),R.Tectonics.Motion!=.123&&R.Tectonics.PlateCount!=5&&R.Tectonics.BeltWidthDegrees!=19);
    const auto Plain=FCLPlanetCompiler::Compile(R,Error);R.Continents.bCoastalMargin=true;
    const auto LegacyStyle=FCLPlanetCompiler::Compile(R,Error);
    TestEqual(TEXT("Hidden legacy coast style cannot change the current resolved settings"),Plain->Continents->SampleDeltaMeters(FVector::XAxisVector),LegacyStyle->Continents->SampleDeltaMeters(FVector::XAxisVector));
    R.RadialGuides.Guides.FindByPredicate([](const auto& G){return G.Name==TEXT("SurfaceMin");})->NormalizedHeight=.9;
    TestFalse(TEXT("Reversed final surface guides are refused"),FCLPlanetCompiler::Compile(R,Error).IsValid());return true;
}
