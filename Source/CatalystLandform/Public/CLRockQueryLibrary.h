#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Model/CLRockLayer.h"
#include "VoxelMinimal.h"
#include "CLRockQueryLibrary.generated.h"
class ACLPlanet;
class UVoxelLinearColorMetadata;
UCLASS()
class CATALYSTLANDFORM_API UCLRockQueryLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Native VPP query of current solid + painted properties, bound to this
     * planet's immutable geology snapshot. Uses VPP futures, layers and metadata. */
    static TVoxelFuture<FCLRockSample> QueryRock(ACLPlanet* Planet,const FVector& WorldPosition);
    UFUNCTION(BlueprintCallable,Category="Landform|Rock")
    static FVoxelFuture QueryExposedRock(FCLRockSample& Result,ACLPlanet* Planet,FVector WorldPosition);
    UFUNCTION(BlueprintCallable,Category="Landform|Rock")
    static FLinearColor MakeFillProperties(double MiningResistance=.15,double Erodibility=.85);
    UFUNCTION(BlueprintPure,Category="Landform|Rock")
    static UVoxelLinearColorMetadata* GetPropertiesMetadata();
};
