#pragma once
#include "CoreMinimal.h"
#include "CLWatershed.generated.h"
struct FCLCompiledPlanetRecipe;

/** Static landscape development; reference runoff is separate from terrain age. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLWatershedSettings
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 AlgorithmVersion=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bEnabled=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double Character=.5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="4",ClampMax="7")) int32 AnalysisSubdivisions=7;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="1026",ClampMax="65538")) int32 MaximumCells=65538;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0.1",ClampMax="100",ForceUnits="m")) double IncisionMeters=4.875;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="1",ClampMax="1000",ForceUnits="m",DisplayName="Largest valley influence")) double ValleyWidthMeters=67.5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="100",ClampMax="10000000")) double ChannelAreaSquareMeters=6750;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="0.01")) double MinimumChannelSlope=.00001;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="10")) double ReferenceRainMetersPerYear=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double RunoffFraction=.65;
    /** Extra internal terminals; directions are snapped to this finite analysis mesh. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TArray<FVector> ClosedBasinDirections;
    void ApplyProgression(double Position);
    bool Validate(FString& Error) const;
};
struct CATALYSTLANDFORM_API FCLWatershedCell
{
    FVector Direction=FVector::ZeroVector;
    TArray<int32> Neighbours;
    int32 Receiver=INDEX_NONE,Rank=INDEX_NONE,Outlet=INDEX_NONE;
    double HeightMeters=0,SpillMeters=0,BedMeters=0,AreaSquareMeters=0,AccumulatedAreaSquareMeters=0,DischargeCubicMetersPerSecond=0,Influence=0;
    bool bOcean=false,bTerminal=false,bLake=false,bShore=false;
};
struct CATALYSTLANDFORM_API FCLWatershedChannel {int32 From=0,To=0;};
/** Immutable domain analysis. UE owns the spatial tree; VPP owns all visible terrain. */
class CATALYSTLANDFORM_API FCLCompiledWatershed final
{
public:
    static TSharedPtr<const FCLCompiledWatershed> Compile(const FCLWatershedSettings&,const FCLCompiledPlanetRecipe&,FString& Error);
    double MapHeight(const FVector& Direction,double NativeHeight) const;
    /** Coherent native query packets reuse exact watertight triangle tests;
     * strict interior hits only, with the original tree deciding all seams. */
    void MapHeights(TConstArrayView<FVector> Directions,TArray<double>& Heights,int32* TreeQueries=nullptr)const;
    /** Reuse the retained native analysis tree to locate a drainage cell. */
    int32 FindCell(const FVector& Direction) const;
    const TArray<FCLWatershedCell>& GetCells() const {return Cells;}
    const TArray<FCLWatershedChannel>& GetChannels() const {return Channels;}
    const TArray<FCLWatershedChannel>& GetDrainageLinks() const {return DrainageLinks;}
    /** Retained native analysis topology; read-only access for derived diagnostics. */
    const TArray<FIntVector>& GetTriangles() const {return Triangles;}
    int32 ConditionedReaches=0;
    double ReferenceRadiusMeters=0,TotalAreaSquareMeters=0,InputDischarge=0,TerminalDischarge=0,OceanExport=0,ClosedInput=0,MaximumEdgeMeters=0,MaximumLakeDepthMeters=0;
    int32 OceanCells=0,LakeCells=0,Catchments=0,ClosedTerminals=0;
    /** Measured conditioning cut/fill along emitted arcs; not conserved sediment. */
    double MaximumMeasuredCutMeters=0,MaximumMeasuredFillMeters=0;
private:
    double TriangleHeight(int32 Id,const FVector& Barycentric,double Height)const;
    bool Route(double SeaHeight,const TSet<int32>& Closed,FString& Error);
    TArray<int32> FloodOrder;
    struct FSpatial;
    TSharedPtr<FSpatial> Spatial;
    FCLWatershedSettings Settings;
    TArray<FCLWatershedCell> Cells,ShapeCells;
    TArray<FCLWatershedChannel> Channels,DrainageLinks;
    TArray<FIntVector> Triangles;
};
