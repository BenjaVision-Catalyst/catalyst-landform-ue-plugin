#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Model/CLPlanetRecipe.h"
#include "Scene/CLScene.h"
#include "Model/CLCompileProgress.h"
#include "Tasks/Task.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "UObject/StrongObjectPtr.h"
namespace
{
FCLPlanetRecipe KarstRecipe(FString& E,FString Name=TEXT("NaturalKarst"))
{
    FString J;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/(TEXT("Examples/")+Name+TEXT(".json"))));
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>());if(!A->ImportRecipeJson(J,E))return {};return A->Recipe;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLNaturalKarstConstructionTest,"Catalyst.Landform.Caves.NaturalKarst.ConstructionAndVersions",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLNaturalKarstConstructionTest::RunTest(const FString&)
{
    FString E;auto R=KarstRecipe(E);const auto P=FCLPlanetCompiler::Compile(R,E);
    if(!P||!P->Caves){AddError(E);return false;}
    TestTrue(TEXT("Included carbonate evidence produces complete natural karst networks"),P->Caves->Networks>0);
    for(const auto& N:P->Caves->NetworkInfo)TestTrue(TEXT("This milestone contains one positively supported family"),N.Family==FCLCompiledCaves::EFamily::CarbonateKarst);
    TestTrue(TEXT("Rooms exist independently of wall-detail noise"),!P->Caves->Chambers.IsEmpty());
    for(const auto& C:P->Caves->Chambers)TestTrue(TEXT("New rooms use the karst chamber construction"),C.Form==ECLCaveForm::KarstChamber);
    TestTrue(TEXT("No box scars or unqualified breakdown are introduced into the new family"),P->Caves->Infills.IsEmpty());
    TArray<FVector> Core;
    for(const auto& V:P->Caves->Passages)
    {
        if(V.Entrance)continue;
        TestTrue(TEXT("Passages keep one coherent construction"),V.Form==ECLCaveForm::KarstPassage&&V.EndForm==V.Form);
        TestEqual(TEXT("Geological frames are retained at every section"),V.BeddingUps.Num(),V.Points.Num());
        for(int32 I=0;I<V.Points.Num();++I)
        {
            Core.Add(V.Points[I]);
            if(I)for(double T:{.25,.5,.75})Core.Add(FMath::Lerp(V.Points[I-1],V.Points[I],T));
        }
    }
    const auto Scene=FCLScene::FromPlanet(P,E);TArray<FCLSceneShapeSample> F;Scene->SampleShape({Core},F,E);
    double Minimum=MAX_dbl;for(const auto& V:F)Minimum=FMath::Min(Minimum,V.FieldMeters);
    TestTrue(TEXT("Passage and junction core probes remain connected air"),Minimum>.02);
    auto Old=R;Old.Caves.AlgorithmVersion=4;const auto Legacy=FCLPlanetCompiler::Compile(Old,E);if(!Legacy){AddError(E);return false;}
    TestTrue(TEXT("Explicit profile 4 retains its previous chamber/scar construction"),Legacy->Caves->Chambers.ContainsByPredicate([](const auto& C){return C.Form==ECLCaveForm::KarstHall;}));
    for(const auto& V:Legacy->Caves->Passages)TestTrue(TEXT("Old saved profiles do not acquire new passage operators"),V.Form!=ECLCaveForm::KarstPassage);
    for(const auto& C:Legacy->Caves->Chambers)TestTrue(TEXT("Old saved profiles do not acquire new room operators"),C.Form!=ECLCaveForm::KarstChamber);
    auto Unknown=MakeShared<FCLCompiledPlanetRecipe>(*P);Unknown->Rock.Reset();
    TestFalse(TEXT("New morphology cannot invent carbonate hosts"),FCLCompiledCaves::Compile(R.Caves,*Unknown,R.Seed,E).IsValid());
    AddInfo(FString::Printf(TEXT("NATURAL_KARST %d networks, %d passages, %d rooms; %d core probes, minimum %.6f m"),P->Caves->Networks,P->Caves->Passages.Num(),P->Caves->Chambers.Num(),Core.Num(),Minimum));
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLNaturalKarstFieldTest,"Catalyst.Landform.Caves.NaturalKarst.NativeFieldAnatomy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLNaturalKarstFieldTest::RunTest(const FString&)
{
    FString E;FCLCavePassage P;P.Network=FGuid(5,1,1,1);P.Points={{-12,0,100},{12,0,100}};P.Sections={{2,3},{2,3}};P.Form=P.EndForm=ECLCaveForm::KarstPassage;P.BeddingUps={FVector::ZAxisVector,FVector::ZAxisVector};
    const auto C=FCLCompiledCaveVolume::Compile(MakeArrayView(&P,1),{},nullptr,731,E);if(!C){AddError(E);return false;}
    TestTrue(TEXT("Main passage has real open volume before adding noise"),C->Compose({0,0,100},-100)>.5);
    TestTrue(TEXT("High rectangular corner stays rock under the vault"),C->Compose({0,1.9,102.9},-100)<-.05);
    TestTrue(TEXT("An incised lower floor region is connected to the main passage"),C->Compose({0,0,97.2},-100)>.05);
    TestTrue(TEXT("Broad upper vault is open away from the centreline"),C->Compose({0,1.3,101.2},-100)>.05);
    TestTrue(TEXT("Lower side wall defines a narrower channel instead of an oval tube"),C->Compose({0,1.3,98.65},-100)<-.05);
    TestTrue(TEXT("Rock remains beneath the floor"),C->Compose({0,0,95.5},-100)<-.1);
    TestTrue(TEXT("Upper side walls are asymmetric"),FMath::Abs(C->Compose({0,1.6,100.7},-100)-C->Compose({0,-1.6,100.7},-100))>.01);
    TArray<FVector> Q;for(int32 I=0;I<512;++I)Q.Add({double(I%17)-8.,double(I%23)*.2-2.2,96.+double(I%31)*.25});
    TArray<double> Fields;Fields.Init(-100,Q.Num());C->ComposeBatch(Q,Fields);
    for(int32 I=0;I<Q.Num();++I)TestTrue(TEXT("Native bulk/sparse field anatomy agrees"),FMath::Abs(Fields[I]-C->Compose(Q[I],-100))<1.e-5);
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLNaturalKarstMinimumTest,"Catalyst.Landform.Caves.NaturalKarst.EnabledMinimumAndReseed",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLNaturalKarstMinimumTest::RunTest(const FString&)
{
    FString E;const auto Original=KarstRecipe(E);
    for(double V:{.01,.5,1.})
    {
        auto R=Original;R.Caves.ApplyProgression(V);const auto P=FCLPlanetCompiler::Compile(R,E);if(!P){AddError(E);return false;}
        TestEqual(TEXT("Explicit Character authoring selects profile 5"),R.Caves.AlgorithmVersion,5);
        TestTrue(TEXT("Enabled Character 1 retains at least one complete eligible cave"),P->Caves->Networks>=1);
    }
    auto R=Original;const auto A=FCLPlanetCompiler::Compile(R,E);++R.Caves.Seed;const auto B=FCLPlanetCompiler::Compile(R,E);if(!A||!B){AddError(E);return false;}
    TestTrue(TEXT("Existing reseed action changes the natural cave layout"),A->Caves->Passages[0].Points!=B->Caves->Passages[0].Points);
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLNaturalKarstBackgroundTest,"Catalyst.Landform.Caves.NaturalKarst.BackgroundAndFrozenStructural",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLNaturalKarstBackgroundTest::RunTest(const FString&)
{
    FString E;const auto R=KarstRecipe(E);const auto Direct=FCLPlanetCompiler::Compile(R,E);
    const auto Prepared=FCLPlanetCompiler::PrepareBackground(R,E);if(!Direct||!Prepared){AddError(E);return false;}
    const auto Control=MakeShared<FCLCompileProgress>();TSharedPtr<const FCLCompiledPlanetRecipe> Worker;FString WE;
    auto Task=UE::Tasks::Launch(UE_SOURCE_LOCATION,[&]{Worker=FCLPlanetCompiler::CompleteBackground(*Prepared,Control,WE);});Task.Wait();
    if(!Worker){AddError(WE);return false;}
    TestEqual(TEXT("Background publication retains complete karst networks"),Worker->Caves->Networks,Direct->Caves->Networks);
    TArray<FVector> Points;const auto& Room=Direct->Caves->Chambers[0];
    for(int32 I=0;I<256;++I)Points.Add(Room.Center+Room.Rotation.RotateVector(FVector((I%17-8)*1.3,(I%13-6)*1.1,(I%11-5)*.9)));
    TArray<double> A,B;A.Init(-100,Points.Num());B=A;Direct->Caves->Cutters->ComposeBatch(Points,A);Worker->Caves->Cutters->ComposeBatch(Points,B);
    for(int32 I=0;I<Points.Num();++I)TestTrue(TEXT("Native shape is identical through background evaluation"),FMath::Abs(A[I]-B[I])<1.e-6);
    auto Fissure=KarstRecipe(E,TEXT("FissureMorphology"));Fissure.Caves.AlgorithmVersion=4;
    const auto Old=FCLPlanetCompiler::Compile(Fissure,E);Fissure.Caves.AlgorithmVersion=5;const auto Fresh=FCLPlanetCompiler::Compile(Fissure,E);
    if(!Old||!Fresh){AddError(E);return false;}
    TestEqual(TEXT("The structural family retains its network topology"),Old->Caves->Passages.Num(),Fresh->Caves->Passages.Num());
    for(int32 I=0;I<Old->Caves->Passages.Num();++I)
    {
        const auto& X=Old->Caves->Passages[I];const auto& Y=Fresh->Caves->Passages[I];
        TestTrue(TEXT("Unchanged family retains geometry, sections and geological frames"),X.Points==Y.Points&&X.Sections==Y.Sections&&X.BeddingUps==Y.BeddingUps&&X.Form==Y.Form);
    }
    Points.Reset();for(const auto& P:Old->Caves->Passages)Points.Append(P.Points);
    A.Init(-100,Points.Num());B=A;Old->Caves->Cutters->ComposeBatch(Points,A);Fresh->Caves->Cutters->ComposeBatch(Points,B);
    for(int32 I=0;I<Points.Num();++I)TestTrue(TEXT("Structural field stays frozen while karst changes"),FMath::Abs(A[I]-B[I])<1.e-6);
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLNaturalKarstPackageTest,"Catalyst.Landform.Caves.NaturalKarst.NativePackage",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLNaturalKarstPackageTest::RunTest(const FString&)
{
    const auto* Saved=LoadObject<UCLPlanetRecipeAsset>(nullptr,TEXT("/Game/LandformDesigner/Caves/DA_NaturalKarst.DA_NaturalKarst"));
    if(!TestNotNull(TEXT("Create-only natural karst review package exists"),Saved))return false;
    TestEqual(TEXT("Native delta package retains profile 5 despite frozen CDO default"),Saved->Recipe.Caves.AlgorithmVersion,5);
    FString E;const auto Expected=FCLPlanetCompiler::Compile(KarstRecipe(E),E),Actual=FCLPlanetCompiler::Compile(Saved->Recipe,E);
    if(!Expected||!Actual){AddError(E);return false;}
    if(!TestEqual(TEXT("Native package retains every passage"),Actual->Caves->Passages.Num(),Expected->Caves->Passages.Num()))return false;
    for(int32 I=0;I<Expected->Caves->Passages.Num();++I)
    {
        const auto& A=Actual->Caves->Passages[I];const auto& B=Expected->Caves->Passages[I];
        TestTrue(TEXT("Native package retains exact passage geometry and bedding"),A.Points==B.Points&&A.Sections==B.Sections&&A.BeddingUps==B.BeddingUps&&A.Form==B.Form);
    }
    if(!TestEqual(TEXT("Native package retains every chamber"),Actual->Caves->Chambers.Num(),Expected->Caves->Chambers.Num()))return false;
    TArray<FVector> Q;
    for(const auto& C:Expected->Caves->Chambers)
        for(int32 I=0;I<128;++I)Q.Add(C.Center+C.Rotation.RotateVector(FVector((I%11-5)*C.HalfExtents.X/4.,(I%13-6)*C.HalfExtents.Y/5.,(I%17-8)*C.HalfExtents.Z/7.)));
    TArray<double> A,B;A.Init(-100,Q.Num());B=A;Expected->Caves->Cutters->ComposeBatch(Q,A);Actual->Caves->Cutters->ComposeBatch(Q,B);
    for(int32 I=0;I<Q.Num();++I)TestTrue(TEXT("Cold-loaded native package reproduces chamber and surrounding rock fields"),FMath::Abs(A[I]-B[I])<1.e-6);
    return !HasAnyErrors();
}
#endif
