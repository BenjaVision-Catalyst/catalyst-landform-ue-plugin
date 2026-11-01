#if WITH_DEV_AUTOMATION_TESTS
#include "Surface/CLSurfaceCompiler.h"
#include "Model/CLTerrainType.h"
#include "Model/CLRegionGeneration.h"
#include "Model/CLSphericalPatches.h"
#include "CLPlanetStamp.h"
#include "CLSectionGeometry.h"
#include "CLSurfacePreview.h"
#include "Surface/VoxelSurfaceTypeTable.h"
#include "VoxelLayers.h"
#include "VoxelQuery.h"
#include "VoxelStampQuery.h"
#include "VoxelStampTransform.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Interfaces/IPluginManager.h"
#include "Async/Async.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Surface/VoxelSurfaceTypeAsset.h"
namespace
{
bool SurfaceRecipe(FCLPlanetRecipe& R,FString& E)
{
    FString J;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/TerrainSurfaces.json")));
    auto* A=NewObject<UCLPlanetRecipeAsset>();if(!A->ImportRecipeJson(J,E))return false;R=A->Recipe;return true;
}
double SurfaceDifference(const FCLSurfaceWeights& A,const FCLSurfaceWeights& B){double E=0;for(int32 I=0;I<8;++I)E=FMath::Max(E,FMath::Abs(A.Values[I]-B.Values[I]));return E;}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLSurfaceRulesTest,"Catalyst.Landform.Surfaces.RuleContract",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLSurfaceRulesTest::RunTest(const FString&)
{
    FCLCompiledSurfaceLibrary L;L.Ids={TEXT("Rock"),TEXT("Scree"),TEXT("Soil")};L.Revisions={1,1,1};FString E;
    FCLSurfaceStack S;S.DefaultSurfaceId=TEXT("Rock");FCLSurfaceLayer A;A.SurfaceId=TEXT("Scree");A.SlopeDegrees=FVector2D(25,45);S.Layers.Add(A);
    FCLCompiledSurfaceStack P;TestTrue(TEXT("Slope rules compile without planet ownership"),FCLCompiledSurfaceStack::Compile(S,L,{},1000,7,P,E));
    for(const auto& Pair:TArray<FVector2D>{{0,0},{35,1},{90,0}})TestTrue(TEXT("Slope band selects its interior and rejects flat/steep ground"),FMath::IsNearlyEqual(P.Evaluate(FVector(0,0,500),-500,Pair.X).Values[1],Pair.Y,1.e-10));
    S.Layers[0].Strength=.5;A.SurfaceId=TEXT("Soil");A.SlopeDegrees=FVector2D(0,90);A.Strength=.25;S.Layers.Add(A);P={};TestTrue(TEXT("Ordered overlays compile"),FCLCompiledSurfaceStack::Compile(S,L,{},1000,7,P,E));auto W=P.Evaluate(FVector(0,0,500),-500,35);
    TestTrue(TEXT("Overlay order preserves normalized uncovered/default weights"),FMath::IsNearlyEqual(W.Values[0],.375)&&FMath::IsNearlyEqual(W.Values[1],.375)&&FMath::IsNearlyEqual(W.Values[2],.25));
    S.Layers.Last().bUseHeightRange=true;S.Layers.Last().LowerHeight.Guide=TEXT("SurfaceMin");S.Layers.Last().UpperHeight.Guide=TEXT("SurfaceMax");
    TArray<FCLHeightGuide> Guides;FCLHeightGuide G;G.Name=TEXT("SurfaceMin");G.HeightMeters=-600;Guides.Add(G);G.Name=TEXT("SurfaceMax");G.HeightMeters=-400;Guides.Add(G);
    P={};TestTrue(TEXT("Height guide mask resolves in supplied context"),FCLCompiledSurfaceStack::Compile(S,L,Guides,1000,7,P,E));TestTrue(TEXT("Height rejects soil outside guide range"),P.Evaluate(FVector(0,0,100),-900,35).Values[2]==0);
    S.Layers.Last().NoiseAmount=1;P={};FCLCompiledSurfaceStack Q;TestTrue(TEXT("Seeded mask compiles"),FCLCompiledSurfaceStack::Compile(S,L,Guides,1000,7,P,E));TestTrue(TEXT("Repeat compiles"),FCLCompiledSurfaceStack::Compile(S,L,Guides,1000,7,Q,E));
    TestTrue(TEXT("Same seed gives exact surface weights"),SurfaceDifference(P.Evaluate(FVector(1,2,500),-500,35),Q.Evaluate(FVector(1,2,500),-500,35))==0);
    S.Layers.Last().bEnabled=false;P={};TestTrue(TEXT("Disabled overlays compile"),FCLCompiledSurfaceStack::Compile(S,L,Guides,1000,7,P,E));TestEqual(TEXT("Disabled overlay contributes no weight"),P.Evaluate(FVector(0,0,500),-500,35).Values[2],0.);
    S.Layers[0].SlopeDegrees=FVector2D(50,10);TestFalse(TEXT("Reversed slope rejected"),S.Validate(E));S.Layers[0].SlopeDegrees=FVector2D(0,90);S.Layers[0].SurfaceRevision=2;P={};TestFalse(TEXT("Unknown revision rejected"),FCLCompiledSurfaceStack::Compile(S,L,Guides,1000,7,P,E));
    S.Layers[0].SurfaceRevision=1;S.Layers.Last().bEnabled=true;S.Layers.Last().LowerHeight.Guide=TEXT("Missing");P={};TestFalse(TEXT("Unresolved guide rejected"),FCLCompiledSurfaceStack::Compile(S,L,Guides,1000,7,P,E));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLSurfacePayloadTest,"Catalyst.Landform.Surfaces.PortablePayload",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLSurfacePayloadTest::RunTest(const FString&)
{
    FString E;FCLPlanetRecipe R;if(!SurfaceRecipe(R,E)){AddError(E);return false;}
    auto* A=NewObject<UCLPlanetRecipeAsset>();A->Recipe=R;FString J;TestTrue(TEXT("Schema 13 exports"),A->ExportJsonString(J,E));auto* B=NewObject<UCLPlanetRecipeAsset>();TestTrue(TEXT("Schema 13 imports"),B->ImportRecipeJson(J,E));TestTrue(TEXT("Stable surface IDs/rules roundtrip exactly"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&R,&B->Recipe,0));
    auto Bad=R;Bad.SchemaVersion=12;TestFalse(TEXT("Older schema cannot silently accept new semantics"),FCLPlanetCompiler::Compile(Bad,E).IsValid());Bad=R;Bad.SurfaceStack.DefaultSurfaceId=TEXT("NotInstalled");TestFalse(TEXT("Missing client surface fails before execution"),FCLPlanetCompiler::Compile(Bad,E).IsValid());
    FString Unknown=J;Unknown.ReplaceInline(TEXT("\"surfaceStack\":"),TEXT("\"surfaceStak\":"));TestFalse(TEXT("Unknown surface field fails atomic import"),B->ImportRecipeJson(Unknown,E));TestTrue(TEXT("Failed import preserves prior payload"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&R,&B->Recipe,0));
    FString T;FFileHelper::LoadFileToString(T,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/TerrainTypes/MountainRidgesSurfaces.json")));auto* Type=NewObject<UCLTerrainTypeAsset>();TestTrue(TEXT("Terrain schema 8 imports"),Type->ImportTerrainTypeJson(T,E));TestTrue(TEXT("Terrain source exports"),Type->ExportJsonString(J,E));TestFalse(TEXT("Reusable Landform has no embedded planet guides"),J.Contains(TEXT("radialGuides")));
    const auto Radius=R.RadiusMeters;const auto Guides=R.RadialGuides;TestTrue(TEXT("Reusable surface rules apply to contextual planet"),Type->TerrainType.ApplyTo(R,E));TestTrue(TEXT("Apply preserves planet-owned radius and guides"),Radius==R.RadiusMeters&&FCLRadialGuideSet::StaticStruct()->CompareScriptStruct(&Guides,&R.RadialGuides,0));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLSurfaceParityTest,"Catalyst.Landform.Surfaces.NativePreviewAndBoundaries",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLSurfaceParityTest::RunTest(const FString&)
{
    FString E;FCLPlanetRecipe R;if(!SurfaceRecipe(R,E)){AddError(E);return false;}const auto P=FCLPlanetCompiler::Compile(R,E);if(!P){AddError(E);return false;}
    TArray<FVector> D;for(int32 Face=0;Face<6;++Face)for(int32 Y=0;Y<=12;++Y)for(int32 X=0;X<=12;++X)D.Add(FCLSphericalPatches::Direction(Face,FVector2D(X/6.-1,Y/6.-1)));for(const auto& J:P->GeneratedRegions->Junctions)D.Add(J.Direction);
    const double Start=FPlatformTime::Seconds();TArray<FCLSurfaceWeights> W;P->SampleSurfaceWeights(D,W);const double Seconds=FPlatformTime::Seconds()-Start;
    const auto Worker=Async(EAsyncExecution::ThreadPool,[P,D]{TArray<FCLSurfaceWeights> V;P->SampleSurfaceWeights(D,V);return V;});const auto Frozen=Worker.Get();
    double SumError=0,Repeat=0,Min=1,Max=0;int32 Counts[8]={};for(int32 I=0;I<D.Num();++I){double Sum=0;int32 Dominant=0;for(int32 K=0;K<8;++K){Sum+=W[I].Values[K];Min=FMath::Min(Min,W[I].Values[K]);Max=FMath::Max(Max,W[I].Values[K]);if(W[I].Values[K]>W[I].Values[Dominant])Dominant=K;}++Counts[Dominant];SumError=FMath::Max(SumError,FMath::Abs(Sum-1));Repeat=FMath::Max(Repeat,SurfaceDifference(W[I],Frozen[I]));}
    TestTrue(TEXT("Every point has bounded normalized material ownership"),SumError<1.e-10&&Min>=0&&Max<=1.00000001);TestEqual(TEXT("Worker snapshots preserve exact surface decisions"),Repeat,0.);for(int32 I=0;I<3;++I)TestTrue(TEXT("Each authored surface dominates real terrain samples"),Counts[I]>0);
    FCLPlanetStamp S;S.bUseRecipe=true;S.Radius=R.RadiusMeters*100;S.Recipe=R;S.Behavior=EVoxelStampBehavior::AffectAll;const auto Runtime=StaticCastSharedPtr<FCLPlanetStampRuntime>(FVoxelStampRuntime::Create(nullptr,FVoxelStampRef::New(S),nullptr));if(!Runtime){AddError(TEXT("Native surface stamp failed"));return false;}
    FVoxelDependencyCollector Dep(STATIC_FNAME("CL.Surfaces"));const auto Table=FVoxelSurfaceTypeTable::Get();const FVoxelQuery Query(0,FVoxelLayers::Empty(),*Table,Dep);TArray<double> H;P->SampleHeightsMeters(D,H);FVoxelDoubleVectorBuffer Points;Points.Allocate(D.Num());for(int32 I=0;I<D.Num();++I)Points.Set(I,D[I]*(R.RadiusMeters+H[I])*100);
    TVoxelArray<float> Distances;Distances.SetNumUninitialized(D.Num());for(float& V:Distances)V=1.e10f;TVoxelArray<FVoxelSurfaceTypeBlend> Native;Native.SetNum(D.Num());
    const auto Sparse=FVoxelVolumeSparseQuery::Create(Query,Distances,Native,FVoxelMetadataMap(),Points,true,{});Runtime->Apply(Sparse,FVoxelVolumeTransform());double Error=0,Residual=0;
    for(int32 I=0;I<D.Num();++I){FCLSurfaceWeights V;for(const auto& Layer:Native[I].GetLayers()){const int32 K=P->Surfaces->NativeTypes.IndexOfByKey(Layer.Type);if(K!=INDEX_NONE)V.Values[K]=Layer.Weight.ToFloat();}Error=FMath::Max(Error,SurfaceDifference(W[I],V));Residual=FMath::Max(Residual,FMath::Abs(double(Distances[I])));}
    TestTrue(TEXT("Actual native sparse metadata agrees within uint16 quantization"),Error<.0001);TestTrue(TEXT("Materials preserve native geometry"),Residual<.02);
    const auto Section=FCLSectionGeometry::Build(*P,0,500);TArray<FVector> SD;for(const auto& V:Section.SourceSurfacePoints)SD.Add(V.GetSafeNormal());TArray<FCLSurfaceWeights> SW;P->SampleSurfaceWeights(SD,SW);double PreviewError=0;
    for(int32 I=0;I<SW.Num();++I){PreviewError=FMath::Max(PreviewError,SurfaceDifference(SW[I],Section.SurfaceWeights[I]));const auto Color=CLPackSurfaceWeights(SW[I]);TestTrue(TEXT("Section packs exact material weights and radial texture positions"),Color.Equals(Section.Colors[I],.000001)&&Section.SurfaceUV1[I].Y==1&&FMath::IsNearlyEqual(Section.SurfaceUV1[I].X,Section.SourceSurfacePoints[I].Z,.000001));}
    TestEqual(TEXT("Designer evaluates the same final field masks"),PreviewError,0.);
    // Material selection never depends on face owner or world Z: a flat sphere is equally gentle everywhere.
    auto Flat=R;Flat.BaseStructure.Reset();Flat.RegionGeneration.bEnabled=false;Flat.NoiseLayers.Reset();Flat.SurfaceStack=R.RegionGeneration.Palette[2].SurfaceStack;const auto F=FCLPlanetCompiler::Compile(Flat,E);if(!F){AddError(E);return false;}TArray<FCLSurfaceWeights> FW;F->SampleSurfaceWeights(D,FW);for(const auto& V:FW)TestTrue(TEXT("Radial up treats poles, equator and underside as flat"),V.Values[P->Surfaces->Find(TEXT("Soil"),2)]>.8);
    AddInfo(FString::Printf(TEXT("Surface parity: %d spherical samples / %d section vertices, weights %.9g, native %.9g / %.9g cm field; %.3f seconds mask evaluation; dominant %d/%d/%d"),D.Num(),SW.Num(),PreviewError,Error,Residual,Seconds,Counts[0],Counts[1],Counts[2]));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLSurfaceInheritanceTest,"Catalyst.Landform.Surfaces.LegacySourceInheritance",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLSurfaceInheritanceTest::RunTest(const FString&)
{
    FString E;FCLPlanetRecipe R;if(!SurfaceRecipe(R,E)){AddError(E);return false;}
    R.SurfaceStack=R.RegionGeneration.Palette[0].SurfaceStack;R.RegionGeneration.Palette.SetNum(1);R.RegionGeneration.Palette[0].SurfaceStack=FCLSurfaceStack();
    const auto P=FCLPlanetCompiler::Compile(R,E);if(!P){AddError(E);return false;}
    TestEqual(TEXT("A legacy source leaves material ownership to the planet"),P->GeneratedRegions->Fields[0]->SurfaceStack.DefaultIndex,INDEX_NONE);
    const FVector D=FVector(1,2,3).GetSafeNormal();for(double Slope:{0.,20.,40.,80.})
    {
        const auto Expected=P->SurfaceStack.Evaluate(D*500,-500,Slope),Actual=P->SurfaceWeightsAt(D,-500,Slope);
        TestTrue(TEXT("Inherited rules keep the planet's physical coordinates and mask seed"),SurfaceDifference(Expected,Actual)<1.e-10);
    }return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLScannedSurfacesTest,"Catalyst.Landform.Surfaces.ScannedBindingsAndDefaults",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLScannedSurfacesTest::RunTest(const FString&)
{
    FString E;const auto L=FCLCompiledSurfaceLibrary::Resolve(E);if(!L){AddError(E);return false;}
    auto* Preview=CastChecked<UMaterialInstanceDynamic>(CLMakeSurfacePreviewMaterial(GetTransientPackage(),*L));
    for(int32 I=0;I<L->Ids.Num();++I)
    {
        const auto& T=L->Textures[I];TestEqual(TEXT("Scanned artwork has an explicit new revision"),L->Revisions[I],2);
        TestTrue(TEXT("Complete PBR maps retain physical scale"),T.BaseColor&&T.Normal&&T.Roughness&&T.TileMeters>=2&&T.TileMeters<=3);
        if(!T.BaseColor||!T.Normal||!T.Roughness)return false;
        TestTrue(TEXT("Colour is sRGB, normal and roughness are linear"),T.BaseColor->SRGB&&!T.Normal->SRGB&&!T.Roughness->SRGB);
        TestTrue(TEXT("Close range artwork retains 2K scan detail"),T.BaseColor->GetSizeX()==2048&&T.Normal->GetSizeX()==2048);
        const auto* Native=L->Owner->Library.Entries.FindByPredicate([&](const auto& V){return V.Id==L->Ids[I];})->SurfaceType->Material.Get();
        for(const auto& Name:{TEXT("CLBaseColor"),TEXT("CLNormal"),TEXT("CLRoughness")})
        {
            UTexture* A=nullptr;UTexture* B=nullptr;
            TestTrue(TEXT("Native and Designer bind the same scan, not a separate approximation"),Native->GetTextureParameterValue(FMaterialParameterInfo(Name),A)&&Preview->GetTextureParameterValue(FMaterialParameterInfo(FName(*(FString(Name)+LexToString(I)))),B)&&A==B);
        }
        FLinearColor A,B;Native->GetVectorParameterValue(FMaterialParameterInfo(TEXT("CLTextureDetail")),A);Preview->GetVectorParameterValue(FMaterialParameterInfo(FName(*FString::Printf(TEXT("CLTextureDetail%d"),I))),B);
        TestTrue(TEXT("Physical texture scale agrees between native and flattened preview"),A.Equals(B)&&FMath::IsNearlyEqual(double(A.R),T.TileMeters*100));
    }
    auto* Copy=NewObject<UCLSurfaceLibraryAsset>();Copy->Library=L->Owner->Library;Copy->Library.Entries[0].Textures.Normal=nullptr;
    TestFalse(TEXT("Incomplete PBR set is rejected before publication"),Copy->ValidateLibrary(E));Copy->Library=L->Owner->Library;Copy->Library.Entries[0].Textures.TileMeters*=2;
    TestFalse(TEXT("Stale native texture scale is rejected"),Copy->ValidateLibrary(E));
    Copy->Library=L->Owner->Library;Copy->Library.Entries[0].Textures={};TestFalse(TEXT("Removing scans requires rebaking both presentation paths"),Copy->ValidateLibrary(E));
    FCLCompiledSurfaceStack Stack;const auto Default=FCLSurfaceStack::MakeDefault();
    TestTrue(TEXT("New-world defaults compile without planet guide ownership"),FCLCompiledSurfaceStack::Compile(Default,*L,{},1000,7,Stack,E));
    TestTrue(TEXT("Flat ground selects soil; cliffs select rock"),Stack.Evaluate(FVector(500,0,0),-500,0).Values[L->Find(TEXT("Soil"),2)]>.99&&Stack.Evaluate(FVector(500,0,0),-500,80).Values[L->Find(TEXT("Rock"),2)]>.99);
    auto Old=Default;Old.DefaultSurfaceRevision=1;Stack={};TestFalse(TEXT("Placeholder artwork is not silently replaced for a pinned old recipe"),FCLCompiledSurfaceStack::Compile(Old,*L,{},1000,7,Stack,E));
    return true;
}
#endif
