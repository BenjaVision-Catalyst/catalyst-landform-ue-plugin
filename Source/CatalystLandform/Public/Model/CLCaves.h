#pragma once
#include "CoreMinimal.h"
#include "Scene/CLVolumes.h"
#include "CLCaves.generated.h"
struct FCLCompiledPlanetRecipe;

/** Geology-conditioned underground spaces; native VPP owns their solid/air field. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLCaveSettings
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 AlgorithmVersion=7;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bEnabled=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double Character=.5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 Seed=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="10")) double NetworksPerSquareKilometer=1.5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(Units="m",ClampMin="1",ClampMax="500")) double LengthMeters=87.5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(Units="m",ClampMin="0.1",ClampMax="10")) double RadiusMeters=2.15;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(Units="m",ClampMin="1",ClampMax="100")) double MinimumCoverMeters=5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(Units="m",ClampMin="1",ClampMax="500")) double DepthMeters=26;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) double Branching=.575;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bSurfaceEntrances=true;
    /** Upgrade only the former automatic density; preserve authored dimensions and custom density. */
    void UpgradeAbundanceProfile();
    void ApplyProgression(double Value);
    bool Validate(FString& Error)const;
};

/** Placement data and native cutters share one immutable result. No preview-only cave geometry. */
class FCLNativeNoiseKernel;
class FCLCaveVolumeData;
class FCLSpatialCave;
class FCLCompiledRock;

/** Geological section families, lowered through native VPP shape kernels. */
enum class ECLCaveForm:uint8 {LegacyOval,JointCleft,BeddingSlot,Phreatic,Keyhole,Canyon,FractureHall,KarstHall,RoofScar,KarstPassage,KarstChamber,BeddedPassage,BeddedChamber};

/** A passage's correlated sections and curve are geological construction data, not a mesh. */
struct CATALYSTLANDFORM_API FCLCavePassage
{
    FGuid Network;
    int32 From=INDEX_NONE,To=INDEX_NONE;
    TArray<FVector> Points;
    /** Half width / half height at each curve control point, in metres. */
    TArray<FVector2D> Sections;
    /** Lower narrow vadose slot relative to the upper half height; zero for round/oval conduits. */
    double Incision=0;
    bool Entrance=false;
    ECLCaveForm Form=ECLCaveForm::LegacyOval,EndForm=ECLCaveForm::LegacyOval;
    TArray<FVector> BeddingUps;
    double Development=0;
    /** Profile 6: restored bedding floor at each control, independent of roof size. */
    int32 GeometryProfile=0;
    double FloorReliefScale=0;
    TArray<double> FloorRadii;
    double FloorOffsetMeters=0;
    TSharedPtr<const FCLCompiledRock> FloorRock;
};
struct CATALYSTLANDFORM_API FCLCaveChamber
{
    FGuid Network;
    int32 Node=INDEX_NONE;
    FVector Center,HalfExtents;
    FQuat Rotation;
    ECLCaveForm Form=ECLCaveForm::LegacyOval;
    int32 GeometryProfile=0;
    double FloorReliefScale=0;
    double FloorRadius=0;
    TSharedPtr<const FCLCompiledRock> FloorRock;
};
/** Native solid cave features; transported blocks retain their roof-rock source. */
struct CATALYSTLANDFORM_API FCLCaveInfill
{
    FGuid Network;
    FVector Center,SourceCenter,HalfExtents;
    FQuat Rotation,SourceRotation;
    /** In-place bedrock remnant; its provenance transform is identity. */
    bool BedrockPier=false;
    bool BeddedRemnant=false;
    FVector WallOutward=FVector::ZeroVector;
    bool BeddedBreakdown=false;
};

/** Native VPP spline sampling, shape kernels, smooth blends and spatial index. */
class CATALYSTLANDFORM_API FCLCompiledCaveVolume
{
public:
    static TSharedPtr<const FCLCompiledCaveVolume> Compile(TConstArrayView<FCLCavePassage>,TConstArrayView<FCLCaveChamber>,TSharedPtr<const FCLNativeNoiseKernel>,int32 Seed,FString& Error,TConstArrayView<FCLCaveInfill> Infills={});
    static TSharedPtr<const FCLCompiledCaveVolume> CompileSpatial(TSharedPtr<const FCLSpatialCave>,FGuid Network);
    double Compose(const FVector3d& P,double Field,FCLSurfaceWeights* Weights=nullptr)const;
    void ComposeBatch(TConstArrayView<FVector3d>,TArrayView<double>)const;
    /** Opt-in delivery prototype: identical ordered field with query-local floor reuse. */
    void ComposeBatchOptimized(TConstArrayView<FVector3d>,TArrayView<double>)const;
    TSharedPtr<const FCLCompiledCaveVolume> Select(FGuid Network)const;
    TSharedPtr<const FCLCompiledCaveVolume> Select(TConstArrayView<FGuid> Networks)const;
    FBox Bounds(FGuid Network=FGuid())const;
    double MinimumHalfWidth(FGuid Network=FGuid())const;
    FVector GeologicalPosition(const FVector& Position)const;
private:
    void ComposeBatchInternal(TConstArrayView<FVector3d>,TArrayView<double>,bool IncludeInfills,bool Optimized=false)const;
    TSharedPtr<const FCLCaveVolumeData> Data;
    TSharedPtr<const FCLSpatialCave> Spatial;
    FGuid SpatialNetwork;
    TSet<FGuid> SelectedNetworks;
};

class CATALYSTLANDFORM_API FCLCompiledCaves
{
public:
    /** Conservative control-polygon envelopes for diagnostics; native splines define the volume. */
    struct FSegment {FGuid Network;FVector A,B;double Radius=0;FVector Scale=FVector::OneVector;bool Entrance=false;};
    TArray<FSegment> Segments;
    struct FNode {FGuid Network;FVector Position;FVector2D Section;bool Chamber=false,Portal=false;};
    TArray<FNode> Nodes;
    TArray<FCLCavePassage> Passages;
    TArray<FCLCaveChamber> Chambers;
    TArray<FCLCaveInfill> Infills;
    TSharedPtr<const FCLCompiledCaveVolume> Cutters;
    TSharedPtr<const FCLSpatialCave> Spatial;
    enum class EFamily:uint8 {Structural,CarbonateKarst,General};
    struct FNetwork {FGuid Id;EFamily Family=EFamily::Structural;int32 RechargeCell=INDEX_NONE,ReceivingCell=INDEX_NONE;double InceptionRadiusMeters=0;FBox InspectionBounds=FBox(ForceInit);FGuid Landform;FVector OriginDirection=FVector::ZeroVector;};
    TArray<FNetwork> NetworkInfo;
    /** Static query/render support groups, not a claim of physical connectivity. */
    struct FInspectionDomain {FGuid Id;TArray<FGuid> Networks;FBox Bounds=FBox(ForceInit);};
    TArray<FInspectionDomain> InspectionDomains;
    void ResolveInspectionDomains();
    int32 Candidates=0,EligibleCandidates=0,RequestedNetworks=0,Networks=0,Entrances=0;
    FString Readiness;
    static TSharedPtr<const FCLCompiledCaves> Compile(const FCLCaveSettings&,const FCLCompiledPlanetRecipe&,int32 PlanetSeed,FString& Error);
private:
    static TSharedPtr<const FCLCompiledCaves> CompilePreferred(const FCLCaveSettings&,const FCLCompiledPlanetRecipe&,int32,FString&);
    static TSharedPtr<const FCLCompiledCaves> CompileMinimum(const FCLCaveSettings&,const FCLCompiledPlanetRecipe&,int32,FString&);
};
