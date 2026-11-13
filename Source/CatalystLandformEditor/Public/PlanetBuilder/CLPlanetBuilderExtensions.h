#pragma once
#include "CoreMinimal.h"
#include "Model/CLPlanetRecipe.h"
class SWidget;
/** Optional authoring overlay, in the planet's metric body frame. Bounded to 8192 samples. */
struct CATALYSTLANDFORMEDITOR_API FCLPlanetBuilderOverlay
{
    TArray<FVector> BodyMeters;
    TArray<FLinearColor> Colors;
};
/** Explicit toolkit/asset context. Getter returns the last accepted immutable terrain plan. */
struct CATALYSTLANDFORMEDITOR_API FCLPlanetBuilderContext
{
    TWeakObjectPtr<UCLPlanetRecipeAsset> Planet;
    TFunction<TSharedPtr<const FCLCompiledPlanetRecipe>()> GetPlan;
    TFunction<void(TSharedPtr<const FCLPlanetBuilderOverlay>)> PublishOverlay;
};
class CATALYSTLANDFORMEDITOR_API ICLPlanetBuilderExtension
{
public:
    virtual ~ICLPlanetBuilderExtension() = default;
    virtual TSharedRef<SWidget> Widget() = 0;
    virtual void SetActive(bool Active) = 0;
    virtual void GetSaveableObjects(TArray<UObject*>& Objects) const = 0;
};
struct CATALYSTLANDFORMEDITOR_API FCLPlanetBuilderProvider
{
    FName Id;
    FText Label;
    TFunction<TSharedRef<ICLPlanetBuilderExtension>(FCLPlanetBuilderContext)> Create;
};
/** Providers register at editor startup. Existing toolkits keep their own sessions until closed. */
class CATALYSTLANDFORMEDITOR_API FCLPlanetBuilderExtensions
{
public:
    static void Register(FCLPlanetBuilderProvider Provider);
    static void Unregister(FName Id);
    static TArray<FCLPlanetBuilderProvider> Providers();
};
