#pragma once
#include "CoreMinimal.h"
#include "SEditorViewport.h"
#include "ProceduralMeshComponent.h"
#include "UObject/StrongObjectPtr.h"
#include "Model/CLPlanetRecipe.h"
#include "CLRadialGuideEditing.h"
class FCLGlobeClient;
class FPreviewScene;
struct CATALYSTLANDFORMEDITOR_API FCLGlobeBuffers
{
    TArray<FVector> Vertices,Normals,SourceDirections;
    TArray<int32> Triangles;
    TArray<FVector2D> UV;
    TArray<FLinearColor> Colors;
    TArray<FProcMeshTangent> Tangents;
    double DisplayRadius=50000;
    static FCLGlobeBuffers Build(const FCLCompiledPlanetRecipe& Plan,int32 Resolution=64);
};
/** Coalesced worker-built globe. Shared field; separate navigation from the cutaway. */
class CATALYSTLANDFORMEDITOR_API SCLPlanetPreview : public SEditorViewport
{
public:
    SLATE_BEGIN_ARGS(SCLPlanetPreview){} SLATE_END_ARGS()
    void Construct(const FArguments&);
    virtual ~SCLPlanetPreview();
    void Rebuild(TSharedPtr<const FCLCompiledPlanetRecipe> Plan);
    void Cancel();
    void Frame();
    void Dolly(double Steps);
    void SetGuides(const FCLRadialGuideSet& Guides,bool Visible);
    double NormalizedAtScreenY(double Y) const;
    const TArray<FCLGuideIndicator>& Indicators() const;
    const TArray<TArray<FVector2D>>& Rings() const;
    bool IsUpdating() const;
protected:
    virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
    virtual void Tick(const FGeometry&,double,float) override;
    virtual FReply OnMouseButtonDown(const FGeometry&,const FPointerEvent&) override;
    virtual FReply OnMouseMove(const FGeometry&,const FPointerEvent&) override;
    virtual FReply OnMouseButtonUp(const FGeometry&,const FPointerEvent&) override;
    virtual FReply OnMouseWheel(const FGeometry&,const FPointerEvent&) override;
private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class FCLGlobeGuidesTest;
#endif
    void Pump();
    TUniquePtr<FPreviewScene> Scene;
    TSharedPtr<FCLGlobeClient> Client;
    TStrongObjectPtr<UProceduralMeshComponent> Mesh;
    TSharedPtr<const FCLCompiledPlanetRecipe> Pending;
    TFuture<FCLGlobeBuffers> Task;
    uint64 Revision=0,TaskRevision=0;
    bool Framed=false;
    FVector2D Previous;
    FKey Button;
};
