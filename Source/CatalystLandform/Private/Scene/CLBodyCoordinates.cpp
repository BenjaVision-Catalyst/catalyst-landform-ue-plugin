#include "Scene/CLBodyCoordinates.h"
#include "Ellipsoid.h"
#include "GeographicCoordinates.h"
#include "HAL/PlatformTLS.h"
THIRD_PARTY_INCLUDES_START
#include "proj.h"
THIRD_PARTY_INCLUDES_END

bool FCLReferenceFigure::Validate(FString& Error) const
{
    Error.Reset();
    if (BodyId.TrimStartAndEnd().IsEmpty() || FrameId.TrimStartAndEnd().IsEmpty() || DatumId.TrimStartAndEnd().IsEmpty())
    { Error = TEXT("Body, frame and geometric datum IDs are required"); return false; }
    if (!FMath::IsFinite(EquatorialRadiusMeters) || !FMath::IsFinite(PolarRadiusMeters) ||
        PolarRadiusMeters < 1 || EquatorialRadiusMeters > 1.e9 || PolarRadiusMeters > EquatorialRadiusMeters)
    { Error = TEXT("Version 1 requires a sphere or oblate ellipsoid with radii in [1, 1e9] metres"); return false; }
    // Highly flattened figures need a separately qualified inverse/height domain.
    if (PolarRadiusMeters / EquatorialRadiusMeters < .99)
    { Error = TEXT("Version 1 requires polar/equatorial radius >= 0.99"); return false; }
    return true;
}

struct FCLBodyCoordinates::FImpl
{
    const FCLReferenceFigure Definition;
    const uint32 OwnerThread = FPlatformTLS::GetCurrentThreadId();
    FEllipsoid Ellipsoid;
    PJ_CONTEXT* Context = nullptr;
    PJ* Cartesian = nullptr;
    explicit FImpl(const FCLReferenceFigure& F) : Definition(F), Ellipsoid(F.EquatorialRadiusMeters, F.EquatorialRadiusMeters, F.PolarRadiusMeters) {}
    ~FImpl() { proj_destroy(Cartesian); proj_context_destroy(Context); }
    bool IsOwner(FString& Error) const
    {
        Error.Reset();
        if (OwnerThread != FPlatformTLS::GetCurrentThreadId())
        { Error = TEXT("Use an independent coordinate context on each query thread"); return false; }
        return true;
    }
    bool ValidCoordinate(const FCLGeodeticCoordinate& G, FString& Error) const
    {
        if (G.HeightReference != ECLHeightReference::EllipsoidNormalMeters)
        { Error = TEXT("Height is not ellipsoid-normal metres; an explicit datum/provider conversion is required"); return false; }
        if (!FMath::IsFinite(G.LatitudeDegrees) || !FMath::IsFinite(G.LongitudeEastDegrees) || !FMath::IsFinite(G.HeightMeters) ||
            FMath::Abs(G.LatitudeDegrees)>90 || FMath::Abs(G.LongitudeEastDegrees)>180 ||
            FMath::Abs(G.HeightMeters)>Definition.HeightBandMeters())
        { Error = TEXT("Coordinate is nonfinite or outside the declared angular/near-surface domain"); return false; }
        return true;
    }
    bool NativeOK(FString& Error) const
    {
        const int Code = proj_errno(Cartesian);
        if (!Code) return true;
        Error = FString::Printf(TEXT("Native PROJ conversion failed: %s"), UTF8_TO_TCHAR(proj_context_errno_string(Context, Code)));
        return false;
    }
};

FCLBodyCoordinates::FCLBodyCoordinates(TUniquePtr<FImpl> InImpl) : Impl(MoveTemp(InImpl)) {}
FCLBodyCoordinates::~FCLBodyCoordinates() = default;
const FCLReferenceFigure& FCLBodyCoordinates::Figure() const { return Impl->Definition; }
FString FCLBodyCoordinates::NativeVersion() { return UTF8_TO_TCHAR(proj_info().version); }

TUniquePtr<FCLBodyCoordinates> FCLBodyCoordinates::TryCreate(const FCLReferenceFigure& Figure, FString& Error)
{
    if (!Figure.Validate(Error)) return nullptr;
    auto I = MakeUnique<FImpl>(Figure);
    I->Context = proj_context_create();
    if (!I->Context) { Error = TEXT("Native PROJ context allocation failed"); return nullptr; }
    proj_context_set_enable_network(I->Context, false);
    proj_log_level(I->Context, PJ_LOG_NONE);
    const FString Operation = FString::Printf(TEXT("+proj=cart +a=%.17g +b=%.17g"), Figure.EquatorialRadiusMeters, Figure.PolarRadiusMeters);
    I->Cartesian = proj_create(I->Context, TCHAR_TO_UTF8(*Operation));
    if (!I->Cartesian) { Error = TEXT("Native PROJ refused the explicit body figure"); return nullptr; }
    return TUniquePtr<FCLBodyCoordinates>(new FCLBodyCoordinates(MoveTemp(I)));
}

bool FCLBodyCoordinates::ToBody(const FCLGeodeticCoordinate& G, FVector3d& Position, FVector3d& Normal, FString& Error)
{
    if (!Impl->IsOwner(Error) || !Impl->ValidCoordinate(G, Error)) return false;
    proj_errno_reset(Impl->Cartesian);
    const PJ_COORD P = proj_trans(Impl->Cartesian, PJ_FWD, proj_coord(FMath::DegreesToRadians(G.LongitudeEastDegrees), FMath::DegreesToRadians(G.LatitudeDegrees), G.HeightMeters, 0));
    const FVector3d Candidate(P.xyz.x,P.xyz.y,P.xyz.z);
    if (!Impl->NativeOK(Error)) return false;
    if (Candidate.ContainsNaN()) { Error = TEXT("Native PROJ returned a nonfinite body coordinate"); return false; }
    const FVector3d N = Impl->Ellipsoid.GeodeticSurfaceNormal(FGeographicCoordinates(G.LongitudeEastDegrees,G.LatitudeDegrees,G.HeightMeters));
    Position = Candidate; Normal = N; return true;
}

bool FCLBodyCoordinates::FromBody(const FVector3d& Position, FCLGeodeticCoordinate& G, FVector3d& Normal, FString& Error)
{
    if (!Impl->IsOwner(Error)) return false;
    if (Position.ContainsNaN() || !FMath::IsFinite(Position.SizeSquared()) || Position.IsNearlyZero())
    { Error = TEXT("A finite near-surface body coordinate is required; the body centre has no unique geodetic coordinate"); return false; }
    proj_errno_reset(Impl->Cartesian);
    const PJ_COORD P = proj_trans(Impl->Cartesian, PJ_INV, proj_coord(Position.X,Position.Y,Position.Z,0));
    if (!Impl->NativeOK(Error)) return false;
    const FCLGeodeticCoordinate Candidate{FMath::RadiansToDegrees(P.lpz.phi), FMath::RadiansToDegrees(P.lpz.lam), P.lpz.z};
    if (!Impl->ValidCoordinate(Candidate, Error)) return false;
    const FVector3d N = Impl->Ellipsoid.GeodeticSurfaceNormal(FGeographicCoordinates(Candidate.LongitudeEastDegrees,Candidate.LatitudeDegrees,Candidate.HeightMeters));
    G = Candidate; Normal = N; return true;
}
