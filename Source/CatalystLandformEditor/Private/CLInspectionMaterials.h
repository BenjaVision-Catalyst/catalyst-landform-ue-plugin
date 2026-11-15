#pragma once
#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
class UMaterialInterface;
class UVoxelMegaMaterial;
class UWorld;
class UPrimitiveComponent;
class UTexture;
class ACLPlanet;
class UMaterialInstanceDynamic;
/** Transient presentation copies. Source materials, scans and native instances are never edited. */
class FCLInspectionMaterials
{
public:
    bool Prepare(UWorld& World, UVoxelMegaMaterial& Source, FString& Error,TConstArrayView<UMaterialInterface*> Sources={});
    bool Apply(UWorld& PlayWorld, FString& Error);
    bool ApplyPrimitive(UPrimitiveComponent&,FString& Error);
    void ApplyTextures(ACLPlanet& Planet);
    UVoxelMegaMaterial* GetMegaMaterial() const {return Mega.Get();}
private:
    TStrongObjectPtr<UVoxelMegaMaterial> Mega;
    TMap<UMaterialInterface*,TStrongObjectPtr<UMaterialInterface>> Materials;
    TArray<TStrongObjectPtr<UObject>> KeepAlive;
    TMap<TWeakObjectPtr<UMaterialInstanceDynamic>,TMap<FName,TWeakObjectPtr<UTexture>>> AppliedTextures;
    UMaterialInterface* ProjectMaterial(UMaterialInterface* Source,FString& Error);
};
