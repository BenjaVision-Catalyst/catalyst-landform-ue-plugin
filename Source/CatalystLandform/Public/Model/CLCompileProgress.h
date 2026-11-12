#pragma once
#include "CoreMinimal.h"
#include <atomic>
struct FCLCompiledPlanetRecipe;
struct FCLPreparedPlanetCompile;
enum class ECLCompileStage:uint8 {Preparing,Geology,Elevation,Drainage,Caves,Presentation,Complete};
/** Per-evaluation plain state. No widget, mutable authoring asset or UObject lookup is used by a worker. */
struct CATALYSTLANDFORM_API FCLCompileProgress
{
    std::atomic<bool> Cancelled{false};
    std::atomic<ECLCompileStage> Stage{ECLCompileStage::Preparing};
    /** One coherent phase-local completed/total snapshot, zero when unmeasured. */
    std::atomic<uint64> Work{0};
    static void Count(uint32 Completed,uint32 Total);
    static bool IsPreparing();
    /** Explicit evaluation-only Solo; normal generation always returns an invalid ID. */
    static FGuid SoloPreviewLandform();
    static int32 ReusedNativeInputs(const FCLPreparedPlanetCompile&);
    static bool Check(FString& Error);
    static void Report(ECLCompileStage Stage);
    static bool Sample(const FCLCompiledPlanetRecipe&,TConstArrayView<FVector>,TArray<double>&,FString&);
};
