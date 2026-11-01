#if WITH_DEV_AUTOMATION_TESTS
#include "Model/CLRegionGeneration.h"
#include "Model/CLTerrainType.h"
#include "CLPlanetStamp.h"
#include "CLSectionGeometry.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Interfaces/IPluginManager.h"
#include "Algo/Reverse.h"
#include "Surface/VoxelSurfaceTypeTable.h"
#include "VoxelLayers.h"
#include "VoxelQuery.h"
namespace
{
bool CharacterRecipe(FCLPlanetRecipe& R,FString& E)
{
    FString J;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/RegionalTerrainCharacter.json")));
    auto* A=NewObject<UCLPlanetRecipeAsset>();if(!A->ImportRecipeJson(J,E))return false;R=A->Recipe;return true;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLRegionalAreaTest,"Catalyst.Landform.RegionalCharacter.AreaAndAssignment",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLRegionalAreaTest::RunTest(const FString&)
{
    FString E;FCLPlanetRecipe R;if(!CharacterRecipe(R,E)){AddError(E);return false;}auto P=FCLPlanetCompiler::Compile(R,E);if(!P){AddError(E);return false;}const auto G=P->GeneratedRegions;
    int32 N=0;double Area=0;TArray<int32> Counts;Counts.Init(0,3);TArray<double> MeanHeight,MeanSlope;MeanHeight.Init(0,3);MeanSlope.Init(0,3);
    for(const auto& C:G->Cells)
    {
        N+=C.CharacterSampleCount;Area+=C.SolidAngle;++Counts[C.PaletteIndex];MeanHeight[C.PaletteIndex]+=C.HeightMeters;MeanSlope[C.PaletteIndex]+=C.UpperSlopeDegrees;
        TestTrue(TEXT("Every cell receives spatial characterization"),C.CharacterSampleCount>20&&C.SolidAngle>0&&C.MinimumBaseHeightMeters<=C.LowerHeightMeters&&C.LowerHeightMeters<=C.UpperHeightMeters&&C.MinimumBaseHeightMeters<=C.HeightMeters&&C.HeightMeters<=C.MaximumBaseHeightMeters&&C.UpperSlopeDegrees>=C.MedianSlopeDegrees);
    }
    TestEqual(TEXT("The equal-area workload is bounded and complete"),N,R.RegionGeneration.RegionCount*128);TestTrue(TEXT("Measured cells cover the whole sphere"),FMath::IsNearlyEqual(Area,4*UE_DOUBLE_PI,1.e-10));
    for(int32 I=0;I<3;++I){TestTrue(TEXT("Every proof Landform has suitable regions"),Counts[I]>0);if(Counts[I]){MeanHeight[I]/=Counts[I];MeanSlope[I]/=Counts[I];}AddInfo(FString::Printf(TEXT("Area source %s: %d regions, mean base radial height %.6f, mean upper slope %.3f"),*G->Palette[I].Name,Counts[I],(R.RadiusMeters+MeanHeight[I])/R.RadiusMeters,MeanSlope[I]));}
    TestTrue(TEXT("Plains occupy lower terrain than plateaus and mountains"),MeanHeight[0]<MeanHeight[2]&&MeanHeight[0]<MeanHeight[1]);
    TestTrue(TEXT("Plateaus favour gentler regions than ridges"),MeanSlope[2]<8&&MeanSlope[1]>MeanSlope[2]);
    auto Reordered=R;Algo::Reverse(Reordered.BaseStructure);Algo::Reverse(Reordered.RegionGeneration.Palette);const auto Q=FCLPlanetCompiler::Compile(Reordered,E);if(!Q){AddError(E);return false;}
    for(int32 I=0;I<G->Cells.Num();++I){const auto& A=G->Cells[I];const auto& B=Q->GeneratedRegions->Cells[I];TestTrue(TEXT("Spatial measurements and assignments are deterministic under source order changes"),A.Id==B.Id&&A.Centre==B.Centre&&A.CharacterSampleCount==B.CharacterSampleCount&&A.HeightMeters==B.HeightMeters&&G->Palette[A.PaletteIndex].Id==Q->GeneratedRegions->Palette[B.PaletteIndex].Id);}
    auto Moved=R;Moved.BaseStructure[0].LocationDegrees.X+=80;const auto Changed=FCLPlanetCompiler::Compile(Moved,E);if(!Changed){AddError(E);return false;}
    int32 ChangedAssignments=0;for(int32 I=0;I<G->Cells.Num();++I)ChangedAssignments+=G->Palette[G->Cells[I].PaletteIndex].Id!=Changed->GeneratedRegions->Palette[Changed->GeneratedRegions->Cells[I].PaletteIndex].Id;TestTrue(TEXT("Editing broad geology produces regional reassignment"),ChangedAssignments>0);
    auto Single=R;Single.RegionGeneration.Palette.SetNum(1);const auto One=FCLPlanetCompiler::Compile(Single,E);if(!One){AddError(E);return false;}for(const auto& C:One->GeneratedRegions->Cells)TestEqual(TEXT("Area evaluation still fills every cell for one selected source"),C.PaletteIndex,0);
    auto* A=NewObject<UCLPlanetRecipeAsset>();A->Recipe=R;FString J;TestTrue(TEXT("Area generation exports"),A->ExportJsonString(J,E));auto* Copy=NewObject<UCLPlanetRecipeAsset>();TestTrue(TEXT("Area generation imports"),Copy->ImportRecipeJson(J,E));TestTrue(TEXT("Algorithm and source metadata roundtrip"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&R,&Copy->Recipe,0));
    AddInfo(FString::Printf(TEXT("Area samples %d; reassigned cells after geology move %d; true three-source junctions %d"),N,ChangedAssignments,G->Junctions.Num()));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLRegionalOffCentreTest,"Catalyst.Landform.RegionalCharacter.OffCentreGeology",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLRegionalOffCentreTest::RunTest(const FString&)
{
    FString E;FCLPlanetRecipe R;if(!CharacterRecipe(R,E)){AddError(E);return false;}R.BaseStructure.Reset();R.RegionGeneration.StructureGuidance=0;for(auto& Entry:R.RegionGeneration.Palette)Entry.NoiseLayers.Reset();
    const auto Initial=FCLPlanetCompiler::Compile(R,E);if(!Initial){AddError(E);return false;}const auto& C=Initial->GeneratedRegions->Cells[0];const FVector D=(C.Centre+C.Frame.GetAxisX()*.12).GetSafeNormal();
    FCLPlanetFeature F;F.Id=FGuid(1,2,3,4);F.LocationDegrees=FVector2D(FMath::RadiansToDegrees(FMath::Atan2(D.X,D.Z)),FMath::RadiansToDegrees(FMath::Asin(D.Y)));F.HalfExtentDegrees=FVector2D(4,4);F.Relief.Mode=ECLHeightReferenceMode::IntervalDelta;F.Relief.Guide=TEXT("SurfaceMin");F.Relief.UpperGuide=TEXT("SurfaceMax");F.Relief.Fraction=.3;R.BaseStructure.Add(F);
    auto Legacy=R;Legacy.RegionGeneration.AlgorithmVersion=1;const auto L=FCLPlanetCompiler::Compile(Legacy,E),A=FCLPlanetCompiler::Compile(R,E);if(!L||!A){AddError(E);return false;}
    const auto& Centre=L->GeneratedRegions->Cells[0];const auto& Area=A->GeneratedRegions->Cells[0];
    TestTrue(TEXT("A real uplift can be absent at the seed"),Centre.Uplift<1.e-10&&Centre.SlopeDegrees<.001);
    TestTrue(TEXT("Area evaluation detects the off-centre structure and slopes"),Area.Uplift>.001&&Area.SlopeDegrees>.01&&Area.MaximumBaseHeightMeters>Centre.HeightMeters+1&&Area.CharacterSampleCount>20);
    TestTrue(TEXT("Changing characterization preserves region geometry"),Centre.Centre==Area.Centre&&Centre.Frame==Area.Frame&&Centre.Id==Area.Id);
    AddInfo(FString::Printf(TEXT("Off-centre uplift: centre %.6f, area %.6f, sampled slope %.4f, relief %.4f m, %d samples"),Centre.Uplift,Area.Uplift,Area.SlopeDegrees,Area.MaximumBaseHeightMeters-Area.MinimumBaseHeightMeters,Area.CharacterSampleCount));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLRegionalJunctionTest,"Catalyst.Landform.RegionalCharacter.ThreeWayNativeParity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLRegionalJunctionTest::RunTest(const FString&)
{
    FString E;FCLPlanetRecipe R;if(!CharacterRecipe(R,E)){AddError(E);return false;}const auto P=FCLPlanetCompiler::Compile(R,E);if(!P){AddError(E);return false;}const auto G=P->GeneratedRegions;TestTrue(TEXT("The naturally assigned palette has real three-way junctions"),G->Junctions.Num()>0);
    TArray<FVector> D;for(const auto& J:G->Junctions)
    {
        const FVector S=G->Signature(J.Direction);TestTrue(TEXT("A junction is a genuine cell-metric tie"),FMath::Abs(G->Cost(J.A,J.Direction,S)-G->Cost(J.B,J.Direction,S))<1.e-8&&FMath::Abs(G->Cost(J.A,J.Direction,S)-G->Cost(J.C,J.Direction,S))<1.e-8);
        TArray<double> W;G->PaletteWeights(J.Direction,W);for(double V:W)TestTrue(TEXT("All three terrain types participate at the junction"),V>.05);
        const FVector X=FVector::CrossProduct(J.Direction,FVector::ZAxisVector).GetSafeNormal(),Y=FVector::CrossProduct(J.Direction,X);
        for(double Radius:{0.,.00001,.001,.01})for(int32 K=0;K<16;++K){const double Angle=K*2*UE_DOUBLE_PI/16;D.Add((J.Direction+(X*FMath::Cos(Angle)+Y*FMath::Sin(Angle))*Radius).GetSafeNormal());}
    }
    if(D.IsEmpty())return false;
    TArray<double> H;P->SampleHeightsMeters(D,H);FCLPlanetStamp Stamp;Stamp.bUseRecipe=true;Stamp.Radius=R.RadiusMeters*100;Stamp.Recipe=R;const auto Runtime=StaticCastSharedPtr<FCLPlanetStampRuntime>(FVoxelStampRuntime::Create(nullptr,FVoxelStampRef::New(Stamp),nullptr));if(!Runtime){AddError(TEXT("Native shape failed"));return false;}
    FVoxelDependencyCollector Dependencies(STATIC_FNAME("CL.AreaRegions"));const auto Surfaces=FVoxelSurfaceTypeTable::Get();const FVoxelQuery Query(0,FVoxelLayers::Empty(),*Surfaces,Dependencies);FVoxelDoubleVectorBuffer Points;Points.Allocate(D.Num());for(int32 I=0;I<D.Num();++I)Points.Set(I,D[I]*(R.RadiusMeters+H[I])*100);const auto Distance=Runtime->SampleDistances(Query,Points);double Maximum=0;for(int32 I=0;I<D.Num();++I)Maximum=FMath::Max(Maximum,FMath::Abs(double(Distance[I])));TestTrue(TEXT("Native VPP agrees throughout three-source junctions"),Maximum<.02);
    const auto Section=FCLSectionGeometry::Build(*P,0,500);double Residual=0;for(const auto& V:Section.SourceSurfacePoints)Residual=FMath::Max(Residual,FMath::Abs(V.Size()-(R.RadiusMeters+P->SampleHeightMeters(V))*100));TestTrue(TEXT("Designer preview shares the three-terrain field"),Residual<.01);
    AddInfo(FString::Printf(TEXT("Three-way parity: %d junctions, %d native samples, %.9g cm native / %.9g cm section residual"),G->Junctions.Num(),D.Num(),Maximum,Residual));return true;
}
#endif
