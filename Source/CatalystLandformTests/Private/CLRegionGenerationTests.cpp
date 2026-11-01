#if WITH_DEV_AUTOMATION_TESTS
#include "Model/CLRegionGeneration.h"
#include "Model/CLTerrainType.h"
#include "Model/CLSphericalPatches.h"
#include "CLPlanetStamp.h"
#include "CLSectionGeometry.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Interfaces/IPluginManager.h"
#include "Algo/Reverse.h"
#include "JsonObjectConverter.h"
#include "Surface/VoxelSurfaceTypeTable.h"
#include "VoxelLayers.h"
#include "VoxelQuery.h"
namespace
{
bool RegionRecipe(FCLPlanetRecipe& R,FString& Error)
{
    FString J;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/GeneratedRegions.json")));
    auto* A=NewObject<UCLPlanetRecipeAsset>();if(!A->ImportRecipeJson(J,Error))return false;R=A->Recipe;return true;
}
TArray<FVector> RegionDirections(int32 N=12)
{TArray<FVector> D;for(int32 F=0;F<6;++F)for(int32 Y=0;Y<=N;++Y)for(int32 X=0;X<=N;++X)D.Add(FCLSphericalPatches::Direction(F,FVector2D(2.*X/N-1,2.*Y/N-1)));return D;}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLGeneratedAssignmentTest,"Catalyst.Landform.Regions.StructureAndPalette",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLGeneratedAssignmentTest::RunTest(const FString&)
{
    FCLPlanetRecipe R;FString E;if(!RegionRecipe(R,E)){AddError(E);return false;}const auto P=FCLPlanetCompiler::Compile(R,E);if(!P){AddError(E);return false;}const auto G=P->GeneratedRegions;
    TestTrue(TEXT("Generated partition is shared by every face"),G&&P->SurfacePatches.Num()==6);if(!G)return false;for(const auto& Face:P->SurfacePatches)TestTrue(TEXT("Every patch shares the immutable regional field"),Face->GeneratedRegions==G);
    TestEqual(TEXT("Requested region count"),G->Cells.Num(),24);TArray<int32> Counts;Counts.Init(0,G->Palette.Num());double MountainUplift=0,PlateauUplift=0;int32 M=0,T=0;
    for(int32 I=0;I<G->Cells.Num();++I){const auto& C=G->Cells[I];++Counts[C.PaletteIndex];TestEqual(TEXT("Every cell owns its seed"),G->Owner(C.Centre),I);TestFalse(TEXT("Every assignment has an explanation"),C.Reason.IsEmpty());if(G->Palette[C.PaletteIndex].Name.Contains(TEXT("Mountain"))){MountainUplift+=C.Uplift;++M;}else{PlateauUplift+=C.Uplift;++T;}}
    TestTrue(TEXT("Both suitable terrain types are assigned"),M>0&&T>0);if(M&&T)TestTrue(TEXT("Ridges favour uplift more strongly than plateaus"),MountainUplift/M>PlateauUplift/T);
    auto Reordered=R;Algo::Reverse(Reordered.BaseStructure);Algo::Reverse(Reordered.RegionGeneration.Palette);const auto Q=FCLPlanetCompiler::Compile(Reordered,E);if(!Q){AddError(E);return false;}
    for(int32 I=0;I<G->Cells.Num();++I){const auto& C=G->Cells[I];const auto& D=Q->GeneratedRegions->Cells[I];TestTrue(TEXT("Palette and structure ordering cannot reshuffle cells or assignments"),C.Id==D.Id&&C.Centre==D.Centre&&G->Palette[C.PaletteIndex].Id==Q->GeneratedRegions->Palette[D.PaletteIndex].Id);}
    auto Single=R;Single.RegionGeneration.Palette.SetNum(1);const auto S=FCLPlanetCompiler::Compile(Single,E);if(!S){AddError(E);return false;}for(const auto& C:S->GeneratedRegions->Cells)TestEqual(TEXT("One selected Landform fills every generated region"),S->GeneratedRegions->Palette[C.PaletteIndex].Id,Single.RegionGeneration.Palette[0].Id);
    auto Ten=R;Ten.RegionGeneration.Palette.Reset();for(int32 I=0;I<10;++I){auto Entry=R.RegionGeneration.Palette[I%2];Entry.Id=FGuid(0x434C544E,0,0,I+1);Entry.Name=FString::Printf(TEXT("Terrain variant %d"),I+1);Entry.NoiseLayers.RemoveAll([](const auto& L){return L.Operation==ECLTerrainOperation::StampDistribution;});Entry.Suitability=FCLLandformSuitability();Ten.RegionGeneration.Palette.Add(Entry);}
    const auto Many=FCLPlanetCompiler::Compile(Ten,E);TestTrue(TEXT("Ten selected Landforms compile within one bounded palette"),Many.IsValid());if(Many)TestEqual(TEXT("All ten candidates participate in assignment"),Many->GeneratedRegions->Palette.Num(),10);
    auto Unguided=R;Unguided.RegionGeneration.StructureGuidance=0;const auto U=FCLPlanetCompiler::Compile(Unguided,E);int32 Changed=0;if(U)for(int32 I=0;I<G->Cells.Num();++I)Changed+=!G->Cells[I].Centre.Equals(U->GeneratedRegions->Cells[I].Centre,1.e-9);TestTrue(TEXT("Underlying structure changes the regional layout"),Changed>G->Cells.Num()/2);
    auto Larger=R;Larger.RadiusMeters*=2;
    // Preserve the stamp count as area quadruples; the fixed proof budgets still
    // apply independently of normalized region layout.
    for(auto& Entry:Larger.RegionGeneration.Palette)for(auto& Layer:Entry.NoiseLayers)if(Layer.Operation==ECLTerrainOperation::StampDistribution)Layer.Distribution.DensityPerSquareKilometer*=.25;const auto L=FCLPlanetCompiler::Compile(Larger,E);TestTrue(*FString(TEXT("Resized planet compiles: ")+E),L.IsValid());if(L)for(int32 I=0;I<G->Cells.Num();++I)TestTrue(TEXT("Normalized regional layout survives proportional resizing"),G->Cells[I].Centre.Equals(L->GeneratedRegions->Cells[I].Centre,1.e-9)&&G->Cells[I].Id==L->GeneratedRegions->Cells[I].Id);
    AddInfo(FString::Printf(TEXT("Regions: %d cells, %d mountain / %d plateau; mean uplift %.5f / %.5f; %d sites respond to guidance"),G->Cells.Num(),M,T,M?MountainUplift/M:0,T?PlateauUplift/T:0,Changed));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLGeneratedBlendTest,"Catalyst.Landform.Regions.BlendAndNativeParity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLGeneratedBlendTest::RunTest(const FString&)
{
    FCLPlanetRecipe R;FString E;if(!RegionRecipe(R,E)){AddError(E);return false;}const auto P=FCLPlanetCompiler::Compile(R,E),Whole=FCLPlanetCompiler::Compile(R,E,nullptr,nullptr,FCLSphericalPatches::WholeSphere);if(!P||!Whole){AddError(E);return false;}const auto G=P->GeneratedRegions;
    auto D=RegionDirections(16);for(int32 I=0;I<G->Boundaries.Num();I+=FMath::Max(1,G->Boundaries.Num()/200)){const auto& B=G->Boundaries[I];const FVector C=(B.A+B.B).GetSafeNormal(),T=FVector::CrossProduct(C,FVector::ZAxisVector).GetSafeNormal();D.Append({C,(C+T*1.e-7).GetSafeNormal(),(C-T*1.e-7).GetSafeNormal()});}
    double WeightError=0,AtlasError=0,OracleError=0;TArray<double> H,W;P->SampleHeightsMeters(D,H);Whole->SampleHeightsMeters(D,W);
    auto BaseR=R;BaseR.RegionGeneration=FCLRegionGenerationSettings();const auto Base=FCLPlanetCompiler::Compile(BaseR,E);TArray<double> BH;Base->SampleHeightsMeters(D,BH);
    for(int32 I=0;I<D.Num();++I)
    {
        TArray<double> CW,PW;G->CellWeights(D[I],CW);G->PaletteWeights(D[I],PW);double Sum=0;for(double V:CW){Sum+=V;TestTrue(TEXT("Cell weights are nonnegative and finite"),FMath::IsFinite(V)&&V>=0);}WeightError=FMath::Max(WeightError,FMath::Abs(Sum-1));
        double Expected=BH[I];for(int32 J=0;J<PW.Num();++J)if(PW[J]>0)Expected+=(G->Fields[J]->SampleHeightMeters(D[I])-G->NeutralHeight)*PW[J]*G->Strength;
        OracleError=FMath::Max(OracleError,FMath::Abs(H[I]-FMath::Clamp(Expected,-R.RadiusMeters,0.)));AtlasError=FMath::Max(AtlasError,FMath::Abs(H[I]-W[I]));
        TestTrue(TEXT("Every sample stays within declared radial bounds"),H[I]>=P->MinimumHeightMeters-1.e-8&&H[I]<=P->MaximumHeightMeters+1.e-8);
    }
    TestTrue(TEXT("Whole sphere is covered by a normalized partition"),WeightError<1.e-12);TestTrue(TEXT("Base structure is retained exactly once under generated relief"),OracleError<.00002);TestTrue(TEXT("Cube patches cannot introduce generated region seams"),AtlasError<.00002);
    FCLPlanetStamp S;S.bUseRecipe=true;S.Radius=R.RadiusMeters*100;S.Recipe=R;const auto Runtime=StaticCastSharedPtr<FCLPlanetStampRuntime>(FVoxelStampRuntime::Create(nullptr,FVoxelStampRef::New(S),nullptr));if(!Runtime){AddError(TEXT("Native generated region shape failed"));return false;}
    FVoxelDependencyCollector Dependencies(STATIC_FNAME("CL.Regions"));const auto Surfaces=FVoxelSurfaceTypeTable::Get();const FVoxelQuery Query(0,FVoxelLayers::Empty(),*Surfaces,Dependencies);FVoxelDoubleVectorBuffer Points;Points.Allocate(D.Num());for(int32 I=0;I<D.Num();++I)Points.Set(I,D[I]*(R.RadiusMeters+H[I])*100);const auto Distance=Runtime->SampleDistances(Query,Points);double Native=0;for(int32 I=0;I<D.Num();++I)Native=FMath::Max(Native,FMath::Abs(double(Distance[I])));TestTrue(TEXT("Native VPP uses the same generated terrain zero set"),Native<.02);
    const auto Section=FCLSectionGeometry::Build(*P,0,500);double SectionError=0;for(const auto& V:Section.SourceSurfacePoints)SectionError=FMath::Max(SectionError,FMath::Abs(V.Size()-(R.RadiusMeters+P->SampleHeightMeters(V))*100));TestTrue(TEXT("Designer cutaway samples the same generated field"),SectionError<.01);
    AddInfo(FString::Printf(TEXT("Generated field: %d directions, %.12g weight error, %.12g m oracle error, %.12g m atlas error, %.9g cm native residual, %.9g cm section residual"),D.Num(),WeightError,OracleError,AtlasError,Native,SectionError));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLGeneratedPortableTest,"Catalyst.Landform.Regions.PortabilityAndOverrides",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLGeneratedPortableTest::RunTest(const FString&)
{
    FCLPlanetRecipe R;FString E,J;if(!RegionRecipe(R,E)){AddError(E);return false;}const auto P=FCLPlanetCompiler::Compile(R,E);if(!P){AddError(E);return false;}const auto& Cell=P->GeneratedRegions->Cells[0];FCLRegionAssignmentOverride O;O.RegionId=Cell.Id;O.LandformId=P->GeneratedRegions->Palette[1-Cell.PaletteIndex].Id;auto Edited=R;Edited.RegionGeneration.Overrides.Add(O);const auto Q=FCLPlanetCompiler::Compile(Edited,E);if(!Q){AddError(E);return false;}
    for(int32 I=0;I<P->GeneratedRegions->Cells.Num();++I)TestTrue(TEXT("Assignment overrides preserve generated sites and IDs"),P->GeneratedRegions->Cells[I].Centre==Q->GeneratedRegions->Cells[I].Centre&&P->GeneratedRegions->Cells[I].Id==Q->GeneratedRegions->Cells[I].Id);
    TestTrue(TEXT("Override selects only the requested Landform and explains why"),Q->GeneratedRegions->Palette[Q->GeneratedRegions->Cells[0].PaletteIndex].Id==O.LandformId&&Q->GeneratedRegions->Cells[0].bOverride);
    auto* Asset=NewObject<UCLPlanetRecipeAsset>();Asset->Recipe=Edited;TestTrue(TEXT("Generated recipe exports self-contained data"),Asset->ExportJsonString(J,E));TestFalse(TEXT("Authoring object paths are outside the recipe"),J.Contains(TEXT("paletteSources")));auto* Copy=NewObject<UCLPlanetRecipeAsset>();TestTrue(TEXT("Server-style recipe import recompiles assignments"),Copy->ImportRecipeJson(J,E));TestTrue(TEXT("Generator, metadata and override fields roundtrip"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Edited,&Copy->Recipe,0));
    const auto Before=Copy->Recipe;auto Reject=[&](FCLPlanetRecipe Bad){FJsonObjectConverter::UStructToJsonObjectString(Bad,J);TestFalse(TEXT("Invalid generated recipe is rejected atomically"),Copy->ImportRecipeJson(J,E));TestTrue(TEXT("Rejected import preserves the previous recipe"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Before,&Copy->Recipe,0));};
    auto Bad=R;Bad.RegionGeneration.Palette.Reset();Reject(Bad);Bad=R;Bad.RegionGeneration.RegionCount=129;Reject(Bad);Bad=R;Bad.RegionGeneration.Palette[1].Id=Bad.RegionGeneration.Palette[0].Id;Reject(Bad);Bad=Edited;Bad.RegionGeneration.Overrides[0].LandformId=FGuid::NewGuid();Reject(Bad);Bad=R;Bad.SchemaVersion=11;Reject(Bad);Bad=R;Bad.RegionGeneration.Palette[0].Suitability.PreferredElevation=FVector2D(1,0);Reject(Bad);
    auto* Source=NewObject<UCLTerrainTypeAsset>();const auto& Entry=R.RegionGeneration.Palette[0];Source->TerrainType.SchemaVersion=7;Source->TerrainType.Id=Entry.Id;Source->TerrainType.Name=Entry.Name;Source->TerrainType.Coordinates=ECLTerrainCoordinates::SphericalSurface;Source->TerrainType.AlgorithmVersion=Entry.AlgorithmVersion;Source->TerrainType.NoiseLayers=Entry.NoiseLayers;Source->TerrainType.PlacementSuitability=Entry.Suitability;
    TestTrue(TEXT("Palette retains an explicit authoring link"),Asset->AddPaletteSource(Source,E));Source->TerrainType.PlacementSuitability.SelectionWeight=3;TestTrue(TEXT("Editing suitability never silently changes the applied planet"),Asset->Recipe.RegionGeneration.Palette[0].Suitability.SelectionWeight==1);TestTrue(TEXT("Explicit palette source refresh updates metadata"),Asset->RefreshPaletteSource(Entry.Id,E));TestEqual(TEXT("Refreshed metadata is snapshotted"),Asset->Recipe.RegionGeneration.Palette[0].Suitability.SelectionWeight,3.);
    TestFalse(TEXT("Malformed import is refused"),Asset->ImportRecipeJson(FString(),E)); // malformed input remains atomic
    TestTrue(TEXT("Valid export"),Asset->ExportJsonString(J,E));TestTrue(TEXT("Valid reimport"),Asset->ImportRecipeJson(J,E));TestTrue(TEXT("Portable import has no live source links"),Asset->PaletteSources.IsEmpty());return true;
}
#endif
