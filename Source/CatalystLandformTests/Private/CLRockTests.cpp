#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Model/CLPlanetRecipe.h"
#include "Scene/CLScene.h"
#include "CLPlanetStamp.h"
#include "CLRockQueryLibrary.h"
#include "VoxelLinearColorMetadata.h"
#include "VoxelLayers.h"
#include "VoxelQuery.h"
#include "Surface/VoxelSurfaceTypeTable.h"
#include "Async/Async.h"
namespace
{
FCLPlanetRecipe RockRecipe(double Position=.65)
{
    auto R=FCLPlanetCompiler::ContinentalBase();R.UseGuidedGeology();R.SchemaVersion=20;R.Rock.bEnabled=true;R.Rock.ApplyProgression(Position);return R;
}
TArray<FVector> RockPoints(int32 N)
{
    TArray<FVector> P;for(int32 I=0;I<N;++I){const double Z=1-2*(I+.5)/N,A=I*2.399963229728653;P.Add(FVector(FMath::Cos(A)*FMath::Sqrt(1-Z*Z),FMath::Sin(A)*FMath::Sqrt(1-Z*Z),Z)*(170+420*((I*17)%N)/double(N)));}return P;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLRockVolumeTest,"Catalyst.Landform.Rock.VolumeAndCaveCoherence",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLRockVolumeTest::RunTest(const FString&)
{
    FString Error;auto R=RockRecipe();const auto Base=FCLPlanetCompiler::Compile(R,Error);if(!Base){AddError(Error);return false;}
    const auto Scene=FCLScene::FromPlanet(Base,Error);const auto P=RockPoints(4096);TArray<FCLRockSample> A;Scene->SampleGeology({P},A,Error);
    FCLSolidOperation Cut;Cut.Id=FGuid(1,2,3,4);Cut.Name=TEXT("Rock cave");Cut.Primitive=ECLSolidPrimitive::Box;Cut.Composition=ECLSolidComposition::Subtract;Cut.CenterMeters=FVector(0,0,250);Cut.HalfExtentsMeters=FVector(15,15,25);R.Volumes.bEnabled=true;R.Volumes.Operations={Cut};
    const auto Edited=FCLPlanetCompiler::Compile(R,Error);if(!Edited){AddError(Error);return false;}const auto Cave=FCLScene::FromPlanet(Edited,Error);TArray<FCLRockSample> B;Cave->SampleGeology({P},B,Error);
    TSet<FGuid> Units,Strata;
    for(int32 I=0;I<P.Num();++I)
    {
        TestTrue(TEXT("Caves retain the original 3D geological identity"),A[I].bValid&&A[I].UnitId==B[I].UnitId&&A[I].StratumId==B[I].StratumId&&A[I].MiningResistance==B[I].MiningResistance);Units.Add(A[I].UnitId);Strata.Add(A[I].StratumId);
    }
    TestTrue(TEXT("Multiple units and actual beds exist underground"),Units.Num()==3&&Strata.Num()>10);
    const TArray<FVector> Center{Cut.CenterMeters};TArray<FCLSceneShapeSample> Shape;Scene->SampleShape({Center},Shape,Error);TestTrue(TEXT("Cave actually removes initially solid terrain"),Shape[0].FieldMeters<0);Cave->SampleShape({Center},Shape,Error);TestTrue(TEXT("Native composed cave is air"),Shape[0].FieldMeters>0);
    const auto Worker=Async(EAsyncExecution::ThreadPool,[Scene,P]{TArray<FCLRockSample> S;FString E;Scene->SampleGeology({P},S,E);return S;});const auto W=Worker.Get();TestEqual(TEXT("Immutable worker keeps stratum identity"),W[997].StratumId,A[997].StratumId);
    AddInfo(FString::Printf(TEXT("4096 3D probes, %d rock units and %d strata; cave leaves geology unchanged"),Units.Num(),Strata.Num()));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLRockControlTest,"Catalyst.Landform.Rock.ControlsVersionsAndHeight",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLRockControlTest::RunTest(const FString&)
{
    FString Error;const auto Points=RockPoints(1024);TArray<double> Previous;TSet<FGuid> First;
    for(int32 Seed:{1729,817,529})for(double Position:{0.,.15,.30,.45,.60,.75,.90,1.})
    {
        auto R=RockRecipe(Position);R.Seed=Seed;const auto P=FCLPlanetCompiler::Compile(R,Error);if(!P){AddError(Error);return false;}R.Rock.bEnabled=false;const auto Off=FCLPlanetCompiler::Compile(R,Error);if(!Off){AddError(Error);return false;}
        TArray<double> A,B;P->SampleHeightsMeters(Points,A);Off->SampleHeightsMeters(Points,B);for(int32 I=0;I<A.Num();++I)TestEqual(TEXT("Rock controls never add a second terrain-height contribution"),A[I],B[I]);
        TSet<FGuid> Units;
        for(const auto& Point:Points){const auto S=P->Rock->Sample(Point);TestTrue(TEXT("Every reviewed position supplies bounded mining properties"),S.bValid&&S.MiningResistance>=0&&S.MiningResistance<=1&&S.Erodibility>=0&&S.Erodibility<=1);Units.Add(S.UnitId);}
        if(Position==0)TestEqual(TEXT("Zero structure is homogeneous bedrock"),Units.Num(),1);else if(Position>=.45)TestEqual(*FString::Printf(TEXT("Developed settings retain three rock types, seed %d character %.2f"),Seed,Position),Units.Num(),3);
    }
    auto R=RockRecipe();R.SchemaVersion=18;TestFalse(TEXT("Old schemas cannot acquire new geology silently"),FCLPlanetCompiler::Compile(R,Error).IsValid());R.SchemaVersion=20;
    R.Rock.Units[1].Id=R.Rock.Units[0].Id;TestFalse(TEXT("Duplicate identities fail closed"),FCLPlanetCompiler::Compile(R,Error).IsValid());R=RockRecipe();R.Rock.Units[1].SurfaceId=TEXT("MissingRockSurface");TestFalse(TEXT("Missing native surfaces fail closed"),FCLPlanetCompiler::Compile(R,Error).IsValid());
    R=RockRecipe();R.Rock.BedThicknessRadiusFraction=0;TestFalse(TEXT("Zero bed thickness fails closed"),FCLPlanetCompiler::Compile(R,Error).IsValid());
    R=RockRecipe();R.Rock.AlgorithmVersion=1;R.Tectonics.bEnabled=false;R.Rock.DipDegrees=R.Rock.FoldAmplitudeRadiusFraction=R.Rock.FaultThrowRadiusFraction=R.Rock.TectonicResponse=0;R.Rock.BedThicknessRadiusFraction=.04;
    const auto Parallel=FCLPlanetCompiler::Compile(R,Error);if(!Parallel){AddError(Error);return false;}const auto Low=Parallel->Rock->Sample(FVector(0,0,700)),High=Parallel->Rock->Sample(FVector(0,0,740));
    TestTrue(TEXT("Independent planar-bed period oracle: one 40 m cycle repeats the unit, advances three beds and has a distinct identity"),Low.UnitId==High.UnitId&&High.StratumIndex-Low.StratumIndex==3&&Low.StratumId!=High.StratumId&&FMath::IsNearlyEqual(Low.MiningResistance,High.MiningResistance));
    auto Center=RockRecipe();Center.Rock.AlgorithmVersion=2;Center.Rock.CrustDepthRadiusFraction=0;
    const auto LegacyCenter=FCLCompiledRock::Compile(Center.Rock,Center.Seed,1000,0,800,Parallel->Surfaces.Get(),nullptr,Error);
    if(!LegacyCenter){AddError(Error);return false;}
    TestEqual(TEXT("V2 with zero basement retains its original finite-bed result at the exact origin"),LegacyCenter->Sample(FVector::ZeroVector).StratumIndex,int64(0));
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>()),B(NewObject<UCLPlanetRecipeAsset>());A->Recipe=RockRecipe();FString Json;TestTrue(TEXT("Resolved geology exports"),A->ExportJsonString(Json,Error));TestTrue(TEXT("Resolved geology imports"),B->ImportRecipeJson(Json,Error));TestTrue(TEXT("All resolved data and IDs round-trip"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&A->Recipe,&B->Recipe,0));
    AddInfo(TEXT("24 seed/character cases retain exact terrain heights; zero, unit closure, versions and portable data pass"));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLRockNativeTest,"Catalyst.Landform.Rock.NativePropertiesAndSurfaces",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLRockNativeTest::RunTest(const FString&)
{
    FString Error;auto R=RockRecipe();const auto P=FCLPlanetCompiler::Compile(R,Error);if(!P){AddError(Error);return false;}
    FCLPlanetStamp Stamp;Stamp.bUseRecipe=true;Stamp.Radius=R.RadiusMeters*100;Stamp.Recipe=R;Stamp.Behavior=EVoxelStampBehavior::AffectAll;
    const auto Ref=FVoxelStampRef::New(Stamp);const auto Native=StaticCastSharedPtr<FCLPlanetStampRuntime>(FVoxelStampRuntime::Create(nullptr,Ref,nullptr));if(!Native){AddError(TEXT("Rock native metadata binding failed"));return false;}
    auto* M=UCLRockQueryLibrary::GetPropertiesMetadata();if(!M){AddError(TEXT("Missing native rock metadata asset"));return false;}const FVoxelMetadataRef Meta(M);
    const auto Points=RockPoints(2048);FVoxelDoubleVectorBuffer XYZ;XYZ.Allocate(Points.Num());TArray<float> Distance;Distance.Init(MAX_flt,Points.Num());TVoxelArray<FVoxelSurfaceTypeBlend> Types;Types.SetNum(Points.Num());
    for(int32 I=0;I<Points.Num();++I)XYZ.Set(I,Points[I]*100);
    TVoxelMap<FVoxelMetadataRef,TSharedRef<FVoxelBuffer>> Buffers;const auto Buffer=Meta.MakeDefaultBuffer(Points.Num());Buffers.Add_CheckNew(Meta,Buffer);const FVoxelMetadataMap Map(Buffers);
    FVoxelDependencyCollector Dependencies(STATIC_FNAME("CL.Rock.Native"));const auto Table=FVoxelSurfaceTypeTable::Get();FVoxelQuery Query(0,FVoxelLayers::Empty(),*Table,Dependencies);
    const auto Q=FVoxelVolumeSparseQuery::Create(Query,MakeVoxelArrayView(Distance),Types,Map,XYZ,true,{Meta});Native->Apply(Q,FVoxelVolumeTransform());
    const auto& Values=Buffer->AsChecked<FVoxelLinearColorBuffer>();double Worst=0;
    for(int32 I=0;I<Points.Num();++I){const auto Expected=P->Rock->Properties(Points[I]);const auto Value=Values[I];Worst=FMath::Max(Worst,double(FMath::Abs(Expected.R-Value.R)+FMath::Abs(Expected.G-Value.G)));TestTrue(TEXT("Native surface belongs to a resolved client unit"),!Types[I].IsNull());}
    TestTrue(TEXT("Native VPP metadata preserves rock properties"),Worst<1.e-6);AddInfo(FString::Printf(TEXT("2048 native material/property probes; property error %.9g"),Worst));return true;
}
#endif
