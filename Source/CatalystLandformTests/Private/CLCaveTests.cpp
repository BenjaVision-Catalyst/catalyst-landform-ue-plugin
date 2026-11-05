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
    const auto U=FCLPlanetCompiler::Compile(Unknown,E);TestTrue(TEXT("Unknown hosts explicitly refuse enabled caves"),!U&&E.Contains(TEXT("declare")));
    Unknown=R;Unknown.Rock.Units[2].FracturePotential=0;const auto Mixed=FCLPlanetCompiler::Compile(Unknown,E);TestTrue(TEXT("A nonfracturable host conservatively blocks this bounded family"),!Mixed&&E.Contains(TEXT("fracture")));
    auto Missing=MakeShared<FCLCompiledPlanetRecipe>(*P);Missing->GeneratedRegions.Reset();const auto N=FCLCompiledCaves::Compile(R.Caves,*Missing,R.Seed,E);TestTrue(TEXT("Missing selected Landform context refuses enabled caves"),!N&&E.Contains(TEXT("require")));
    for(auto& Entry:R.RegionGeneration.Palette)Entry.bEnabled=Entry.Suitability.Province.Type==ECLProvinceType::CarbonatePlatform;
    const auto Unselected=FCLPlanetCompiler::Compile(R,E);TestTrue(TEXT("Unavailable provinces refuse an enabled cave build"),!Unselected&&E.Contains(TEXT("no complete")));
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLCaveInspectionFieldTest,"Catalyst.Landform.Caves.InspectionVolume",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLCaveInspectionFieldTest::RunTest(const FString&)
{
    FString E;const auto R=CaveRecipe(E);const auto P=FCLPlanetCompiler::Compile(R,E);if(!P||!P->Caves||!P->Caves->Cutters){AddError(E);return false;}
    const auto S=FCLScene::FromPlanet(P,E);const auto& Segment=P->Caves->Segments[0];const FVector Centre=(Segment.A+Segment.B)*.5;
    TArray<FVector> Points={Centre};
    // Include an unrelated natural surface exactly on its zero set. Negating
    // the final solid alone would create a false sheet over the entire globe.
    const FVector D=-Centre.GetSafeNormal();Points.Add(D*(P->RadiusMeters+P->SampleHeightMeters(D)));Points.Add(Centre.GetSafeNormal()*(P->RadiusMeters+P->SampleHeightMeters(Centre)+20));
    TArray<FCLSceneShapeSample> Void;TestTrue(TEXT("Cave void samples"),S->SampleCaveVoid({Points},Void,E));
    TestTrue(TEXT("Generated underground air is inside inspection volume"),Void[0].FieldMeters<0);
    TestTrue(TEXT("No diagnostic sheet over unrelated terrain"),Void[1].FieldMeters>0);
    TestTrue(TEXT("Exterior entrance cutter is clipped by the real surface"),Void[2].FieldMeters>0);
    FCLPlanetStamp Stamp;Stamp.bUseRecipe=true;Stamp.Recipe=R;Stamp.CompiledRecipe=P;Stamp.Radius=R.RadiusMeters*100;Stamp.bApplyOnVoid=true;Stamp.bCaveInspectionPreview=true;
    const auto Native=StaticCastSharedPtr<FCLPlanetStampRuntime>(FVoxelStampRuntime::Create(nullptr,FVoxelStampRef::New(Stamp),nullptr));if(!TestTrue(TEXT("Inspection uses actual native stamp"),Native&&Native->Scene))return false;
    FVoxelDependencyCollector Dependencies(STATIC_FNAME("CL.CaveInspection"));const auto Table=FVoxelSurfaceTypeTable::Get();FVoxelQuery Q(0,FVoxelLayers::Empty(),*Table,Dependencies);
    TVoxelArray<float> Fields;Fields.Init(FVoxelUtilities::NaNf(),Points.Num());TVoxelArray<FVoxelSurfaceTypeBlend> Materials;Materials.SetNum(Points.Num());FVoxelDoubleVectorBuffer XYZ;XYZ.Allocate(Points.Num());for(int32 I=0;I<Points.Num();++I)XYZ.Set(I,Points[I]*100);
    Native->Apply(FVoxelVolumeSparseQuery::Create(Q,Fields,Materials,FVoxelMetadataMap(),XYZ,true,{}),FVoxelVolumeTransform());for(int32 I=0;I<Points.Num();++I)TestEqual(TEXT("Native inspection field equals shared scene"),Fields[I],float(Void[I].FieldMeters*100));
    auto Selected=Stamp;Selected.CaveInspectionNetwork=P->Caves->Segments[0].Network;
    const auto One=StaticCastSharedPtr<FCLPlanetStampRuntime>(FVoxelStampRuntime::Create(nullptr,FVoxelStampRef::New(Selected),nullptr));if(!TestTrue(TEXT("Network domain uses an initialized native stamp"),One&&!One->FailedToInitialize()&&One->InspectionCutters))return false;
    const auto* Other=P->Caves->Segments.FindByPredicate([&](const auto& Segment){return Segment.Network!=Selected.CaveInspectionNetwork;});if(!TestNotNull(TEXT("Fixture has another separate network"),Other))return false;
    FVoxelDoubleVectorBuffer Pair;Pair.Allocate(2);Pair.Set(0,Centre*100);Pair.Set(1,(Other->A+Other->B)*50);
    const auto SelectedFields=One->SampleDistances(Q,Pair);TestTrue(TEXT("Bounded domain contains its own actual cave"),SelectedFields[0]<0);TestTrue(TEXT("Bounded domain does not duplicate another network"),SelectedFields[1]>0);
    TestTrue(TEXT("Native domain bounds retain selected geometry"),One->GetLocalBounds().Contains(Centre*100));TestTrue(TEXT("Native domain bounds exclude remote geometry"),!One->GetLocalBounds().Contains((Other->A+Other->B)*50));
    auto Filled=R;Filled.Volumes.bEnabled=true;FCLSolidOperation O;O.Id=FGuid(1,2,3,4);O.Name=TEXT("Inspection backfill");O.CenterMeters=Centre;O.RadiusMeters=5;Filled.Volumes.Operations={O};const auto FP=FCLPlanetCompiler::Compile(Filled,E);if(!FP){AddError(E);return false;}FCLScene::FromPlanet(FP,E)->SampleCaveVoid({Points},Void,E);TestTrue(TEXT("Backfill removes the filled cave from inspection"),Void[0].FieldMeters>0);
    O.Composition=ECLSolidComposition::Subtract;O.CenterMeters=Points[1]-D*2;Filled.Volumes.Operations={O};const auto EP=FCLPlanetCompiler::Compile(Filled,E);if(!EP){AddError(E);return false;}FCLScene::FromPlanet(EP,E)->SampleCaveVoid({Points},Void,E);TestTrue(TEXT("Unrelated authored excavation is not mislabelled generated cave"),Void[1].FieldMeters>0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLCaveProgressionTest,"Catalyst.Landform.Caves.KnobAndBackground",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLCaveProgressionTest::RunTest(const FString&)
{
    FString E;const auto R=CaveRecipe(E);auto Off=R;Off.Caves.bEnabled=false;const auto Base=FCLPlanetCompiler::Compile(Off,E);if(!Base){AddError(E);return false;}
    TSet<int32> Counts;double PreviousRadius=0;for(int32 I=0;I<=10;++I){auto S=R.Caves;S.ApplyProgression(I/10.);TestTrue(TEXT("Every knob position validates"),S.Validate(E));const auto C=FCLCompiledCaves::Compile(S,*Base,R.Seed,E);if(!C){AddError(E);return false;}Counts.Add(C->Segments.Num());TestTrue(TEXT("Cave aperture develops across the knob"),S.RadiusMeters>PreviousRadius);PreviousRadius=S.RadiusMeters;if(I==0)TestTrue(TEXT("Enabled at zero still has its minimum cave"),!C->Segments.IsEmpty());}
    TestTrue(TEXT("Knob changes occurrence and branching as well as dimensions"),Counts.Num()>2);
    const auto P=FCLPlanetCompiler::Compile(R,E);auto S=R.Caves;++S.Seed;const auto V=FCLCompiledCaves::Compile(S,*Base,R.Seed,E);TestTrue(TEXT("Reseeding changes the deterministic underground layout"),V&&(V->Segments.Num()!=P->Caves->Segments.Num()||(!V->Segments.IsEmpty()&&V->Segments[0].A!=P->Caves->Segments[0].A)));
    auto Large=MakeShared<FCLCompiledPlanetRecipe>(*Base);Large->RadiusMeters=100000;TestFalse(TEXT("Physical candidate budget never silently coarsens a large planet"),FCLCompiledCaves::Compile(R.Caves,*Large,R.Seed,E).IsValid());
    const auto Prepared=FCLPlanetCompiler::PrepareBackground(R,E);if(!Prepared){AddError(E);return false;}const auto Control=MakeShared<FCLCompileProgress>();TSharedPtr<const FCLCompiledPlanetRecipe> Worker;FString WE;
    auto Task=UE::Tasks::Launch(UE_SOURCE_LOCATION,[&]{Worker=FCLPlanetCompiler::CompleteBackground(*Prepared,Control,WE);});Task.Wait();if(!Worker){AddError(WE);return false;}TestEqual(TEXT("Existing background worker produces the same native cave input"),Worker->Caves->Segments.Num(),P->Caves->Segments.Num());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLCaveSmallPlanetTest,"Catalyst.Landform.Caves.SmallPlanetMinimumAndScale",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLCaveSmallPlanetTest::RunTest(const FString&)
{
    FString J,E;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/CavesSmall.json")));
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>());if(!A->ImportRecipeJson(J,E)){AddError(E);return false;}const auto R=A->Recipe;const auto P=FCLPlanetCompiler::Compile(R,E);if(!P||!P->Caves){AddError(E);return false;}
    TestEqual(TEXT("Frozen saved small planet is 500 m"),R.RadiusMeters,500.);TestTrue(TEXT("Saved high-end recipe now supplies multiple complete caves without moving the knob"),P->Caves->Networks>=3);TestEqual(TEXT("Legacy dimensions remain authored"),R.Caves.LengthMeters,92.);TestEqual(TEXT("Legacy automatic density upgrades on import"),R.Caves.NetworksPerSquareKilometer,6.);AddInfo(P->Caves->Readiness);
    auto Off=R;Off.Caves.bEnabled=false;const auto Base=FCLPlanetCompiler::Compile(Off,E);if(!Base){AddError(E);return false;}
    for(int32 Seed=73;Seed<81;++Seed){auto C=R.Caves;C.Seed=Seed;const auto V=FCLCompiledCaves::Compile(C,*Base,R.Seed,E);TestTrue(TEXT("Each seed supplies a cave or explicitly refuses incompatible geometry"),(V&&V->Networks>0)||(!V&&E.Contains(TEXT("no complete"))));if(V)AddInfo(FString::Printf(TEXT("seed %d: %s"),Seed,*V->Readiness));}
    for(double Value:{0.,.01,.25,.5,1.}){auto C=R.Caves;C.ApplyProgression(Value);const auto V=FCLCompiledCaves::Compile(C,*Base,R.Seed,E);if(!V){AddError(E);return false;}TestTrue(TEXT("Every enabled knob position produces at least one cave"),V->Networks>=1);}
    auto Density=R.Caves;Density.NetworksPerSquareKilometer=0;const auto Zero=FCLCompiledCaves::Compile(Density,*Base,R.Seed,E);TestTrue(TEXT("Zero density means no extras beyond the enabled minimum"),Zero&&Zero->Networks==1);
    Density.bEnabled=false;const auto Disabled=FCLCompiledCaves::Compile(Density,*Base,R.Seed,E);TestTrue(TEXT("The eye toggle is the way to produce zero caves"),Disabled&&Disabled->Segments.IsEmpty());
    auto Unknown=MakeShared<FCLCompiledPlanetRecipe>(*Base);Unknown->Rock.Reset();const auto Missing=FCLCompiledCaves::Compile(R.Caves,*Unknown,R.Seed,E);TestTrue(TEXT("A minimum does not bypass missing geological prerequisites"),!Missing&&E.Contains(TEXT("require")));
    auto Original=CaveRecipe(E);Original.Caves.ApplyProgression(1.);const auto Large=FCLPlanetCompiler::Compile(Original,E);TestTrue(TEXT("Density still adds networks on the existing reference planet"),Large&&Large->Caves->Networks>P->Caves->Networks&&!Large->Caves->Readiness.Contains(TEXT("minimum applied")));
    const auto Reload=FCLPlanetCompiler::Compile(R,E);TestTrue(TEXT("Minimum network identity is deterministic"),Reload&&Reload->Caves->Segments[0].Network==P->Caves->Segments[0].Network&&Reload->Caves->Segments[0].A==P->Caves->Segments[0].A);return true;
}
#endif
