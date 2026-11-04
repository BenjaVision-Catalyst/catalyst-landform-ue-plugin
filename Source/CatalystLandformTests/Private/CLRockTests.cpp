#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLRockPaletteTest,"Catalyst.Landform.Rock.CharacterPaletteAndDefaults",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLRockPaletteTest::RunTest(const FString&)
{
    // Default values are part of UE's saved-asset contract, independent of the
    // current authoring curves. These came from the previous native binary.
    FCLRockLayer Legacy;
    TestEqual(TEXT("Omitted algorithm retains the original profile"),Legacy.AlgorithmVersion,1);
    TestEqual(TEXT("Published default bed thickness"),Legacy.BedThicknessRadiusFraction,.04990661443148686);
    TestEqual(TEXT("Published default interbeds"),Legacy.InterbedFraction,.39);
    TestEqual(TEXT("Published default dip"),Legacy.DipDegrees,8.518518518518519);
    TestEqual(TEXT("Published default wavelength"),Legacy.FoldWavelengthRadiusFraction,.42777777777777776);
    TestEqual(TEXT("Published default response"),Legacy.TectonicResponse,.32592592592592595);
    TestEqual(TEXT("Published default fault throw"),Legacy.FaultThrowRadiusFraction,.001056000000000001);
    TestTrue(TEXT("Published default deposition stays absent"),Legacy.YoungerCoverRadiusFraction==0&&Legacy.DepositionalVariation==0);
    auto Authored=Legacy;Authored.AlgorithmVersion=4;Authored.Seed=817;Authored.FoldCompetentUnit=Authored.Units[1].Id;Authored.FoldViscosityContrast=48;Authored.CrustDepthRadiusFraction=.22;Authored.ContactFeatherFraction=.01;Authored.Units[1].MiningResistance=.63;
    const auto Before=Authored;Authored.ApplyProgression(.58);
    TestTrue(TEXT("Character preserves authored palette, seed, rheology and independent depth/contact controls"),Authored.Seed==817&&Authored.FoldCompetentUnit==Before.FoldCompetentUnit&&Authored.FoldViscosityContrast==48&&Authored.CrustDepthRadiusFraction==.22&&Authored.ContactFeatherFraction==.01&&Authored.Units[1].MiningResistance==.63);
    const FString Folder=FPaths::ProjectSavedDir()/TEXT("Verification/rock-character");IFileManager::Get().MakeDirectory(*Folder,true);
    FString Metrics=TEXT("seed,position,beds,weak_fraction,restoration_range_m,changed_with_fixed_spacing\n");
    FString Error;int32 Reviewed=0;
    for(int32 Seed:{1729,817,529})
    {
        TArray<double> Previous;TArray<FVector> SolidPoints;double PreviousShape=0;
        for(double Position:{0.,.18,.38,.58,.78,1.})
        {
            auto R=RockRecipe(Position);R.Seed=Seed;
            // Hold spacing fixed: a character change must still change lithology
            // and restored structure, rather than passing by varying bed height.
            R.Rock.BedThicknessRadiusFraction=.03;const auto P=FCLPlanetCompiler::Compile(R,Error);if(!P){AddError(Error);return false;}
            const double Bottom=FMath::Max(0.,R.RadiusMeters+P->Guides.FindByPredicate([](const auto& G){return G.Name==TEXT("SurfaceMin");})->HeightMeters-R.Rock.CrustDepthRadiusFraction*R.RadiusMeters);
            if(SolidPoints.IsEmpty())
            {
                TArray<FVector> Directions;for(int32 I=0;I<2048;++I){const double Z=1-2*(I+.5)/2048,A=I*2.399963229728653;Directions.Add(FVector(FMath::Cos(A)*FMath::Sqrt(1-Z*Z),FMath::Sin(A)*FMath::Sqrt(1-Z*Z),Z));}
                TArray<double> Heights;P->SampleHeightsMeters(Directions,Heights);
                for(int32 I=0;I<2048;++I)SolidPoints.Add(Directions[I]*FMath::Lerp(Bottom,R.RadiusMeters+Heights[I],.05+.9*((I*17)%2048)/2048.));
            }
            TArray<double> Current;int32 Weak=0,Changed=0;double Lo=DBL_MAX,Hi=-DBL_MAX;
            for(int32 I=0;I<2048;++I)
            {
                const auto V=SolidPoints[I];const auto D=V.GetSafeNormal();
                const auto G=P->Rock->Sample(V);Current.Add(G.MiningResistance);Weak+=G.UnitId==R.Rock.Units[2].Id;
                if(Previous.Num())Changed+=FMath::Abs(Previous[I]-Current[I])>.05;
                const double Offset=P->Rock->RestoredRadiusMeters(D*700)-700;Lo=FMath::Min(Lo,Offset);Hi=FMath::Max(Hi,Offset);
            }
            if(Previous.Num())TestTrue(*FString::Printf(TEXT("Character %.2f changes at least 5%% of the actual geological properties with identical bed spacing, seed %d"),Position,Seed),Changed>102);
            if(Position==.58)TestTrue(TEXT("Folded interval restores a different structure from the laminated interval"),FMath::Abs((Hi-Lo)-PreviousShape)>1.);
            Metrics+=FString::Printf(TEXT("%d,%.2f,%d,%.9f,%.9f,%d\n"),Seed,Position,P->Rock->GetBeds().Num(),Weak/2048.,Hi-Lo,Changed);
            Previous=MoveTemp(Current);PreviousShape=Hi-Lo;++Reviewed;
            if(Seed==1729)
            {
                // Diagnostic field sections use the normal curved profile.
                R.Rock.ApplyProgression(Position);const auto Section=FCLPlanetCompiler::Compile(R,Error);if(!Section){AddError(Error);return false;}
                const double Top=R.RadiusMeters+Section->Guides.FindByPredicate([](const auto& G){return G.Name==TEXT("SurfaceMax");})->HeightMeters;
                TArray<FVector> SectionPoints;
                for(int32 Z=0;Z<=160;++Z)for(int32 X=0;X<=240;++X)SectionPoints.Add(FVector(-.65*Top+1.3*Top*X/240.,0,Bottom+(Top-Bottom)*Z/160.));
                TArray<double> Heights;Section->SampleHeightsMeters(SectionPoints,Heights);
                FString CSV=TEXT("x_m,z_m,unit,stratum,solid\n");
                for(int32 I=0;I<SectionPoints.Num();++I)
                {
                    const auto V=SectionPoints[I];const auto G=Section->Rock->Sample(V);
                    const int32 Unit=R.Rock.Units.IndexOfByPredicate([&](const FCLRockUnit& U){return U.Id==G.UnitId;});
                    CSV+=FString::Printf(TEXT("%.6f,%.6f,%d,%lld,%d\n"),V.X,V.Z,Unit,G.StratumIndex,V.Length()<=R.RadiusMeters+Heights[I]?1:0);
                }
                TestTrue(TEXT("Character field section exports"),FFileHelper::SaveStringToFile(CSV,*(Folder/FString::Printf(TEXT("character-%02d.csv"),FMath::RoundToInt(Position*100)))));
            }
        }
        for(int32 I=0;I<=200;++I)
        {
            auto R=RockRecipe(I/200.);R.Seed=Seed;const auto P=FCLPlanetCompiler::Compile(R,Error);if(!P){AddError(FString::Printf(TEXT("Seed %d position %.3f: %s"),Seed,I/200.,*Error));return false;}
            const auto G=P->Rock->Sample(FVector(125,0,700));TestTrue(TEXT("Every intermediate position compiles with bounded mining properties"),G.bValid&&FMath::IsFinite(G.MiningResistance)&&G.MiningResistance>=0&&G.MiningResistance<=1);
        }
    }
    TestTrue(TEXT("Character metrics export"),FFileHelper::SaveStringToFile(Metrics,*(Folder/TEXT("metrics.csv"))));
    AddInfo(FString::Printf(TEXT("603 intermediate profiles and %d fixed-spacing geological comparisons across three seeds"),Reviewed));return true;
}
#endif
