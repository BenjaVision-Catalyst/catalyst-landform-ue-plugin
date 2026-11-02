#if WITH_DEV_AUTOMATION_TESTS
#include "Presentation/CLProjectionGPUVerification.h"
#include "Misc/AutomationTest.h"

class FCLWaitForProjectionParity final : public IAutomationLatentCommand
{
public:
    FCLWaitForProjectionParity(FAutomationTestBase* InTest, TSharedPtr<FCLProjectionGPUVerification> InVerification)
        : Test(InTest), Verification(MoveTemp(InVerification)) {}
    bool Update() override
    {
        if (!Verification->Tick()) return false;
        const auto& R = Verification->GetResult();
        Test->TestTrue(*R.Message, R.bPassed);
        Test->TestEqual(TEXT("All chart/curvature/radius combinations checked"), R.Charts, 18);
        Test->TestEqual(TEXT("All valid, edge and invalid queries checked"), R.Queries, 2520);
        Test->AddInfo(FString::Printf(TEXT("GPU parity: %d charts, %d queries; max position %.12g radii, inverse %.12g radii, vector %.12g, normal %.12g"),
            R.Charts, R.Queries, R.MaxPositionErrorRadii, R.MaxInverseErrorRadii, R.MaxVectorError, R.MaxNormalError));
        return true;
    }
private:
    FAutomationTestBase* Test;
    TSharedPtr<FCLProjectionGPUVerification> Verification;
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLProjectionGPUParityTest, "Catalyst.Landform.Presentation.GPUParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLProjectionGPUParityTest::RunTest(const FString&)
{
    FString Error;
    const auto Verification = FCLProjectionGPUVerification::TryStart(Error);
    if (!TestTrue(*Error, Verification.IsValid())) return false;
    ADD_LATENT_AUTOMATION_COMMAND(FCLWaitForProjectionParity(this, Verification));
    return true;
}
#endif
