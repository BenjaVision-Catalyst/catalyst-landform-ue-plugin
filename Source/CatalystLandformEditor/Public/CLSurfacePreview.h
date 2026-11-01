#pragma once
#include "CoreMinimal.h"
#include "Surface/CLSurfaceCompiler.h"
class UMaterialInterface;
/** UV0 = source XY cm; UV1 = source Z cm / surface mask; UV2/3 = weights 4..7. */
CATALYSTLANDFORMEDITOR_API UMaterialInterface* CLMakeSurfacePreviewMaterial(UObject* Outer,const FCLCompiledSurfaceLibrary& Library);
CATALYSTLANDFORMEDITOR_API FLinearColor CLPackSurfaceWeights(const FCLSurfaceWeights& Weights);
