#pragma once
#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
class UMaterialInterface;
class UVoxelMegaMaterial;
class UWorld;
/** Transient presentation copies. Source materials, scans and native instances are never edited. */
class FCLInspectionMaterials
{
public:
    bool Prepare(UWorld& World, UVoxelMegaMaterial& Source, FString& Error);
    bool Apply(UWorld& PlayWorld, FString& Error);
    UVoxelMegaMaterial* GetMegaMaterial() const {return Mega.Get();}
private:
    TStrongObjectPtr<UVoxelMegaMaterial> Mega;
    TMap<UMaterialInterface*,TStrongObjectPtr<UMaterialInterface>> Materials;
    TArray<TStrongObjectPtr<UObject>> KeepAlive;
    UMaterialInterface* ProjectMaterial(UMaterialInterface* Source,FString& Error);
};
