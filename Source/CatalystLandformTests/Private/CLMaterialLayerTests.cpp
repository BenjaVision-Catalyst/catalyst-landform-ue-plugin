#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLRegionGeneration.h"
#include "Scene/CLScene.h"
#include "CLPlanetStamp.h"
#include "CLRockQueryLibrary.h"
#include "VoxelLinearColorMetadata.h"
#include "Surface/VoxelSurfaceTypeTable.h"
#include "VoxelLayers.h"
#include "VoxelQuery.h"
#include "Algo/Reverse.h"

namespace
{
FCLPlanetRecipe DepositRecipe()
{
    auto R=FCLPlanetCompiler::ContinentalBase();R.UseGuidedGeology();R.SchemaVersion=23;
    R.RadiusMeters=600;R.Caves.bEnabled=false;R.Tectonics.bEnabled=false;R.Continents.bEnabled=false;R.NoiseLayers=FCLPlanetCompiler::RollingPlains().NoiseLayers;R.ElevationDistribution.bEnabled=false;
    R.Rock.bEnabled=true;R.SurfaceStack=FCLSurfaceStack::MakeDefault();R.RegionGeneration.bEnabled=true;R.RegionGeneration.AlgorithmVersion=2;
    FCLLandformPaletteEntry P;P.Id=FGuid(1,2,3,4);P.Name=TEXT("Deposit test landform");
    FCLNoiseLayer N;N.Name=TEXT("Zero local relief");N.Blend=ECLNoiseBlend::Add;P.NoiseLayers.Add(N);
    auto& D=P.Deposits.AddDefaulted_GetRef();D.Unit.Id=FGuid(4,3,2,1);D.Unit.Name=TEXT("Alluvial test body");D.Coverage=1;D.ThicknessVariation=0;D.ThicknessMeters=4;D.MaximumSlopeDegrees=90;
    R.RegionGeneration.Palette.Add(P);return R;
}
TSharedPtr<const FCLScene> MakeScene(const FCLPlanetRecipe& R,FString& E,TSharedPtr<const FCLCompiledPlanetRecipe>& P)
{P=FCLPlanetCompiler::Compile(R,E);return P?FCLScene::FromPlanet(P,E):nullptr;}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLMaterialLayerContractTest,"Catalyst.Landform.MaterialLayers.FiniteDepthAndIdentity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLMaterialLayerContractTest::RunTest(const FString&)
{
    FString E;auto R=DepositRecipe();TSharedPtr<const FCLCompiledPlanetRecipe> P;const auto Scene=MakeScene(R,E,P);if(!Scene){AddError(E);return false;}
    auto Bare=R;Bare.RegionGeneration.Palette[0].Deposits.Reset();TSharedPtr<const FCLCompiledPlanetRecipe> BP;const auto Base=MakeScene(Bare,E,BP);if(!Base){AddError(E);return false;}
    TArray<FVector> Points;for(const FVector D:{FVector::XAxisVector,FVector::ZAxisVector,-FVector::ZAxisVector})
        for(const double Depth:{0.,1.,3.,5.,30.})Points.Add(D*(P->RadiusMeters+P->SampleHeightMeters(D)-Depth));
    TArray<FCLMaterialSample> S;Scene->SampleMaterials({Points},S,E);
    TArray<FCLSceneShapeSample> A,B;Scene->SampleShape({Points},A,E);Base->SampleShape({Points},B,E);
    for(int32 I=0;I<Points.Num();++I)
    {
        const bool Covered=I%5<3;TestTrue(TEXT("Finite cover ends before exposed deeper rock at poles and equator"),S[I].Material.Origin==(Covered?ECLMaterialOrigin::LandformDeposit:ECLMaterialOrigin::Rock));
        const auto Rock=P->Rock->Sample(P->RockPositionMeters(Points[I]));
        TestTrue(TEXT("Underlying rock identity survives the cover"),S[I].Substrate.UnitId==Rock.UnitId&&S[I].Substrate.StratumId==Rock.StratumId&&S[I].Material.SubstrateStratumId==Rock.StratumId);
        TestEqual(TEXT("Material classification never modifies terrain density"),A[I].FieldMeters,B[I].FieldMeters);
    }
    auto Cut=R;Cut.Volumes.bEnabled=true;auto& O=Cut.Volumes.Operations.AddDefaulted_GetRef();O.Id=FGuid(6,7,8,9);O.Name=TEXT("Fixed excavation");O.Primitive=ECLSolidPrimitive::Box;O.Composition=ECLSolidComposition::Subtract;O.CenterMeters=Points[3];O.HalfExtentsMeters=FVector(10,10,3);
    TSharedPtr<const FCLCompiledPlanetRecipe> CP;const auto CS=MakeScene(Cut,E,CP);if(!CS){AddError(E);return false;}TArray<FCLMaterialSample> Excavated;CS->SampleMaterials({Points},Excavated,E);
    for(int32 I=0;I<S.Num();++I)TestTrue(TEXT("Excavation cannot drag deposits down onto its new floor"),S[I].Material.UnitId==Excavated[I].Material.UnitId&&S[I].Material.StratumId==Excavated[I].Material.StratumId);
    auto Layered=R;auto Second=Layered.RegionGeneration.Palette[0].Deposits[0];Second.Unit.Id=FGuid(10,20,30,40);Second.Unit.MiningResistance=.07;Second.Priority=20;Layered.RegionGeneration.Palette[0].Deposits.Add(Second);
    TSharedPtr<const FCLCompiledPlanetRecipe> LP,RP;const auto LS=MakeScene(Layered,E,LP);Algo::Reverse(Layered.RegionGeneration.Palette[0].Deposits);const auto RS=MakeScene(Layered,E,RP);if(!LS||!RS){AddError(E);return false;}
    TArray<FCLMaterialSample> L,V;LS->SampleMaterials({Points},L,E);RS->SampleMaterials({Points},V,E);
    for(int32 I=0;I<L.Num();++I){TestTrue(TEXT("Priority is independent of array order"),L[I].Material.UnitId==V[I].Material.UnitId&&L[I].Properties==V[I].Properties);if(I%5<3)TestEqual(TEXT("Shared artwork cannot merge distinct physical identities"),L[I].Material.DepositId,Second.Unit.Id);}
    auto Physical=L[0];const auto Identity=Physical.Material;const auto Props=Physical.Properties;FCLSurfaceWeights Cover;Cover.Values[P->Surfaces->Find(TEXT("Soil"),2)]=1;Physical.OverlayAppearance(Cover,1);
    TestTrue(TEXT("Appearance-only cover preserves physical occupant and properties"),Physical.Material.UnitId==Identity.UnitId&&Physical.Material.StratumId==Identity.StratumId&&Physical.Properties==Props);
    auto Bad=R;Bad.RegionGeneration.Palette[0].Deposits[0].ThicknessMeters=0;TestFalse(TEXT("Invalid dimensions fail before accepting the recipe"),FCLPlanetCompiler::Compile(Bad,E).IsValid());Bad=R;Bad.RegionGeneration.Palette[0].Deposits[0].Unit.SurfaceId=TEXT("UnboundDeposit");TestFalse(TEXT("Missing deposit bindings fail closed"),FCLPlanetCompiler::Compile(Bad,E).IsValid());
    auto* Asset=NewObject<UCLPlanetRecipeAsset>();Asset->Recipe=R;FString Json;TestTrue(TEXT("Deposit portable export"),Asset->ExportJsonString(Json,E));auto* Copy=NewObject<UCLPlanetRecipeAsset>();TestTrue(TEXT("Deposit portable import"),Copy->ImportRecipeJson(Json,E));TestTrue(TEXT("All physical settings/IDs roundtrip"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&R,&Copy->Recipe,0));
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLMaterialLayerNativeTest,"Catalyst.Landform.MaterialLayers.FootprintAndNativeDelivery",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLMaterialLayerNativeTest::RunTest(const FString&)
{
    FString E;auto R=DepositRecipe();auto Other=R.RegionGeneration.Palette[0];Other.Id=FGuid(7,7,7,7);Other.Name=TEXT("Bare landform");Other.Deposits.Reset();R.RegionGeneration.Palette.Add(Other);
    for(const bool Rock:{true,false})
    {
        R.Rock.bEnabled=Rock;TSharedPtr<const FCLCompiledPlanetRecipe> P;const auto Scene=MakeScene(R,E,P);if(!Scene){AddError(E);return false;}
        TArray<FVector> Points;for(const auto& C:P->GeneratedRegions->Cells)for(double Depth:{1.,5.})Points.Add(C.Centre*(P->RadiusMeters+P->SampleHeightMeters(C.Centre)-Depth));
        TArray<FCLMaterialSample> Samples;Scene->SampleMaterials({Points},Samples,E);
        int32 Deposits=0,Bare=0;
        for(int32 I=0;I<Points.Num();I+=2)
        {
            TArray<double> Weights;P->GeneratedRegions->PaletteWeights(Points[I].GetSafeNormal(),Weights);const int32 K=P->GeneratedRegions->Palette.IndexOfByPredicate([&](const auto& X){return X.Id==R.RegionGeneration.Palette[0].Id;});
            if(Weights[K]>.99){++Deposits;TestEqual(TEXT("Assigned landform owns its deposit"),Samples[I].Material.Origin,ECLMaterialOrigin::LandformDeposit);}
            if(Weights[K]==0){++Bare;TestTrue(TEXT("Other landform has no invented deposit"),Samples[I].Material.Origin!=ECLMaterialOrigin::LandformDeposit);}
            TestTrue(TEXT("All deeper points retain substrate without regenerating cover"),Samples[I+1].Material.Origin!=ECLMaterialOrigin::LandformDeposit);
        }
        TestTrue(TEXT("Both covered and uncovered assigned regions are exercised"),Deposits>0&&Bare>0);
        FCLPlanetStamp Stamp;Stamp.bUseRecipe=true;Stamp.Radius=R.RadiusMeters*100;Stamp.Recipe=R;Stamp.CompiledRecipe=P;Stamp.Behavior=EVoxelStampBehavior::AffectAll;
        const auto Native=StaticCastSharedPtr<FCLPlanetStampRuntime>(FVoxelStampRuntime::Create(nullptr,FVoxelStampRef::New(Stamp),nullptr));if(!Native){AddError(TEXT("Native deposit stamp failed"));return false;}
        const FVoxelMetadataRef Meta(UCLRockQueryLibrary::GetPropertiesMetadata());const auto Buffer=Meta.MakeDefaultBuffer(Points.Num());TVoxelMap<FVoxelMetadataRef,TSharedRef<FVoxelBuffer>> Buffers;Buffers.Add_CheckNew(Meta,Buffer);
        FVoxelDoubleVectorBuffer XYZ;XYZ.Allocate(Points.Num());for(int32 I=0;I<Points.Num();++I)XYZ.Set(I,Points[I]*100);
        TArray<float> Distance;Distance.Init(MAX_flt,Points.Num());TVoxelArray<FVoxelSurfaceTypeBlend> Types;Types.SetNum(Points.Num());
        FVoxelDependencyCollector Dependencies(STATIC_FNAME("CL.Deposit.Native"));const auto Table=FVoxelSurfaceTypeTable::Get();FVoxelQuery Q(0,FVoxelLayers::Empty(),*Table,Dependencies);
        Native->Apply(FVoxelVolumeSparseQuery::Create(Q,MakeVoxelArrayView(Distance),Types,FVoxelMetadataMap(Buffers),XYZ,true,{Meta}),FVoxelVolumeTransform());
        const auto& Values=Buffer->AsChecked<FVoxelLinearColorBuffer>();double Error=0;
        for(int32 I=0;I<Points.Num();++I)
        {
            TestTrue(TEXT("Native physical properties equal shared rock/deposit composition"),Values[I].Equals(Samples[I].Properties,1.e-6));FCLSurfaceWeights Actual;
            for(const auto& L:Types[I].GetLayers()){const int32 K=P->Surfaces->NativeTypes.IndexOfByKey(L.Type);if(K!=INDEX_NONE)Actual.Values[K]=L.Weight.ToFloat();}
            for(int32 K=0;K<P->Surfaces->Ids.Num();++K)Error=FMath::Max(Error,FMath::Abs(Actual.Values[K]-Samples[I].Surfaces.Values[K]));
        }
        TestTrue(TEXT("Native appearance and properties use the same finite material field"),Error<1.e-4);
        AddInfo(FString::Printf(TEXT("%d stock VPP material/property probes, Rock %d, surface error %.9g"),Points.Num(),Rock,Error));
    }
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLMaterialLayerPlacedTest,"Catalyst.Landform.MaterialLayers.PlacedFootprintsAndSeed",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLMaterialLayerPlacedTest::RunTest(const FString&)
{
    FString E;auto R=DepositRecipe();FCLTerrainRegion Region;Region.Id=FGuid(12,34,56,78);Region.Name=TEXT("Placed deposit");Region.TerrainTypeId=FGuid(98,76,54,32);Region.TerrainTypeName=TEXT("Placed deposit source");Region.bSphericalMask=true;Region.SphericalMask.Shape=ECLTerrainRegionShape::Disc;Region.SphericalMask.HalfWidthMeters=100;Region.SphericalMask.TransitionMeters=20;Region.NoiseLayers=R.RegionGeneration.Palette[0].NoiseLayers;Region.Deposits=R.RegionGeneration.Palette[0].Deposits;
    R.RegionGeneration.bEnabled=false;R.TerrainRegions.Add(Region);TSharedPtr<const FCLCompiledPlanetRecipe> P;const auto Scene=MakeScene(R,E,P);if(!Scene){AddError(E);return false;}
    TArray<FVector> Points;for(const FVector D:{FVector::ZAxisVector,FVector::XAxisVector,-FVector::ZAxisVector})Points.Add(D*(P->RadiusMeters+P->SampleHeightMeters(D)-1));
    TArray<FCLMaterialSample> S;Scene->SampleMaterials({Points},S,E);
    TestEqual(TEXT("Placed footprint owns material at its centre"),S[0].Material.Origin,ECLMaterialOrigin::LandformDeposit);TestEqual(TEXT("Placed domain has its own source identity"),S[0].Material.SourceId,Region.Id);
    TestEqual(TEXT("Outside placed footprint retains rock"),S[1].Material.Origin,ECLMaterialOrigin::Rock);TestEqual(TEXT("Antipode retains rock"),S[2].Material.Origin,ECLMaterialOrigin::Rock);
    auto Patch=DepositRecipe();Patch.RegionGeneration.Palette[0].Deposits[0].Coverage=.7;Patch.RegionGeneration.Palette[0].Deposits[0].ThicknessVariation=.5;
    TSharedPtr<const FCLCompiledPlanetRecipe> A,B,C;const auto AS=MakeScene(Patch,E,A),BS=MakeScene(Patch,E,B);++Patch.RegionGeneration.Palette[0].Deposits[0].Seed;const auto CS=MakeScene(Patch,E,C);if(!AS||!BS||!CS){AddError(E);return false;}
    Points.Reset();for(int32 I=0;I<512;++I){const double Z=1-2*(I+.5)/512,Angle=I*2.399963229728653;const FVector D(FMath::Cos(Angle)*FMath::Sqrt(1-Z*Z),FMath::Sin(Angle)*FMath::Sqrt(1-Z*Z),Z);Points.Add(D*(A->RadiusMeters+A->SampleHeightMeters(D)-1));}
    TArray<FCLMaterialSample> V,W,X;const double Start=FPlatformTime::Seconds();AS->SampleMaterials({Points},V,E);const double Elapsed=FPlatformTime::Seconds()-Start;BS->SampleMaterials({Points},W,E);CS->SampleMaterials({Points},X,E);int32 Changed=0;
    for(int32 I=0;I<Points.Num();++I){TestTrue(TEXT("Identical seeds reproduce physical identity and weights exactly"),V[I].Material.StratumId==W[I].Material.StratumId&&V[I].Properties==W[I].Properties);Changed+=V[I].Properties!=X[I].Properties;}
    TestTrue(TEXT("Reseeding changes the actual material coverage"),Changed>32);
    auto Composite=S[1];const auto RockId=Composite.Material.UnitId;FCLSurfaceWeights Artwork;Artwork.Values[P->Surfaces->Find(TEXT("Scree"),2)]=1;
    auto D=S[0].Material;Composite.Overlay(Artwork,D,.3);D.UnitId=FGuid(77,77,77,77);Composite.Overlay(Artwork,D,.3);
    TestEqual(TEXT("Two deposits sharing artwork do not outvote the larger rock contribution"),Composite.Material.UnitId,RockId);
    TestEqual(TEXT("Shared physical sample exposes the composed contact resistance"),Composite.Material.MiningResistance,double(Composite.Properties.R));
    TestEqual(TEXT("Shared physical sample exposes the composed contact erodibility"),Composite.Material.Erodibility,double(Composite.Properties.G));
    AddInfo(FString::Printf(TEXT("512 near-surface composition probes %.3f ms, %d change on reseed"),Elapsed*1000,Changed));return !HasAnyErrors();
}
#endif
