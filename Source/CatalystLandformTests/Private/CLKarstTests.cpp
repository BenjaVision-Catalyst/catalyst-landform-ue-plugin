#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "VoxelMinimal.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLWatershed.h"
#include "Model/CLProvinceDefinition.h"
#include "Scene/CLScene.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "UObject/StrongObjectPtr.h"
#include "Tasks/Task.h"
#include "Model/CLCompileProgress.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLKarstTest,"Catalyst.Landform.Caves.CarbonateKarstVolumesAndHosts",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLKarstTest::RunTest(const FString&)
{
    FString E,J;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/KarstCaves.json")));
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>());if(!A->ImportRecipeJson(J,E)){AddError(E);return false;}
    const auto R=A->Recipe;const auto P=FCLPlanetCompiler::Compile(R,E);if(!P||!P->Caves){AddError(E);return false;}const auto C=P->Caves;
    TestTrue(TEXT("Selected carbonate province produces multiple karst networks"),C->Networks>1);AddInfo(C->Readiness);
    const auto& Cells=P->Watershed->GetCells();
    for(const auto& N:C->NetworkInfo)
    {
        TestTrue(TEXT("Every family is actually carbonate karst"),N.Family==FCLCompiledCaves::EFamily::CarbonateKarst);
        TestTrue(TEXT("Native watershed lookup recovers retained cell"),P->Watershed->FindCell(Cells[N.RechargeCell].Direction)==N.RechargeCell);
        int32 Cell=N.RechargeCell,Steps=0;while(Cell!=N.ReceivingCell&&Cell!=INDEX_NONE&&Steps++<256)Cell=Cells[Cell].Receiver;
        TestEqual(TEXT("Receiving continuation follows the existing drainage graph"),Cell,N.ReceivingCell);
        TMap<int32,TArray<int32>> Adjacency;int32 First=INDEX_NONE,Count=0;
        for(const auto& Path:C->Passages)if(Path.Network==N.Id){Adjacency.FindOrAdd(Path.From).Add(Path.To);Adjacency.FindOrAdd(Path.To).Add(Path.From);First=Path.From;}
        for(const auto& Node:C->Nodes)Count+=Node.Network==N.Id;
        TSet<int32> Reached;TArray<int32> Queue={First};Reached.Add(First);
        for(int32 I=0;I<Queue.Num();++I)for(int32 Next:Adjacency[Queue[I]])if(!Reached.Contains(Next)){Reached.Add(Next);Queue.Add(Next);}
        TestEqual(TEXT("Karst branches, chamber and recharge form one connected volume graph"),Reached.Num(),Count);
    }
    TArray<FVector> Points;int32 Keyholes=0;double MinWidth=MAX_dbl,MaxWidth=0;
    for(const auto& Path:C->Passages)
    {
        Keyholes+=Path.Incision>0;
        if(Path.Entrance)continue;
        for(int32 I=0;I<Path.Points.Num();++I)
        {
            Points.Add(Path.Points[I]);MinWidth=FMath::Min(MinWidth,Path.Sections[I].X);MaxWidth=FMath::Max(MaxWidth,Path.Sections[I].X);
            if(I)for(int32 K=1;K<4;++K)Points.Add(FMath::Lerp(Path.Points[I-1],Path.Points[I],K/4.));
        }
    }
    TArray<double> Field;Field.Init(-1000,Points.Num());C->Cutters->ComposeBatch(Points,Field);double Clearance=MAX_dbl;for(double F:Field)Clearance=FMath::Min(Clearance,F);
    AddInfo(FString::Printf(TEXT("Karst minimum chord clearance %.6f m"),Clearance));
    TestTrue(TEXT("Karst paths are open volumes between controls and at junctions"),Clearance>.1);
    TestTrue(TEXT("Vadose keyholes coexist with phreatic sections and enlarged chambers"),Keyholes>0&&Keyholes<C->Passages.Num()&&!C->Chambers.IsEmpty()&&MaxWidth/MinWidth>1.4);
    auto Off=R;Off.Caves.bEnabled=false;const auto Base=FCLPlanetCompiler::Compile(Off,E);if(!Base){AddError(E);return false;}
    const auto Natural=FCLScene::FromPlanet(Base,E),Caved=FCLScene::FromPlanet(P,E);TArray<FCLSceneShapeSample> Shape;Caved->SampleShape({Points},Shape,E);TArray<FCLRockSample> Before,After;Natural->SampleGeology({Points},Before,E);Caved->SampleGeology({Points},After,E);
    for(int32 I=0;I<Points.Num();++I){TestTrue(TEXT("Routes cut actual underground terrain"),Shape[I].FieldMeters>.1);TestEqual(TEXT("Karst excavation retains canonical rock strata"),After[I].StratumId,Before[I].StratumId);}
    TArray<FVector> InspectionPoints=Points;
    for(const auto& Path:C->Passages)
    {
        const auto Q=Path.Points[0].GetSafeNormal();const double Surface=Base->RadiusMeters+Base->SampleHeightMeters(Q);
        for(double Offset:{-20.,-1.,0.,1.,20.})InspectionPoints.Add(Q*(Surface+Offset));
    }
    TArray<FCLSceneShapeSample> Original,Final,Inspection;Natural->SampleShape({InspectionPoints},Original,E);Caved->SampleShape({InspectionPoints},Final,E);Caved->SampleCaveVoid({InspectionPoints},Inspection,E);
    TArray<double> Air;Air.Init(FVoxelUtilities::NaNf(),InspectionPoints.Num());C->Cutters->ComposeBatch(InspectionPoints,Air);
    double MaxError=0;int32 Worst=0;for(int32 I=0;I<InspectionPoints.Num();++I){const double Delta=FMath::Abs(Inspection[I].FieldMeters-FMath::Max3(Original[I].FieldMeters,-Air[I],-Final[I].FieldMeters));if(Delta>MaxError){MaxError=Delta;Worst=I;}}
    AddInfo(FString::Printf(TEXT("Inspection full/pruned max error %.9g at %s: original %.9g air %.9g final %.9g inspection %.9g"),MaxError,*InspectionPoints[Worst].ToString(),Original[Worst].FieldMeters,Air[Worst],Final[Worst].FieldMeters,Inspection[Worst].FieldMeters));
    TestTrue(TEXT("Pruned native inspection equals the independent full intersection, including entrances and exterior"),MaxError<1.e-5);
    const FVector Audit(1433.651,202.126,241.025);
    for(const auto Axis:{FVector::XAxisVector,FVector::YAxisVector,FVector::ZAxisVector})
    {
        double Previous=0,MaxStep=0;for(int32 K=-10;K<=10;++K){const auto V=Audit+Axis*(K*.02);TArray<FCLSceneShapeSample> Probe;Caved->SampleCaveVoid({MakeArrayView(&V,1)},Probe,E);if(K>-10)MaxStep=FMath::Max(MaxStep,FMath::Abs(Probe[0].FieldMeters-Previous));Previous=Probe[0].FieldMeters;}
        TestTrue(TEXT("Native segment union has no closest-key field jump at the reproduced karst corner"),MaxStep<.15);
        AddInfo(FString::Printf(TEXT("Reproduced corner axis %s maximum 2 cm field step %.6f m"),*Axis.ToString(),MaxStep));
    }
    const auto* Library=LoadObject<UCLPlanetRecipeAsset>(nullptr,TEXT("/Game/LandformDesigner/Caves/DA_KarstCaves"));if(!TestNotNull(TEXT("Karst review exists in the native asset library"),Library))return false;
    TestEqual(TEXT("Karst library links only its selected carbonate source"),Library->PaletteSources.Num(),1);
    FString LibraryJson;Library->ExportJsonString(LibraryJson,E);TStrongObjectPtr<UCLPlanetRecipeAsset> LibraryCopy(NewObject<UCLPlanetRecipeAsset>());if(!LibraryCopy->ImportRecipeJson(LibraryJson,E)){AddError(E);return false;}TestTrue(TEXT("Cold-loaded native karst asset round-trips all geology and cave controls"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Library->Recipe,&LibraryCopy->Recipe,0));
    // Independent native shape-section probe: a narrow lower slot is a real
    // negative space below the upper oval, not a painted line or preview mesh.
    FCLCavePassage Path;Path.Network=FGuid(1,2,3,4);Path.Points={{-10,0,100},{10,0,100}};Path.Sections={{3,2},{3,2}};Path.Incision=1;
    const auto Keyhole=FCLCompiledCaveVolume::Compile({Path},{},nullptr,0,E);Path.Incision=0;const auto Oval=FCLCompiledCaveVolume::Compile({Path},{},nullptr,0,E);
    TestTrue(TEXT("Native keyhole union opens a deep narrow slot"),Keyhole&&Oval&&Keyhole->Compose({0,0,97},-1000)>.2&&Oval->Compose({0,0,97},-1000)<-.5&&Keyhole->Compose({0,2.5,97},-1000)<-.5);
    auto NoWater=MakeShared<FCLCompiledPlanetRecipe>(*Base);NoWater->Watershed.Reset();const auto Dry=FCLCompiledCaves::Compile(R.Caves,*NoWater,R.Seed,E);TestTrue(TEXT("Missing drainage retains general caves without inventing karst history"),Dry&&Dry->Networks>0&&Dry->NetworkInfo[0].Family==FCLCompiledCaves::EFamily::General);
    auto Bad=Off;Bad.Caves.bEnabled=true;Bad.Rock.Units[0].DissolutionPotential=0;const auto Insoluble=FCLPlanetCompiler::Compile(Bad,E);TestTrue(TEXT("Insoluble hosts retain caves without claiming carbonate chemistry"),Insoluble&&Insoluble->Caves->Networks>0&&Insoluble->Caves->NetworkInfo[0].Family==FCLCompiledCaves::EFamily::General);
    Bad=Off;Bad.Caves.bEnabled=true;Bad.Rock.Units[0].Lithology=ECLRockLithology::Clastic;const auto Clastic=FCLPlanetCompiler::Compile(Bad,E);TestTrue(TEXT("Clastic hosts retain general caves"),Clastic&&Clastic->Caves->Networks>0&&Clastic->Caves->NetworkInfo[0].Family==FCLCompiledCaves::EFamily::General);
    auto NoProvince=R;for(auto& Entry:NoProvince.RegionGeneration.Palette)Entry.bEnabled=Entry.Suitability.Province.Type==ECLProvinceType::TransformBelt;
    const auto Structural=FCLPlanetCompiler::Compile(NoProvince,E);TestTrue(TEXT("Excluded carbonate Landform cannot emit karst"),Structural&&!Structural->Caves->NetworkInfo.ContainsByPredicate([](const auto& N){return N.Family==FCLCompiledCaves::EFamily::CarbonateKarst;}));
    auto Rain=R;Rain.Watershed.ReferenceRainMetersPerYear*=2;const auto Wet=FCLPlanetCompiler::Compile(Rain,E);if(!Wet){AddError(E);return false;}TestEqual(TEXT("Instantaneous reference rain does not regenerate cave history"),Wet->Caves->Passages.Num(),C->Passages.Num());for(int32 I=0;I<C->Passages.Num();++I)TestTrue(TEXT("Rain preserves exact cave geometry"),Wet->Caves->Passages[I].Points==C->Passages[I].Points);
    TStrongObjectPtr<UCLPlanetRecipeAsset> Reload(NewObject<UCLPlanetRecipeAsset>());A->ExportJsonString(J,E);if(!Reload->ImportRecipeJson(J,E)){AddError(E);return false;}const auto Copy=FCLPlanetCompiler::Compile(Reload->Recipe,E);TestTrue(TEXT("Portable karst recipe restores identical graph and sections"),Copy&&Copy->Caves->Passages.Num()==C->Passages.Num()&&Copy->Caves->Passages[0].Points==C->Passages[0].Points&&Copy->Caves->Passages[0].Incision==C->Passages[0].Incision);
    const auto Prepared=FCLPlanetCompiler::PrepareBackground(R,E);TSharedPtr<const FCLCompiledPlanetRecipe> Worker;const auto Progress=MakeShared<FCLCompileProgress>();FString WE;
    if(!Prepared){AddError(E);return false;}auto Task=UE::Tasks::Launch(UE_SOURCE_LOCATION,[&]{Worker=FCLPlanetCompiler::CompleteBackground(*Prepared,Progress,WE);});Task.Wait();if(!Worker){AddError(WE);return false;}TestTrue(TEXT("Existing background worker produces identical karst construction"),Worker->Caves->Passages[0].Points==C->Passages[0].Points);
    AddInfo(FString::Printf(TEXT("Karst %d networks, %d curved passages, %d keyholes, %d chamber lobes, %d route probes; minimum clearance %.4f m"),C->Networks,C->Passages.Num(),Keyholes,C->Chambers.Num(),Points.Num(),Clearance));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLCaveAbundanceTest,"Catalyst.Landform.Caves.AbundanceUpgradeAndScale",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLCaveAbundanceTest::RunTest(const FString&)
{
    FCLCaveSettings Old;Old.AlgorithmVersion=1;Old.Character=1;Old.NetworksPerSquareKilometer=.8;Old.LengthMeters=92;Old.UpgradeAbundanceProfile();TestTrue(TEXT("Legacy automatic density upgrades while preserving authored dimensions"),Old.AlgorithmVersion==2&&Old.NetworksPerSquareKilometer==6&&Old.LengthMeters==92);
    Old.AlgorithmVersion=1;Old.NetworksPerSquareKilometer=.37;Old.UpgradeAbundanceProfile();TestEqual(TEXT("Custom density survives migration"),Old.NetworksPerSquareKilometer,.37);
    FString E,J;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/CavesSmall.json")));TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>());if(!A->ImportRecipeJson(J,E)){AddError(E);return false;}
    auto R=A->Recipe;R.Caves.bEnabled=false;const auto Base=FCLPlanetCompiler::Compile(R,E);if(!Base){AddError(E);return false;}
    int32 Previous=0;for(double V:{0.,.01,.25,.5,.75,1.})
    {auto S=A->Recipe.Caves;S.ApplyProgression(V);const auto C=FCLCompiledCaves::Compile(S,*Base,R.Seed,E);if(!C){AddError(E);return false;}TestTrue(TEXT("Increasing knob does not reduce abundance in the small review recipe"),C->Networks>=Previous);Previous=C->Networks;AddInfo(FString::Printf(TEXT("500 m knob %.0f: %s"),100*V,*C->Readiness));}
    TestTrue(TEXT("Maximum adds several real caves on the small planet"),Previous>=3);
    auto Bigger=MakeShared<FCLCompiledPlanetRecipe>(*Base);const double Reference=Base->RadiusMeters+(Base->MinimumHeightMeters+Base->MaximumHeightMeters)*.5;Bigger->RadiusMeters+=Reference*(FMath::Sqrt(2.)-1.);
    auto S=A->Recipe.Caves;S.LengthMeters=35;S.DepthMeters=10;const auto First=FCLCompiledCaves::Compile(S,*Base,R.Seed,E),Second=FCLCompiledCaves::Compile(S,*Bigger,R.Seed,E);
    TestTrue(TEXT("Doubling physical area doubles candidate count within rounding"),First&&Second&&FMath::Abs(Second->Candidates-2*First->Candidates)<=1);return true;
}
#endif
