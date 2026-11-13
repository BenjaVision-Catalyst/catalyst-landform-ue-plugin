#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLRegionGeneration.h"
#include "Model/CLProvinceDefinition.h"
#include "Scene/CLScene.h"
#include "CLPlanetStamp.h"
#include "VoxelQuery.h"
#include "VoxelLayers.h"
#include "Surface/VoxelSurfaceTypeTable.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/StrongObjectPtr.h"
#include "Model/CLCompileProgress.h"
#include "Tasks/Task.h"
#include "Misc/CommandLine.h"

namespace
{
FCLPlanetRecipe MixedRecipe(FString& Error)
{
    FString J;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/MixedCaves.json")));
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>());if(!A->ImportRecipeJson(J,Error))return {};return A->Recipe;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLMixedCaveTest,"Catalyst.Landform.Caves.MixedGeneratedFamilies",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLMixedCaveTest::RunTest(const FString&)
{
    FString E;const auto R=MixedRecipe(E);const auto P=FCLPlanetCompiler::Compile(R,E);if(!P||!P->Caves){AddError(E);return false;}
    int32 Karst=0,Structural=0;TSet<FGuid> Ids;TArray<FVector> Probes;
    for(const auto& N:P->Caves->NetworkInfo)
    {
        Ids.Add(N.Id);Karst+=N.Family==FCLCompiledCaves::EFamily::CarbonateKarst;Structural+=N.Family==FCLCompiledCaves::EFamily::Structural;
        const auto* Node=P->Caves->Nodes.FindByPredicate([&](const auto& V){return V.Network==N.Id;});if(!TestNotNull(TEXT("Formation has actual nodes"),Node))return false;
        const auto* Source=P->GeneratedRegions->Palette.FindByPredicate([&](const auto& L){return L.Id==N.Landform;});if(!TestNotNull(TEXT("Formation retains its selected Landform provenance"),Source))return false;const auto Type=Source->Suitability.Province.Type;
        TestTrue(TEXT("Formation origin has positive upstream geological evidence"),Source->Suitability.Province.Match(FCLProvinceGeology::Sample(*P,N.OriginDirection))==ECLProvinceMatch::Eligible);
        TestTrue(TEXT("Selected native Landforms determine the correct formation family"),N.Family==FCLCompiledCaves::EFamily::CarbonateKarst?Type==ECLProvinceType::CarbonatePlatform:Type==ECLProvinceType::ContinentalRift||Type==ECLProvinceType::TransformBelt);
    }
    TestTrue(TEXT("One generated planet contains both geological cave families"),Karst>0&&Structural>0);
    for(const auto& Path:P->Caves->Passages)if(!Path.Entrance)Probes.Append(Path.Points);
    TArray<FCLSceneShapeSample> Fields;const auto Scene=FCLScene::FromPlanet(P,E);Scene->SampleShape({Probes},Fields,E);for(const auto& S:Fields)TestTrue(TEXT("Both families cut real open terrain"),S.FieldMeters>.1);
    TSet<FGuid> DomainMembers;for(const auto& Domain:P->Caves->InspectionDomains)
    {
        for(auto Id:Domain.Networks){TestFalse(TEXT("Every formation belongs to exactly one static diagnostic union"),DomainMembers.Contains(Id));DomainMembers.Add(Id);}
    }
    TestTrue(TEXT("Native domains cover every formation without dropping a family"),DomainMembers.Num()==Ids.Num());
    auto Off=R;Off.Caves.bEnabled=false;const auto Base=FCLPlanetCompiler::Compile(Off,E);if(!Base){AddError(E);return false;}
    int32 Same=0,Cross=0;FVector FirstCross=FVector::ZeroVector;TArray<TSharedPtr<const FCLCompiledCaveVolume>> Singles;
    for(const auto& N:P->Caves->NetworkInfo)Singles.Add(P->Caves->Cutters->Select(N.Id));
    for(int32 I=0;I<P->Caves->NetworkInfo.Num();++I)for(int32 J=I+1;J<P->Caves->NetworkInfo.Num();++J)
    {
        if(!Singles[I]->Bounds().Intersect(Singles[J]->Bounds()))continue;
        bool Joined=false;
        for(const auto& Path:P->Caves->Passages)if(!Path.Entrance&&(Path.Network==P->Caves->NetworkInfo[I].Id||Path.Network==P->Caves->NetworkInfo[J].Id))
        {
            for(int32 K=0;K<Path.Points.Num()&&!Joined;++K)for(int32 Q=0;Q<4&&!Joined;++Q)
            {
                const auto V=K?FMath::Lerp(Path.Points[K-1],Path.Points[K],Q/4.):Path.Points[0];
                if(Singles[I]->Compose(V,-1000)>.4&&Singles[J]->Compose(V,-1000)>.4){Joined=true;if(P->Caves->NetworkInfo[I].Family!=P->Caves->NetworkInfo[J].Family)FirstCross=V;}
            }
            if(Joined)break;
        }
        if(Joined){if(P->Caves->NetworkInfo[I].Family==P->Caves->NetworkInfo[J].Family)++Same;else ++Cross;}
    }
    AddInfo(FString::Printf(TEXT("Mixed seed %d: %s; %d native domains, %d same-family and %d cross-family positive-overlap pairs; cross %s"),R.Caves.Seed,*P->Caves->Readiness,P->Caves->InspectionDomains.Num(),Same,Cross,*FirstCross.ToString()));
    TestTrue(TEXT("Geologically generated families form a real shared air junction"),Cross>0);
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>()),Reload(NewObject<UCLPlanetRecipeAsset>());A->Recipe=R;FString Json;A->ExportJsonString(Json,E);if(!Reload->ImportRecipeJson(Json,E)){AddError(E);return false;}const auto Copy=FCLPlanetCompiler::Compile(Reload->Recipe,E);if(!Copy){AddError(E);return false;}
    TestEqual(TEXT("Recipe reload retains all formation identities"),Copy->Caves->NetworkInfo.Num(),P->Caves->NetworkInfo.Num());TestEqual(TEXT("Recipe reload retains every native passage"),Copy->Caves->Passages.Num(),P->Caves->Passages.Num());
    for(int32 I=0;I<P->Caves->Passages.Num();++I)TestTrue(TEXT("Mixed recipe reload is geometrically exact"),Copy->Caves->Passages[I].Points==P->Caves->Passages[I].Points);
    const auto Prepared=FCLPlanetCompiler::PrepareBackground(R,E);if(!Prepared){AddError(E);return false;}TSharedPtr<const FCLCompiledPlanetRecipe> Worker;const auto Progress=MakeShared<FCLCompileProgress>();FString WE;auto Job=UE::Tasks::Launch(UE_SOURCE_LOCATION,[&]{Worker=FCLPlanetCompiler::CompleteBackground(*Prepared,Progress,WE);});Job.Wait();if(!Worker){AddError(WE);return false;}TestTrue(TEXT("Existing background worker preserves mixed family generation"),Worker->Caves->Passages[0].Points==P->Caves->Passages[0].Points);
    FString LibraryPath;
    if(FParse::Value(FCommandLine::Get(),TEXT("CLMixedLibraryRecipe="),LibraryPath))
    {
        FString LibraryJson;TStrongObjectPtr<UCLPlanetRecipeAsset> Library(NewObject<UCLPlanetRecipeAsset>());
        if(!FFileHelper::LoadFileToString(LibraryJson,*LibraryPath)||!Library->ImportRecipeJson(LibraryJson,E)){AddError(TEXT("Native mixed library export failed: ")+E);return false;}
        const auto NativeLibrary=FCLPlanetCompiler::Compile(Library->Recipe,E);if(!NativeLibrary||!NativeLibrary->Caves){AddError(E);return false;}
        TestEqual(TEXT("Actual library export retains every qualified formation"),NativeLibrary->Caves->NetworkInfo.Num(),P->Caves->NetworkInfo.Num());
        if(!TestEqual(TEXT("Actual library export retains every qualified passage"),NativeLibrary->Caves->Passages.Num(),P->Caves->Passages.Num()))return false;
        for(int32 I=0;I<P->Caves->Passages.Num();++I)TestTrue(TEXT("Source-linked library export has the exact qualified cave geometry"),NativeLibrary->Caves->Passages[I].Points==P->Caves->Passages[I].Points&&NativeLibrary->Caves->Passages[I].Sections==P->Caves->Passages[I].Sections&&NativeLibrary->Caves->Passages[I].Incision==P->Caves->Passages[I].Incision&&NativeLibrary->Caves->Passages[I].Network==P->Caves->Passages[I].Network);
        auto* Saved=LoadObject<UCLPlanetRecipeAsset>(nullptr,TEXT("/Game/LandformDesigner/Caves/DA_MixedCaves.DA_MixedCaves"));
        if(!TestNotNull(TEXT("Actual mixed library package reloads in this fresh process"),Saved))return false;
        TestEqual(TEXT("Saved mixed package retains explicit profile 3 across UE delta serialization"),Saved->Recipe.Caves.AlgorithmVersion,3);
        const auto Reloaded=FCLPlanetCompiler::Compile(Saved->Recipe,E);if(!Reloaded||!Reloaded->Caves){AddError(E);return false;}
        if(!TestEqual(TEXT("Reloaded library package retains every passage"),Reloaded->Caves->Passages.Num(),P->Caves->Passages.Num()))return false;
        for(int32 I=0;I<P->Caves->Passages.Num();++I)TestTrue(TEXT("Reloaded native package retains the qualified cave volume construction"),Reloaded->Caves->Passages[I].Points==P->Caves->Passages[I].Points&&Reloaded->Caves->Passages[I].Sections==P->Caves->Passages[I].Sections&&Reloaded->Caves->Passages[I].Incision==P->Caves->Passages[I].Incision);
        AddInfo(TEXT("Actual source-linked mixed library export and fresh package load reproduce the qualified native cave graph exactly."));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLCaveLegacyAssetTest,"Catalyst.Landform.Caves.LegacyNativeAssetProfiles",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLCaveLegacyAssetTest::RunTest(const FString&)
{
    // These published Lab packages were saved with profile 2 against a profile-2
    // CDO; its property may be omitted from the native package's delta payload.
    for(const auto* Name:{TEXT("DA_FissureCaves"),TEXT("DA_KarstCaves")})
    {
        const FString Path=FString::Printf(TEXT("/Game/LandformDesigner/Caves/%s.%s"),Name,Name);
        auto* A=LoadObject<UCLPlanetRecipeAsset>(nullptr,*Path);if(!TestNotNull(TEXT("Published legacy Lab package loads"),A))return false;
        TestEqual(TEXT("Loading a saved package preserves its separated profile until explicit authoring"),A->Recipe.Caves.AlgorithmVersion,2);
        TestFalse(TEXT("Legacy load does not dirty the native package"),A->GetOutermost()->IsDirty());
        FString E;TestTrue(TEXT("Loaded legacy cave recipe still compiles natively"),FCLPlanetCompiler::Compile(A->Recipe,E).IsValid());
    }
    return true;
}


#endif
