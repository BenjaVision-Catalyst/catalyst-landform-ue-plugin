#pragma once
#include "CoreMinimal.h"

struct FCLProjectionGPUVerificationResult
{
    bool bComplete = false;
    bool bPassed = false;
    int32 Charts = 0;
    int32 Queries = 0;
    double MaxPositionErrorRadii = 0;
    double MaxInverseErrorRadii = 0;
    double MaxVectorError = 0;
    double MaxNormalError = 0;
    FString Message;
};

/** One CPU/GPU qualification path shared by editor automation and the packaged lab.
 * Explicit diagnostic only. Tick never blocks; requests retire on completion/failure/timeout.
 */
class CATALYSTLANDFORM_API FCLProjectionGPUVerification
{
public:
    static TSharedPtr<FCLProjectionGPUVerification> TryStart(FString& Error);
    ~FCLProjectionGPUVerification();
    bool Tick();
    const FCLProjectionGPUVerificationResult& GetResult() const;
private:
    FCLProjectionGPUVerification();
    struct FImpl;
    TUniquePtr<FImpl> Impl;
};
