#if WITH_DEV_AUTOMATION_TESTS
#include "CLGlobePlacement.h"
#include "CLPlanetStamp.h"
#include "Model/CLSphericalPatches.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Interfaces/IPluginManager.h"
#include "Surface/VoxelSurfaceTypeTable.h"
#include "VoxelLayers.h"
#include "VoxelQuery.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLGlobePlacementNativeTest,"Catalyst.Landform.PlanetBuilder.EditedPlacementNativeParity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLGlobePlacementNativeTest::RunTest(const FString&)
{
    FString J,E;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/PlanetAssembly.json")));
    auto* Asset=NewObject<UCLPlanetRecipeAsset>();if(!Asset->ImportRecipeJson(J,E)){AddError(E);return false;}const auto Original=Asset->Recipe;auto Edited=Original;
    const auto Targets=FCLGlobePlacementEditing::Resolve(Original);
    for(auto T:Targets)
    {
        if(!T.Key.bStructure)
        {
            if(T.Priority==0)T=FCLGlobePlacementEditing::Drag(T,ECLGlobePlacementHandle::Centre,T.Frame.GetAxisZ(),FCLCompiledPlanetFeature::Frame(FVector2D(-20,35),0).GetAxisZ());
            else T=FCLGlobePlacementEditing::Drag(T,ECLGlobePlacementHandle::Rotation,T.Frame.RotateVector(FCLGlobePlacementEditing::Direction(FVector2D(.1,0))),T.Frame.RotateVector(FCLGlobePlacementEditing::Direction(FVector2D(.07,.07))));
            if(!FCLGlobePlacementEditing::Apply(Edited,T,false,E)){AddError(E);return false;}
        }
        else if(T.bRectangle){T.HalfExtentDegrees*=FVector2D(.8,.7);if(!FCLGlobePlacementEditing::Apply(Edited,T,true,E)){AddError(E);return false;}}
    }
    const auto Before=FCLPlanetCompiler::Compile(Original,E),After=FCLPlanetCompiler::Compile(Edited,E);if(!Before||!After){AddError(E);return false;}
    TArray<FVector> D;for(int32 F=0;F<6;++F)for(int32 Y=0;Y<=16;++Y)for(int32 X=0;X<=16;++X)D.Add(FCLSphericalPatches::Direction(F,FVector2D(X/8.-1,Y/8.-1)));
    TArray<double> H,B;After->SampleHeightsMeters(D,H);Before->SampleHeightsMeters(D,B);double Change=0;for(int32 I=0;I<H.Num();++I)Change=FMath::Max(Change,FMath::Abs(H[I]-B[I]));TestTrue(TEXT("Globe placement edits change the compiled terrain"),Change>5);
    FCLPlanetStamp S;S.bUseRecipe=true;S.Radius=Edited.RadiusMeters*100;S.Recipe=Edited;const auto Runtime=StaticCastSharedPtr<FCLPlanetStampRuntime>(FVoxelStampRuntime::Create(nullptr,FVoxelStampRef::New(S),nullptr));if(!Runtime){AddError(TEXT("Edited native VPP shape failed"));return false;}
    FVoxelDependencyCollector Dependencies(STATIC_FNAME("CL.GlobePlacement"));const auto Surfaces=FVoxelSurfaceTypeTable::Get();const FVoxelQuery Query(0,FVoxelLayers::Empty(),*Surfaces,Dependencies);
    FVoxelDoubleVectorBuffer P;P.Allocate(D.Num());for(int32 I=0;I<D.Num();++I)P.Set(I,D[I]*(Edited.RadiusMeters+H[I])*100);
    const auto Distance=Runtime->SampleDistances(Query,P);double Residual=0;for(int32 I=0;I<D.Num();++I)Residual=FMath::Max(Residual,FMath::Abs(double(Distance[I])));TestTrue(TEXT("Moved, rotated and resized placements retain the native VPP zero set"),Residual<.02);
    Asset->Recipe=Edited;TestTrue(TEXT("Edited placements export"),Asset->ExportJsonString(J,E));auto* Copy=NewObject<UCLPlanetRecipeAsset>();TestTrue(TEXT("Edited placements import"),Copy->ImportRecipeJson(J,E));TestTrue(TEXT("Portable placement fields roundtrip"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Edited,&Copy->Recipe,0));
    TestEqual(TEXT("Source identity survives editing"),Edited.TerrainRegions[0].TerrainTypeId,Original.TerrainRegions[0].TerrainTypeId);TestEqual(TEXT("Structure binding survives editing"),Edited.TerrainRegions[0].Placement.StructureFeatureId,Original.TerrainRegions[0].Placement.StructureFeatureId);
    AddInfo(FString::Printf(TEXT("Edited globe placement: %d directions; %.3f m maximum terrain change; %.9g cm native residual"),D.Num(),Change,Residual));return true;
}
#endif
