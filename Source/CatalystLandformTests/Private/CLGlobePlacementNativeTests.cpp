#if WITH_DEV_AUTOMATION_TESTS
#include "CLGlobePlacement.h"
#include "Model/CLTerrainType.h"
#include "Model/CLCompileProgress.h"
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLPlacedReliefTest,"Catalyst.Landform.PlanetBuilder.PlacedReliefFollowsGround",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLPlacedReliefTest::RunTest(const FString&)
{
    auto Base=FCLPlanetCompiler::TectonicBase();Base.BaseSurfaceNormalized=.75;FString Error;
    const auto Ground=FCLPlanetCompiler::Compile(Base,Error);if(!Ground){AddError(Error);return false;}
    auto* Source=LoadObject<UCLTerrainTypeAsset>(nullptr,TEXT("/CatalystLandform/TerrainTypes/DA_MountainRidgesSpherical"));
    if(!TestNotNull(TEXT("Packaged mountain source"),Source))return false;
    const auto OriginalSource=Source->TerrainType;auto Recipe=Base;FCLTerrainRegion R;R.Id=FGuid::NewGuid();R.Name=TEXT("Grounded mountain");R.bPlacedLandform=true;R.Placement.HalfExtentDegrees=FVector2D(25,20);Recipe.TerrainRegions.Add(R);
    if(!Source->TerrainType.ApplyToRegion(Recipe,R.Id,Error)){AddError(Error);return false;}
    // Isolate a purely additive mountain stamp from the carrier's signed ridges/valleys.
    Recipe.TerrainRegions[0].NoiseLayers.RemoveAll([](const auto& L){return L.Operation!=ECLTerrainOperation::StampDistribution;});
    const auto Prepared=FCLPlanetCompiler::PrepareBackground(Recipe,Error);
    const auto Placed=Prepared?FCLPlanetCompiler::CompleteBackground(*Prepared,MakeShared<FCLCompileProgress>(),Error):nullptr;
    if(!Placed){AddError(Error);return false;}
    const auto& Region=Placed->Regions.Last();TArray<FVector> D;
    for(int32 F=0;F<6;++F)for(int32 Y=0;Y<=20;++Y)for(int32 X=0;X<=20;++X)D.Add(FCLSphericalPatches::Direction(F,FVector2D(X/10.-1,Y/10.-1)));
    TArray<double> H,B;Placed->SampleHeightsMeters(D,H);Ground->SampleHeightsMeters(D,B);double Peak=0;int32 Outside=0,Inside=0;
    for(int32 I=0;I<D.Num();++I)
    {
        const double Weight=Region.Weight(D[I],Placed->RadiusMeters+Placed->BaseHeightMeters);
        if(Weight==0){++Outside;TestEqual(TEXT("Placement leaves the geological landscape outside its footprint exact"),H[I],B[I]);}
        else {++Inside;TestTrue(TEXT("Positive mountain relief never excavates a crater"),H[I]>=B[I]-1.e-8);Peak=FMath::Max(Peak,H[I]-B[I]);}
    }
    TestTrue(TEXT("Regression samples both the feature and surrounding landscape"),Inside>20&&Outside>1000&&Peak>1);
    const FVector Edge=Region.Placement.Orientation.RotateVector(FCLGlobePlacementEditing::Direction(FVector2D(FMath::DegreesToRadians(25.)*(1-1.e-7),0)));
    TestTrue(TEXT("Feature fades continuously into the local ground"),FMath::Abs(Placed->SampleHeightMeters(Edge)-Ground->SampleHeightMeters(Edge))<.001);
    FCLPlanetStamp Stamp;Stamp.bUseRecipe=true;Stamp.Radius=Recipe.RadiusMeters*100;Stamp.Recipe=Recipe;
    const auto Runtime=StaticCastSharedPtr<FCLPlanetStampRuntime>(FVoxelStampRuntime::Create(nullptr,FVoxelStampRef::New(Stamp),nullptr));if(!Runtime){AddError(TEXT("Placed native VPP shape failed"));return false;}
    FVoxelDependencyCollector Dependencies(STATIC_FNAME("CL.PlacedGround"));const auto Surfaces=FVoxelSurfaceTypeTable::Get();const FVoxelQuery Query(0,FVoxelLayers::Empty(),*Surfaces,Dependencies);
    FVoxelDoubleVectorBuffer Positions;Positions.Allocate(D.Num());for(int32 I=0;I<D.Num();++I)Positions.Set(I,D[I]*(Recipe.RadiusMeters+H[I])*100);
    const auto Distance=Runtime->SampleDistances(Query,Positions);double Residual=0;for(int32 I=0;I<D.Num();++I)Residual=FMath::Max(Residual,FMath::Abs(double(Distance[I])));
    TestTrue(TEXT("Grounded relief retains native VPP surface parity"),Residual<.02);
    TestTrue(TEXT("Placing source never edits its asset"),FCLTerrainType::StaticStruct()->CompareScriptStruct(&OriginalSource,&Source->TerrainType,0));
    // Signed additive sources must still excavate intentional valleys.
    auto Canyon=Recipe;auto& Layers=Canyon.TerrainRegions[0].NoiseLayers;Layers.Reset();FCLNoiseLayer Cut;Cut.Name=TEXT("Intentional valley");Cut.Blend=ECLNoiseBlend::Add;Cut.Minimum.OffsetMeters=-20;Cut.Maximum.OffsetMeters=-10;Layers.Add(Cut);
    const auto Valley=FCLPlanetCompiler::Compile(Canyon,Error);if(!Valley){AddError(Error);return false;}
    const FVector Centre=Region.Placement.Orientation.GetAxisZ();TestTrue(TEXT("Signed additive valley remains below the local geological surface"),Valley->SampleHeightMeters(Centre)<Ground->SampleHeightMeters(Centre)-5);
    AddInfo(FString::Printf(TEXT("Grounded relief: %d inside / %d outside; %.3f m peak; %.9g cm native residual"),Inside,Outside,Peak,Residual));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLPlacedSignedBlendTest,"Catalyst.Landform.PlanetBuilder.SignedReliefPreservesTerrain",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLPlacedSignedBlendTest::RunTest(const FString&)
{
    auto R=FCLPlanetCompiler::TectonicBase();R.BaseSurfaceNormalized=.75;FString Error;
    auto* Source=LoadObject<UCLTerrainTypeAsset>(nullptr,TEXT("/CatalystLandform/TerrainTypes/DA_MountainRidgesSpherical"));if(!Source)return false;
    const auto Original=Source->TerrainType;FCLTerrainRegion Region;Region.Id=FGuid::NewGuid();Region.Name=TEXT("Signed ridge field");Region.bPlacedLandform=true;R.TerrainRegions.Add(Region);
    if(!Source->TerrainType.ApplyToRegion(R,Region.Id,Error)){AddError(Error);return false;}
    // Isolate the absolute carrier; stamps and explicitly signed detail must not
    // become part of the carrier datum or create a uniform elevation pedestal.
    R.TerrainRegions[0].NoiseLayers.SetNum(1);
    const auto P=FCLPlanetCompiler::Compile(R,Error);if(!P){AddError(Error);return false;}const auto& Placed=P->Regions.Last();
    TArray<FVector> D;TArray<double> W;constexpr int32 Side=97;
    for(int32 Y=0;Y<Side;++Y)for(int32 X=0;X<Side;++X)
    {
        const FVector2D A=Placed.Placement.HalfExtentRadians*FVector2D(2.*(X+.37)/Side-1,2.*(Y+.63)/Side-1);
        const FVector V=FCLGlobePlacementEditing::Direction(A);D.Add(Placed.Placement.Orientation.RotateVector(V));
        W.Add(A.Size()>1.e-12?FMath::Sin(A.Size())/A.Size():1.);
    }
    TArray<double> H,B;P->SampleHeightsMeters(D,H);P->PlacedSurface->SampleHeightsMeters(D,B);
    double Sum=0,Area=0,Low=0,High=0;int32 Outside=0;
    for(int32 I=0;I<D.Num();++I)
    {
        const double Delta=H[I]-B[I];Sum+=Delta*W[I];Area+=Placed.Placement.Weight(D[I])*W[I];Low=FMath::Min(Low,Delta);High=FMath::Max(High,Delta);
        if(Placed.Placement.Weight(D[I])==0){++Outside;TestEqual(TEXT("Zero influence leaves the original terrain exact"),H[I],B[I]);}
    }
    TestTrue(TEXT("Carrier adds ridges and removes valleys instead of raising an oval pedestal"),Low<-.5&&High>.5&&FMath::Abs(Sum/Area)<.15&&Outside>100);
    // A uniform additive brush is relative to every individual ground sample.
    for(double Delta:{-12.,12.})
    {
        auto Changed=R;auto& L=Changed.TerrainRegions[0].NoiseLayers[0];L.Blend=ECLNoiseBlend::Add;L.Minimum.Mode=L.Maximum.Mode=ECLHeightReferenceMode::IntervalDelta;L.Minimum.Fraction=L.Maximum.Fraction=0;L.Minimum.OffsetMeters=L.Maximum.OffsetMeters=Delta;
        const auto Q=FCLPlanetCompiler::Compile(Changed,Error);if(!Q){AddError(Error);return false;}TArray<double> V;Q->SampleHeightsMeters(D,V);
        for(int32 I=0;I<D.Num();++I)TestTrue(TEXT("Positive and negative brushes add signed relief to varying local terrain"),FMath::IsNearlyEqual(V[I],B[I]+Delta*Placed.Placement.Weight(D[I]),1.e-7));
    }
    TestTrue(TEXT("Relative fitting preserves the reusable source asset"),FCLTerrainType::StaticStruct()->CompareScriptStruct(&Original,&Source->TerrainType,0));
    AddInfo(FString::Printf(TEXT("Signed carrier: %.4f m mean change, %.3f..%.3f m relief; %d untouched exterior samples"),Sum/Area,Low,High,Outside));return true;
}


#endif
