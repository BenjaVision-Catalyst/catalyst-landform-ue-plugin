#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Model/CLPlanetRecipe.h"
#include "Scene/CLScene.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "UObject/StrongObjectPtr.h"
namespace
{
TSharedPtr<const FCLCompiledCaveVolume> Section(ECLCaveForm Form,FString& Error)
{
    FCLCavePassage P;P.Network=FGuid(4,1,1,1);P.Points={{-10,0,100},{10,0,100}};P.Sections={{2,3},{2,3}};P.Form=P.EndForm=Form;P.Development=.65;P.BeddingUps={FVector::ZAxisVector,FVector::ZAxisVector};P.Incision=Form==ECLCaveForm::Keyhole?1.1:0;
    return FCLCompiledCaveVolume::Compile(MakeArrayView(&P,1),{},nullptr,176,Error);
}
FCLPlanetRecipe Recipe(FString Name,FString& E)
{
    FString J;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples")/(Name+TEXT(".json"))));
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>());if(!A->ImportRecipeJson(J,E))return {};return A->Recipe;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLCaveSectionAnatomyTest,"Catalyst.Landform.Caves.Morphology.NativeSectionAnatomy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLCaveSectionAnatomyTest::RunTest(const FString&)
{
    FString E;const auto Cleft=Section(ECLCaveForm::JointCleft,E),Round=Section(ECLCaveForm::Phreatic,E),Keyhole=Section(ECLCaveForm::Keyhole,E),Canyon=Section(ECLCaveForm::Canyon,E);
    if(!Cleft||!Round||!Keyhole||!Canyon){AddError(E);return false;}
    const FVector Corner(0,1.65,102.4);TestTrue(TEXT("Fracture section retains planar corner volume absent from an elliptical conduit"),Cleft->Compose(Corner,-100)>.1&&Round->Compose(Corner,-100)<-.1);
    TestTrue(TEXT("Keyhole has a broad upper conduit"),Keyhole->Compose({0,1.4,101},-100)>.1);
    TestTrue(TEXT("Keyhole has a deep narrow canyon floor"),Keyhole->Compose({0,0,95.5},-100)>.1);
    TestTrue(TEXT("Rock shoulders remain beside the lower canyon"),Keyhole->Compose({0,1.4,95.5},-100)<-.1);
    TestTrue(TEXT("Canyon has high planar sides rather than an oval silhouette"),Canyon->Compose({0,1,102.7},-100)>.05&&Round->Compose({0,1,102.7},-100)<0);
    TArray<FVector> P;for(int32 I=0;I<19;++I)P.Add({0,(I-9)*.23,100+(I%5-2)*1.1});TArray<double> F;F.Init(-100,P.Num());Keyhole->ComposeBatch(P,F);for(int32 I=0;I<P.Num();++I)TestTrue(TEXT("Native sparse/bulk anatomy agrees"),FMath::Abs(F[I]-Keyhole->Compose(P[I],-100))<1.e-5);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLCaveSectionDevelopmentTest,"Catalyst.Landform.Caves.Morphology.GenerationAndProvenance",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLCaveSectionDevelopmentTest::RunTest(const FString&)
{
    FString E;const auto R=Recipe(TEXT("CaveMorphology"),E);const auto P=FCLPlanetCompiler::Compile(R,E);if(!P||!P->Caves){AddError(E);return false;}
    TSet<ECLCaveForm> Forms;int32 Karst=0,Structural=0;
    for(const auto& N:P->Caves->NetworkInfo){Karst+=N.Family==FCLCompiledCaves::EFamily::CarbonateKarst;Structural+=N.Family==FCLCompiledCaves::EFamily::Structural;}
    TestTrue(TEXT("One physical planet retains both geological families"),Karst>0&&Structural>0);
    TArray<FVector> Core;for(const auto& Path:P->Caves->Passages){Forms.Add(Path.Form);Forms.Add(Path.EndForm);TestEqual(TEXT("Every modern control has a sampled geological frame"),Path.BeddingUps.Num(),Path.Points.Num());if(!Path.Entrance)Core.Append(Path.Points);}
    TestTrue(TEXT("Distinct cleft, bedding, tube, keyhole and canyon constructions are present"),Forms.Contains(ECLCaveForm::JointCleft)&&Forms.Contains(ECLCaveForm::BeddingSlot)&&Forms.Contains(ECLCaveForm::Phreatic)&&Forms.Contains(ECLCaveForm::Keyhole)&&Forms.Contains(ECLCaveForm::Canyon));
    const auto Scene=FCLScene::FromPlanet(P,E);TArray<FCLSceneShapeSample> Fields;Scene->SampleShape({Core},Fields,E);for(const auto& F:Fields)TestTrue(TEXT("Geological sections keep shared graph cores open"),F.FieldMeters>.02);
    TestTrue(TEXT("Developed networks include real native solid breakdown"),!P->Caves->Infills.IsEmpty());
    int32 Exposed=0;
    for(const auto& B:P->Caves->Infills)
    {
        for(double H:{.3,.6,.85})
        {
            const FVector V=B.Center+B.Rotation.GetAxisZ()*B.HalfExtents.Z*H,Source=P->RockPositionMeters(V);if(Source.Equals(V,1.e-8))continue;
            ++Exposed;Scene->SampleShape({MakeArrayView(&V,1)},Fields,E);TestTrue(TEXT("Breakdown is solid terrain, not a decorative mesh"),Fields[0].FieldMeters<0);
            TArray<FCLRockSample> Geology;Scene->SampleGeology({MakeArrayView(&V,1)},Geology,E);const auto Original=P->Rock->Sample(Source);
            TestTrue(TEXT("Displaced blocks retain source unit and stratum"),Geology[0].UnitId==Original.UnitId&&Geology[0].StratumId==Original.StratumId);
        }
    }
    TestTrue(TEXT("At least one exposed transported block has source-geology coverage"),Exposed>0);
    auto Old=R;Old.Caves.AlgorithmVersion=3;const auto Legacy=FCLPlanetCompiler::Compile(Old,E);if(!Legacy){AddError(E);return false;}
    TestTrue(TEXT("Frozen profile 3 does not acquire new breakdown"),Legacy->Caves->Infills.IsEmpty());for(const auto& Path:Legacy->Caves->Passages)TestTrue(TEXT("Frozen profile 3 retains its previous section operator"),Path.Form==ECLCaveForm::LegacyOval);
    for(double V:{.01,.5,1.}){auto Changed=R;Changed.Caves.ApplyProgression(V);const auto Q=FCLPlanetCompiler::Compile(Changed,E);if(!Q){AddError(E);return false;}TestTrue(TEXT("Enabled morphology resolves a complete cave across the knob"),Q->Caves->Networks>=1);}
    AddInfo(FString::Printf(TEXT("MORPHOLOGY %d structural, %d karst; %d passages, %d chamber/scar volumes, %d transported blocks, %d source-geology probes"),Structural,Karst,P->Caves->Passages.Num(),P->Caves->Chambers.Num(),P->Caves->Infills.Num(),Exposed));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLCaveMorphologyLibraryTest,"Catalyst.Landform.Caves.Morphology.NativeLibrary",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLCaveMorphologyLibraryTest::RunTest(const FString&)
{
    FString E;
    for(const FString Name:{TEXT("FissureMorphology"),TEXT("KarstMorphology"),TEXT("CaveMorphology")})
    {
        const FString Path=TEXT("/Game/LandformDesigner/Caves/DA_")+Name;
        const auto* Saved=LoadObject<UCLPlanetRecipeAsset>(nullptr,*(Path+TEXT(".DA_")+Name));if(!TestNotNull(TEXT("Morphology library package exists"),Saved))return false;
        TestEqual(TEXT("Native delta package retains explicit morphology profile"),Saved->Recipe.Caves.AlgorithmVersion,4);
        const auto Expected=FCLPlanetCompiler::Compile(Recipe(Name,E),E),Actual=FCLPlanetCompiler::Compile(Saved->Recipe,E);if(!Expected||!Actual){AddError(E);return false;}
        if(!TestEqual(TEXT("Native package retains every qualified passage"),Actual->Caves->Passages.Num(),Expected->Caves->Passages.Num()))return false;
        for(int32 I=0;I<Expected->Caves->Passages.Num();++I)
        {
            const auto& A=Actual->Caves->Passages[I];const auto& B=Expected->Caves->Passages[I];
            TestTrue(TEXT("Native package retains exact geometry, section development and geological frames"),A.Points==B.Points&&A.Sections==B.Sections&&A.BeddingUps==B.BeddingUps&&A.Form==B.Form&&A.EndForm==B.EndForm&&A.Incision==B.Incision);
        }
        TestEqual(TEXT("Native package retains every breakdown block"),Actual->Caves->Infills.Num(),Expected->Caves->Infills.Num());
        for(int32 I=0;I<Expected->Caves->Infills.Num();++I){const auto& A=Actual->Caves->Infills[I];const auto& B=Expected->Caves->Infills[I];TestTrue(TEXT("Native package retains rigid block provenance"),A.Center==B.Center&&A.SourceCenter==B.SourceCenter&&A.Rotation==B.Rotation&&A.HalfExtents==B.HalfExtents);}
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLCaveBreakthroughFixtureTest,"Catalyst.Landform.Caves.Morphology.NativeBreakthroughFixture",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLCaveBreakthroughFixtureTest::RunTest(const FString&)
{
    FString E;const auto R=Recipe(TEXT("CaveBreakthrough"),E);const auto P=FCLPlanetCompiler::Compile(R,E);if(!P){AddError(E);return false;}
    TestFalse(TEXT("Controlled earthworks fixture is not a third generated cave family"),R.Caves.bEnabled);
    const auto Scene=FCLScene::FromPlanet(P,E);const TArray<FVector> Points={{5,0,517},{7,0,517},{9,0,517},{8.5,0,517},{7,0,514.8},{7,0,525.2}};TArray<FCLSceneShapeSample> F;
    if(!Scene->SampleShape({Points},F,E)){AddError(E);return false;}
    TestTrue(TEXT("Both separated room interiors are genuine air"),F[0].FieldMeters>.4&&F[2].FieldMeters>.4);
    TestTrue(TEXT("Two-metre barrier is genuine solid rock"),F[1].FieldMeters<-.4);
    TestTrue(TEXT("The intended sculpt brush centre is already in the second void"),F[3].FieldMeters>.2);
    TestTrue(TEXT("Solid floor and roof enclose the barrier"),F[4].FieldMeters<-.1&&F[5].FieldMeters<-.1);
    return true;
}
#endif
