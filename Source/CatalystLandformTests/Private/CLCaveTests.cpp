#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLCompileProgress.h"
#include "Scene/CLScene.h"
#include "CLPlanetStamp.h"
#include "VoxelQuery.h"
#include "VoxelLayers.h"
#include "Surface/VoxelSurfaceTypeTable.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "UObject/StrongObjectPtr.h"
#include "Tasks/Task.h"
namespace
{
FCLPlanetRecipe CaveRecipe(FString& Error)
{
    FString J;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/Caves.json")));
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>());if(!A->ImportRecipeJson(J,Error))return {};return A->Recipe;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLCaveEligibilityTest,"Catalyst.Landform.Caves.EligibilityAndVersions",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLCaveEligibilityTest::RunTest(const FString&)
{
    FString E;auto R=CaveRecipe(E);const auto P=FCLPlanetCompiler::Compile(R,E);if(!P||!P->Caves){AddError(E);return false;}
    TestTrue(TEXT("Declared hosts and selected structural provinces produce cave networks"),P->Caves->Networks>0);AddInfo(P->Caves->Readiness);
    auto Unknown=R;for(auto& U:Unknown.Rock.Units){U.Lithology=ECLRockLithology::Unknown;U.FracturePotential=0;}
    const auto U=FCLPlanetCompiler::Compile(Unknown,E);TestTrue(TEXT("Generic rock remains untouched with an explicit readiness reason"),U&&U->Caves&&!U->Caves->Cutters&&U->Caves->Readiness.Contains(TEXT("declare")));
    Unknown=R;Unknown.Rock.Units[2].FracturePotential=0;const auto Mixed=FCLPlanetCompiler::Compile(Unknown,E);TestTrue(TEXT("A nonfracturable host conservatively blocks this bounded family"),Mixed&&Mixed->Caves&&!Mixed->Caves->Cutters);
    auto Missing=MakeShared<FCLCompiledPlanetRecipe>(*P);Missing->GeneratedRegions.Reset();const auto N=FCLCompiledCaves::Compile(R.Caves,*Missing,R.Seed,E);TestTrue(TEXT("No selected Landform context creates no random caves"),N&&!N->Cutters);
    for(auto& Entry:R.RegionGeneration.Palette)Entry.bEnabled=Entry.Suitability.Province.Type==ECLProvinceType::CarbonatePlatform;
    const auto Unselected=FCLPlanetCompiler::Compile(R,E);TestTrue(TEXT("Unavailable rift/shear Landforms cannot supply cave placement"),Unselected&&Unselected->Caves&&!Unselected->Caves->Cutters);
    R=CaveRecipe(E);R.SchemaVersion=22;TestFalse(TEXT("Caves and typed hosts require explicit schema 23"),FCLPlanetCompiler::Compile(R,E).IsValid());
    auto Invalid=P->SourceRecipe->Caves;Invalid.RadiusMeters=-1;TestFalse(TEXT("Invalid dimensions are refused"),Invalid.Validate(E));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLCaveFieldTest,"Catalyst.Landform.Caves.NativeFieldsAndPersistence",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLCaveFieldTest::RunTest(const FString&)
{
    FString E;const auto R=CaveRecipe(E);const auto P=FCLPlanetCompiler::Compile(R,E);if(!P||!P->Caves||!P->Caves->Cutters){AddError(E);return false;}
    auto Off=R;Off.Caves.bEnabled=false;const auto Base=FCLPlanetCompiler::Compile(Off,E);const auto Scene=FCLScene::FromPlanet(P,E),Natural=FCLScene::FromPlanet(Base,E);
    TestFalse(TEXT("Caves use a volume scene"),Scene->IsHeightOnly());TestFalse(TEXT("Radial fallback is refused"),FCLScene::FromLegacyRadial(P,E).IsValid());
    TArray<FVector3d> Points;for(const auto& S:P->Caves->Segments)Points.Add((S.A+S.B)*.5);
    TArray<FCLSceneShapeSample> C,B;Scene->SampleShape({Points},C,E);Natural->SampleShape({Points},B,E);
    for(int32 I=0;I<Points.Num();++I){TestTrue(TEXT("Native cutters expose actual underground air"),C[I].FieldMeters>0);TestTrue(TEXT("The uncut host was solid"),B[I].FieldMeters<0);}
    TArray<FCLSceneSurfaceSample> SC,SB;Scene->SampleSurfaces({Points},SC,E);Natural->SampleSurfaces({Points},SB,E);TArray<FCLRockSample> GC,GB;Scene->SampleGeology({Points},GC,E);Natural->SampleGeology({Points},GB,E);
    for(int32 I=0;I<Points.Num();++I){TestEqual(TEXT("Excavation keeps canonical geological identity"),GC[I].StratumId,GB[I].StratumId);TestEqual(TEXT("Exposed surface weight count is inherited"),SC[I].Weights.Num(),SB[I].Weights.Num());for(int32 J=0;J<SC[I].Weights.Num();++J)TestEqual(TEXT("Cutters do not paint replacement rock"),SC[I].Weights[J].Weight,SB[I].Weights[J].Weight);}
    FCLPlanetStamp Stamp;Stamp.bUseRecipe=true;Stamp.Recipe=R;Stamp.Radius=R.RadiusMeters*100;Stamp.bApplyOnVoid=true;Stamp.Behavior=EVoxelStampBehavior::AffectAll;
    const auto Native=StaticCastSharedPtr<FCLPlanetStampRuntime>(FVoxelStampRuntime::Create(nullptr,FVoxelStampRef::New(Stamp),nullptr));if(!TestTrue(TEXT("Actual VPP stamp owns the same cave scene"),Native&&Native->Scene))return false;
    FVoxelDependencyCollector Dependencies(STATIC_FNAME("CL.Caves"));const auto Table=FVoxelSurfaceTypeTable::Get();FVoxelQuery Q(0,FVoxelLayers::Empty(),*Table,Dependencies);
    const FVector Start=Points[0]*100-FVector(400);const FIntVector Size(3,3,33);constexpr float Step=25;const int32 Count=Size.X*Size.Y*Size.Z;
    TVoxelArray<FVoxelSurfaceTypeBlend> Materials;Materials.SetNum(Count);TVoxelArray<float> Bulk,Sparse;Bulk.Init(FVoxelUtilities::NaNf(),Count);Sparse.Init(FVoxelUtilities::NaNf(),Count);FVoxelDoubleVectorBuffer XYZ;XYZ.Allocate(Count);TArray<FVector3d> Body;
    for(int32 Z=0;Z<Size.Z;++Z)for(int32 Y=0;Y<Size.Y;++Y)for(int32 X=0;X<Size.X;++X){const FVector V=Start+FVector(X,Y,Z)*Step;XYZ.Set(Body.Num(),V);Body.Add(V*.01);}
    Native->Apply(FVoxelVolumeBulkQuery::Create(Q,Bulk,Start,Size,Step),FVoxelVolumeTransform());Native->Apply(FVoxelVolumeSparseQuery::Create(Q,Sparse,Materials,FVoxelMetadataMap(),XYZ,true,{}),FVoxelVolumeTransform());
    Scene->SampleShape({Body},C,E);double Worst=0;for(int32 I=0;I<Count;++I){Worst=FMath::Max(Worst,FMath::Abs(double(Bulk[I])-Sparse[I]));TestEqual(TEXT("Native cm field equals shared cave scene"),Bulk[I],float(C[I].FieldMeters*100));}TestTrue(TEXT("Native bulk/sparse parity"),Worst<1.e-5);
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>()),Copy(NewObject<UCLPlanetRecipeAsset>());A->Recipe=R;FString J;TestTrue(TEXT("Cave recipe exports"),A->ExportJsonString(J,E));TestTrue(TEXT("Cave recipe reloads without source assets"),Copy->ImportRecipeJson(J,E));TestTrue(TEXT("Reload preserves explicit host fields and cave settings"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&R,&Copy->Recipe,0));
    const auto Reload=FCLPlanetCompiler::Compile(Copy->Recipe,E);TestEqual(TEXT("Reload preserves deterministic geometry count"),Reload->Caves->Segments.Num(),P->Caves->Segments.Num());for(int32 I=0;I<Points.Num();++I)TestTrue(TEXT("Reload preserves exact body-space cave coordinates"),Reload->Caves->Segments[I].A==P->Caves->Segments[I].A);
    // Authored solid earthworks follow natural cave subtraction, so a recipe can fill a cavity.
    auto Fill=R;Fill.Volumes.bEnabled=true;FCLSolidOperation O;O.Id=FGuid(0x43415645,0,1,1);O.Name=TEXT("Backfill");O.CenterMeters=Points[0];O.RadiusMeters=4;O.SurfaceId=TEXT("Soil");Fill.Volumes.Operations.Add(O);const auto Filled=FCLPlanetCompiler::Compile(Fill,E);const auto FS=FCLScene::FromPlanet(Filled,E);FS->SampleShape({MakeArrayView(Points.GetData(),1)},B,E);TestTrue(TEXT("Later earthworks can backfill natural cave voids"),B[0].FieldMeters<0);
    TArray<FCLSceneSurfaceSample> Backfill;FS->SampleSurfaces({MakeArrayView(Points.GetData(),1)},Backfill,E);TestTrue(TEXT("Explicit backfill material composes over the natural cave void"),Backfill[0].Weights.ContainsByPredicate([](const auto& W){return W.Id==TEXT("Soil")&&W.Weight>.99;}));
    AddInfo(FString::Printf(TEXT("%d networks, %d cutters; %d native bulk/sparse points, max residual %.9g cm"),P->Caves->Networks,P->Caves->Segments.Num(),Count,Worst));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLCaveProgressionTest,"Catalyst.Landform.Caves.KnobAndBackground",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLCaveProgressionTest::RunTest(const FString&)
{
    FString E;const auto R=CaveRecipe(E);auto Off=R;Off.Caves.bEnabled=false;const auto Base=FCLPlanetCompiler::Compile(Off,E);if(!Base){AddError(E);return false;}
    TSet<int32> Counts;double PreviousRadius=0;for(int32 I=0;I<=10;++I){auto S=R.Caves;S.ApplyProgression(I/10.);TestTrue(TEXT("Every knob position validates"),S.Validate(E));const auto C=FCLCompiledCaves::Compile(S,*Base,R.Seed,E);if(!C){AddError(E);return false;}Counts.Add(C->Segments.Num());TestTrue(TEXT("Cave aperture develops across the knob"),S.RadiusMeters>PreviousRadius);PreviousRadius=S.RadiusMeters;if(I==0)TestTrue(TEXT("Zero development preserves the solid world"),C->Segments.IsEmpty());}
    TestTrue(TEXT("Knob changes occurrence and branching as well as dimensions"),Counts.Num()>2);
    const auto P=FCLPlanetCompiler::Compile(R,E);auto S=R.Caves;++S.Seed;const auto V=FCLCompiledCaves::Compile(S,*Base,R.Seed,E);TestTrue(TEXT("Reseeding changes the deterministic underground layout"),V&&(V->Segments.Num()!=P->Caves->Segments.Num()||(!V->Segments.IsEmpty()&&V->Segments[0].A!=P->Caves->Segments[0].A)));
    auto Large=MakeShared<FCLCompiledPlanetRecipe>(*Base);Large->RadiusMeters=100000;TestFalse(TEXT("Physical candidate budget never silently coarsens a large planet"),FCLCompiledCaves::Compile(R.Caves,*Large,R.Seed,E).IsValid());
    const auto Prepared=FCLPlanetCompiler::PrepareBackground(R,E);if(!Prepared){AddError(E);return false;}const auto Control=MakeShared<FCLCompileProgress>();TSharedPtr<const FCLCompiledPlanetRecipe> Worker;FString WE;
    auto Task=UE::Tasks::Launch(UE_SOURCE_LOCATION,[&]{Worker=FCLPlanetCompiler::CompleteBackground(*Prepared,Control,WE);});Task.Wait();if(!Worker){AddError(WE);return false;}TestEqual(TEXT("Existing background worker produces the same native cave input"),Worker->Caves->Segments.Num(),P->Caves->Segments.Num());
    return true;
}
#endif
