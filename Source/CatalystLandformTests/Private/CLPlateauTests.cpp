#if WITH_DEV_AUTOMATION_TESTS
#include "Model/CLTerrainType.h"
#include "CLTerrainEditing.h"
#include "CLStrataEditing.h"
#include "CLSectionGeometry.h"
#include "CLPlanet.h"
#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Interfaces/IPluginManager.h"
#include "Async/Async.h"
#include "Surface/VoxelSurfaceTypeTable.h"
#include "VoxelLayers.h"
#include "VoxelQuery.h"

namespace
{
UCLTerrainTypeAsset* Plateaus(FString& Error)
{
    FString Json;
    FFileHelper::LoadFileToString(Json, *(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir() / TEXT("Examples/TerrainTypes/Plateaus.json")));
    auto* Type = NewObject<UCLTerrainTypeAsset>();
    return Type->ImportTerrainTypeJson(Json, Error) ? Type : nullptr;
}
FVector PlateauDirection(int32 I, int32 Num)
{
    const double Z = 1 - 2 * (I + .5) / Num, A = I * 2.399963229728653;
    return FVector(FMath::Sqrt(1 - Z * Z) * FMath::Cos(A), FMath::Sqrt(1 - Z * Z) * FMath::Sin(A), Z);
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLTerraceShapeTest, "Catalyst.Landform.Plateaus.TerraceSemantics",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLTerraceShapeTest::RunTest(const FString&)
{
    FString Error;
    FCLPlanetRecipe R; R.RadiusMeters = 1000; R.AlgorithmVersion = 2; R.BaseSurfaceNormalized = .335;
    FCLNoiseLayer T; T.Name = TEXT("Terracing"); T.Operation = ECLTerrainOperation::Terrace;
    T.Terrace.Origin.Guide = NAME_None; T.Terrace.Origin.OffsetMeters = -700;
    T.Terrace.StepHeight.Mode = ECLHeightReferenceMode::Height; T.Terrace.StepHeight.Guide = T.Terrace.StepHeight.UpperGuide = NAME_None;
    T.Terrace.StepHeight.Fraction = 0; T.Terrace.StepHeight.OffsetMeters = 30;
    T.Terrace.Softness = .25;
    R.NoiseLayers = {T};
    auto Height = [&]() { const auto P = FCLPlanetCompiler::Compile(R, Error); if (!P) { AddError(Error); return 0.0; } return P->SampleHeightMeters(FVector::ZAxisVector); };
    TestTrue(TEXT("35m above origin is on the 30m plateau"), FMath::Abs(Height() + 670) < 1.e-9);
    R.BaseSurfaceNormalized = .3595; // 59.5m above origin: last 0.5m of the smooth riser.
    const double A = (29.5 / 30 - .75) / .25;
    const double Expected = -700 + 30 * (1 + A*A*(3-2*A));
    TestTrue(TEXT("Soft riser follows the documented smoothstep curve"), FMath::Abs(Height() - Expected) < 1.e-9);
    R.NoiseLayers[0].Terrace.Strength = 0;
    TestTrue(TEXT("Zero strength is an exact bypass"), FMath::Abs(Height() - (-640.5)) < 1.e-9);
    R.NoiseLayers[0].Terrace.Strength = .5;
    TestTrue(TEXT("Strength blends original and shaped height"), FMath::Abs(Height() - (.5 * (-640.5 + Expected))) < 1.e-9);
    R.NoiseLayers[0].Terrace.Strength = 1; R.NoiseLayers[0].Terrace.Softness = 0;
    TestTrue(TEXT("Zero softness gives a hard step"), FMath::Abs(Height() + 670) < 1.e-9);
    R.BaseSurfaceNormalized = .295;
    TestTrue(TEXT("Negative positions floor to the preceding step"), FMath::Abs(Height() + 730) < 1.e-9);
    R.NoiseLayers[0].Terrace.StepHeight.OffsetMeters = 0;
    TestFalse(TEXT("Zero resolved step is rejected"), FCLPlanetCompiler::Compile(R, Error).IsValid());
    R.NoiseLayers[0] = T; R.NoiseLayers[0].Terrace.Softness = 1.1;
    TestFalse(TEXT("Invalid softness is rejected"), FCLPlanetCompiler::Compile(R, Error).IsValid());
    R.NoiseLayers[0] = T; R.SchemaVersion = 3;
    TestFalse(TEXT("Old recipe schemas cannot silently execute new operations"), FCLPlanetCompiler::Compile(R, Error).IsValid());
    for (int32 Algorithm : {1,2})
    {
        auto Old = FCLPlanetCompiler::RollingPlains(); Old.AlgorithmVersion = Algorithm; Old.SchemaVersion = 3;
        const auto Before = FCLPlanetCompiler::Compile(Old, Error); Old.SchemaVersion = 4;
        const auto After = FCLPlanetCompiler::Compile(Old, Error);
        for (int32 I=0; I<128; ++I)
            TestEqual(TEXT("Schema extension preserves every old noise field sample"), After->SampleHeightMeters(PlateauDirection(I,128)), Before->SampleHeightMeters(PlateauDirection(I,128)));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLTerraceAuthoringTest, "Catalyst.Landform.Plateaus.AuthoringAndResize",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLTerraceAuthoringTest::RunTest(const FString&)
{
    FString Error, Json; auto* Type = Plateaus(Error);
    if (!TestNotNull(TEXT("Plateaus preset imports"), Type)) { AddError(Error); return false; }
    auto Session = MakeShared<FCLTerrainEditing>(); Session->Open(Type);
    TestEqual(TEXT("Preset contains noise, terracing and detail"), Type->TerrainType.NoiseLayers.Num(),3);
    const auto Original = Session->CompilePreview(Error);
    if (!TestTrue(TEXT("Plateaus preview compiles"), Original.IsValid())) { AddError(Error); return false; }
    Session->Selected = 1; auto Edit = Type->TerrainType.NoiseLayers[1]; Edit.Terrace.Softness = .55;
    TestTrue(TEXT("Terracing operation edits transactionally"), Session->SetLayer(Edit));
    GEditor->UndoTransaction(); TestEqual(TEXT("Undo restores softness"), Type->TerrainType.NoiseLayers[1].Terrace.Softness,.28);
    GEditor->RedoTransaction(); TestEqual(TEXT("Redo restores the edit"), Type->TerrainType.NoiseLayers[1].Terrace.Softness,.55);
    Session->SetSolo(1); const auto Solo = Session->CompilePreview(Error);
    TestTrue(TEXT("Terrace Solo retains upstream noise and omits downstream detail"), Solo && Solo->Layers.Num()==2);
    TestTrue(TEXT("Terracing type exports"), Type->ExportJsonString(Json,Error));
    auto* Copy = NewObject<UCLTerrainTypeAsset>(); TestTrue(TEXT("Terrace parameters roundtrip"),Copy->ImportTerrainTypeJson(Json,Error));
    TestEqual(TEXT("Solo never changes the saved operation count"), Copy->TerrainType.NoiseLayers.Num(),3);
    Session->SetSolo(INDEX_NONE);
    TestTrue(TEXT("Terracing duplicates"),Session->Duplicate()); TestEqual(TEXT("Duplicate retains operation kind"),Type->TerrainType.NoiseLayers[2].Operation,ECLTerrainOperation::Terrace);
    TestTrue(TEXT("Terracing reorders"),Session->Move(-1)); TestTrue(TEXT("Terracing removes"),Session->Remove());
    TestTrue(TEXT("Add terrace creates another editable operation"),Session->Add(ECLTerrainOperation::Terrace));
    auto R=Session->Context; R.RadiusMeters=1000; TestTrue(TEXT("Preset applies to planet"),Copy->TerrainType.ApplyTo(R,Error));
    auto A=FCLPlanetCompiler::Compile(R,Error); R.RadiusMeters=2000; auto B=FCLPlanetCompiler::Compile(R,Error);
    TestEqual(TEXT("Guide-scaled step doubles with radius"),B->Layers[1].StepHeight,A->Layers[1].StepHeight*2);
    for(int32 I=0;I<512;++I)
    {
        const FVector D=PlateauDirection(I,512);
        const double H=A->SampleHeightMeters(D);
        TestTrue(TEXT("Terraced samples remain inside transformed compiled bounds"), H>=A->MinimumHeightMeters && H<=A->MaximumHeightMeters);
        TestTrue(TEXT("Terraced field resizes proportionally"),FMath::Abs(A->SampleHeightMeters(D)/1000-B->SampleHeightMeters(D)/2000)<1.e-5);
    }
    const auto Work=Async(EAsyncExecution::ThreadPool,[A]{return A->SampleHeightMeters(FVector(1,2,3));});
    TestEqual(TEXT("Terracing evaluates on immutable worker data"),Work.Get(),A->SampleHeightMeters(FVector(1,2,3)));
    const int32 Index=R.Strata.Stratum.IndexOfByPredicate([](const auto& S){return S.Name==TEXT("SurfaceMin");});
    TestTrue(TEXT("Guide rename updates terrace origin and step"),FCLStrataEditing::Rename(R,Index,TEXT("TerrainFloor"),Error));
    TestEqual(TEXT("Step lower guide follows rename"),R.NoiseLayers[1].Terrace.StepHeight.Guide,FName(TEXT("TerrainFloor")));
    TestEqual(TEXT("Terrace origin follows rename"),R.NoiseLayers[1].Terrace.Origin.Guide,FName(TEXT("TerrainFloor")));
    TestTrue(TEXT("Renamed terracing still compiles"),FCLPlanetCompiler::Compile(R,Error).IsValid());
    auto* Recipe=NewObject<UCLPlanetRecipeAsset>(); Recipe->Recipe=R;
    TestTrue(TEXT("Planet with terracing exports"),Recipe->ExportJsonString(Json,Error));
    auto* Restored=NewObject<UCLPlanetRecipeAsset>(); TestTrue(TEXT("Planet with terracing reimports"),Restored->ImportRecipeJson(Json,Error));
    TestEqual(TEXT("New recipe schema is explicit"),Restored->Recipe.SchemaVersion,6);
    auto Invalid=Copy->TerrainType; Invalid.SchemaVersion=2; TestFalse(TEXT("Old terrain schema refuses terrace operations"),Invalid.Validate(Error));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLPlateauSectionTest, "Catalyst.Landform.Plateaus.DesignerSphereParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLPlateauSectionTest::RunTest(const FString&)
{
    FString Error;
    auto* Type = Plateaus(Error);
    if (!TestNotNull(TEXT("Plateaus type loaded"), Type)) return false;
    auto* World = GEditor->GetEditorWorldContext().World();
    auto* Planet = World->SpawnActorDeferred<ACLPlanet>(ACLPlanet::StaticClass(), FTransform(FRotator(17,38,-12), FVector(123456,-45678,9876)));
    Planet->bEnableNanite = false; Planet->bEnableTessellation = false; Planet->bEnableNavigation = false;
    Planet->RecipeAsset = NewObject<UCLPlanetRecipeAsset>(Planet);
    Planet->FinishSpawning(Planet->GetActorTransform());
    FVoxelDependencyCollector Dependencies(STATIC_FNAME("CL.NativeGenerationParity"));
    const auto Surfaces = FVoxelSurfaceTypeTable::Get();
    const FVoxelQuery Query(0, FVoxelLayers::Empty(), *Surfaces, Dependencies);
    double MaxFieldError = 0, MaxPreviewError = 0; int32 Samples = 0;
    for (int32 Case = 0; Case < 4; ++Case)
    {
        auto R = FCLPlanetCompiler::RollingPlains(); R.RadiusMeters = Case == 0 ? 100 : 1000;
        R.Seed = 1729 + Case * 999; R.Strata = FCLStrataPresets::Defaults()[2];
        if (Case == 2)
        { Type->TerrainType.NoiseLayers[1].Terrace.Softness = 0; Type->TerrainType.NoiseLayers[2].bEnabled = false; }
        if (Case == 3)
        {
            auto& T = Type->TerrainType.NoiseLayers[1].Terrace;
            T.Softness = .5; T.Strength = .6; T.StepHeight.Fraction = .2; T.Origin.OffsetMeters = 5;
            Type->TerrainType.NoiseLayers[2].bEnabled = true;
            Type->TerrainType.NoiseLayers[0].Scale = ECLNoiseScale::Meters;
            Type->TerrainType.NoiseLayers[0].WavelengthMeters = 127;
        }
        if (!Type->TerrainType.ApplyTo(R, Error)) { AddError(Error); break; }
        const auto Plan = FCLPlanetCompiler::Compile(R, Error);
        Planet->RecipeAsset->Recipe = R; Planet->RebuildTerrain();
        const auto Runtime = Planet->GetSamplingRuntime();
        if (!TestTrue(TEXT("Actual spherical VPP runtime initialized"), Runtime && Runtime->RecipePlan && Runtime->RecipePlan->NativePerlin)) break;
        TArray<FVector> Points;
        // All Designer top vertices at both ends and default of the coverage range.
        for (double Width : {100.0, 500.0, 1000.0})
        {
            const auto Section = FCLSectionGeometry::Build(*Plan, 0, Width);
            Points.Append(Section.SourceSurfacePoints);
            for (int32 I = 0; I < Section.SurfaceVertexCount; ++I)
                MaxPreviewError = FMath::Max(MaxPreviewError, FMath::Abs(Section.Vertices[I].Z / 100000 -
                    Section.SourceSurfacePoints[I].Size() / (100 * Plan->RadiusMeters)));
        }
        // Whole globe, poles, both sides of the longitude seam.
        for (int32 I = 0; I < 1024; ++I)
        {
            const FVector D = PlateauDirection(I, 1024);
            Points.Add(D * (Plan->RadiusMeters + Plan->SampleHeightMeters(D)) * 100);
        }
        for (FVector D : {FVector::ZAxisVector, -FVector::ZAxisVector, FVector(-1,1.e-9,0), FVector(-1,-1.e-9,0)})
            Points.Add(D.GetSafeNormal() * (Plan->RadiusMeters + Plan->SampleHeightMeters(D)) * 100);
        FVoxelDoubleVectorBuffer Buffer; Buffer.Allocate(Points.Num());
        for (int32 I = 0; I < Points.Num(); ++I) Buffer.Set(I, Points[I]);
        const auto Distances = Runtime->SampleDistances(Query, Buffer);
        for (int32 I = 0; I < Points.Num(); ++I) MaxFieldError = FMath::Max(MaxFieldError, FMath::Abs(double(Distances[I])));
        // The two radial sides of the surface must be solid/empty, not just a
        // coincident zero returned for every query.
        FVoxelDoubleVectorBuffer Sides; Sides.Allocate(2);
        const FVector P = Points[Points.Num()/2], D = P.GetSafeNormal();
        Sides.Set(0, P - D * 100); Sides.Set(1, P + D * 100);
        const auto SD = Runtime->SampleDistances(Query, Sides);
        TestTrue(TEXT("VPP surface has correct inside/outside signs"), SD[0] < -95 && SD[1] > 95);
        Samples += Points.Num();
    }
    World->DestroyActor(Planet);
    AddInfo(FString::Printf(TEXT("Plateau parity: %d source samples; maximum VPP field residual %.6f cm; normalized flat preview error %.12f"), Samples, MaxFieldError, MaxPreviewError));
    TestTrue(TEXT("Every Designer/whole-globe point lies within 5 cm of the spherical VPP zero surface"), MaxFieldError < 5);
    TestTrue(TEXT("Flat display faithfully unwraps source radial heights"), MaxPreviewError < 1.e-9);
    return true;
}

#endif
