#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "CLInspectionRecoveryLibrary.generated.h"

/** Explicit editor recovery boundary before restoring package files on disk. */
UCLASS()
class CATALYSTLANDFORMEDITOR_API UCLInspectionRecoveryLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    static FDelegateHandle InstallSlateShutdownGuard();
    static void RemoveSlateShutdownGuard(FDelegateHandle Handle);
    /** Fully load the named packages and release their readers without saving or clearing dirtiness. */
    UFUNCTION(BlueprintCallable, Category="Landform|Inspection|Recovery")
    static bool DetachRecoveryPackages(const TArray<FString>& PackageNames, FString& Error);
};
