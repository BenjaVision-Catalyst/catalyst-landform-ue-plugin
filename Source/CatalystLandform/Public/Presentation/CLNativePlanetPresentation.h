#pragma once
#include "CoreMinimal.h"
class ACLPlanet;
class FJsonObject;
class UMaterialInterface;
class UPrimitiveComponent;
struct FCLCompiledPlanetRecipe;
class FCLPlanetProjection;

struct FCLNanitePresentationOptions
{
    bool bWorldAnchored = false, bConsoleCurvature = true;
    bool bGameplay = false, bNativeMaterials = false, bNativeControl = false, bSweep = false, bFixed = false;
    double InitialCurvature = 1;
    double MaxArcRadians = UE_PI / 3;
    double ReferenceRadiusCm = 0; // Zero retains the recipe's physical base reference.
    int32 BoundsMode = 1;
};

/** Native VPP render binding. Recipes, generated geometry, collision and actor transforms remain canonical.
 */
class CATALYSTLANDFORM_API FCLNativePlanetPresentation
{
public:
    static TUniquePtr<FCLNativePlanetPresentation> TryCreate(ACLPlanet &Planet,
                                                             const FCLCompiledPlanetRecipe &Plan,
                                                             UMaterialInterface &Material, FString &Error);
    static TUniquePtr<FCLNativePlanetPresentation> TryCreate(ACLPlanet &Planet,
                                                             const FCLCompiledPlanetRecipe &Plan,
                                                             UMaterialInterface &Material,
                                                             const FCLNanitePresentationOptions &Options,
                                                             FString &Error);
    ~FCLNativePlanetPresentation();
    bool AddPrimitive(UPrimitiveComponent& Component, FString& Error);
    bool Tick(const FVector &Anchor, double Elapsed, bool bMeasure, FString &Error);
    bool SetCurvature(double Value);
    bool GetCurrentProjection(FCLPlanetProjection &Out) const;
    void WriteReport(FJsonObject &Report) const;

private:
    FCLNativePlanetPresentation();
    struct FImpl;
    TSharedPtr<FImpl> Impl;
};
