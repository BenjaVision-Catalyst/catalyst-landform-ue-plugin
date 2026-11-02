#include "Presentation/CLProjectionGPUVerification.h"
#include "Presentation/CLPlanetProjection.h"
#include "CLProjectionGPU.h"
#include "HAL/PlatformTime.h"
#include "Math/RandomStream.h"
#include <limits>

struct FCLProjectionGPUVerification::FImpl
{
    struct FExpected { FVector Position, Inverse, Vector, Normal; bool bPosition, bInverse, bVector, bNormal; };
    TArray<FExpected> Expected;
    TSharedPtr<FCLProjectionGPUReadback> Readback;
    FCLProjectionGPUVerificationResult Result;
    double Started = FPlatformTime::Seconds();

    bool StartChart(FString& Error)
    {
        const double Curvatures[] = {0, 1.e-12, 1.e-6, .1, .5, 1};
        const double Radii[] = {1000, 100000, 1000000};
        FRandomStream Random(0x434C4750 + Result.Charts);
        FCLPlanetProjectionSettings S;
        S.Center = FVector(1.e9, -2.e9, 3.e9);
        S.ReferenceRadiusCm = Radii[Result.Charts / 6];
        S.AnchorDirection = Random.VRand();
        S.Curvature = Curvatures[Result.Charts % 6];
        S.MinAltitudeCm = -.4 * S.ReferenceRadiusCm;
        S.MaxAltitudeCm = .3 * S.ReferenceRadiusCm;
        // GPU input is FP32. Test the exact CPU mapping with the shader's documented
        // admission band; do not mistake world-origin rounding at an exact edge for a math error.
        auto Oracle = S;
        Oracle.MinAltitudeCm -= 2.e-6 * S.ReferenceRadiusCm;
        Oracle.MaxAltitudeCm += 2.e-6 * S.ReferenceRadiusCm;
        Oracle.MaxArcRadians += 2.e-6;
        FCLPlanetProjection Projection;
        if (!FCLPlanetProjection::TryCreate(Oracle, Projection)) { Error = TEXT("CPU fixture chart refused"); return false; }
        TArray<FCLProjectionGPUInput> Inputs;
        Expected.Reset();
        FVector D1, D2;
        S.AnchorDirection.FindBestAxisVectors(D1, D2);
        const auto Add = [&](FVector Source, FVector Vector, FVector Normal)
        {
            FExpected E{};
            E.bPosition = Projection.TryProjectPosition(Source, E.Position);
            E.bVector = Projection.TryProjectVector(Source, Vector, E.Vector);
            E.bNormal = Projection.TryProjectNormal(Source, Normal, E.Normal);
            const FVector Presented = E.bPosition ? E.Position : Source;
            E.bInverse = Projection.TryUnprojectPosition(Presented, E.Inverse);
            const auto Unit = [&](FVector V) { return FVector4f(FVector3f((V - S.Center) / S.ReferenceRadiusCm), 0); };
            FCLProjectionGPUInput Q{Unit(Source), Unit(Presented), FVector4f(FVector3f(Vector), 0), FVector4f(FVector3f(Normal), 0)};
            if (E.bPosition) E.Position = (E.Position - S.Center) / S.ReferenceRadiusCm;
            if (E.bInverse) E.Inverse = (E.Inverse - S.Center) / S.ReferenceRadiusCm;
            Inputs.Add(Q);
            Expected.Add(E);
        };
        for (int32 I = 0; I < 128; ++I)
        {
            const double Theta = I == 0 ? 0 : (I == 1 ? S.MaxArcRadians : Random.FRandRange(0, .98) * S.MaxArcRadians);
            const double Phi = Random.FRandRange(-UE_PI, UE_PI);
            const double H = I == 1 ? S.MinAltitudeCm : Random.FRandRange(-.39, .29) * S.ReferenceRadiusCm;
            const FVector U = FMath::Cos(Theta) * S.AnchorDirection + FMath::Sin(Theta) * (FMath::Cos(Phi) * D1 + FMath::Sin(Phi) * D2);
            Add(S.Center + (S.ReferenceRadiusCm + H) * U, Random.VRand(), Random.VRand());
        }
        const FVector Valid = S.Center + S.ReferenceRadiusCm * S.AnchorDirection;
        for (const FVector Source : {S.Center, S.Center - S.ReferenceRadiusCm * S.AnchorDirection,
            S.Center + 1.5 * S.ReferenceRadiusCm * S.AnchorDirection, S.Center + .5 * S.ReferenceRadiusCm * S.AnchorDirection,
            S.Center + S.ReferenceRadiusCm * (FMath::Cos(S.MaxArcRadians + .001) * S.AnchorDirection + FMath::Sin(S.MaxArcRadians + .001) * D1),
            FVector(std::numeric_limits<double>::quiet_NaN(), 0, 0), FVector(std::numeric_limits<double>::infinity(), 0, 0)})
            Add(Source, FVector::XAxisVector, FVector::ZAxisVector);
        Add(Valid, FVector::ZeroVector, FVector::ZeroVector);
        Add(Valid, FVector::ZeroVector, FVector(.00009));
        Add(Valid, FVector::ZeroVector, FVector(.00011, 0, 0));
        Add(Valid, FVector::ZeroVector, FVector(1.e30, -2.e30, 3.e30));
        Add(Valid, FVector(std::numeric_limits<double>::quiet_NaN(), 0, 0), FVector(std::numeric_limits<double>::infinity(), 0, 0));
        FCLProjectionGPUFrame Frame;
        Frame.AnchorDirection = FVector3f(S.AnchorDirection);
        Frame.Curvature = S.Curvature;
        Frame.MaxArcRadians = S.MaxArcRadians;
        Frame.MinAltitude = S.MinAltitudeCm / S.ReferenceRadiusCm;
        Frame.MaxAltitude = S.MaxAltitudeCm / S.ReferenceRadiusCm;
        Readback = FCLProjectionGPUReadback::TryStart(Frame, MoveTemp(Inputs), Error);
        return Readback.IsValid();
    }

    bool Check(const TArray<FCLProjectionGPUOutput>& Outputs)
    {
        if (Outputs.Num() != Expected.Num()) { Result.Message = TEXT("GPU returned wrong query count"); return false; }
        for (int32 I = 0; I < Outputs.Num(); ++I)
        {
            const auto& A = Outputs[I];
            const auto& E = Expected[I];
            const auto Compare = [&](const FVector4f& Actual, const FVector& Wanted, bool bValid, double Tolerance, double& Maximum, const TCHAR* Name)
            {
                const FVector V(Actual.X, Actual.Y, Actual.Z);
                const double Difference = bValid ? FVector::Distance(V, Wanted) : 0;
                if (!FMath::IsFinite(Actual.W) || Actual.W != (bValid ? 1.f : 0.f) || V.ContainsNaN() ||
                    (!bValid && V != FVector::ZeroVector) || !FMath::IsFinite(Difference) || Difference > Tolerance)
                {
                    Result.Message = FString::Printf(TEXT("GPU %s mismatch, chart %d query %d: valid %g/%d, error %.12g"), Name, Result.Charts, I, Actual.W, bValid, Difference);
                    return false;
                }
                Maximum = FMath::Max(Maximum, Difference);
                return true;
            };
            if (!Compare(A.Position, E.Position, E.bPosition, 2.e-6, Result.MaxPositionErrorRadii, TEXT("position")) ||
                !Compare(A.Inverse, E.Inverse, E.bInverse, 2.e-6, Result.MaxInverseErrorRadii, TEXT("inverse")) ||
                !Compare(A.Vector, E.Vector, E.bVector, 5.e-6, Result.MaxVectorError, TEXT("vector")) ||
                !Compare(A.Normal, E.Normal, E.bNormal, 5.e-6, Result.MaxNormalError, TEXT("normal"))) return false;
            ++Result.Queries;
        }
        ++Result.Charts;
        return true;
    }
};

FCLProjectionGPUVerification::FCLProjectionGPUVerification() : Impl(MakeUnique<FImpl>()) {}
FCLProjectionGPUVerification::~FCLProjectionGPUVerification() = default;
TSharedPtr<FCLProjectionGPUVerification> FCLProjectionGPUVerification::TryStart(FString& Error)
{
    auto Verification = TSharedPtr<FCLProjectionGPUVerification>(new FCLProjectionGPUVerification());
    if (!Verification->Impl->StartChart(Error)) return nullptr;
    return Verification;
}
bool FCLProjectionGPUVerification::Tick()
{
    check(IsInGameThread());
    auto& I = *Impl;
    if (I.Result.bComplete) return true;
    TArray<FCLProjectionGPUOutput> Outputs;
    const auto Status = I.Readback->Poll(Outputs);
    if (Status == ECLProjectionReadbackStatus::Failed || FPlatformTime::Seconds() - I.Started > 60)
        I.Result.Message = TEXT("GPU projection readback failed or timed out");
    else if (Status == ECLProjectionReadbackStatus::Pending) return false;
    else if (I.Check(Outputs))
    {
        if (I.Result.Charts == 18)
        {
            I.Result.bComplete = I.Result.bPassed = true;
            I.Result.Message = TEXT("CPU/GPU position, inverse, vector, normal and rejection parity passed");
        }
        else if (I.StartChart(I.Result.Message)) return false;
    }
    I.Result.bComplete = true;
    I.Readback.Reset();
    return true;
}
const FCLProjectionGPUVerificationResult& FCLProjectionGPUVerification::GetResult() const { return Impl->Result; }
