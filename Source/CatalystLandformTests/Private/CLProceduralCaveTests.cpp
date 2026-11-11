#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Interfaces/IPluginManager.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLSpatialCave.h"
#include "Async/Async.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLProceduralCaveTest,"Catalyst.Landform.Caves.ProceduralFamily.TopologyFloorsAndStrata",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLProceduralCaveTest::RunTest(const FString&)
{
    FString J,E;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/ProceduralCaves.json")));
    TStrongObjectPtr<UCLPlanetRecipeAsset> Asset(NewObject<UCLPlanetRecipeAsset>());if(!Asset->ImportRecipeJson(J,E)){AddError(E);return false;}const auto Recipe=Asset->Recipe;
    TArray<TSharedPtr<FJsonValue>> Cases;
    for(int32 Seed:{176,731,2049})
    {
        auto R=Recipe;R.Caves.Seed=Seed;const auto P=FCLPlanetCompiler::Compile(R,E);if(!P){AddError(FString::Printf(TEXT("Seed %d: %s"),Seed,*E));return false;}
        const auto& C=*P->Caves;int32 Levels,Cells,Ramps;C.Spatial->GetFamilyStats(Levels,Cells,Ramps);
        TestEqual(TEXT("Reference has three levels"),Levels,3);TestTrue(TEXT("At least two independent ramps per level pair"),Ramps>=4*C.Networks);
        TestTrue(TEXT("Graph has loops and alternate routes"),C.Passages.Num()-C.Nodes.Num()+C.Networks>=5*C.Networks);
        TSet<int32> Reached;Reached.Add(0);bool Changed=true;while(Changed){Changed=false;for(const auto& Path:C.Passages)if(Reached.Contains(Path.From)||Reached.Contains(Path.To)){const int32 Before=Reached.Num();Reached.Add(Path.From);Reached.Add(Path.To);Changed|=Before!=Reached.Num();}}
        int32 FirstNetworkNodes=0;for(const auto& N:C.Nodes)FirstNetworkNodes+=N.Network==C.Nodes[0].Network;
        TestEqual(TEXT("All levels are physically connected in the first route graph"),Reached.Num(),FirstNetworkNodes);
        TArray<FVector> Points;double MaximumGrade=0;int32 FoundationFailures=0,ClearanceFailures=0;
        for(const auto& Path:C.Passages)
        {
            for(int32 I=0;I<Path.Points.Num();I+=3)
            {
                const FVector Q=Path.Points[I],Up=Q.GetSafeNormal();Points.Add(Q);Points.Add(Q-Up*2.5);
                const double Head=C.Cutters->Compose(Q,-1000),Foot=C.Cutters->Compose(Q-Up*2.5,-1000);
                ClearanceFailures+=Head<.45;FoundationFailures+=Foot>-.05;
            }
            for(int32 I=1;I<Path.Points.Num();++I)
            {const auto A=Path.Points[I-1],B=Path.Points[I];const double Horizontal=FVector::VectorPlaneProject(B-A,(A+B).GetSafeNormal()).Length();MaximumGrade=FMath::Max(MaximumGrade,FMath::Abs(B.Length()-A.Length())/FMath::Max(.001,Horizontal));}
        }
        TestEqual(TEXT("Continuous head clearance on every gallery and ramp"),ClearanceFailures,0);
        TestEqual(TEXT("Solid foundation through chambers, joins and both ramp alternatives"),FoundationFailures,0);
        TestTrue(TEXT("Physical route grade stays below native walk slope"),MaximumGrade<.50);
        TArray<double> A,B;C.Spatial->Sample(Points,A);C.Spatial->Sample(Points,B,false);double Error=0;for(int32 I=0;I<A.Num();++I)Error=FMath::Max(Error,FMath::Abs(A[I]-B[I]));TestTrue(TEXT("SIMD family agrees with independent scalar field within a millimetre"),Error<.001);
        TArray<FVector> Grid;for(double X=-240;X<=240;X+=12)for(double Y=-240;Y<=240;Y+=12)for(double Z=-110;Z<=40;Z+=10)Grid.Add(C.Spatial->BodyPoint({X,Y,Z}));
        TArray<double> Indexed,Unpruned;C.Spatial->Sample(Grid,Indexed);C.Spatial->Sample(Grid,Unpruned,false);int32 SignErrors=0,DeepRock=0;
        for(int32 I=0;I<Grid.Num();++I){SignErrors+=FMath::Abs(Unpruned[I])>.05&&((Indexed[I]>0)!=(Unpruned[I]>0));DeepRock+=Indexed[I]<-8;}
        TestEqual(TEXT("Native broad-phase omits no void against unpruned scalar cells"),SignErrors,0);
        TArray<FVector> WallProbes;
        for(const auto& Path:C.Passages)for(int32 I=1;I<Path.Points.Num();I+=19)
        {
            const auto Q=Path.Points[I],Up=Q.GetSafeNormal(),Side=FVector::CrossProduct(Up,Path.Points[I]-Path.Points[I-1]).GetSafeNormal();
            for(double Across=-30;Across<=30;Across+=2)for(double Rise:{0.,5.,12.})WallProbes.Add(Q+Side*Across+Up*Rise);
        }
        TArray<double> WallNative,WallScalar;C.Spatial->Sample(WallProbes,WallNative);C.Spatial->Sample(WallProbes,WallScalar,false);
        int32 WallSigns=0,WallNear=0;double WallError=0;
        for(int32 I=0;I<WallProbes.Num();++I)
        {
            WallSigns+=FMath::Abs(WallScalar[I])>.001&&((WallNative[I]>0)!=(WallScalar[I]>0));
            if(FMath::Abs(WallScalar[I])<1){++WallNear;WallError=FMath::Max(WallError,FMath::Abs(WallNative[I]-WallScalar[I]));}
        }
        TestEqual(TEXT("Exact edge pruning preserves wall/roof void signs"),WallSigns,0);
        TestTrue(TEXT("Independent wall/roof probes exercise actual boundaries"),WallNear>100);
        TestTrue(TEXT("Wall and roof geometry retain the existing millimetre budget"),WallError<.001);
        AddInfo(FString::Printf(TEXT("FAMILY_WALL_PARITY seed=%d probes=%d boundary=%d signs=%d maxErrorMeters=%.9f"),Seed,WallProbes.Num(),WallNear,WallSigns,WallError));
        const auto Worker=Async(EAsyncExecution::ThreadPool,[&C,Points]{TArray<double> V;C.Spatial->Sample(Points,V);return V;}).Get();TestTrue(TEXT("Immutable worker field parity"),A==Worker);
        int32 PreparedWeights=0,SurfaceMismatches=0;
        for(const auto& Q:Points)
        {
            FCLSurfaceWeights Reference,Fast;P->Rock->ApplySurfaces(Q,Reference);
            if(C.Spatial->TryPreparedSurfaces(Q,Fast)){++PreparedWeights;for(int32 K=0;K<P->Surfaces->Ids.Num();++K)SurfaceMismatches+=Fast.Values[K]!=Reference.Values[K];}
        }
        TestTrue(TEXT("Each seed exercises prepared strata"),PreparedWeights>0);
        TestEqual(TEXT("Every seed preserves canonical strata surface weights exactly"),SurfaceMismatches,0);
        TSet<FGuid> Units,Strata;TSet<FName> Surfaces;
        for(const auto& N:C.Nodes)for(int32 I=-6;I<30;++I){const auto Q=N.Position+N.Position.GetSafeNormal()*I;const auto G=P->Rock->Sample(P->RockPositionMeters(Q));Units.Add(G.UnitId);Strata.Add(G.StratumId);Surfaces.Add(G.SurfaceId);TestTrue(TEXT("Caves preserve canonical rock positions"),P->RockPositionMeters(Q).Equals(Q,1.e-8));}
        TestEqual(TEXT("Original three geological unit bindings remain exposed"),Surfaces.Num(),3);TestTrue(TEXT("Physical strata remain distinct"),Strata.Num()>6);
        auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("seed"),Seed);O->SetNumberField(TEXT("networks"),C.Networks);O->SetNumberField(TEXT("levels"),Levels);O->SetNumberField(TEXT("cells"),Cells);O->SetNumberField(TEXT("ramps"),Ramps);O->SetNumberField(TEXT("nodes"),C.Nodes.Num());O->SetNumberField(TEXT("passages"),C.Passages.Num());O->SetNumberField(TEXT("routeProbePoints"),Points.Num());O->SetNumberField(TEXT("foundationFailures"),FoundationFailures);O->SetNumberField(TEXT("clearanceFailures"),ClearanceFailures);O->SetNumberField(TEXT("maximumPhysicalGrade"),MaximumGrade);O->SetNumberField(TEXT("scalarErrorMeters"),Error);O->SetNumberField(TEXT("strata"),Strata.Num());O->SetNumberField(TEXT("unprunedSignProbes"),Grid.Num());O->SetNumberField(TEXT("unprunedSignErrors"),SignErrors);O->SetNumberField(TEXT("deepRockProbes"),DeepRock);Cases.Add(MakeShared<FJsonValueObject>(O));
        // Character gestures retain this family and preserve existing UI shape.
        auto Settings=R.Caves;Settings.ApplyProgression(.65);TestEqual(TEXT("Spatial authoring retains procedural family"),Settings.AlgorithmVersion,9);
    }
    auto O=MakeShared<FJsonObject>();O->SetArrayField(TEXT("cases"),Cases);FString Text;FJsonSerializer::Serialize(O,TJsonWriterFactory<>::Create(&Text));FString Path;if(FParse::Value(FCommandLine::Get(),TEXT("CLFamilyReport="),Path))FFileHelper::SaveStringToFile(Text,*Path);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLProceduralCaveControlsTest,"Catalyst.Landform.Caves.ProceduralFamily.ControlsDeterminismAndSelection",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLProceduralCaveControlsTest::RunTest(const FString&)
{
    FString J,E;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/ProceduralCaves.json")));
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>());if(!A->ImportRecipeJson(J,E)){AddError(E);return false;}
    TArray<int32> LevelsSeen;
    for(double Character:{.01,.5,.85,1.})
    {
        auto R=A->Recipe;R.Caves.ApplyProgression(Character);R.Caves.NetworksPerSquareKilometer=.2;
        const auto P=FCLPlanetCompiler::Compile(R,E);if(!P){AddError(E);return false;}
        int32 Levels,Cells,Ramps;P->Caves->Spatial->GetFamilyStats(Levels,Cells,Ramps);LevelsSeen.Add(Levels);
        TestEqual(TEXT("Existing authoring gesture keeps family 9"),R.Caves.AlgorithmVersion,9);
        TestEqual(TEXT("Legacy serialized struct default remains frozen"),FCLCaveSettings().AlgorithmVersion,7);
        const auto& C=*P->Caves;
        for(const auto& Path:C.Passages)for(int32 I=1;I<Path.Points.Num();I+=11)
        {
            const auto Q=Path.Points[I],Up=Q.GetSafeNormal(),Side=FVector::CrossProduct(Up,Path.Points[I]-Path.Points[I-1]).GetSafeNormal();
            for(double Sign:{-.5,0.,.5})
            {TestTrue(TEXT("Capsule-width head clearance across Character range"),C.Cutters->Compose(Q+Side*Sign,-1000)>.35);TestTrue(TEXT("Capsule-width foundation across Character range"),C.Cutters->Compose(Q+Side*Sign-Up*2.5,-1000)<-.05);}
        }
    }
    TestTrue(TEXT("Existing controls produce one, two and three levels"),LevelsSeen==TArray<int32>({1,2,3,3}));
    auto R=A->Recipe;R.RadiusMeters=1600;R.Caves.NetworksPerSquareKilometer=.25;const auto P=FCLPlanetCompiler::Compile(R,E);if(!P){AddError(E);return false;}
    TestTrue(TEXT("Physical density can place several complete networks"),P->Caves->Networks>=2);
    const auto Again=FCLPlanetCompiler::Compile(R,E);if(!Again){AddError(E);return false;}
    TestEqual(TEXT("Same seed preserves network count"),P->Caves->Networks,Again->Caves->Networks);
    TestEqual(TEXT("Same seed preserves route count"),P->Caves->Passages.Num(),Again->Caves->Passages.Num());
    for(int32 I=0;I<P->Caves->Passages.Num();++I)TestTrue(TEXT("Same seed reproduces exact route metadata"),P->Caves->Passages[I].Points==Again->Caves->Passages[I].Points);
    TArray<FVector> Probes;for(const auto& N:P->Caves->Nodes)Probes.Add(N.Position);
    TArray<double> X,Y;P->Caves->Spatial->Sample(Probes,X);Again->Caves->Spatial->Sample(Probes,Y);TestTrue(TEXT("Same seed reproduces exact worker field"),X==Y);
    TArray<FVector> Scattered;for(int32 I=0;I<512;++I)Scattered.Add(P->Caves->Spatial->BodyPoint({double(I%17-8)*31,double(I%23-11)*24,double(I%19-9)*14}));
    TArray<double> Bulk;P->Caves->Spatial->Sample(Scattered,Bulk);double PacketError=0;
    for(int32 I=0;I<Scattered.Num();++I){TArray<double> Single;P->Caves->Spatial->Sample(MakeArrayView(&Scattered[I],1),Single);PacketError=FMath::Max(PacketError,FMath::Abs(Single[0]-Bulk[I]));}
    AddInfo(FString::Printf(TEXT("FAMILY_PACKET_ERROR maxMeters=%.12g"),PacketError));TestTrue(TEXT("Coherent and sparse native indexing agree within 0.1 mm"),PacketError<.0001);
    int32 GapProbes=0;const FBox Whole=P->Caves->Spatial->Bounds();
    for(int32 I=1;I<20;++I){const FVector Q=FMath::Lerp(Whole.Min,Whole.Max,I/20.);double Nearest=MAX_dbl;for(const auto& N:P->Caves->NetworkInfo)Nearest=FMath::Min(Nearest,FMath::Sqrt(N.InspectionBounds.ComputeSquaredDistanceToPoint(Q)));if(Nearest<20)continue;TArray<double> V;P->Caves->Spatial->Sample(MakeArrayView(&Q,1),V);TestTrue(TEXT("Solid gaps between networks retain conservative distance for native coarse rejection"),V[0]<=-.25*(Nearest+2)+1.e-7);++GapProbes;}
    TestTrue(TEXT("The distribution fixture exercises rock between independent networks"),GapProbes>0);
    const auto Selected=P->Caves->Cutters->Select(P->Caves->NetworkInfo[0].Id);
    for(const auto& N:P->Caves->Nodes)TestTrue(TEXT("Native inspection selection retains only its requested network"),N.Network==P->Caves->NetworkInfo[0].Id?Selected->Compose(N.Position,-1000)>.5:Selected->Compose(N.Position,-1000)<0);
    R.Caves.bSurfaceEntrances=true;const auto Access=FCLPlanetCompiler::Compile(R,E);TestTrue(TEXT("Surface access retains a real enabled cave"),Access&&Access->Caves&&Access->Caves->Cutters&&Access->Caves->Entrances>0);
    return !HasAnyErrors();
}

#endif
