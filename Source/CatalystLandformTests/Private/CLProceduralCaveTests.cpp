#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLSpatialCave.h"
#include "Scene/CLScene.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLProceduralCaveAnatomyTest,"Catalyst.Landform.Caves.ProceduralFamily.AnatomyFloorsAndStrata",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLProceduralCaveAnatomyTest::RunTest(const FString&)
{
    const auto* Asset=LoadObject<UCLPlanetRecipeAsset>(nullptr,TEXT("/Game/LandformDesigner/Provinces/DA_ContinentalRiftReview"));
    if(!TestNotNull(TEXT("Actual review planet"),Asset))return false;
    TSet<int32> Topologies;TSet<int32> ChamberWidths,Depths;double Smallest=MAX_dbl,Largest=0;
    for(int32 Seed:{176,731,2049})
    {
        auto R=Asset->Recipe;R.Caves.bEnabled=true;R.Caves.Seed=Seed;R.Caves.ApplyProgression(1.);FString E;
        const auto P=FCLPlanetCompiler::Compile(R,E);if(!P||!P->Caves){AddError(E);return false;}
        const auto& C=*P->Caves;const auto& Shape=*C.Spatial;int32 Levels,Cells,Ramps;Shape.GetFamilyStats(Levels,Cells,Ramps);
        TestTrue(TEXT("Generated family has multiple connected passages and chambers per network"),C.Passages.Num()>C.Networks&&C.Chambers.Num()>C.Networks);
        TestTrue(TEXT("Actual spatial construction includes stacked levels and connecting ramps"),Levels>=3&&Ramps>0);
        Topologies.Add(C.Passages.Num());
        for(const auto& N:C.NetworkInfo){Depths.Add(N.Levels);Smallest=FMath::Min(Smallest,N.AnatomyScale);Largest=FMath::Max(Largest,N.AnatomyScale);}
        for(const auto& Room:C.Chambers){TestEqual(TEXT("Chambers retain spatial morphology"),Room.GeometryProfile,8);ChamberWidths.Add(FMath::RoundToInt(Room.HalfExtents.X*100));}
        TArray<FVector> Heads,Feet;double Grade=0;
        for(const auto& Path:C.Passages)
        {
            TestTrue(TEXT("Passages carry a connected graph edge"),C.Nodes.IsValidIndex(Path.From)&&C.Nodes.IsValidIndex(Path.To)&&Path.From!=Path.To);
            for(int32 I=0;I<Path.Points.Num();++I)
            {
                const auto Q=Path.Points[I],Up=Q.GetSafeNormal();
                const auto Forward=Path.Points[FMath::Min(I+1,Path.Points.Num()-1)]-Path.Points[FMath::Max(I-1,0)];
                const auto Side=FVector::CrossProduct(Up,Forward).GetSafeNormal();
                for(double Offset:{-.5,0.,.5}){Heads.Add(Q+Side*Offset);Feet.Add(Q+Side*Offset-Up*2.5);}
                if(I){const auto A=Path.Points[I-1];Grade=FMath::Max(Grade,FMath::Abs(Q.Length()-A.Length())/FMath::Max(.001,FVector::VectorPlaneProject(Q-A,(A+Q).GetSafeNormal()).Length()));}
            }
        }
        TArray<double> Native,Scalar,Foundation;Shape.Sample(Heads,Native);Shape.Sample(Heads,Scalar,false);Shape.Sample(Feet,Foundation);
        double Residual=0,Clearance=MAX_dbl,Support=-MAX_dbl;
        for(int32 I=0;I<Heads.Num();++I){Residual=FMath::Max(Residual,FMath::Abs(Native[I]-Scalar[I]));Clearance=FMath::Min(Clearance,Native[I]);Support=FMath::Max(Support,Foundation[I]);}
        TestTrue(TEXT("Entire generated passage graph has head clearance"),Clearance>.3);
        TestTrue(TEXT("Entire generated passage graph has continuous rock footing"),Support<-.05);
        TestTrue(TEXT("Passages, chamber mouths and level transitions are walkable"),Grade<.45);
        TArray<FVector> VolumeProbes;
        for(int32 I=0;I<Heads.Num();I+=15)for(double Z:{3.,8.,15.,25.,40.})VolumeProbes.Add(Heads[I]+Heads[I].GetSafeNormal()*Z);
        TArray<double> VolumeNative,VolumeReference;Shape.Sample(VolumeProbes,VolumeNative);Shape.Sample(VolumeProbes,VolumeReference,false);
        for(int32 I=0;I<VolumeProbes.Num();++I)Residual=FMath::Max(Residual,FMath::Abs(VolumeNative[I]-VolumeReference[I]));
        TestTrue(TEXT("Generalized scalar and native SIMD fields agree throughout walls and roof pockets"),Residual<.001);
        for(const auto& Network:C.NetworkInfo)
        {
            TSet<int32> Reached;for(int32 I=0;I<C.Nodes.Num();++I)if(C.Nodes[I].Network==Network.Id){Reached.Add(I);break;}
            bool Changed=true;while(Changed){Changed=false;for(const auto& Path:C.Passages)if(Path.Network==Network.Id&&(Reached.Contains(Path.From)||Reached.Contains(Path.To))){const int32 Before=Reached.Num();Reached.Add(Path.From);Reached.Add(Path.To);Changed|=Before!=Reached.Num();}}
            int32 Nodes=0;double Low=MAX_dbl,High=-MAX_dbl;
            for(int32 I=0;I<C.Nodes.Num();++I)if(C.Nodes[I].Network==Network.Id){++Nodes;Low=FMath::Min(Low,C.Nodes[I].Position.Length());High=FMath::Max(High,C.Nodes[I].Position.Length());}
            TestEqual(TEXT("Every chamber and junction belongs to the connected graph"),Reached.Num(),Nodes);
            TestTrue(TEXT("Levels occupy physically distinct floor elevations"),Network.Levels==1||High-Low>25*Network.AnatomyScale);
        }
        // Check the actual terrain scene and canonical strata on a bounded set
        // from all networks, rather than assuming cutter counts imply delivery.
        TArray<FVector> Probes;for(int32 I=0;I<Heads.Num();I+=FMath::Max(1,Heads.Num()/256))Probes.Add(Heads[I]);
        auto Off=R;Off.Caves.bEnabled=false;const auto Uncut=FCLPlanetCompiler::Compile(Off,E);if(!Uncut){AddError(E);return false;}
        const auto Scene=FCLScene::FromPlanet(P,E),Natural=FCLScene::FromPlanet(Uncut,E);TArray<FCLSceneSurfaceSample> A,B;TArray<FCLRockSample> GA,GB;TArray<FCLSceneShapeSample> Field;
        Scene->SampleShape({Probes},Field,E);Scene->SampleSurfaces({Probes},A,E);Natural->SampleSurfaces({Probes},B,E);Scene->SampleGeology({Probes},GA,E);Natural->SampleGeology({Probes},GB,E);
        for(int32 I=0;I<Probes.Num();++I){TestTrue(TEXT("Generated cave is real air in the review terrain"),Field[I].FieldMeters>0);TestEqual(TEXT("Caves preserve rock stratum identity"),GA[I].StratumId,GB[I].StratumId);TestEqual(TEXT("Caves preserve geological material bindings"),A[I].Weights.Num(),B[I].Weights.Num());for(int32 J=0;J<A[I].Weights.Num();++J){TestEqual(TEXT("Strata surface IDs are unchanged"),A[I].Weights[J].Id,B[I].Weights[J].Id);TestEqual(TEXT("Strata weights are unchanged"),A[I].Weights[J].Weight,B[I].Weights[J].Weight);}}
        AddInfo(FString::Printf(TEXT("SPATIAL_FAMILY seed=%d networks=%d rooms=%d passages=%d levels=%d ramps=%d probes=%d clearance=%.4f support=%.4f grade=%.4f scalarResidual=%.9g"),Seed,C.Networks,C.Chambers.Num(),C.Passages.Num(),Levels,Ramps,Heads.Num(),Clearance,Support,Grade,Residual));
    }
    TestTrue(TEXT("Planet family includes single-level caves and deep systems"),Depths.Contains(1)&&Depths.Num()>=3);
    TestTrue(TEXT("Small and huge caves share the family with a broad size distribution"),Largest>Smallest*3);
    TestTrue(TEXT("Seeds change graph topology"),Topologies.Num()>1);TestTrue(TEXT("Chambers vary independently, rather than copying one composition"),ChamberWidths.Num()>12);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLProceduralCaveSelectionTest,"Catalyst.Landform.Caves.ProceduralFamily.DeterminismAndSelection",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLProceduralCaveSelectionTest::RunTest(const FString&)
{
    auto R=FCLPlanetCompiler::BasePlanet();R.NoiseLayers.Reset();R.Caves.bEnabled=true;R.Caves.ApplyProgression(1.);FString E;
    const auto P=FCLPlanetCompiler::Compile(R,E),Again=FCLPlanetCompiler::Compile(R,E);if(!P||!Again){AddError(E);return false;}
    const auto& C=*P->Caves;TestTrue(TEXT("Density retains multiple networks"),C.Networks>1);
    TestEqual(TEXT("Seed preserves count"),C.Networks,Again->Caves->Networks);
    for(int32 I=0;I<C.Passages.Num();++I)TestTrue(TEXT("Seed preserves exact generated geometry and floor route"),C.Passages[I].Points==Again->Caves->Passages[I].Points);
    const auto Selected=C.Cutters->Select(C.NetworkInfo[0].Id);
    for(const auto& N:C.Nodes)if(N.Network==C.NetworkInfo[0].Id)TestTrue(TEXT("Native selection retains every connected level"),Selected->Compose(N.Position,-1000)>.3);
    const auto* Remote=C.NetworkInfo.FindByPredicate([&](const auto& N){return !N.InspectionBounds.Intersect(C.NetworkInfo[0].InspectionBounds);});
    if(Remote)for(const auto& N:C.Nodes)if(N.Network==Remote->Id)TestTrue(TEXT("Native selection excludes remote networks"),Selected->Compose(N.Position,-1000)<0);
    return !HasAnyErrors();
}
#endif
