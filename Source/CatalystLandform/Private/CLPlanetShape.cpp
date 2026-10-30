#include "CLPlanetShape.h"

namespace
{
void ChartBasis(const FCLPlanetShape& Shape, FVector& N, FVector& X, FVector& Y)
{
    N = Shape.AnchorDirection.GetSafeNormal(UE_SMALL_NUMBER, FVector::XAxisVector);
    X = FVector::VectorPlaneProject(Shape.ChartXAxis, N).GetSafeNormal();
    if (X.IsNearlyZero())
    {
        FVector Other;
        N.FindBestAxisVectors(X, Other);
    }
    Y = FVector::CrossProduct(N, X).GetSafeNormal();
}
}

bool FCLFlatStampGrid::IsValid() const
{
    return Resolution.X >= 2 && Resolution.Y >= 2 &&
        static_cast<int64>(Resolution.X) * Resolution.Y == Heights.Num() &&
        FMath::IsFinite(Size.X) && FMath::IsFinite(Size.Y) && Size.X > 0 && Size.Y > 0;
}

double FCLFlatStampGrid::Sample(const FVector2D& Coordinates) const
{
    if (!IsValid() || !FMath::IsFinite(Coordinates.X) || !FMath::IsFinite(Coordinates.Y) ||
        FMath::Abs(Coordinates.X) > Size.X * .5 || FMath::Abs(Coordinates.Y) > Size.Y * .5)
    {
        return 0;
    }
    const double U = (Coordinates.X / Size.X + .5) * (Resolution.X - 1);
    const double V = (Coordinates.Y / Size.Y + .5) * (Resolution.Y - 1);
    const int32 X = FMath::Clamp(FMath::FloorToInt(U), 0, Resolution.X - 2);
    const int32 Y = FMath::Clamp(FMath::FloorToInt(V), 0, Resolution.Y - 2);
    const auto At = [this](int32 I, int32 J) { return Heights[J * Resolution.X + I]; };
    return FMath::Lerp(
        FMath::Lerp(static_cast<double>(At(X, Y)), static_cast<double>(At(X + 1, Y)), U - X),
        FMath::Lerp(static_cast<double>(At(X, Y + 1)), static_cast<double>(At(X + 1, Y + 1)), U - X), V - Y);
}

double FCLFlatStampGrid::MaxAbsoluteHeight() const
{
    double Maximum = 0;
    for (float Height : Heights)
    {
        if (FMath::IsFinite(Height)) Maximum = FMath::Max(Maximum, FMath::Abs(static_cast<double>(Height)));
    }
    return Maximum;
}

FCLFlatStampGrid FCLFlatStampGrid::MakeProofRidge()
{
    FCLFlatStampGrid Grid;
    Grid.Heights.SetNumZeroed(Grid.Resolution.X * Grid.Resolution.Y);
    for (int32 Y = 1; Y < Grid.Resolution.Y - 1; ++Y)
    {
        for (int32 X = 1; X < Grid.Resolution.X - 1; ++X)
        {
            const double U = 2.0 * X / (Grid.Resolution.X - 1) - 1;
            const double V = 2.0 * Y / (Grid.Resolution.Y - 1) - 1;
            const double Window = FMath::Square(FMath::Cos(UE_PI * U / 2)) *
                FMath::Square(FMath::Cos(UE_PI * V / 2));
            Grid.Heights[Y * Grid.Resolution.X + X] = 800 * Window *
                (.85 + .15 * FMath::Cos(2 * UE_PI * U) * FMath::Cos(UE_PI * V));
        }
    }
    return Grid;
}

UCLFlatStampAsset::UCLFlatStampAsset()
{
    Grid = FCLFlatStampGrid::MakeProofRidge();
}

bool FCLPlanetShape::ToChart(const FVector& Direction, FVector2D& Coordinates) const
{
    Coordinates = FVector2D::ZeroVector;
    if (Radius <= 0 || !FMath::IsFinite(Radius) || Direction.IsNearlyZero()) return false;
    FVector N, X, Y;
    ChartBasis(*this, N, X, Y);
    const FVector D = Direction.GetSafeNormal();
    const double Cosine = FMath::Clamp(FVector::DotProduct(N, D), -1.0, 1.0);
    if (Cosine < -1 + 1.e-10) return false; // A local chart has no unique antipode.
    const FVector Tangent = D - Cosine * N;
    const double Sine = Tangent.Size();
    if (Sine < 1.e-10) return true;
    const double Arc = Radius * FMath::Atan2(Sine, Cosine);
    Coordinates = FVector2D(FVector::DotProduct(Tangent, X), FVector::DotProduct(Tangent, Y)) * (Arc / Sine);
    return true;
}

FVector FCLPlanetShape::FromChart(const FVector2D& Coordinates) const
{
    FVector N, X, Y;
    ChartBasis(*this, N, X, Y);
    const double Arc = Coordinates.Size();
    if (Arc < 1.e-10 || Radius <= 0) return N;
    const double Angle = Arc / Radius;
    return (FMath::Cos(Angle) * N + FMath::Sin(Angle) *
        (Coordinates.X * X + Coordinates.Y * Y) / Arc).GetSafeNormal();
}

double FCLPlanetShape::HeightAt(const FVector& Direction) const
{
    if (!FlatStamp.IsValid() || Radius <= 0 || Direction.IsNearlyZero()) return 0;
    const FVector N = AnchorDirection.GetSafeNormal(UE_SMALL_NUMBER, FVector::XAxisVector);
    const double MaxAngle = FMath::Min(FlatStamp.Size.Size() * .5 / Radius, UE_PI - 1.e-5);
    if (FVector::DotProduct(Direction.GetSafeNormal(), N) < FMath::Cos(MaxAngle)) return 0;
    FVector2D Coordinates;
    return ToChart(Direction, Coordinates) ? FlatStamp.Sample(Coordinates) : 0;
}

double FCLPlanetShape::DistanceAt(const FVector& Position) const
{
    const double Length = Position.Size();
    return Length - Radius - (Length > UE_SMALL_NUMBER ? HeightAt(Position / Length) : 0);
}

FVoxelBox FCLPlanetShape::GetLocalBounds() const
{
    const double Extent = FMath::Max(Radius, 100.0) + FlatStamp.MaxAbsoluteHeight();
    return FVoxelBox(-Extent, Extent);
}

void FCLPlanetShape::Sample(TVoxelArrayView<float> OutDistances, const FVoxelDoubleVectorBuffer& Positions) const
{
    check(OutDistances.Num() == Positions.Num());
    for (int32 Index = 0; Index < OutDistances.Num(); ++Index)
    {
        OutDistances[Index] = static_cast<float>(DistanceAt(Positions[Index]));
    }
}
