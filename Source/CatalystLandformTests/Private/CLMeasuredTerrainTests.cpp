#include "Misc/AutomationTest.h"
#include "Model/CLMeasuredTerrain.h"
#include "Model/CLPlanetRecipe.h"
#include "Heightmap/VoxelHeightmap.h"
#include "Heightmap/VoxelHeightmap_Height.h"
#include "UObject/StrongObjectPtr.h"
#include "Scene/CLScene.h"
#include "Surface/CLSurfaceLibrary.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"

namespace
{
UVoxelHeightmap *Raster(int32 W, int32 H, TFunction<float(int32, int32)> Value, bool Wrap)
{
    auto *M = NewObject<UVoxelHeightmap>();
    M->ScaleXY = 1;
    M->Height->ScaleZ = 1;
    M->Height->OffsetZ = 0;
    M->Height->bUseBicubic = false;
    TVoxelArray<float> Data;
    Data.SetNum((W + 4) * (H + 4));
    for (int32 Y = 0; Y < H + 4; ++Y)
        for (int32 X = 0; X < W + 4; ++X)
        {
            const int32 SX = Wrap ? (X - 2 + W) % W : FMath::Clamp(X - 2, 0, W - 1);
            Data[Y * (W + 4) + X] = Value(SX, FMath::Clamp(Y - 2, 0, H - 1));
        }
    M->Height->SetHeights(W + 4, H + 4, Data);
    return M;
}
TStrongObjectPtr<UCLMeasuredTerrainLibrary> Library()
{
    TStrongObjectPtr<UCLMeasuredTerrainLibrary> L(NewObject<UCLMeasuredTerrainLibrary>());
    auto &E = L->Entries.AddDefaulted_GetRef();
    E.Id = TEXT("test-measured");
    E.BodyId = TEXT("test-body");
    E.FrameId = TEXT("test-centric-east");
    E.Datum = TEXT("test-planetary-radius");
    E.MinimumRadiusMeters = 2000;
    E.MaximumRadiusMeters = 3100;
    E.Global.Heightmap = Raster(8, 4, [](int32 X, int32 Y) { return float(1000 + 10 * Y + X); }, true);
    E.Global.Width = 8;
    E.Global.Height = 4;
    E.Global.WestCenterDegrees = 22.5;
    E.Global.NorthCenterDegrees = 67.5;
    E.Global.LongitudeStepDegrees = 45;
    E.Global.LatitudeStepDegrees = 45;
    E.Global.RadiusOffsetMeters = 1000;
    E.Global.bGlobal = true;
    E.Global.NorthPoleOffsetMeters = 1003.5;
    E.Global.SouthPoleOffsetMeters = 1033.5;
    FString Error;
    check(L->BakeDigests(Error));
    return L;
}
FCLMeasuredTerrainReference Ref(const UCLMeasuredTerrainLibrary &L)
{
    FCLMeasuredTerrainReference R;
    R.Id = L.Entries[0].Id;
    R.Digest = L.Entries[0].DefinitionDigest;
    return R;
}
double Height(const FCLCompiledMeasuredTerrain &P, double Lat, double Lon)
{
    const FVector D = FCLCompiledMeasuredTerrain::NativeDirection(Lat, Lon);
    TArray<double> H;
    P.SampleHeights(MakeArrayView(&D, 1), H);
    return H[0];
}
} // namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLMeasuredSamplingTest, "Catalyst.Landform.Measured.NativeSampling",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLMeasuredSamplingTest::RunTest(const FString &)
{
    auto L = Library();
    FString Error;
    auto P = FCLCompiledMeasuredTerrain::Compile(Ref(*L), 10000, Error, L.Get());
    if (!TestTrue(Error, P.IsValid()))
        return false;
    TestTrue(TEXT("Native buffers have bounded resident storage"), P->ResidentBytes > 0 && P->ResidentBytes < 100000);
    for (int32 Y = 0; Y < 4; ++Y)
        for (int32 X = 0; X < 8; ++X)
            TestEqual(TEXT("Original raster pixel centre retains physical radius"),
                      Height(*P, 67.5 - Y * 45, 22.5 + X * 45), double(2000 + 10 * Y + X - 10000));
    TestEqual(TEXT("Native bilinear interior interpolation"), Height(*P, 0, 45), -7984.5);
    TestTrue(TEXT("Periodic longitude seam is continuous"),
             FMath::Abs(Height(*P, 0, .000001) - Height(*P, 0, 359.999999)) < .00001);
    for (double Lon : {0., 45., 180., 315.})
    {
        TestEqual(TEXT("North pole is longitude independent"), Height(*P, 90, Lon), -7996.5);
        TestEqual(TEXT("South pole is longitude independent"), Height(*P, -90, Lon), -7966.5);
    }
    TestTrue(TEXT("East-positive scientific Y explicitly maps to native -Y"),
             FCLCompiledMeasuredTerrain::NativeDirection(0, 90).Equals(FVector(0, -1, 0), 1e-12));
    const auto East = FCLCompiledMeasuredTerrain::SourceCoordinates(FVector(0, -1, 0));
    TestEqual(TEXT("Independent native east axis recovers 90 east"), East.Y, 90.);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLMeasuredResourceTest, "Catalyst.Landform.Measured.ResourceClosure",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLMeasuredResourceTest::RunTest(const FString &)
{
    auto L = Library();
    FString Error;
    auto R = Ref(*L);
    R.Digest = TEXT("wrong");
    TestFalse(TEXT("Wrong resource identity refuses before activation"),
              FCLCompiledMeasuredTerrain::Compile(R, 10000, Error, L.Get()).IsValid());
    R = Ref(*L);
    R.Revision = 2;
    TestFalse(TEXT("Missing resource revision refuses"),
              FCLCompiledMeasuredTerrain::Compile(R, 10000, Error, L.Get()).IsValid());
    TestFalse(TEXT("Terrain outside outer sky is refused"),
              FCLCompiledMeasuredTerrain::Compile(Ref(*L), 2500, Error, L.Get()).IsValid());
    L->Entries[0].Global.Heightmap = Raster(8, 4, [](int32, int32) { return 1234.f; }, true);
    TestFalse(TEXT("Edited native source cannot reuse a baked digest"),
              FCLCompiledMeasuredTerrain::Compile(Ref(*L), 10000, Error, L.Get()).IsValid());
    L->Entries[0].Global.Heightmap = nullptr;
    TestFalse(TEXT("Required missing data cannot become zero elevation/air"),
              FCLCompiledMeasuredTerrain::Compile(Ref(*L), 10000, Error, L.Get()).IsValid());
    L = Library();
    auto Folded=L->Entries[0];Folded.Id=FName(*Folded.Id.ToString().ToUpper());
    TestEqual(TEXT("Semantic digest ignores FName display casing"),UCLMeasuredTerrainLibrary::DefinitionDigest(Folded),L->Entries[0].DefinitionDigest);
    const auto Duplicate=L->Entries[0];L->Entries.Add(Duplicate);
    TestFalse(TEXT("Duplicate pinned resource identities are ambiguous"),FCLCompiledMeasuredTerrain::Compile(Ref(*L),10000,Error,L.Get()).IsValid());
    L = Library();L->Entries[0].Global.WestCenterDegrees=0;L->BakeDigests(Error);
    TestFalse(TEXT("Unsupported global pixel registration fails closed"),FCLCompiledMeasuredTerrain::Compile(Ref(*L),10000,Error,L.Get()).IsValid());
    L = Library();L->Entries[0].MinimumRadiusMeters=2050;L->BakeDigests(Error);
    TestFalse(TEXT("A declared bound cannot exclude resident source radii"),FCLCompiledMeasuredTerrain::Compile(Ref(*L),10000,Error,L.Get()).IsValid());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLMeasuredCoverageTest, "Catalyst.Landform.Measured.Coverage",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLMeasuredCoverageTest::RunTest(const FString &)
{
    auto L = Library();
    FString Error;
    auto Base = FCLCompiledMeasuredTerrain::Compile(Ref(*L), 10000, Error, L.Get());
    auto &R = L->Entries[0].Regions.AddDefaulted_GetRef();
    R.Heightmap = Raster(4, 4, [](int32, int32) { return 1500.f; }, false);
    R.Coverage = Raster(4, 4, [](int32, int32) { return 0.f; }, false);
    R.Width = R.Height = 4;
    R.WestCenterDegrees = 0;
    R.NorthCenterDegrees = 2;
    R.LongitudeStepDegrees = R.LatitudeStepDegrees = 1;
    R.RadiusOffsetMeters = 1000;
    R.FeatherMeters = 1;
    TestTrue(Error, L->BakeDigests(Error));
    auto P = FCLCompiledMeasuredTerrain::Compile(Ref(*L), 10000, Error, L.Get());
    if (!TestTrue(Error, P.IsValid()))
        return false;
    TestEqual(TEXT("Regional NoData uses declared valid global context"), Height(*P, .5, 1.5), Height(*Base, .5, 1.5));
    // Baking transactionally replaces Entries; reacquire the authoring reference.
    L->Entries[0].Regions[0].Coverage = Raster(4, 4, [](int32, int32) { return 1.f; }, false);
    L->BakeDigests(Error);
    P = FCLCompiledMeasuredTerrain::Compile(Ref(*L), 10000, Error, L.Get());
    if (!TestTrue(Error, P.IsValid()))
        return false;
    TestEqual(TEXT("Valid regional data overrides in physical radius units"), Height(*P, .5, 1.5), -7500.);
    TestEqual(TEXT("Outside regional coverage retains global terrain"), Height(*P, -5, 20), Height(*Base, -5, 20));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLMeasuredRecipeTest, "Catalyst.Landform.Measured.Recipe",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLMeasuredRecipeTest::RunTest(const FString &)
{
    auto L = Library();
    auto *S = GetMutableDefault<UCLMeasuredTerrainSettings>();
    TGuardValue<TSoftObjectPtr<UCLMeasuredTerrainLibrary>> Guard(S->Library,
                                                                 TSoftObjectPtr<UCLMeasuredTerrainLibrary>(L.Get()));
    auto R = FCLPlanetCompiler::BasePlanet();
    R.SchemaVersion = 19;
    R.Coordinates = ECLTerrainCoordinates::SphericalSurface;
    R.RadiusMeters = 10000;
    R.MeasuredTerrain = Ref(*L);
    FString Error;
    auto P = FCLPlanetCompiler::Compile(R, Error);
    if (!TestTrue(Error, P.IsValid()))
        return false;
    const FVector D = FCLCompiledMeasuredTerrain::NativeDirection(67.5, 22.5);
    TestEqual(TEXT("Recipe keeps source elevations rather than fitting normalized guides"), P->SampleHeightMeters(D),
              -8000.);
    TArray<double> H;
    P->SampleHeightsMeters(MakeArrayView(&D, 1), H);
    TestEqual(TEXT("Scalar and native batched scene share one measured provider"), H[0], -8000.);
    auto Scene = FCLScene::FromPlanet(P, Error);
    TArray<FVector> Centre{FVector::ZeroVector};TArray<FCLSceneShapeSample> Samples;
    TestTrue(TEXT("Native radial generation classifies the centre as known solid"),
             Scene && Scene->SampleShape({Centre}, Samples, Error) && Samples.Num()==1 && Samples[0].State==ECLSceneSampleState::Known && Samples[0].FieldMeters<0);
    R.NoiseLayers = FCLPlanetCompiler::RollingPlains().NoiseLayers;
    TestFalse(TEXT("Unsupported procedural remapping cannot silently alter measured elevations"),
              FCLPlanetCompiler::Compile(R, Error).IsValid());
    R.NoiseLayers.Reset();
    R.SchemaVersion = 18;
    TestFalse(TEXT("Old schemas cannot silently acquire measured behavior"),
              FCLPlanetCompiler::Compile(R, Error).IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLMeasuredMarsTest, "Catalyst.Landform.Measured.MarsLibrarySourceOracle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLMeasuredMarsTest::RunTest(const FString&)
{
    auto* Asset=LoadObject<UCLPlanetRecipeAsset>(nullptr,TEXT("/CatalystLandform/Measured/DA_Mars.DA_Mars"));
    if(!TestNotNull(TEXT("Reusable Mars library recipe reopens"),Asset))return false;
    FString Error,Text;const auto Plugin=IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"));
    if(!TestTrue(TEXT("Pinned original PDS/PROJ oracle is available"),Plugin.IsValid()&&FFileHelper::LoadFileToString(Text,*(Plugin->GetBaseDir()/TEXT("conformance/Mars/source-oracle-v1.json")))))return false;
    TSharedPtr<FJsonObject> Oracle;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Oracle))return false;
    const auto* DefaultSurfaces=GetDefault<UCLSurfaceSettings>();const auto Catalog=DefaultSurfaces->SurfaceLibrary;const auto Material=DefaultSurfaces->PlanetMaterial;
    auto Plan=FCLPlanetCompiler::Compile(Asset->Recipe,Error);if(!TestTrue(Error,Plan.IsValid()))return false;
    TestTrue(TEXT("Mars binds its native VPP artwork without mutating global settings"),Plan->Surfaces&&Plan->MeasuredTerrain&&Plan->MeasuredTerrain->GetMaterial()&&Plan->Surfaces->Owner.Get()==Plan->MeasuredTerrain->GetSurfaceLibrary()&&DefaultSurfaces->SurfaceLibrary==Catalog&&DefaultSurfaces->PlanetMaterial==Material);
    TestTrue(TEXT("Native resident raster budget is below 512 MiB"),Plan->MeasuredTerrain->ResidentBytes>0&&Plan->MeasuredTerrain->ResidentBytes<512LL*1024*1024);
    for(const auto& Kind:TArray<FString>{TEXT("globalOracle"),TEXT("regionalOracle")})
        for(const auto& Value:Oracle->GetArrayField(Kind))
        {
            const auto P=Value->AsObject();
            // This MOLA pixel is inside the explicit HiRISE override; the five
            // independent HiRISE/areoid points below qualify that coverage.
            if(Kind==TEXT("globalOracle")&&P->GetStringField(TEXT("name"))==TEXT("Jezero"))continue;
            const double Lat=FMath::DegreesToRadians(P->GetNumberField(TEXT("latitude"))),Lon=FMath::DegreesToRadians(P->GetNumberField(TEXT("longitude")));
            const FVector D(FMath::Cos(Lat)*FMath::Cos(Lon),-FMath::Cos(Lat)*FMath::Sin(Lon),FMath::Sin(Lat));
            const double Expected=P->GetNumberField(Kind==TEXT("globalOracle")?TEXT("radiusMeters"):TEXT("planetaryRadiusMeters"));
            const double Actual=Plan->RadiusMeters+Plan->SampleHeightMeters(D);
            TestTrue(FString::Printf(TEXT("Independent %s radius %.9f, native %.9f"),*Kind,Expected,Actual),FMath::Abs(Expected-Actual)<.02);
        }
    return true;
}
