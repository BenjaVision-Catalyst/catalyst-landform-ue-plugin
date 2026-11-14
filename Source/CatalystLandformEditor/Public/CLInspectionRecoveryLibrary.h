#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "CLInspectionRecoveryLibrary.generated.h"

/** Explicit editor recovery boundary before restoring package files on disk. */
UCLASS()
class CATALYSTLANDFORMEDITOR_API UCLInspectionRecoveryLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Fully load the named packages and release their readers without saving or clearing dirtiness. */
    UFUNCTION(BlueprintCallable, Category="Landform|Inspection|Recovery")
    static bool DetachRecoveryPackages(const TArray<FString>& PackageNames, FString& Error);

    /** Native asset rename; saves only the renamed definition/redirector, leaves referencers dirty. */
    UFUNCTION(BlueprintCallable, Category="Landform|Inspection|Recovery")
    static bool RenameAssetPreservingUnsavedReferences(UObject* Asset, const FString& NewName, const TArray<FString>& ReferencingPackages, FString& Error);
};
