#pragma once
#include "Components/DynamicMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"

/** Editor-only mesh upload. UE's Dynamic Mesh buffers retain 32-bit UVs, including
 * canonical source coordinates in centimetres. No planet-size-dependent encoding. */
void CLSetPreviewMesh(UDynamicMeshComponent& Component,const TArray<FVector>& Vertices,const TArray<int32>& Triangles,
    const TArray<FVector>& Normals,const TArray<FVector2D>& UV0,const TArray<FVector2D>& UV1,
    const TArray<FVector2D>& UV2,const TArray<FVector2D>& UV3,const TArray<FLinearColor>& Colors,const TArray<FProcMeshTangent>& Tangents);
inline void CLSetPreviewMesh(UDynamicMeshComponent& Component,const TArray<FVector>& Vertices,const TArray<int32>& Triangles,
    const TArray<FVector>& Normals,const TArray<FVector2D>& UV0,const TArray<FLinearColor>& Colors,const TArray<FProcMeshTangent>& Tangents)
{CLSetPreviewMesh(Component,Vertices,Triangles,Normals,UV0,{},{},{},Colors,Tangents);}

inline FBox CLPreviewMeshBounds(const UDynamicMeshComponent& Component)
{const auto B=Component.GetMesh()->GetBounds();return FBox(B.Min,B.Max);}
