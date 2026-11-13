#include "Misc/AutomationTest.h"
#include "CLPlanetInspection.h"
#include "CLPlanetWalker.h"
#include "CLRadialGravityMovement.h"
#include "Components/InputComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLPluginWalkHostTest,"Catalyst.Landform.Inspection.PluginWalkingHost",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLPluginWalkHostTest::RunTest(const FString&)
{
    TestTrue(TEXT("The plugin registers walking without a host project adapter"),FCLPlanetInspection::Launch.IsBound());
    const auto* Pawn=GetDefault<ACLPlanetWalker>();
    TestTrue(TEXT("The plugin's reusable character uses native radial movement"),Pawn->GetCharacterMovement()->IsA<UCLRadialGravityMovement>());
    TestTrue(TEXT("The player inherits VPP's native movement-base contract"),Pawn->IsA<AVoxelCharacter>());
    auto* Input=NewObject<UInputComponent>(GetTransientPackage());
    GetMutableDefault<ACLPlanetWalker>()->SetupPlayerInputComponent(Input);
    TestEqual(TEXT("The player is owned by Landform"),Pawn->GetClass()->GetOutermost()->GetName(),FString(TEXT("/Script/CatalystLandform")));
    TestNull(TEXT("Inspection waits for its explicit planet before creating a pawn"),GetDefault<ACLPlanetInspectionGameMode>()->DefaultPawnClass.Get());
    FCLPlanetInspectionRequest MissingSelection;FString Error;
    if(FCLPlanetInspection::Launch.IsBound())
    {
        TestFalse(TEXT("A missing selection is refused before starting PIE"),FCLPlanetInspection::Launch.Execute(MoveTemp(MissingSelection),Error));
        TestFalse(TEXT("A refusal explains why walking did not start"),Error.IsEmpty());
    }
    return true;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "HAL/IConsoleManager.h"
#include "Model/CLPlanetRecipe.h"
#include "Misc/DefaultValueHelper.h"
// Development-only entry into the exact editor host used by the marker. The
// asset and body-space direction are explicit; no fixture/world is substituted.
static FAutoConsoleCommand CLVerifyExistingWorldWalk(
    TEXT("landform.VerifyWalk"),TEXT("VerifyWalk <Planet asset path> <body direction X> <Y> <Z>"),
    FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
    {
        FString Error;double X,Y,Z;
        if(Args.Num()!=4||!FDefaultValueHelper::ParseDouble(Args[1],X)||!FDefaultValueHelper::ParseDouble(Args[2],Y)||!FDefaultValueHelper::ParseDouble(Args[3],Z))
        {UE_LOG(LogTemp,Error,TEXT("LANDFORM_VERIFY_WALK: expected explicit Planet asset and body direction"));return;}
        auto* Asset=LoadObject<UCLPlanetRecipeAsset>(nullptr,*Args[0]);FCLPlanetInspectionRequest Request;
        Request.SourceAsset=Asset;Request.Direction=FVector(X,Y,Z);
        if(!Asset||!Asset->ExportJsonString(Request.RecipeJson,Error)||!FCLPlanetInspection::Launch.IsBound()||
            !FCLPlanetInspection::Launch.Execute(MoveTemp(Request),Error))
            UE_LOG(LogTemp,Error,TEXT("LANDFORM_VERIFY_WALK: %s"),*Error);
    }));
#endif
