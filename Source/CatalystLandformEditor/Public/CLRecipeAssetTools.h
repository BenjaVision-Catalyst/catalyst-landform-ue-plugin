#pragma once
#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "AssetDefinitions/CFAssetDefinition_Model.h"
#include "CLRecipeAssetTools.generated.h"

UCLASS()
class CATALYSTLANDFORMEDITOR_API UCLPlanetRecipeFactory : public UFactory
{
    GENERATED_BODY()
public:
    UCLPlanetRecipeFactory();
    virtual UObject* FactoryCreateNew(UClass* Class, UObject* Parent, FName Name, EObjectFlags Flags,
        UObject* Context, FFeedbackContext* Warn) override;
};

UCLASS()
class CATALYSTLANDFORMEDITOR_API UCLRecipeAssetDefinition : public UCFAssetDefinition_Model
{
    GENERATED_BODY()
public:
    virtual TSoftClassPtr<UObject> GetAssetClass() const override;
    virtual FText GetAssetDisplayName() const override { return NSLOCTEXT("CL", "RecipeAsset", "Landform Planet Recipe"); }
    virtual EAssetCommandResult OpenAssets(const FAssetOpenArgs& Args) const override;
};

UCLASS()
class CATALYSTLANDFORMEDITOR_API UCLTerrainTypeFactory : public UFactory
{
    GENERATED_BODY()
public:
    UCLTerrainTypeFactory();
    virtual UObject* FactoryCreateNew(UClass* Class, UObject* Parent, FName Name, EObjectFlags Flags,
        UObject* Context, FFeedbackContext* Warn) override;
};

UCLASS()
class CATALYSTLANDFORMEDITOR_API UCLTerrainTypeAssetDefinition : public UCFAssetDefinition_Model
{
    GENERATED_BODY()
public:
    virtual TSoftClassPtr<UObject> GetAssetClass() const override;
    virtual FText GetAssetDisplayName() const override { return NSLOCTEXT("CL", "TerrainTypeAsset", "Landform Terrain Type"); }
    virtual EAssetCommandResult OpenAssets(const FAssetOpenArgs& Args) const override;
};
