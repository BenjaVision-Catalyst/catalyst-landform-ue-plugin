#include "Presentation/CLProjectionChunk.h"

namespace
{
double Sinc(double X)
{
    if (FMath::Abs(X) < 1.e-4) return 1 - X * X / 6 + X * X * X * X / 120;
    return FMath::Sin(X) / X;
}
}
bool FCLProjectionChunk::TryCreate(const FCLPlanetProjection& Projection, const FBox& Box, FCLProjectionChunk& Out)
{
    if (!Box.IsValid || Box.Min.ContainsNaN() || Box.Max.ContainsNaN() ||
        Box.Min.X > Box.Max.X || Box.Min.Y > Box.Max.Y || Box.Min.Z > Box.Max.Z) return false;
    const auto& S = Projection.GetSettings();
    FCLProjectionChunk Fit;
    Fit.SourcePivot = Box.GetCenter();
    const FVector V = Fit.SourcePivot - S.Center;
    const double Rho = V.Length(), B = Box.GetExtent().Length();
    if (!FMath::IsFinite(Rho) || !FMath::IsFinite(B) || Rho <= 0 ||
        !Projection.TryProjectPosition(Fit.SourcePivot, Fit.PresentedPivot)) return false;
    const FVector U = V / Rho;
    const double RhoMin = (Box.GetClosestPointTo(S.Center) - S.Center).Length();
    if (RhoMin <= 0) return false;
    double RhoMax = 0, ThetaMax = 0;
    for (int32 I = 0; I < 8; ++I)
    {
        const FVector Corner(I & 1 ? Box.Max.X : Box.Min.X, I & 2 ? Box.Max.Y : Box.Min.Y, I & 4 ? Box.Max.Z : Box.Min.Z);
        const FVector Delta = Corner - S.Center;
        const double Radius = Delta.Length();
        RhoMax = FMath::Max(RhoMax, Radius);
        ThetaMax = FMath::Max(ThetaMax, FMath::Atan2(FVector::CrossProduct(Delta, S.AnchorDirection).Length(),
            FVector::DotProduct(Delta, S.AnchorDirection)));
    }
    const double HMin = RhoMin - S.ReferenceRadiusCm, HMax = RhoMax - S.ReferenceRadiusCm;
    // Radial extrema are exact box bounds. A cone strictly inside a hemisphere is
    // convex, so enclosing all box corners encloses every interior point/segment.
    if (ThetaMax > S.MaxArcRadians || HMin < S.MinAltitudeCm || HMax > S.MaxAltitudeCm) return false;
    FVector PresentedUp;
    if (!Projection.TryProjectVector(Fit.SourcePivot, U, PresentedUp)) return false;
    Fit.Rotation = S.Curvature == 1 ? FQuat::Identity : FQuat::FindBetweenNormals(U, PresentedUp.GetSafeNormal());
    if (S.Curvature == 1) Fit.PresentedPivot = Fit.SourcePivot;
    const double MinMeridian = (S.ReferenceRadiusCm + S.Curvature * HMax) / (S.ReferenceRadiusCm + HMax);
    const double MaxMeridian = (S.ReferenceRadiusCm + S.Curvature * HMin) / (S.ReferenceRadiusCm + HMin);
    const double LogStretch = 1 / Sinc(ThetaMax);
    const double MaxAzimuth = MaxMeridian * Sinc(S.Curvature * ThetaMax) * LogStretch;
    Fit.MaxStretch = FMath::Max(1., MaxAzimuth);
    const double StretchDeviation = FMath::Max(FMath::Abs(MinMeridian - 1), FMath::Abs(MaxAzimuth - 1));
    // J = Q S. ||S-I|| <= StretchDeviation; Q's rotation-vector derivative is bounded
    // by (1-c) * theta/sin(theta) / rhoMin. Integrate J-Q(pivot) along a segment.
    const double RotationDerivative = (1 - S.Curvature) * LogStretch / RhoMin;
    Fit.MaxResidualCm = S.Curvature == 1 ? 0 : StretchDeviation * B + .5 * RotationDerivative * B * B;
    if (!FMath::IsFinite(Fit.MaxResidualCm) || !FMath::IsFinite(Fit.MaxStretch)) return false;
    for (int32 I = 0; I < 8; ++I)
    {
        const FVector Corner(I & 1 ? Box.Max.X : Box.Min.X, I & 2 ? Box.Max.Y : Box.Min.Y, I & 4 ? Box.Max.Z : Box.Min.Z);
        Fit.PresentedBounds += Fit.ProjectRigidPosition(Corner);
    }
    // Include double arithmetic slack; the renderer must add its FP32 quantization allowance.
    Fit.PresentedBounds = Fit.PresentedBounds.ExpandBy(Fit.MaxResidualCm + S.ReferenceRadiusCm * 1.e-10);
    if (Fit.PresentedBounds.Min.ContainsNaN() || Fit.PresentedBounds.Max.ContainsNaN()) return false;
    Out = Fit;
    return true;
}
FVector FCLProjectionChunk::ProjectRigidPosition(const FVector& Canonical) const
{
    return PresentedPivot + Rotation.RotateVector(Canonical - SourcePivot);
}
FTransform FCLProjectionChunk::ProjectRigidTransform(const FTransform& Canonical) const
{
    return FTransform(Rotation * Canonical.GetRotation(), ProjectRigidPosition(Canonical.GetLocation()), Canonical.GetScale3D());
}
