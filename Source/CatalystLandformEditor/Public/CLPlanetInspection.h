#pragma once
#include "CoreMinimal.h"
class UCLPlanetRecipeAsset;
class ACLPlanet;
class UMaterialInterface;
enum class ECLInspectionPreparation : uint8 { Ready, Pending, Failed };
using FCLInspectionPreparation=TFunction<ECLInspectionPreparation(ACLPlanet&,FString&)>;
/** Immutable editor-to-host handoff. The host owns streaming, collision and movement. */
struct CATALYSTLANDFORMEDITOR_API FCLPlanetInspectionRequest
{
    FGuid Id=FGuid::NewGuid();
    FString RecipeJson;
    TWeakObjectPtr<UCLPlanetRecipeAsset> SourceAsset;
    FVector Direction=FVector::ZeroVector;
    TOptional<FVector> SurfacePointMeters;
    TFunction<void(FGuid,FVector,FString)> OnReturned;
    TFunction<void(FGuid,FVector)> OnPositionReturned;
    TFunction<void(FGuid,const FString&)> OnProgress;
};
DECLARE_DELEGATE_RetVal_TwoParams(bool,FCLLaunchPlanetInspection,FCLPlanetInspectionRequest,FString&);
/** One explicitly registered walking host per editor. No runtime dependency on editor UI. */
struct CATALYSTLANDFORMEDITOR_API FCLPlanetInspection
{
    static FCLLaunchPlanetInspection Launch;
    /** Optional providers prepare their own content in the explicit source world. */
    static void RegisterPreparation(FName Owner,FCLInspectionPreparation Prepare);
    static void UnregisterPreparation(FName Owner);
    static ECLInspectionPreparation Prepare(ACLPlanet& Planet,FString& Status);
    static void RegisterMaterialSources(FName Owner,TFunction<void(ACLPlanet&,TArray<UMaterialInterface*>&)> Collect);
    static void UnregisterMaterialSources(FName Owner);
    static void CollectMaterials(ACLPlanet&,TArray<UMaterialInterface*>&);
    static void Startup();
    static void Shutdown();
};
#if WITH_DEV_AUTOMATION_TESTS
/** Qualification only: route a real down/up pair through the open Planet's marker. */
CATALYSTLANDFORMEDITOR_API bool CLVerifyPlanetPlayerMarker(FVector Direction,FString& Error);
#endif
