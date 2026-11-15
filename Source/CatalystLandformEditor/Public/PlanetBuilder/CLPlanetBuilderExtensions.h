#pragma once
#include "CoreMinimal.h"
#include "Model/CLPlanetRecipe.h"
class SWidget;
struct FCLPlanetBuilderOverlaySegment { FVector A,B; FLinearColor Color=FLinearColor::White; float WidthPixels=1.25f; };
struct FCLPlanetBuilderOverlayLabel { FVector BodyMeters; FString Text; FLinearColor Color=FLinearColor::White; };
/** Overlay is a presentation radius. Absence means the outer sky boundary. */
inline double CLPlanetOverlayRadiusMeters(const FCLCompiledPlanetRecipe& Plan)
{
    const auto* Guide=Plan.Guides.FindByPredicate([](const auto& G){return G.Name==TEXT("Overlay");});
    return Guide?FMath::Max(0.,Plan.RadiusMeters+Guide->HeightMeters):Plan.RadiusMeters;
}
/** Optional authoring overlay, in the planet's metric body frame. Bounded to 8192 samples. */
struct CATALYSTLANDFORMEDITOR_API FCLPlanetBuilderOverlay
{
    TArray<FVector> BodyMeters;
    TArray<FLinearColor> Colors;
    /** Optional metric vectors at matching sample indices. At most 512 arrows are drawn. */
    TArray<FVector> BodyVectorsMeters;
    float PointSizePixels = 7;
    /** Outward mathematical triangle winding; at most 8192 vertices / 49152 indices. */
    TArray<FVector> MeshBodyMeters;
    TArray<FLinearColor> MeshColors;
    TArray<int32> MeshIndices;
    TArray<FCLPlanetBuilderOverlaySegment> Segments;
    TArray<FCLPlanetBuilderOverlayLabel> Labels;
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
