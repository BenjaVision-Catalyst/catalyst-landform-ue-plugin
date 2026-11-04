#pragma once
#include "CoreMinimal.h"
#include <atomic>
struct FCLCompiledPlanetRecipe;
struct FCLPreparedPlanetCompile;
enum class ECLCompileStage:uint8 {Preparing,Geology,Elevation,Drainage,Complete};
/** Per-evaluation plain state. No widget, mutable authoring asset or UObject lookup is used by a worker. */
struct CATALYSTLANDFORM_API FCLCompileProgress
{
    std::atomic<bool> Cancelled{false};
    std::atomic<ECLCompileStage> Stage{ECLCompileStage::Preparing};
    static bool IsPreparing();
    static int32 ReusedNativeInputs(const FCLPreparedPlanetCompile&);
    static bool Check(FString& Error);
    static void Report(ECLCompileStage Stage);
    static bool Sample(const FCLCompiledPlanetRecipe&,TConstArrayView<FVector>,TArray<double>&,FString&);
};
