#if WITH_DEV_AUTOMATION_TESTS
#include "Model/CLPlanetRecipe.h"
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
FCLPlanetFeature BroadFeature()
{
    FCLPlanetFeature F;F.Id=FGuid(0x434C5046,0,0,1);F.LocationDegrees=FVector2D(45,0);F.HalfExtentDegrees=FVector2D(55,30);F.HeadingDegrees=15;return F;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLBasePlanetTest,"Catalyst.Landform.PlanetStructure.AuthoringAndBounds",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLBasePlanetTest::RunTest(const FString&)
{
    FString Error;auto R=FCLPlanetCompiler::BasePlanet();const auto Sphere=FCLPlanetCompiler::Compile(R,Error);
    if(!TestTrue(TEXT("New planet is an unadorned sphere"),Sphere.IsValid()))return false;
    TestEqual(TEXT("Base surface is distinct from sky radius"),Sphere->SampleHeightMeters(FVector::ZAxisVector),-550.);
    auto F=BroadFeature();R.BaseStructure.Add(F);const auto Plan=FCLPlanetCompiler::Compile(R,Error);
    if(!TestTrue(TEXT("Manual broad feature compiles"),Plan.IsValid()))return false;
    const auto Orientation=FCLCompiledPlanetFeature::Frame(F.LocationDegrees,F.HeadingDegrees);const FVector Center=Orientation.RotateVector(FVector::ZAxisVector);
    TestTrue(TEXT("Exact authored location receives full guide-relative uplift"),FMath::IsNearlyEqual(Plan->SampleHeightMeters(Center),-475.,1.e-9));
    TestEqual(TEXT("Other hemisphere retains base surface"),Plan->SampleHeightMeters(-Center),-550.);
    FCLCompiledPlanetFeature Shape;Shape.Orientation=Orientation;Shape.HalfExtentRadians=F.HalfExtentDegrees*(UE_PI/180);
    auto At=[&](double A){return Orientation.RotateVector(FVector(FMath::Sin(A),0,FMath::Cos(A)));};
    const double Edge=Shape.HalfExtentRadians.X,E=.0001;
    TestEqual(TEXT("Feature support is exactly zero at/outside edge"),Shape.Weight(At(Edge+E)),0.);
    TestTrue(TEXT("Feature value and derivatives fade smoothly at support edge"),Shape.Weight(At(Edge-E))<1.e-9&&Shape.Weight(At(Edge-2*E))<1.e-8);
    R.RadiusMeters*=2;const auto Big=FCLPlanetCompiler::Compile(R,Error);TestTrue(TEXT("Guide-relative uplift resizes proportionally"),FMath::IsNearlyEqual(Big->SampleHeightMeters(Center),-950.,1.e-9));
    R.BaseStructure[0].bEnabled=false;TestEqual(TEXT("Disable preserves neutral planet"),FCLPlanetCompiler::Compile(R,Error)->SampleHeightMeters(Center),-1100.);
    R.BaseStructure[0].bEnabled=true;R.BaseStructure.Add(F);TestFalse(TEXT("Duplicate feature identity fails"),FCLPlanetCompiler::Compile(R,Error).IsValid());R.BaseStructure.Pop();
    R.BaseStructure[0].HalfExtentDegrees.X=90;TestFalse(TEXT("Ambiguous hemisphere-spanning feature is rejected"),FCLPlanetCompiler::Compile(R,Error).IsValid());
    auto* Authoring=NewObject<UCLPlanetRecipeAsset>();Authoring->Recipe=FCLPlanetCompiler::BasePlanet();Authoring->Recipe.BaseStructure={F,F};
    TArray<TMap<FString,int32>> Indices;Indices.Add({{TEXT("BaseStructure"),1}});
    FPropertyChangedEvent Event(FindFProperty<FProperty>(FCLPlanetRecipe::StaticStruct(),TEXT("BaseStructure")),EPropertyChangeType::Duplicate);
    Event.ObjectIteratorIndex=0;Event.SetArrayIndexPerObject(Indices);Authoring->PostEditChangeProperty(Event);
    TestEqual(TEXT("Details duplicate preserves the original feature identity"),Authoring->Recipe.BaseStructure[0].Id,F.Id);
    TestTrue(TEXT("Details duplicate assigns a new instance identity"),Authoring->Recipe.BaseStructure[1].Id.IsValid()&&Authoring->Recipe.BaseStructure[1].Id!=F.Id);
    TestTrue(TEXT("Duplicated broad feature remains compilable"),FCLPlanetCompiler::Compile(Authoring->Recipe,Error).IsValid());
    R.BaseStructure[0]=F;R.SchemaVersion=9;TestFalse(TEXT("Old schema cannot acquire new structure semantics"),FCLPlanetCompiler::Compile(R,Error).IsValid());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLStructureCompositionTest,"Catalyst.Landform.PlanetStructure.LandformsAndNativeVolume",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLStructureCompositionTest::RunTest(const FString&)
{
    FString Error,Json;FFileHelper::LoadFileToString(Json,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/SphericalPatchComposition.json")));
    auto* A=NewObject<UCLPlanetRecipeAsset>();if(!A->ImportRecipeJson(Json,Error)){AddError(Error);return false;}
    auto R=A->Recipe;const auto Before=FCLPlanetCompiler::Compile(R,Error);R.SchemaVersion=10;R.BaseStructure.Add(BroadFeature());
    auto Basin=BroadFeature();Basin.Id.D=2;Basin.Name=TEXT("Basin");Basin.LocationDegrees=FVector2D(-110,-15);Basin.Relief.Fraction=-.2;R.BaseStructure.Add(Basin);
    const auto Plan=FCLPlanetCompiler::Compile(R,Error);if(!Plan){AddError(Error);return false;}
    auto Reordered=R;Swap(Reordered.BaseStructure[0],Reordered.BaseStructure[1]);const auto Other=FCLPlanetCompiler::Compile(Reordered,Error);
    TArray<FVector> D;for(int32 Face=0;Face<6;++Face)for(int32 Y=0;Y<=20;++Y)for(int32 X=0;X<=20;++X)D.Add(FCLSphericalPatches::Direction(Face,FVector2D(X/10.-1,Y/10.-1)));
    TArray<double> H,H0,HO;Plan->SampleHeightsMeters(D,H);Before->SampleHeightsMeters(D,H0);Other->SampleHeightsMeters(D,HO);double MaxError=0;
    for(int32 I=0;I<D.Num();++I)
    {
        double Delta=0;for(const auto& F:R.BaseStructure){FCLCompiledPlanetFeature S;S.Orientation=FCLCompiledPlanetFeature::Frame(F.LocationDegrees,F.HeadingDegrees);S.HalfExtentRadians=F.HalfExtentDegrees*(UE_PI/180);Delta+=S.Weight(D[I])*F.Relief.Fraction*300;}
        MaxError=FMath::Max(MaxError,FMath::Abs(H[I]-FMath::Clamp(H0[I]+Delta,-1000.,0.)));
        TestTrue(TEXT("Canonical feature order"),FMath::IsNearlyEqual(H[I],HO[I],1.e-9));
    }
    TestTrue(TEXT("Broad structure survives region overrides and is applied exactly once"),MaxError<1.e-8);
    TArray<FCLResolvedStampPlacement> P0,P1;Before->GetStampPlacements(P0);Plan->GetStampPlacements(P1);
    TestEqual(TEXT("Broad layout does not reshuffle regional PCG"),P0.Num(),P1.Num());
    for(const auto& P:P0){const auto* Q=P1.FindByPredicate([&](const auto& V){return V.Id==P.Id;});TestTrue(TEXT("Stamp identity, position and size remain stable"),Q&&Q->Anchor==P.Anchor&&Q->WidthMeters==P.WidthMeters&&Q->ReliefMeters==P.ReliefMeters);}
    FCLPlanetStamp Stamp;Stamp.bUseRecipe=true;Stamp.Radius=100000;Stamp.Recipe=R;
    const auto Ref=FVoxelStampRef::New(Stamp);const auto Runtime=StaticCastSharedPtr<FCLPlanetStampRuntime>(FVoxelStampRuntime::Create(nullptr,Ref,nullptr));
    if(!TestTrue(TEXT("Native VPP shape compiles"),Runtime.IsValid()))return false;
    FVoxelDependencyCollector Dependencies(STATIC_FNAME("CL.PlanetStructure"));const auto Surfaces=FVoxelSurfaceTypeTable::Get();const FVoxelQuery Query(0,FVoxelLayers::Empty(),*Surfaces,Dependencies);
    FVoxelDoubleVectorBuffer Points;Points.Allocate(D.Num());for(int32 I=0;I<D.Num();++I)Points.Set(I,D[I]*(1000+H[I])*100);
    const auto Dist=Runtime->SampleDistances(Query,Points);double Residual=0;for(int32 I=0;I<D.Num();++I)Residual=FMath::Max(Residual,FMath::Abs(double(Dist[I])));
    TestTrue(TEXT("Native volume and composed preview share zero set"),Residual<.01);
    A->Recipe=R;TestTrue(TEXT("Editorial placement exports"),A->ExportJsonString(Json,Error));auto* Reload=NewObject<UCLPlanetRecipeAsset>();TestTrue(TEXT("Editorial placement imports"),Reload->ImportRecipeJson(Json,Error));
    TestTrue(TEXT("Identity, location, orientation, extents and relief roundtrip"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&R,&Reload->Recipe,0));
    AddInfo(FString::Printf(TEXT("Planet structure: %d directions; %.12g m once-only composition error; %.9g cm native residual"),D.Num(),MaxError,Residual));return true;
}
#endif
