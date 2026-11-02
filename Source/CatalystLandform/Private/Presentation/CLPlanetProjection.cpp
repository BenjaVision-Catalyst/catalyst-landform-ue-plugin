#include "Presentation/CLPlanetProjection.h"

namespace
{
bool Finite(const FVector& V)
{
    return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z);
}

// Avoid division by curvature and cancellation near the flat endpoint.
double Sinc(double X)
{
    if (FMath::Abs(X) < 1.e-4)
    {
        const double X2 = X * X;
        return 1 - X2 / 6 + X2 * X2 / 120;
    }
    return FMath::Sin(X) / X;
}
}

bool FCLPlanetProjection::TryCreate(const FCLPlanetProjectionSettings& In, FCLPlanetProjection& Out)
{
    if (!Finite(In.Center) || !Finite(In.AnchorDirection) ||
        !FMath::IsFinite(In.ReferenceRadiusCm) || In.ReferenceRadiusCm <= 0 ||
        !FMath::IsFinite(In.Curvature) || In.Curvature < 0 || In.Curvature > 1 ||
        !FMath::IsFinite(In.MaxArcRadians) || In.MaxArcRadians <= 0 || In.MaxArcRadians >= UE_HALF_PI ||
        !FMath::IsFinite(In.MinAltitudeCm) || !FMath::IsFinite(In.MaxAltitudeCm) ||
        In.MinAltitudeCm > In.MaxAltitudeCm || In.MinAltitudeCm <= -In.ReferenceRadiusCm)
        return false;
    const double Length = In.AnchorDirection.Length();
    if (!FMath::IsFinite(Length) || Length < 1.e-12) return false;
    FCLPlanetProjection Candidate;
    Candidate.Settings = In;
    Candidate.Settings.AnchorDirection /= Length;
    Candidate.bValid = true;
    FBox Bounds;
    if (!Candidate.TryGetChartBounds(Bounds)) return false;
    Out = Candidate;
    return true;
}

bool FCLPlanetProjection::Evaluate(const FVector& Source, FDifferential& Out) const
{
    if (!bValid || !Finite(Source)) return false;
    const auto& S = Settings;
    const FVector Offset = Source - S.Center;
    const double Radius = Offset.Length();
    if (!FMath::IsFinite(Radius) || Radius <= 0) return false;
    const FVector U = Offset / Radius;
    const double CosTheta = FMath::Clamp(FVector::DotProduct(U, S.AnchorDirection), -1., 1.);
    const FVector Cross = FVector::CrossProduct(S.AnchorDirection, U);
    const double SinTheta = Cross.Length();
    const double Theta = FMath::Atan2(SinTheta, CosTheta);
    const double H = Radius - S.ReferenceRadiusCm;
    const double Tolerance = S.ReferenceRadiusCm * 1.e-12;
    if (Theta > S.MaxArcRadians + 1.e-12 || H < S.MinAltitudeCm - Tolerance || H > S.MaxAltitudeCm + Tolerance)
        return false;

    FVector D;
    if (SinTheta > 1.e-12) D = FVector::CrossProduct(Cross, S.AnchorDirection) / SinTheta;
    else
    {
        FVector Other;
        S.AnchorDirection.FindBestAxisVectors(D, Other);
    }
    const double A = S.Curvature * Theta;
    const double SinA = FMath::Sin(A), CosA = FMath::Cos(A);
    const double Arc = S.ReferenceRadiusCm * Theta;
    const double TangentDistance = (S.ReferenceRadiusCm + S.Curvature * H) * Theta * Sinc(A);
    const double Z = H * CosA - .5 * S.Curvature * Arc * Theta * FMath::Square(Sinc(A / 2));
    Out.Position = S.Center + S.ReferenceRadiusCm * S.AnchorDirection + TangentDistance * D + Z * S.AnchorDirection;
    Out.SourceRadial = U;
    Out.SourceMeridian = -SinTheta * S.AnchorDirection + CosTheta * D;
    Out.Azimuth = FVector::CrossProduct(S.AnchorDirection, D);
    Out.PresentedRadial = CosA * S.AnchorDirection + SinA * D;
    Out.PresentedMeridian = -SinA * S.AnchorDirection + CosA * D;
    Out.MeridianScale = (S.ReferenceRadiusCm + S.Curvature * H) / Radius;
    Out.AzimuthScale = Out.MeridianScale * Sinc(A) / Sinc(Theta);
    return Finite(Out.Position) && FMath::IsFinite(Out.MeridianScale) && Out.MeridianScale > 0 &&
        FMath::IsFinite(Out.AzimuthScale) && Out.AzimuthScale > 0;
}

bool FCLPlanetProjection::TryProjectPosition(const FVector& Source, FVector& Out) const
{
    FDifferential D;
    if (!Evaluate(Source, D)) return false;
    Out = D.Position;
    return true;
}

bool FCLPlanetProjection::TryUnprojectPosition(const FVector& Presented, FVector& Out) const
{
    if (!bValid || !Finite(Presented)) return false;
    const auto& S = Settings;
    const FVector V = Presented - (S.Center + S.ReferenceRadiusCm * S.AnchorDirection);
    const double Z = FVector::DotProduct(V, S.AnchorDirection);
    const FVector Tangent = V - Z * S.AnchorDirection;
    const double X = Tangent.Length();
    const double K = S.Curvature / S.ReferenceRadiusCm;
    const double KX = K * X, OnePlusKZ = 1 + K * Z;
    // A chart lies inside a hemisphere, with strictly positive radial scale.
    if (OnePlusKZ <= 0 || !FMath::IsFinite(X)) return false;
    const double Arc = K > 0 ? FMath::Atan2(KX, OnePlusKZ) / K : X;
    const double H = K > 0 ? (2 * Z + K * (X * X + Z * Z)) /
        (FMath::Sqrt(KX * KX + OnePlusKZ * OnePlusKZ) + 1) : Z;
    const double Theta = Arc / S.ReferenceRadiusCm;
    const FVector Direction = X > 0 ? Tangent / X : FVector::ZeroVector;
    const FVector Source = S.Center + (S.ReferenceRadiusCm + H) *
        (FMath::Cos(Theta) * S.AnchorDirection + FMath::Sin(Theta) * Direction);
    FDifferential Check;
    if (!Evaluate(Source, Check) || !Check.Position.Equals(Presented, S.ReferenceRadiusCm * 1.e-9)) return false;
    Out = Source;
    return true;
}

bool FCLPlanetProjection::TryProjectVector(const FVector& Source, const FVector& Vector, FVector& Out) const
{
    FDifferential D;
    if (!Finite(Vector) || !Evaluate(Source, D)) return false;
    const FVector Result = FVector::DotProduct(Vector, D.SourceRadial) * D.PresentedRadial +
        FVector::DotProduct(Vector, D.SourceMeridian) * D.MeridianScale * D.PresentedMeridian +
        FVector::DotProduct(Vector, D.Azimuth) * D.AzimuthScale * D.Azimuth;
    if (!Finite(Result)) return false;
    Out = Result;
    return true;
}

bool FCLPlanetProjection::TryProjectNormal(const FVector& Source, const FVector& Normal, FVector& Out) const
{
    FDifferential D;
    if (!Finite(Normal) || Normal.IsNearlyZero() || !Evaluate(Source, D)) return false;
    const FVector Unit = (Normal / Normal.GetAbsMax()).GetSafeNormal();
    const FVector Result = (FVector::DotProduct(Unit, D.SourceRadial) * D.PresentedRadial +
        FVector::DotProduct(Unit, D.SourceMeridian) / D.MeridianScale * D.PresentedMeridian +
        FVector::DotProduct(Unit, D.Azimuth) / D.AzimuthScale * D.Azimuth).GetSafeNormal();
    if (!Finite(Result) || Result.IsNearlyZero()) return false;
    Out = Result;
    return true;
}

bool FCLPlanetProjection::TryGetChartBounds(FBox& Out) const
{
    if (!bValid) return false;
    const auto& S = Settings;
    const double A = S.Curvature * S.MaxArcRadians;
    const double Radius = (S.ReferenceRadiusCm + S.Curvature * S.MaxAltitudeCm) * S.MaxArcRadians * Sinc(A);
    const double MinZ = S.MinAltitudeCm * FMath::Cos(A) - .5 * S.Curvature * S.ReferenceRadiusCm *
        FMath::Square(S.MaxArcRadians * Sinc(A / 2));
    const double HalfHeight = (S.MaxAltitudeCm - MinZ) / 2;
    const FVector Center = S.Center + (S.ReferenceRadiusCm + (S.MaxAltitudeCm + MinZ) / 2) * S.AnchorDirection;
    FVector Extent;
    for (int32 I = 0; I < 3; ++I)
        Extent[I] = Radius * FMath::Sqrt(FMath::Max(0., 1 - FMath::Square(S.AnchorDirection[I]))) +
            HalfHeight * FMath::Abs(S.AnchorDirection[I]) + S.ReferenceRadiusCm * 1.e-10;
    if (!Finite(Center) || !Finite(Extent)) return false;
    const FBox Bounds(Center - Extent, Center + Extent);
    if (!Finite(Bounds.Min) || !Finite(Bounds.Max)) return false;
    Out = Bounds;
    return true;
}
