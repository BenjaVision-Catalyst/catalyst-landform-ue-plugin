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
