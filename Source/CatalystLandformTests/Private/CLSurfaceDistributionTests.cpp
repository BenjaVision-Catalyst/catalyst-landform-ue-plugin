#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLSurfaceRange.h"
#include "Model/CLSphericalPatches.h"
#include "Scene/CLScene.h"
#include "CLPlanetStamp.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Async/Async.h"
#include "VoxelLayers.h"
#include "VoxelQuery.h"
#include "Surface/VoxelSurfaceTypeTable.h"
namespace
{
FCLPlanetRecipe ScaleRecipe(int32 Area,FString& Error)
{
    FString Json;FFileHelper::LoadFileToString(Json,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/FString::Printf(TEXT("Examples/SurfaceScale%d.json"),Area)));
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>());if(!A->ImportRecipeJson(Json,Error))return {};return A->Recipe;
}
TArray<FVector> AreaProbes(int32 N)
{
    TArray<FVector> D;D.Reserve(N);for(int32 I=0;I<N;++I){const double Z=1-2*(I+.5)/N,A=(I+.827)*2.399963229728653;D.Add(FVector(FMath::Cos(A)*FMath::Sqrt(1-Z*Z),FMath::Sin(A)*FMath::Sqrt(1-Z*Z),Z));}return D;
}
FString ScaleFolder(){const FString P=FPaths::ProjectSavedDir()/TEXT("Verification/surface-scale");IFileManager::Get().MakeDirectory(*P,true);return P;}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLPhysicalScaleTest,"Catalyst.Landform.SurfaceScale.AreaDensityAndMetricSizes",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLPhysicalScaleTest::RunTest(const FString&)
{
    FString Error,Metrics=TEXT("area_km2,count,density_per_km2,min_width_m,max_width_m,wavelength_m,compile_s\n");
    TArray<FCLResolvedStampPlacement> First;
    for(int32 Area:{5,10,20})
    {
        auto R=ScaleRecipe(Area,Error);R.ElevationDistribution.bEnabled=false;
        FCLNoiseLayer Stamps;Stamps.Name=TEXT("Metric native mountain stamps");Stamps.Operation=ECLTerrainOperation::StampDistribution;
        auto& S=Stamps.Distribution;S.DensityPerSquareKilometer=20;S.SizeMode=ECLStampSizeMode::Absolute;S.MinimumSizeMeters=80;S.MaximumSizeMeters=120;S.ReliefMode=ECLStampReliefMode::FootprintRatio;R.NoiseLayers.Add(Stamps);
        const double Started=FPlatformTime::Seconds();const auto P=FCLPlanetCompiler::Compile(R,Error);if(!P){AddError(Error);return false;}
        TArray<FCLResolvedStampPlacement> Placements;P->GetStampPlacements(Placements);
        TestEqual(TEXT("Feature population follows reference area at 20 per km²"),Placements.Num(),Area*20);
        double Low=DBL_MAX,High=0;for(const auto& F:Placements){Low=FMath::Min(Low,F.WidthMeters);High=FMath::Max(High,F.WidthMeters);TestTrue(TEXT("Feature footprint stays 80–120 m as area increases"),F.WidthMeters>=79.999&&F.WidthMeters<=120.001);}
        if(Area==5)First=Placements;
        else for(int32 I=0;I<First.Num();++I)TestEqual(TEXT("Native seeded sizing remains independent of total area"),Placements[I].WidthMeters,First[I].WidthMeters);
        Metrics+=FString::Printf(TEXT("%d,%d,20,%.9f,%.9f,250,%.6f\n"),Area,Placements.Num(),Low,High,FPlatformTime::Seconds()-Started);
        auto TooMany=R;TooMany.NoiseLayers.Last().Distribution.DensityPerSquareKilometer=1100./Area;
        TestFalse(TEXT("The total placement bound refuses excess without reducing density"),FCLPlanetCompiler::Compile(TooMany,Error).IsValid());
    }
    auto R=ScaleRecipe(10,Error);R.NoiseLayers[0].Scale=ECLNoiseScale::PlanetRadius;R.NoiseLayers[0].WavelengthRadiusFraction=.23;
    const auto Original=R;const auto Before=FCLPlanetCompiler::Compile(R,Error);if(!Before){AddError(Error);return false;}
    const double Size=FCLPhysicalDetailScale::MaximumMeters(R);
    TestTrue(TEXT("Physical locking reuses existing terrain fields"),FCLPhysicalDetailScale::SetMaximumMeters(R,Size,Error));
    const auto After=FCLPlanetCompiler::Compile(R,Error);if(!After){AddError(Error);return false;}
    const auto D=AreaProbes(1024);TArray<double> A,B;Before->SampleHeightsMeters(D,A);After->SampleHeightsMeters(D,B);
    for(int32 I=0;I<D.Num();++I)TestEqual(TEXT("Locking the current physical size preserves the exact field"),A[I],B[I]);
    TestTrue(TEXT("Lock preserves global geology and uses native metre mode"),FCLPhysicalDetailScale::UsesMeters(R)&&FCLTectonicPreset::StaticStruct()->CompareScriptStruct(&R.Tectonics,&Original.Tectonics,0)&&FCLContinentalLayer::StaticStruct()->CompareScriptStruct(&R.Continents,&Original.Continents,0));
    auto Empty=R;Empty.NoiseLayers.Reset();FCLPhysicalDetailScale::AddNativeDetail(Empty);FCLPhysicalDetailScale::AddNativeDetail(Empty);TestEqual(TEXT("Adding/enabling detail never duplicates the generated native layer"),Empty.NoiseLayers.Num(),1);
    R.RadiusMeters*=2;TestEqual(TEXT("Resizing a locked planet retains physical detail size"),FCLPhysicalDetailScale::MaximumMeters(R),Size);
    TestTrue(TEXT("Metric population evidence exports"),FFileHelper::SaveStringToFile(Metrics,*(ScaleFolder()/TEXT("density.csv"))));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLSurfaceDistributionTest,"Catalyst.Landform.SurfaceScale.ElevationAreaAndSlope",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLSurfaceDistributionTest::RunTest(const FString&)
{
    FString Error,CSV=TEXT("area_km2,seed,character,low_fraction,high_fraction,median,q25,q75,rms_slope,max_slope\n"),CDF=TEXT("area_km2,seed,character,height,area_below\n");
    const auto D=AreaProbes(131072);double WorstTail=0;
    for(int32 Area:{5,10,20})for(int32 Seed:{1729,817,529})for(double Character:{0.,.5,1.})
    {
        auto R=ScaleRecipe(Area,Error);R.Seed=Seed;R.ElevationDistribution.ApplyProgression(Character);
        const auto P=FCLPlanetCompiler::Compile(R,Error);if(!P){AddError(Error);return false;}
        const auto Range=P->SurfaceRange;TestTrue(TEXT("One new immutable distribution maps the whole surface"),Range&&Range->Distribution);
        TestEqual(TEXT("Minimum remains exactly SurfaceMin"),P->SampleHeightMeters(Range->MinimumDirection),Range->TargetMin);
        TestEqual(TEXT("Maximum remains exactly SurfaceMax"),P->SampleHeightMeters(Range->MaximumDirection),Range->TargetMax);
        TArray<double> H;P->SampleHeightsMeters(D,H);int32 Low=0,High=0;
        for(auto& V:H){V=(V-Range->TargetMin)/(Range->TargetMax-Range->TargetMin);TestTrue(TEXT("Distribution never leaves the allowed base-surface interval"),V>=0&&V<=1);Low+=V<=.1;High+=V>=.9;}H.Sort();
        const double L=double(Low)/H.Num(),U=double(High)/H.Num();WorstTail=FMath::Max(WorstTail,FMath::Max(FMath::Abs(L-.01),FMath::Abs(U-.01)));
        TestTrue(TEXT("Independent equal-area probes retain 1% at each end"),FMath::Abs(L-.01)<.003&&FMath::Abs(U-.01)<.003);
        TestTrue(TEXT("The actual median follows the selected terrain character"),FMath::Abs(H[H.Num()/2]-R.ElevationDistribution.TypicalHeightPercent/100)<.015);
        const auto Target=R.ElevationDistribution.Quantiles();TestTrue(TEXT("Measured quartiles follow the full target curve"),FMath::Abs(H[H.Num()/4]-Target[2].Y)<.015&&FMath::Abs(H[H.Num()*3/4]-Target[4].Y)<.015);
        for(int32 I=0;I<=100;++I)CDF+=FString::Printf(TEXT("%d,%d,%.2f,%.8f,%.2f\n"),Area,Seed,Character,H[FMath::Min(H.Num()-1,I*(H.Num()-1)/100)],I/100.);
        const auto S=AreaProbes(4096);TArray<FVector> Offset;const double Radius=P->RadiusMeters+P->BaseHeightMeters,Angle=2/Radius;
        for(const auto& V:S){FVector X,Y;V.FindBestAxisVectors(X,Y);Offset.Add(V*FMath::Cos(Angle)+X*FMath::Sin(Angle));}
        TArray<double> A,B;P->SampleHeightsMeters(S,A);P->SampleHeightsMeters(Offset,B);double Sum=0,Max=0;
        for(int32 I=0;I<S.Num();++I){const double Slope=FMath::Abs(A[I]-B[I])/2;TestTrue(TEXT("Metric slopes remain finite after vertical shaping"),FMath::IsFinite(Slope));Sum+=Slope*Slope;Max=FMath::Max(Max,Slope);}
        CSV+=FString::Printf(TEXT("%d,%d,%.2f,%.8f,%.8f,%.8f,%.8f,%.8f,%.8f,%.8f\n"),Area,Seed,Character,L,U,H[H.Num()/2],H[H.Num()/4],H[H.Num()*3/4],FMath::Sqrt(Sum/S.Num()),Max);
        if(Seed==1729&&Character==.5)
        {
            // Statistical metric spacing: pairs are separated by true arc metres.
            FString Structure=TEXT("lag_m,mean_squared_height_difference\n");
            for(double Lag:{8.,16.,32.,64.,128.,256.})
            {
                Offset.Reset();for(const auto& V:S){FVector X,Y;V.FindBestAxisVectors(X,Y);Offset.Add(V*FMath::Cos(Lag/Radius)+X*FMath::Sin(Lag/Radius));}
                P->SampleHeightsMeters(Offset,B);double Variance=0;for(int32 I=0;I<S.Num();++I)Variance+=FMath::Square(A[I]-B[I]);
                Structure+=FString::Printf(TEXT("%.0f,%.9f\n"),Lag,Variance/S.Num());
            }
            TestTrue(TEXT("Physical spacing evidence exports"),FFileHelper::SaveStringToFile(Structure,*(ScaleFolder()/FString::Printf(TEXT("spacing-%d.csv"),Area))));
        }
    }
    TestTrue(TEXT("Elevation metrics export"),FFileHelper::SaveStringToFile(CSV,*(ScaleFolder()/TEXT("elevation.csv"))));
    TestTrue(TEXT("Measured curve evidence exports"),FFileHelper::SaveStringToFile(CDF,*(ScaleFolder()/TEXT("cdf.csv"))));
    AddInfo(FString::Printf(TEXT("27 size/seed/character cases; 3,538,944 independent area probes; worst tail error %.6f"),WorstTail));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLRegionalElevationTest,"Catalyst.Landform.SurfaceScale.RegionalBlendsAndNativeVolume",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLRegionalElevationTest::RunTest(const FString&)
{
    FString Error;auto R=ScaleRecipe(10,Error);FCLTerrainRegion Region;Region.Id=FGuid(1,2,3,4);Region.Name=TEXT("Upland region");Region.bSphericalMask=true;Region.SphericalMask.Shape=ECLTerrainRegionShape::HalfPlane;Region.SphericalMask.Axis=FVector::XAxisVector;Region.SphericalMask.OffsetMeters=0;Region.SphericalMask.TransitionMeters=120;Region.TerrainTypeId=FGuid(2,3,4,5);Region.TerrainTypeName=TEXT("Metric regional snapshot");Region.NoiseLayers=R.NoiseLayers;Region.SurfaceStack=R.SurfaceStack;
    Region.ElevationDistribution.bEnabled=true;Region.ElevationDistribution.ApplyProgression(1);R.TerrainRegions={Region};
    const auto P=FCLPlanetCompiler::Compile(R,Error);if(!P){AddError(Error);return false;}const auto D=AreaProbes(65536);TArray<FVector> Local;
    const double Radius=P->RadiusMeters+P->BaseHeightMeters;for(const auto& V:D)if(Region.SphericalMask.Weight(V,Radius)>.9999)Local.Add(V);
    TArray<double> H;P->SampleHeightsMeters(Local,H);const auto Range=P->SurfaceRange;for(auto& V:H)V=(V-Range->TargetMin)/(Range->TargetMax-Range->TargetMin);H.Sort();int32 Low=0,High=0;for(double V:H){Low+=V<=.1;High+=V>=.9;}
    TestTrue(TEXT("A regional profile owns its interior median"),FMath::Abs(H[H.Num()/2]-.78)<.015);
    TestTrue(TEXT("Regional interior keeps independently requested rare ends"),FMath::Abs(double(Low)/H.Num()-.01)<.004&&FMath::Abs(double(High)/H.Num()-.01)<.004);
    TArray<FVector> Seam;for(int32 I=0;I<=100;++I){const double X=-100+2.*I;Seam.Add(FVector(FMath::Sin(X/Radius),0,FMath::Cos(X/Radius)));}
    P->SampleHeightsMeters(Seam,H);double LargestStep=0;for(int32 I=1;I<H.Num();++I)LargestStep=FMath::Max(LargestStep,FMath::Abs(H[I]-H[I-1]));
    TestTrue(TEXT("Existing region fade introduces no height jump"),LargestStep<20);
    FCLPlanetStamp Stamp;Stamp.bUseRecipe=true;Stamp.Radius=R.RadiusMeters*100;Stamp.Recipe=R;const auto Ref=FVoxelStampRef::New(Stamp);const auto Native=StaticCastSharedPtr<FCLPlanetStampRuntime>(FVoxelStampRuntime::Create(nullptr,Ref,nullptr));if(!Native){AddError(TEXT("Native VPP refused schema-21 field"));return false;}
    const auto Probe=AreaProbes(2048);P->SampleHeightsMeters(Probe,H);FVoxelDoubleVectorBuffer Points;Points.Allocate(Probe.Num());for(int32 I=0;I<Probe.Num();++I)Points.Set(I,Probe[I]*(P->RadiusMeters+H[I])*100);
    FVoxelDependencyCollector Dependencies(STATIC_FNAME("CL.SurfaceDistribution"));const auto Table=FVoxelSurfaceTypeTable::Get();FVoxelQuery Query(0,FVoxelLayers::Empty(),*Table,Dependencies);const auto Dist=Native->SampleDistances(Query,Points);double Worst=0;for(int32 I=0;I<Probe.Num();++I)Worst=FMath::Max(Worst,FMath::Abs(double(Dist[I])));
    TestTrue(TEXT("VPP owns the new shaped zero set"),Worst<.03);
    R.Volumes.bEnabled=true;FCLSolidOperation Cave;Cave.Id=FGuid(3,4,5,6);Cave.Name=TEXT("Post-distribution cave");Cave.Primitive=ECLSolidPrimitive::Sphere;Cave.Composition=ECLSolidComposition::Subtract;Cave.CenterMeters=FVector::ZAxisVector*(P->RadiusMeters+P->SampleHeightMeters(FVector::ZAxisVector)-20);Cave.RadiusMeters=12;R.Volumes.Operations={Cave};
    const auto BeforeCave=FCLScene::FromPlanet(P,Error);TArray<FCLSceneShapeSample> BeforeShape;BeforeCave->SampleShape({TArray<FVector>{Cave.CenterMeters}},BeforeShape,Error);TestTrue(TEXT("The cave removes initially solid shaped terrain"),BeforeShape[0].FieldMeters<0);
    const auto Cut=FCLPlanetCompiler::Compile(R,Error);if(!Cut){AddError(Error);return false;}
    TestEqual(TEXT("Caves retain the exact shaped base surface"),Cut->SampleHeightMeters(FVector::ZAxisVector),P->SampleHeightMeters(FVector::ZAxisVector));
    const auto Scene=FCLScene::FromPlanet(Cut,Error);TArray<FCLSceneShapeSample> Shape;Scene->SampleShape({TArray<FVector>{Cave.CenterMeters}},Shape,Error);TestTrue(TEXT("Volume subtraction follows distribution fitting"),Shape[0].FieldMeters>0);
    AddInfo(FString::Printf(TEXT("Regional median %.6f; max 2m blend step %.6f m; 2048 native probes residual %.9g cm"),R.TerrainRegions[0].ElevationDistribution.TypicalHeightPercent,LargestStep,Worst));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLDistributionPersistenceTest,"Catalyst.Landform.SurfaceScale.PersistenceLegacyAndRefusals",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLDistributionPersistenceTest::RunTest(const FString&)
{
    FString Error;auto R=ScaleRecipe(5,Error);TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>()),B(NewObject<UCLPlanetRecipeAsset>());A->Recipe=R;FString Json;
    TestTrue(TEXT("New opt-in fields export"),A->ExportJsonString(Json,Error));TestTrue(TEXT("Schema 21 imports"),B->ImportRecipeJson(Json,Error));TestTrue(TEXT("Resolved controls roundtrip exactly"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&R,&B->Recipe,0));
    const auto P=FCLPlanetCompiler::Compile(R,Error),Q=FCLPlanetCompiler::Compile(B->Recipe,Error);if(!P||!Q){AddError(Error);return false;}
    const auto D=AreaProbes(1024);const auto Work=Async(EAsyncExecution::ThreadPool,[P,D]{TArray<double> H;P->SampleHeightsMeters(D,H);return H;});TArray<double> H;Q->SampleHeightsMeters(D,H);const auto W=Work.Get();for(int32 I=0;I<D.Num();++I)TestEqual(TEXT("Worker snapshot is immutable and deterministic"),H[I],W[I]);
    auto Old=R;Old.SchemaVersion=18;Old.ElevationDistribution.bEnabled=false;const auto Legacy=FCLPlanetCompiler::Compile(Old,Error);if(!Legacy){AddError(Error);return false;}
    auto Disabled=Old;Disabled.ElevationDistribution.TypicalHeightPercent=77;Disabled.ElevationDistribution.HighEndAreaPercent=20;const auto Unchanged=FCLPlanetCompiler::Compile(Disabled,Error);if(!Unchanged){AddError(Error);return false;}
    for(const auto& V:D)TestEqual(TEXT("Disabled new settings cannot change published legacy geometry"),Legacy->SampleHeightMeters(V),Unchanged->SampleHeightMeters(V));
    Old.ElevationDistribution.bEnabled=true;TestFalse(TEXT("Old schemas cannot acquire shaping silently"),FCLPlanetCompiler::Compile(Old,Error).IsValid());
    auto NonNative=R;NonNative.AlgorithmVersion=1;TestFalse(TEXT("New elevation authoring requires the native VPP noise platform"),FCLPlanetCompiler::Compile(NonNative,Error).IsValid());
    auto Mixed=R;Mixed.Rock.bEnabled=true;Mixed.Rock.ApplyProgression(.58);Mixed.Volumes.GeologicalUnits.SetNum(1);
    TestFalse(TEXT("Schema 21 preserves the single geological-source invariant"),FCLPlanetCompiler::Compile(Mixed,Error).IsValid());TestTrue(TEXT("Mixed geology is rejected before legacy unit execution"),Error.Contains(TEXT("one geological source")));
    auto Inactive=R;Inactive.Volumes.Operations.SetNum(1);Inactive.Volumes.Operations[0].RadiusMeters=-1;TestTrue(TEXT("Disabled volume data retains existing disabled semantics"),FCLPlanetCompiler::Compile(Inactive,Error).IsValid());
    auto Bad=R;Bad.ElevationDistribution.HighEndAreaPercent=0;TestFalse(TEXT("Unresolvable zero-probability tails fail closed"),FCLPlanetCompiler::Compile(Bad,Error).IsValid());
    Bad=R;Bad.ElevationDistribution.CalibrationSamples=9999999;TestFalse(TEXT("Calibration allocation is bounded before sampling"),FCLPlanetCompiler::Compile(Bad,Error).IsValid());
    Bad=R;Bad.NoiseLayers.Reset();TestFalse(TEXT("A constant sphere cannot be remapped into a distribution"),FCLPlanetCompiler::Compile(Bad,Error).IsValid());
    Bad=R;Bad.NoiseLayers[0].Minimum.Guide=Bad.NoiseLayers[0].Maximum.Guide=TEXT("SurfaceMin");TestFalse(TEXT("A tied native source is not repaired by injecting noise"),FCLPlanetCompiler::Compile(Bad,Error).IsValid());return true;
}
#endif
