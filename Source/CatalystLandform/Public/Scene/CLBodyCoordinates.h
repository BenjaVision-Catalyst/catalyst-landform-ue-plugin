#pragma once
#include "CoreMinimal.h"

/** Height semantics are mandatory. This conversion only admits ellipsoid-normal metres. */
enum class ECLHeightReference : uint8 { EllipsoidNormalMeters, AreoidMeters, RadialMeters, NormalizedGuide };

/** Version-1 geometric datum: no epoch, plate motion, geoid or areoid correction.
 * Body-fixed Cartesian metres: X at longitude 0, Y at 90 east, Z north.
 * This is a mathematical right-handed CRS; UE display handedness is a separate boundary.
 * IDs are supplied by the caller, never inferred from the figure's radii. */
struct CATALYSTLANDFORM_API FCLReferenceFigure
{
    FString BodyId, FrameId, DatumId;
    double EquatorialRadiusMeters = 0;
    double PolarRadiusMeters = 0;
    bool Validate(FString& Error) const;
    double HeightBandMeters() const { return FMath::Min(10000., (PolarRadiusMeters == EquatorialRadiusMeters ? .1 : .02) * PolarRadiusMeters); }
};

struct CATALYSTLANDFORM_API FCLGeodeticCoordinate
{
    double LatitudeDegrees = 0, LongitudeEastDegrees = 0, HeightMeters = 0;
    ECLHeightReference HeightReference = ECLHeightReference::EllipsoidNormalMeters;
};

/** Native PROJ cart conversion, with UE's ellipsoid-normal implementation.
 * One independent context per owner/thread; create and query on that thread.
 * No world/UObject, global lookup, network, grid I/O or retained query cache.
 * This does not reinterpret or change existing spherical recipes or radial guides.
 * Near-surface contract: |height| <= min(10 km, 2% polar radius) for oblate figures; spheres use 10%.
 * Failed calls leave output values unchanged; pole longitude is not unique. */
class CATALYSTLANDFORM_API FCLBodyCoordinates final
{
public:
    static TUniquePtr<FCLBodyCoordinates> TryCreate(const FCLReferenceFigure& Figure, FString& Error);
    ~FCLBodyCoordinates();
    FCLBodyCoordinates(const FCLBodyCoordinates&) = delete;
    FCLBodyCoordinates& operator=(const FCLBodyCoordinates&) = delete;
    const FCLReferenceFigure& Figure() const;
    bool ToBody(const FCLGeodeticCoordinate& Coordinate, FVector3d& PositionMeters, FVector3d& EllipsoidNormal, FString& Error);
    bool FromBody(const FVector3d& PositionMeters, FCLGeodeticCoordinate& Coordinate, FVector3d& EllipsoidNormal, FString& Error);
    static FString NativeVersion();
private:
    struct FImpl;
    explicit FCLBodyCoordinates(TUniquePtr<FImpl> InImpl);
    TUniquePtr<FImpl> Impl;
};
