#pragma once
#include "CoreMinimal.h"
#include "Model/CLHeightReference.h"
#include "CLSurfaceLayers.generated.h"

/** Ordered surface overlays. Names/revisions resolve through the client's surface library. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLSurfaceLayer
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName Name=TEXT("Surface layer");
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bEnabled=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName SurfaceId=TEXT("Rock");
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="1")) int32 SurfaceRevision=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(DisplayName="Slope range",Units="deg")) FVector2D SlopeDegrees=FVector2D(0,90);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(DisplayName="Slope feather",Units="deg",ClampMin="0.1",ClampMax="45")) double SlopeFeatherDegrees=5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bUseHeightRange=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(EditCondition="bUseHeightRange")) FCLHeightReference LowerHeight;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(EditCondition="bUseHeightRange")) FCLHeightReference UpperHeight;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(EditCondition="bUseHeightRange",ClampMin="0.00001",ClampMax="1")) double HeightFeatherRadiusFraction=.005;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double Strength=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double NoiseAmount=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(DisplayName="Noise wavelength",Units="m",ClampMin="0.1")) double NoiseWavelengthMeters=80;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 NoiseSeed=31;
};
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLSurfaceStack
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 AlgorithmVersion=1;
    /** None preserves the legacy material path. An active stack always has a fallback. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName DefaultSurfaceId;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="1")) int32 DefaultSurfaceRevision=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(TitleProperty="Name")) TArray<FCLSurfaceLayer> Layers;
    bool IsActive() const {return !DefaultSurfaceId.IsNone()||!Layers.IsEmpty();}
    bool Validate(FString& Error) const;
    void RenameGuide(FName Old,FName New);
};
