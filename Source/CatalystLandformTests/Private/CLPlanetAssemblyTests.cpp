#if WITH_DEV_AUTOMATION_TESTS
#include "Model/CLPlanetRecipe.h"
#include "Model/CLTerrainType.h"
#include "Model/CLSphericalPatches.h"
#include "CLPlanetStamp.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Interfaces/IPluginManager.h"
#include "Surface/VoxelSurfaceTypeTable.h"
#include "VoxelLayers.h"
#include "VoxelQuery.h"
namespace
{
UCLPlanetRecipeAsset* Assembly(FAutomationTestBase* T)
{
    FString J,E;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/PlanetAssembly.json")));
    auto* A=NewObject<UCLPlanetRecipeAsset>();if(!A->ImportRecipeJson(J,E)){T->AddError(E);return nullptr;}return A;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLLandformPlacementTest,"Catalyst.Landform.PlanetAssembly.PlacementAndStructure",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLLandformPlacementTest::RunTest(const FString&)
{
    auto* A=Assembly(this);if(!A)return false;auto R=A->Recipe;FString E;
    const auto P=FCLPlanetCompiler::Compile(R,E,nullptr,nullptr,FCLSphericalPatches::WholeSphere);if(!P){AddError(E);return false;}
    // Independent blend oracle: source-local samples, planet-owned masks, then broad structure once.
    double Worst=0;TArray<FVector> D;
    for(int32 Face=0;Face<6;++Face)for(int32 Y=0;Y<=12;++Y)for(int32 X=0;X<=12;++X)D.Add(FCLSphericalPatches::Direction(Face,FVector2D(X/6.-1,Y/6.-1)));
    TArray<double> H;P->SampleHeightsMeters(D,H);
    for(int32 I=0;I<D.Num();++I)
    {
        double Expected=P->BaseHeightMeters;
        for(const auto& Region:P->Regions){const double Source=Region.Field->SampleHeightMeters(Region.Placement.Orientation.UnrotateVector(D[I]));Expected=FMath::Lerp(Expected,P->BaseHeightMeters+(Source-P->BaseHeightMeters)*Region.Placement.Strength,Region.Placement.Weight(D[I]));}
        for(const auto& F:P->BaseStructure)if(!F.Heightmap)Expected+=F.Weight(D[I])*F.ReliefMeters;
        // Separate canyon below; avoid its support for this independent structure oracle.
        const FQuat C=FCLCompiledPlanetFeature::Frame(R.BaseStructure[1].LocationDegrees,R.BaseStructure[1].HeadingDegrees);
        if(C.UnrotateVector(D[I]).Z<0)Worst=FMath::Max(Worst,FMath::Abs(H[I]-FMath::Clamp(Expected,-P->RadiusMeters,0.)));
    }
    TestTrue(TEXT("Composed Landform fields retain base structure once within native float precision"),Worst<1.e-6);
    const auto Before=P->Regions[0].Placement;auto Moved=R;Moved.BaseStructure[0].LocationDegrees=FVector2D(-70,10);Moved.BaseStructure[0].HeadingDegrees=32;Moved.BaseStructure[0].HalfExtentDegrees*=.8;
    const auto Q=FCLPlanetCompiler::Compile(Moved,E,nullptr,nullptr,FCLSphericalPatches::WholeSphere);if(!Q){AddError(E);return false;}
    const auto After=Q->Regions[0].Placement;TestTrue(TEXT("Binding follows parent extent"),After.HalfExtentRadians.Equals(Before.HalfExtentRadians*.8,1.e-12));
    const FVector Local=FVector(.1,.05,1).GetSafeNormal();TestTrue(TEXT("Moving and rotating carries source and influence together"),FMath::IsNearlyEqual(P->Regions[0].Field->SampleHeightMeters(Local),Q->Regions[0].Field->SampleHeightMeters(Local),1.e-9)&&Before.Orientation!=After.Orientation);
    TArray<FCLResolvedStampPlacement> B,C;P->GetStampPlacements(B);Q->GetStampPlacements(C);TestEqual(TEXT("Moving macro layout does not reshuffle regional PCG"),B.Num(),C.Num());
    for(const auto& V:B){const auto* W=C.FindByPredicate([&](const auto& X){return X.Id==V.Id;});TestTrue(TEXT("Regional source-local stamp identity and position are stable"),W&&Before.Orientation.UnrotateVector(V.Anchor).Equals(After.Orientation.UnrotateVector(W->Anchor),1.e-9));}
    auto Bad=R;Bad.TerrainRegions[0].Placement.StructureFeatureId=FGuid::NewGuid();TestFalse(TEXT("Missing structure is an error, not a silent placement fallback"),FCLPlanetCompiler::Compile(Bad,E).IsValid());
    Bad=R;Bad.SchemaVersion=10;TestFalse(TEXT("Schema 10 cannot acquire placement semantics"),FCLPlanetCompiler::Compile(Bad,E).IsValid());
    FString J;TestTrue(TEXT("Assembly exports"),A->ExportJsonString(J,E));auto* Reload=NewObject<UCLPlanetRecipeAsset>();TestTrue(TEXT("Assembly imports"),Reload->ImportRecipeJson(J,E));TestTrue(TEXT("Portable applied recipe roundtrips"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&R,&Reload->Recipe,0));
    AddInfo(FString::Printf(TEXT("Assembly structure oracle: %d directions; %.12g m residual"),D.Num(),Worst));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLAssemblyNativeTest,"Catalyst.Landform.PlanetAssembly.NativeParityAndCanyon",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLAssemblyNativeTest::RunTest(const FString&)
{
    auto* A=Assembly(this);if(!A)return false;const auto R=A->Recipe;FString E;
    const auto Atlas=FCLPlanetCompiler::Compile(R,E),Whole=FCLPlanetCompiler::Compile(R,E,nullptr,nullptr,FCLSphericalPatches::WholeSphere);if(!Atlas||!Whole){AddError(E);return false;}
    TArray<FVector> D;for(int32 F=0;F<6;++F)for(int32 Y=0;Y<=24;++Y)for(int32 X=0;X<=24;++X)D.Add(FCLSphericalPatches::Direction(F,FVector2D(X/12.-1,Y/12.-1)));
    TArray<double> H,W;Atlas->SampleHeightsMeters(D,H);Whole->SampleHeightsMeters(D,W);double Residual=0;
    for(int32 I=0;I<D.Num();++I)Residual=FMath::Max(Residual,FMath::Abs(H[I]-W[I]));TestTrue(TEXT("All six patches agree with whole-field oracle"),Residual<1.e-8);
    FCLPlanetStamp S;S.bUseRecipe=true;S.Radius=R.RadiusMeters*100;S.Recipe=R;const auto Runtime=StaticCastSharedPtr<FCLPlanetStampRuntime>(FVoxelStampRuntime::Create(nullptr,FVoxelStampRef::New(S),nullptr));if(!Runtime){AddError(TEXT("Native assembly volume failed"));return false;}
    FVoxelDependencyCollector Dependencies(STATIC_FNAME("CL.Assembly"));const auto Surfaces=FVoxelSurfaceTypeTable::Get();const FVoxelQuery Query(0,FVoxelLayers::Empty(),*Surfaces,Dependencies);
    FVoxelDoubleVectorBuffer Points;Points.Allocate(D.Num());for(int32 I=0;I<D.Num();++I)Points.Set(I,D[I]*(R.RadiusMeters+H[I])*100);
    const auto Dist=Runtime->SampleDistances(Query,Points);double Native=0;for(int32 I=0;I<D.Num();++I)Native=FMath::Max(Native,FMath::Abs(double(Dist[I])));TestTrue(TEXT("Native VPP zero set matches preview assembly"),Native<.02);
    auto Without=R;Without.BaseStructure.Pop();const auto Base=FCLPlanetCompiler::Compile(Without,E);TArray<double> BH;Base->SampleHeightsMeters(D,BH);double Depth=0;
    for(int32 I=0;I<D.Num();++I){TestTrue(TEXT("Canyon only removes terrain"),H[I]<=BH[I]+1.e-8);Depth=FMath::Max(Depth,BH[I]-H[I]);}
    TestTrue(TEXT("Giant canyon reaches its authored deep relief"),Depth>95&&Depth<=102.01);
    AddInfo(FString::Printf(TEXT("Assembly: %d samples; %.12g m atlas error; %.9g cm native residual; %.3f m canyon depth"),D.Num(),Residual,Native,Depth));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLAssemblySourceTest,"Catalyst.Landform.PlanetAssembly.SourceOwnership",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLAssemblySourceTest::RunTest(const FString&)
{
    auto* A=Assembly(this);if(!A)return false;FString E,J;auto* Source=NewObject<UCLTerrainTypeAsset>();const auto Region=A->Recipe.TerrainRegions[1];
    Source->TerrainType.SchemaVersion=6;Source->TerrainType.Coordinates=ECLTerrainCoordinates::SphericalSurface;Source->TerrainType.AlgorithmVersion=Region.AlgorithmVersion;Source->TerrainType.Id=Region.TerrainTypeId;Source->TerrainType.Name=Region.TerrainTypeName;Source->TerrainType.NoiseLayers=Region.NoiseLayers;
    TestTrue(TEXT("Planet retains an editor source link separately from its applied snapshot"),A->SetLandformSource(Region.Id,Source,E));
    Source->TerrainType.Name=TEXT("Edited plateau source");TestTrue(TEXT("Source edits do not silently mutate the planet"),A->Recipe.TerrainRegions[1].TerrainTypeName!=Source->TerrainType.Name);
    TestTrue(TEXT("Explicit refresh applies source"),A->RefreshLandformSource(Region.Id,E));TestEqual(TEXT("Refresh preserves planet placement"),A->Recipe.TerrainRegions[1].Placement.StructureFeatureId,Region.Placement.StructureFeatureId);
    TestEqual(TEXT("Refresh updates snapshot identity"),A->Recipe.TerrainRegions[1].TerrainTypeName,Source->TerrainType.Name);
    TestTrue(TEXT("Portable export resolves applied configuration"),A->ExportJsonString(J,E));TestFalse(TEXT("No UObject source links leak into backend recipes"),J.Contains(TEXT("landformSources"))||J.Contains(Source->GetPathName()));
    TestTrue(TEXT("Import succeeds"),A->ImportRecipeJson(J,E));TestTrue(TEXT("Import removes editor links"),A->LandformSources.IsEmpty());return true;
}
#endif
