#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Model/CLPlanetRecipe.h"
#include "Scene/CLScene.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLCaveMorphologyTest,"Catalyst.Landform.Caves.ConnectedVolumetricMorphology",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLCaveMorphologyTest::RunTest(const FString&)
{
    FCLCaveSettings Defaults,Resolved;Resolved.ApplyProgression(.5);
    TestTrue(TEXT("New caves start at the resolved middle of the one-knob profile"),FMath::IsNearlyEqual(Defaults.LengthMeters,Resolved.LengthMeters)&&FMath::IsNearlyEqual(Defaults.RadiusMeters,Resolved.RadiusMeters)&&FMath::IsNearlyEqual(Defaults.DepthMeters,Resolved.DepthMeters)&&FMath::IsNearlyEqual(Defaults.Branching,Resolved.Branching));
    FString E,J;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/CavesSmall.json")));
    TStrongObjectPtr<UCLPlanetRecipeAsset> Asset(NewObject<UCLPlanetRecipeAsset>());if(!Asset->ImportRecipeJson(J,E)){AddError(E);return false;}
    auto R=Asset->Recipe;R.Caves.bEnabled=false;const auto Base=FCLPlanetCompiler::Compile(R,E);if(!Base){AddError(E);return false;}
    int32 PreviousEdges=0;TSet<uint32> Layouts;
    for(double Character:{.01,.5,1.})for(int32 Seed=73;Seed<77;++Seed)
    {
        auto Settings=Asset->Recipe.Caves;Settings.ApplyProgression(Character);Settings.Seed=Seed;
        const auto C=FCLCompiledCaves::Compile(Settings,*Base,R.Seed,E);if(!C){AddError(E);return false;}
        TestTrue(TEXT("Every enabled example has a complete cave"),C->Networks>=1);
        auto SealedSettings=Settings;SealedSettings.bSurfaceEntrances=false;const auto Sealed=FCLCompiledCaves::Compile(SealedSettings,*Base,R.Seed,E);
        if(!Sealed){AddError(E);return false;}
        TestEqual(TEXT("Entrance toggle retains chamber count"),Sealed->Chambers.Num(),C->Chambers.Num());
        for(int32 I=0;I<C->Chambers.Num();++I)TestTrue(TEXT("Entrance toggle does not regenerate underground chambers"),C->Chambers[I].Center==Sealed->Chambers[I].Center&&C->Chambers[I].HalfExtents==Sealed->Chambers[I].HalfExtents);

        TMap<int32,TArray<int32>> Adjacency;
        TArray<FVector> Core;double Narrowest=MAX_dbl,Widest=0,CurveDeviation=0;FBox Extent(ForceInit);
        for(const auto& P:C->Passages)
        {
            Adjacency.FindOrAdd(P.From).Add(P.To);Adjacency.FindOrAdd(P.To).Add(P.From);
            for(int32 I=0;I<P.Points.Num();++I)
            {
                const auto Section=P.Sections[I];Narrowest=FMath::Min(Narrowest,Section.X);Widest=FMath::Max(Widest,Section.X);Extent+=P.Points[I];
                CurveDeviation=FMath::Max(CurveDeviation,FMath::PointDistToSegment(P.Points[I],P.Points[0],P.Points.Last()));
                if(!P.Entrance)
                {
                    Core.Add(P.Points[I]);
                    // Independent chord probes between controls must remain
                    // inside the real cut; a graph line alone cannot pass.
                    if(I)for(int32 K=1;K<4;++K)Core.Add(FMath::Lerp(P.Points[I-1],P.Points[I],K/4.));
                }
            }
        }
        TSet<int32> Reached;TArray<int32> Queue;Queue.Add(0);Reached.Add(0);
        for(int32 I=0;I<Queue.Num();++I)for(int32 Next:Adjacency[Queue[I]])if(!Reached.Contains(Next)){Reached.Add(Next);Queue.Add(Next);}
        int32 FirstNodes=0,FirstEdges=0;const auto Id=C->Nodes[0].Network;
        for(const auto& N:C->Nodes)FirstNodes+=N.Network==Id;for(const auto& P:C->Passages)FirstEdges+=P.Network==Id;
        TestEqual(TEXT("Every node of the first cave belongs to one connected component"),Reached.Num(),FirstNodes);
        TestTrue(TEXT("Cave includes genuine lateral and vertical extent"),Extent.GetSize().GetMin()>Settings.RadiusMeters*2);
        TestTrue(TEXT("Passages bend between shared junctions"),CurveDeviation>Settings.RadiusMeters*.15);
        TestTrue(TEXT("Passage section varies along the network"),Widest/Narrowest>1.4);
        TestTrue(TEXT("A chamber is part of the same cave volume"),!C->Chambers.IsEmpty());
        TArray<double> Field;Field.Init(-1000.,Core.Num());C->Cutters->ComposeBatch(Core,Field);double MinimumClearance=MAX_dbl;for(double V:Field)MinimumClearance=FMath::Min(MinimumClearance,V);
        TestTrue(TEXT("Dense route probes have open volumetric clearance, including junctions"),MinimumClearance>.1);
        const auto& Chamber=C->Chambers[0];const auto Center=Chamber.Center;
        // Probe actual occupied air in a chamber's cross-section, independently
        // of the renderer. Its width must exceed an ordinary passage.
        TArray<FVector> Cross;const auto Axis=Chamber.Rotation.GetAxisY();
        for(int32 I=-10;I<=10;++I)Cross.Add(Center+Axis*(I*Chamber.HalfExtents.Y/10.));
        Field.Init(-1000.,Cross.Num());C->Cutters->ComposeBatch(Cross,Field);int32 Open=0;for(double V:Field)Open+=V>.1;
        TestTrue(TEXT("Chambers have substantial interior area across their width"),Open>=17&&Chamber.HalfExtents.Y>Settings.RadiusMeters*1.2);
        if(Character==1.)TestTrue(TEXT("Developed networks include a reconnecting route"),FirstEdges-FirstNodes+1>=1);
        if(Seed==73){TestTrue(TEXT("Knob changes network topology"),FirstEdges>PreviousEdges);PreviousEdges=FirstEdges;}
        Layouts.Add(GetTypeHash(C->Passages[0].Points[2]));
        AddInfo(FString::Printf(TEXT("knob %.0f seed %d: %d networks, %d nodes, %d curved passages, %d chamber lobes; minimum route clearance %.3f m; width ratio %.3f; curve deviation %.3f m"),Character*100,Seed,C->Networks,C->Nodes.Num(),C->Passages.Num(),C->Chambers.Num(),MinimumClearance,Widest/Narrowest,CurveDeviation));
    }
    TestEqual(TEXT("Seeds and knob positions produce distinct physical networks"),Layouts.Num(),12);
    return true;
}
#endif
