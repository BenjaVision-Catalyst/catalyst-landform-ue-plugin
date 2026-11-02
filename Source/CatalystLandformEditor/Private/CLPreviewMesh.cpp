#include "CLPreviewMesh.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"

void CLSetPreviewMesh(UDynamicMeshComponent& Component,const TArray<FVector>& Vertices,const TArray<int32>& Triangles,
    const TArray<FVector>& Normals,const TArray<FVector2D>& UV0,const TArray<FVector2D>& UV1,
    const TArray<FVector2D>& UV2,const TArray<FVector2D>& UV3,const TArray<FLinearColor>& Colors,const TArray<FProcMeshTangent>& Tangents)
{
    using namespace UE::Geometry;
    FDynamicMesh3 Mesh;Mesh.EnableAttributes();auto* A=Mesh.Attributes();
    A->SetNumUVLayers(4);A->EnablePrimaryColors();A->EnableTangents();
    const TArray<FVector2D>* UVs[]{&UV0,&UV1,&UV2,&UV3};
    for(int32 I=0;I<Vertices.Num();++I)
    {
        Mesh.AppendVertex(Vertices[I]);
        const FVector N=Normals.IsValidIndex(I)?Normals[I]:FVector::ZAxisVector;
        const FVector T=Tangents.IsValidIndex(I)?Tangents[I].TangentX:FVector::XAxisVector;
        const FVector B=FVector::CrossProduct(N,T)*(Tangents.IsValidIndex(I)&&Tangents[I].bFlipTangentY?-1:1);
        A->PrimaryNormals()->AppendElement(FVector3f(N));A->PrimaryTangents()->AppendElement(FVector3f(T));A->PrimaryBiTangents()->AppendElement(FVector3f(B));
        const FLinearColor C=Colors.IsValidIndex(I)?Colors[I]:FLinearColor::White;
        A->PrimaryColors()->AppendElement(FVector4f(C.R,C.G,C.B,C.A));
        for(int32 L=0;L<4;++L)A->GetUVLayer(L)->AppendElement(UVs[L]->IsValidIndex(I)?FVector2f((*UVs[L])[I]):FVector2f::ZeroVector);
    }
    for(int32 I=0;I<Triangles.Num();I+=3)
    {
        const FIndex3i Indices(Triangles[I],Triangles[I+1],Triangles[I+2]);
        const int32 Triangle=Mesh.AppendTriangle(Indices);check(Triangle>=0);
        A->PrimaryNormals()->SetTriangle(Triangle,Indices);A->PrimaryTangents()->SetTriangle(Triangle,Indices);A->PrimaryBiTangents()->SetTriangle(Triangle,Indices);
        A->PrimaryColors()->SetTriangle(Triangle,Indices);
        for(int32 L=0;L<4;++L)A->GetUVLayer(L)->SetTriangle(Triangle,Indices);
    }
    Component.SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Component.SetVertexColorSpaceTransformMode(EDynamicMeshVertexColorTransformMode::NoTransform);
    Component.SetTangentsType(EDynamicMeshComponentTangentsMode::ExternallyProvided);
    Component.SetMesh(MoveTemp(Mesh));
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "CLSectionGeometry.h"
#include "Model/CLPlanetRecipe.h"
#include "UObject/StrongObjectPtr.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLSectionPrecisionTest,"Catalyst.Landform.Surfaces.LargeRadiusSectionPrecision",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLSectionPrecisionTest::RunTest(const FString&)
{
    FString Error;
    for(double Radius:{1000.,10000.,6371000.})
    {
        auto R=FCLPlanetCompiler::BasePlanet();R.SchemaVersion=17;R.RadiusMeters=Radius;R.BaseSurfaceNormalized=.75;R.SurfaceStack=FCLSurfaceStack::MakeDefault();
        const auto P=FCLPlanetCompiler::Compile(R,Error);if(!P){AddError(Error);return false;}
        const auto Section=FCLSectionGeometry::Build(*P,0,500);
        TStrongObjectPtr<UDynamicMeshComponent> Mesh(NewObject<UDynamicMeshComponent>());
        TArray<FVector> Normals;Normals.Init(FVector::ZAxisVector,Section.Vertices.Num());TArray<FProcMeshTangent> Tangents;Tangents.Init(FProcMeshTangent(),Section.Vertices.Num());
        CLSetPreviewMesh(*Mesh.Get(),Section.Vertices,Section.Triangles,Normals,Section.SurfaceUV,Section.SurfaceUV1,Section.SurfaceUV2,Section.SurfaceUV3,Section.Colors,Tangents);
        const auto* A=Mesh->GetMesh()->Attributes();double Worst=0;
        for(int32 I=0;I<Section.SurfaceVertexCount;++I)
        {
            const auto XY=A->GetUVLayer(0)->GetElement(I);const auto Z=A->GetUVLayer(1)->GetElement(I);
            const FVector Actual(XY.X,XY.Y,Z.X);TestFalse(TEXT("Designer source coordinates remain finite above 655 m"),Actual.ContainsNaN());
            Worst=FMath::Max(Worst,FVector::Distance(Actual,Section.SourceSurfacePoints[I]));
            TestEqual(TEXT("Designer surface mask survives upload"),Z.Y,1.f);
        }
        for(int32 I=Section.SurfaceVertexCount;I<Section.Vertices.Num();++I)TestEqual(TEXT("Cut walls retain their plain display mask"),A->GetUVLayer(1)->GetElement(I).Y,0.f);
        TestTrue(TEXT("Cross-section retains float32 source precision"),Worst<Radius*100*1.e-7);
        TestEqual(TEXT("Full-precision mesh retains every preview vertex"),Mesh->GetMesh()->VertexCount(),Section.Vertices.Num());
        TestEqual(TEXT("Editor mesh has no collision"),Mesh->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
        AddInfo(FString::Printf(TEXT("Designer radius %.0f m: %d surface vertices; maximum source rounding %.6g cm"),Radius,Section.SurfaceVertexCount,Worst));
    }
    return true;
}
#endif
