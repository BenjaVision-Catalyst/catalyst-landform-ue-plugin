#pragma once
#include "CoreMinimal.h"
#include "Model/CLPlanetRecipe.h"
class SWidget;
struct FCLPlanetBuilderOverlaySegment { FVector A,B; FLinearColor Color=FLinearColor::White; float WidthPixels=1.25f; };
struct FCLPlanetBuilderOverlayLabel { FVector BodyMeters; FString Text; FLinearColor Color=FLinearColor::White; };
/** One closed spherical path. Signed presentation speed never advances a domain clock. */
struct CATALYSTLANDFORMEDITOR_API FCLPlanetBuilderOverlayFlow
{
    uint64 Key=0;
    /** Identity of the scalar contour level, independent of loop enumeration. */
    uint64 Family=0;
    TArray<FVector> BodyMeters;
    TArray<double> ArcMeters;
    double SpeedMetersPerSecond=0;
    int32 ArrowCount=0;
    bool BuildArcLengths();
    double LengthMeters()const{return ArcMeters.IsEmpty()?0:ArcMeters.Last();}
    double AdvancePhase(double Phase,double Seconds)const;
    bool Sample(double DistanceMeters,FVector& Point,FVector& Tangent)const;
};
/** View-owned moving sites. Count changes never redistribute existing sites. */
struct CATALYSTLANDFORMEDITOR_API FCLPlanetBuilderFlowAnimation
{
    struct FHead{uint64 Id=0;double DistanceMeters=0;float Opacity=0;bool Retiring=false;};
    FCLPlanetBuilderOverlayFlow Flow;
    TArray<FHead> Heads;
    uint64 NextId=1;
    void SetFlow(const FCLPlanetBuilderOverlayFlow& Next);
    void Tick(double Seconds);
};
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
    /** Optional sun direction in canonical body coordinates. No value restores the studio rig. */
    TOptional<FVector> SunDirection;
    /** Otherwise legacy heatmap opacity remains 0.18. */
    bool bUseMeshOpacity=false;
    /** Optional contiguous triangle range shaded by standard scene lights. Other triangles remain unlit. */
    int32 LitTriangleStart=0,LitTriangleCount=0;
    /** Optional metric vectors at matching sample indices. At most 512 arrows are drawn. */
    TArray<FVector> BodyVectorsMeters;
    float PointSizePixels = 7;
    /** Outward mathematical triangle winding; at most 16384 vertices / 98304 indices. */
    TArray<FVector> MeshBodyMeters;
    TArray<FLinearColor> MeshColors;
    TArray<int32> MeshIndices;
    TArray<FCLPlanetBuilderOverlaySegment> Segments;
    TArray<FCLPlanetBuilderOverlayLabel> Labels;
    /** At most 256 closed paths / 16384 edges and 2048 arrowheads in total. */
    TArray<FCLPlanetBuilderOverlayFlow> Flows;
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
