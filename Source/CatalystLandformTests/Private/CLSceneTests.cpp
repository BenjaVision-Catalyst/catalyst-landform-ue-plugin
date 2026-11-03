#if WITH_DEV_AUTOMATION_TESTS
#include "Scene/CLBodyFrame.h"
#include "Scene/CLScene.h"
#include "CLPlanetStamp.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLRegionGeneration.h"
#include "Model/CLSphericalPatches.h"
#include "Async/Async.h"
#include "Interfaces/IPluginManager.h"
#include "JsonObjectConverter.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Surface/VoxelSurfaceTypeTable.h"
#include "VoxelLayers.h"
#include "VoxelQuery.h"
#include "VoxelStampQuery.h"
#include "VoxelStampTransform.h"
#include <limits>

namespace
{
TSharedPtr<FJsonObject> ReadPreservationReference(const TCHAR* File=TEXT("scene-preservation-v1.json"))
{
    FString Text;
    const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"));
    if (!FFileHelper::LoadFileToString(Text, *(Plugin->GetBaseDir() / TEXT("conformance") / File))) return nullptr;
    TSharedPtr<FJsonObject> Result;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Result);
    return Result;
}

TArray<FVector> ReferenceDirections(const FCLCompiledPlanetRecipe& Plan)
{
    TArray<FVector> Directions;
    for (int32 Face = 0; Face < 6; ++Face)
        for (int32 Y = 0; Y <= 12; ++Y)
            for (int32 X = 0; X <= 12; ++X)
                Directions.Add(FCLSphericalPatches::Direction(Face, FVector2D(X / 6. - 1, Y / 6. - 1)));
    if (Plan.GeneratedRegions) for (const auto& Junction : Plan.GeneratedRegions->Junctions) Directions.Add(Junction.Direction);
    return Directions;
}

bool ImportCase(const TSharedPtr<FJsonObject>& Case, FCLPlanetRecipe& Recipe, FString& Error)
{
    FString Json;
    FJsonSerializer::Serialize(Case->GetObjectField(TEXT("recipe")).ToSharedRef(), TJsonWriterFactory<>::Create(&Json));
    auto* Asset = NewObject<UCLPlanetRecipeAsset>();
    if (!Asset->ImportRecipeJson(Json, Error)) return false;
    Recipe = Asset->Recipe;
    return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLSceneContractTest, "Catalyst.Landform.Scene.QueryAndFrameContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLSceneContractTest::RunTest(const FString&)
{
    FString Error;
    FCLBodyFrame Frame;
    Frame.AnchorMeters = FVector3d(3396200.125, -1234567.875, 2345678.5);
    Frame.BodyFromLocal = FRotator3d(17, 38, -12).Quaternion();
    TestTrue(TEXT("Finite rigid frame validates"), Frame.Validate(Error));
    FCLBodyFrame QuarterTurn;
    QuarterTurn.AnchorMeters = FVector3d(10, 20, 30);
    QuarterTurn.BodyFromLocal = FQuat4d(FVector3d::ZAxisVector, UE_DOUBLE_PI / 2.);
    TestTrue(TEXT("Independent axis/unit oracle: 100/200/300 cm becomes rotated -2/1/3 metres"),
        QuarterTurn.NativeToBodyMeters(FVector3d(100, 200, 300)).Equals(FVector3d(8, 21, 33), 1.e-12));
    double RoundTripError = 0;
    for (const FVector3d P : {FVector3d::ZeroVector, FVector3d(.01, -.02, .03), FVector3d(12500, -6789, 1234)})
        RoundTripError = FMath::Max(RoundTripError, (Frame.BodyToNativeCentimeters(Frame.NativeToBodyMeters(P)) - P).Size());
    TestTrue(TEXT("Local/native metre-centimetre round trip retains sub-micrometre precision"), RoundTripError < .00001);
    Frame.BodyFromLocal = FQuat4d(0, 0, 0, 0);
    TestFalse(TEXT("Invalid rotation is not silently normalized"), Frame.Validate(Error));

    auto Recipe = FCLPlanetCompiler::BasePlanet();
    Recipe.RadiusMeters = 1000;
    Recipe.BaseSurfaceNormalized = .5;
    Recipe.NoiseLayers.Reset();
    auto Plan = FCLPlanetCompiler::Compile(Recipe, Error);
    const auto Scene = FCLScene::FromLegacyRadial(Plan, Error);
    if (!TestTrue(TEXT("Closed radial scene compiles"), Scene.IsValid())) { AddError(Error); return false; }
    TestTrue(TEXT("Radial field is not misrepresented as a Euclidean SDF"), Scene->FieldMetric() == ECLSceneFieldMetric::SignedImplicit && !Scene->HasConservativeStepBound());
    TestEqual(TEXT("Bounds use the outer sky radius"), Scene->SolidBoundsMeters().Max, FVector3d(1000));
    const TArray<FVector3d> Points = {FVector3d::ZeroVector, FVector3d(499, 0, 0), FVector3d(501, 0, 0),
        FVector3d(1200, 0, 0), FVector3d(std::numeric_limits<double>::quiet_NaN(), 0, 0),
        FVector3d(std::numeric_limits<double>::infinity(), 0, 0), FVector3d(1.e300, 0, 0)};
    TArray<FCLSceneShapeSample> Samples;
    TestTrue(TEXT("Mixed valid/invalid point query returns one state per point"), Scene->SampleShape({Points}, Samples, Error) && Samples.Num() == Points.Num());
    if (Samples.Num() != Points.Num()) return false;
    TestTrue(TEXT("Core is known solid, not missing data"), Samples[0].State == ECLSceneSampleState::Known && Samples[0].FieldMeters < 0);
    TestTrue(TEXT("Inside and outside have metric radial sign values"), FMath::Abs(Samples[1].FieldMeters + 1) < 1.e-9 && FMath::Abs(Samples[2].FieldMeters - 1) < 1.e-9);
    TestTrue(TEXT("Beyond solid bounds is known air, not a resource miss"), Samples[3].State == ECLSceneSampleState::Known && Samples[3].FieldMeters > 0);
    for (int32 I = 4; I < Points.Num(); ++I) TestTrue(TEXT("Non-finite or overflowing input is invalid, never known air"), Samples[I].State == ECLSceneSampleState::InvalidQuery);
    for (double Footprint : {-1., .25, std::numeric_limits<double>::quiet_NaN()})
        TestTrue(TEXT("Unsupported filtering refuses the request and clears stale output"), !Scene->SampleShape({Points, Footprint}, Samples, Error) && Samples.IsEmpty() && !Error.IsEmpty());
    TestTrue(TEXT("Empty request succeeds without stale samples"), Scene->SampleShape({}, Samples, Error) && Samples.IsEmpty() && Error.IsEmpty());
    for (int32 Count : {1, 31, 512, 513, 1025})
    {
        TArray<FVector3d> Batch;
        for (int32 I = 0; I < Count; ++I) Batch.Add(FVector3d(480 + I * .125, 0, 0));
        if (!Scene->SampleShape({Batch}, Samples, Error) || Samples.Num() != Count) return false;
        for (int32 I = 0; I < Count; ++I)
            TestTrue(TEXT("Batch boundaries retain every analytic point"), Samples[I].State == ECLSceneSampleState::Known && FMath::Abs(Samples[I].FieldMeters - (-20 + I * .125)) < 1.e-9);
    }
    TArray<FCLSceneSurfaceSample> Surfaces;
    TestTrue(TEXT("Material-less body is not a missing shape"), Scene->SampleSurfaces({Points}, Surfaces, Error) && Surfaces[1].State == ECLSceneSampleState::NoContribution);
    Plan.Reset();
    Recipe.BaseSurfaceNormalized = .9;
    TestTrue(TEXT("Scene owns the frozen plan after caller release and recipe edits"), Scene->SampleShape({Points}, Samples, Error) && FMath::Abs(Samples[1].FieldMeters + 1) < 1.e-9);
    const auto Worker = Async(EAsyncExecution::ThreadPool, [Scene, Points]
    {
        TArray<FCLSceneShapeSample> Result; FString WorkerError;
        return Scene->SampleShape({Points}, Result, WorkerError) && Result[1].FieldMeters == -1.;
    });
    TestTrue(TEXT("Immutable scene evaluates on a worker without authoring state"), Worker.Get());
    TestFalse(TEXT("A missing plan cannot masquerade as an empty planet"), FCLScene::FromLegacyRadial(nullptr, Error).IsValid());
    const auto Face = FCLPlanetCompiler::Compile(Recipe, Error, nullptr, nullptr, 0);
    TestTrue(TEXT("A face-scoped provider cannot masquerade as a complete body"), Face && !FCLScene::FromLegacyRadial(Face, Error) && !Error.IsEmpty());
    AddInfo(FString::Printf(TEXT("Body frame round-trip error %.12g cm"), RoundTripError));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLScenePreservationTest, "Catalyst.Landform.Scene.PreMigrationPreservation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLScenePreservationTest::RunTest(const FString&)
{
    const auto Old=ReadPreservationReference(),Current=ReadPreservationReference(TEXT("volume-preservation-v1.json"));
    if (!TestTrue(TEXT("Both independently captured binary references are present"), Old.IsValid()&&Current.IsValid())) return false;
    double HeightError = 0, MaterialError = 0, SceneResidual = 0;
    int32 Count = 0;
    FString Error;
    for(const auto& Reference:{Old,Current})for (const auto& Value : Reference->GetArrayField(TEXT("cases")))
    {
        const auto Case = Value->AsObject();
        FCLPlanetRecipe Recipe;
        if (!ImportCase(Case, Recipe, Error)) { AddError(Error); return false; }
        const auto Before = Recipe;
        const auto Plan = FCLPlanetCompiler::Compile(Recipe, Error);
        if (!Plan) { AddError(Error); return false; }
        const auto Scene = FCLScene::FromLegacyRadial(Plan, Error);
        if (Plan->Coordinates == ECLTerrainCoordinates::LocalPatch)
            TestFalse(TEXT("Local-patch reference is preserved without claiming a radial scene"), Scene.IsValid());
        else if (!Scene) { AddError(Case->GetStringField(TEXT("id")) + TEXT(": ") + Error); return false; }
        TestTrue(TEXT("Compilation does not mutate the authored recipe"), FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Before, &Recipe, 0));
        const auto All = ReferenceDirections(*Plan);
        TArray<FVector> Directions;
        for (const auto& Index : Case->GetArrayField(TEXT("sampleIndices")))
        {
            const int32 I = int32(Index->AsNumber());
            if (!All.IsValidIndex(I)) { AddError(TEXT("Reference direction is missing")); return false; }
            Directions.Add(All[I]);
        }
        TArray<double> Heights; Plan->SampleHeightsMeters(Directions, Heights);
        TArray<FCLSurfaceWeights> Weights; Plan->SampleSurfaceWeights(Directions, Weights);
        TArray<FVector3d> Points;
        for (int32 I = 0; I < Directions.Num(); ++I)
        {
            HeightError = FMath::Max(HeightError, FMath::Abs(Heights[I] - Case->GetArrayField(TEXT("heightsMeters"))[I]->AsNumber()));
            for (int32 K = 0; K < 8; ++K) MaterialError = FMath::Max(MaterialError, FMath::Abs(Weights[I].Values[K] - Case->GetArrayField(TEXT("surfaceWeights"))[I]->AsArray()[K]->AsNumber()));
            Points.Add(Directions[I] * (Plan->RadiusMeters + FMath::Min(Heights[I], 0.)));
        }
        if (Scene)
        {
            TArray<FCLSceneShapeSample> Shape;
            if (!Scene->SampleShape({Points}, Shape, Error)) { AddError(Error); return false; }
            TArray<FCLSceneSurfaceSample> Surface;
            if (!Scene->SampleSurfaces({Points}, Surface, Error)) { AddError(Error); return false; }
            for (int32 I = 0; I < Shape.Num(); ++I)
            {
                TestTrue(TEXT("Preserved exterior is a known interface"), Shape[I].State == ECLSceneSampleState::Known);
                SceneResidual = FMath::Max(SceneResidual, FMath::Abs(Shape[I].FieldMeters));
                if (Plan->Surfaces) for (const auto& W : Surface[I].Weights)
                {
                    const int32 K = Plan->Surfaces->Find(W.Id, W.Revision);
                    if (!TestTrue(TEXT("Scene material keeps logical ID and revision"), K != INDEX_NONE)) return false;
                    MaterialError = FMath::Max(MaterialError, FMath::Abs(W.Weight - Weights[I].Values[K]));
                }
            }
        }
        TArray<FCLResolvedStampPlacement> Placements; Plan->GetStampPlacements(Placements);
        Placements.Sort([](const auto& A, const auto& B) { return A.Id.ToString() < B.Id.ToString(); });
        const auto& ExpectedPlacements = Case->GetArrayField(TEXT("stampPlacements"));
        TestEqual(TEXT("Pre-migration stamp count is preserved"), Placements.Num(), ExpectedPlacements.Num());
        for (int32 I = 0; I < FMath::Min(Placements.Num(), ExpectedPlacements.Num()); ++I)
        {
            FCLResolvedStampPlacement Expected;
            if (!FJsonObjectConverter::JsonObjectToUStruct(ExpectedPlacements[I]->AsObject().ToSharedRef(), &Expected, 0, 0)) return false;
            TestTrue(TEXT("Full deterministic stamp placement is preserved"), FCLResolvedStampPlacement::StaticStruct()->CompareScriptStruct(&Expected, &Placements[I], 0));
        }
        Count += Directions.Num();
    }
    TestTrue(TEXT("Pre-migration height samples remain unchanged within 0.01 mm"), HeightError < .00001);
    TestTrue(TEXT("Pre-migration and shared-scene materials match"), MaterialError < .00001);
    TestTrue(TEXT("Shared XYZ scene preserves the exterior within 0.1 mm"), SceneResidual < .0001);
    AddInfo(FString::Printf(TEXT("Two independent preservation captures / %d samples: height %.12g m, material %.12g, scene interface %.12g m"), Count, HeightError, MaterialError, SceneResidual));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLSceneNativeParityTest, "Catalyst.Landform.Scene.NativeBulkSparseParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLSceneNativeParityTest::RunTest(const FString&)
{
    const auto Reference = ReadPreservationReference();
    if (!Reference) return false;
    FString Error;
    FCLPlanetRecipe Recipe;
    if (!ImportCase(Reference->GetArrayField(TEXT("cases"))[4]->AsObject(), Recipe, Error)) { AddError(Error); return false; }
    FCLPlanetStamp Stamp;
    Stamp.bUseRecipe = true; Stamp.Radius = Recipe.RadiusMeters * 100; Stamp.Recipe = Recipe;
    Stamp.Behavior = EVoxelStampBehavior::AffectAll; Stamp.bApplyOnVoid = true;
    const auto Runtime = StaticCastSharedPtr<FCLPlanetStampRuntime>(FVoxelStampRuntime::Create(nullptr, FVoxelStampRef::New(Stamp), nullptr));
    if (!TestTrue(TEXT("Actual native recipe runtime uses the shared scene"), Runtime && Runtime->Scene && Runtime->RecipePlan->Surfaces)) return false;
    FVoxelDependencyCollector Dependencies(STATIC_FNAME("CL.SceneParity"));
    const auto Table = FVoxelSurfaceTypeTable::Get();
    const FVoxelQuery Query(0, FVoxelLayers::Empty(), *Table, Dependencies);
    const FIntVector Size(7, 5, 3);
    const FVector Direction = FVector(1, 2, 3).GetSafeNormal();
    const FVector Start = Direction * (Recipe.RadiusMeters + Runtime->RecipePlan->SampleHeightMeters(Direction)) * 100;
    constexpr float Step = 25;
    const int32 Count = Size.X * Size.Y * Size.Z;
    TVoxelArray<float> BulkDistances, SparseDistances;
    BulkDistances.Init(FVoxelUtilities::NaNf(), Count);
    SparseDistances.Init(FVoxelUtilities::NaNf(), Count);
    FVoxelDoubleVectorBuffer Positions; Positions.Allocate(Count);
    TArray<FVector3d> BodyPositions;
    for (int32 Z = 0; Z < Size.Z; ++Z) for (int32 Y = 0; Y < Size.Y; ++Y) for (int32 X = 0; X < Size.X; ++X)
    {
        const FVector P = Start + FVector(X, Y, Z) * Step;
        Positions.Set(BodyPositions.Num(), P); BodyPositions.Add(P * .01);
    }
    Runtime->Apply(FVoxelVolumeBulkQuery::Create(Query, BulkDistances, Start, Size, Step), FVoxelVolumeTransform());
    TVoxelArray<FVoxelSurfaceTypeBlend> NativeSurfaces; NativeSurfaces.SetNum(Count);
    Runtime->Apply(FVoxelVolumeSparseQuery::Create(Query, SparseDistances, NativeSurfaces, FVoxelMetadataMap(), Positions, true, {}), FVoxelVolumeTransform());
    TArray<FCLSceneShapeSample> Shape; Runtime->Scene->SampleShape({BodyPositions}, Shape, Error);
    TArray<FCLSceneSurfaceSample> Surfaces; Runtime->Scene->SampleSurfaces({BodyPositions}, Surfaces, Error);
    double FieldError = 0, SurfaceError = 0, LegacyDifference = 0;
    const auto LegacyHeights = Runtime->SampleHeights(Query, Positions);
    for (int32 I = 0; I < Count; ++I)
    {
        FieldError = FMath::Max(FieldError, FMath::Abs(double(BulkDistances[I]) - SparseDistances[I]));
        FieldError = FMath::Max(FieldError, FMath::Abs(double(BulkDistances[I]) - Shape[I].FieldMeters * 100));
        LegacyDifference = FMath::Max(LegacyDifference, FMath::Abs(double(BulkDistances[I]) - (Positions[I].Size() - Stamp.Radius - LegacyHeights[I])));
        for (const auto& W : Surfaces[I].Weights)
        {
            const int32 K = Runtime->RecipePlan->Surfaces->Find(W.Id, W.Revision);
            double NativeWeight = 0;
            for (const auto& Layer : NativeSurfaces[I].GetLayers()) if (Layer.Type == Runtime->RecipePlan->Surfaces->NativeTypes[K]) NativeWeight += Layer.Weight.ToFloat();
            SurfaceError = FMath::Max(SurfaceError, FMath::Abs(NativeWeight - W.Weight));
        }
    }
    TestTrue(TEXT("Both native query forms consume the shared XYZ field"), FieldError < .0001);
    TestTrue(TEXT("Native metadata keeps scene material weights within uint16 quantization"), SurfaceError < .0001);
    TestTrue(TEXT("Removing intermediate height-float rounding preserves legacy native field within 0.1 mm"), LegacyDifference < .01);
    // Unmigrated actor/planar source profiles stay explicit; never double-apply them.
    Stamp.bPlanarSources = true;
    const auto Unmigrated = StaticCastSharedPtr<FCLPlanetStampRuntime>(FVoxelStampRuntime::Create(nullptr, FVoxelStampRef::New(Stamp), nullptr));
    TestTrue(TEXT("Legacy source profile does not falsely claim shared-scene coverage"), Unmigrated && !Unmigrated->Scene);
    Stamp.bPlanarSources = false;
    Stamp.Radius *= .75;
    const auto CustomRadius = StaticCastSharedPtr<FCLPlanetStampRuntime>(FVoxelStampRuntime::Create(nullptr, FVoxelStampRef::New(Stamp), nullptr));
    if (!TestTrue(TEXT("Legacy independent stamp radius remains explicit"), CustomRadius && !CustomRadius->Scene)) return false;
    const auto CustomDistances = CustomRadius->SampleDistances(Query, Positions);
    for (int32 I = 0; I < Count; ++I)
        TestTrue(TEXT("Unmigrated custom-radius geometry retains its original equation"), FMath::Abs(CustomDistances[I] - float(Positions[I].Size() - Stamp.Radius - LegacyHeights[I])) < .0001);
    AddInfo(FString::Printf(TEXT("Native %d-point bulk/sparse parity: field %.12g cm, material %.12g, legacy rounding delta %.12g cm"), Count, FieldError, SurfaceError, LegacyDifference));
    return true;
}
#endif
