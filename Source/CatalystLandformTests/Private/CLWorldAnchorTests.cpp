#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "CLAnchoredGPU.h"
#include "Presentation/CLPlanetProjection.h"
#include "Misc/AutomationTest.h"
namespace
{
struct FWorldAnchorWitnesses
{
    TArray<FCLAnchoredGPUInput> Inputs;
    TArray<FVector> Positions, Normals, Vectors;
    TArray<double> PositionBudgets;
    TSharedPtr<FCLAnchoredGPUReadback> Job;
    double Started = FPlatformTime::Seconds();
};
class FWorldAnchorWait final : public IAutomationLatentCommand
{
    FAutomationTestBase *Test;
    TSharedRef<FWorldAnchorWitnesses> W;

public:
    FWorldAnchorWait(FAutomationTestBase *T, TSharedRef<FWorldAnchorWitnesses> In) : Test(T), W(In)
    {
    }
    bool Update() override
    {
        TArray<FCLAnchoredGPUOutput> Results;
        const auto Status = W->Job->Poll(Results);
        if (Status == ECLProjectionReadbackStatus::Pending && FPlatformTime::Seconds() - W->Started < 60)
            return false;
        if (!Test->TestTrue(TEXT("World-anchor GPU readback completed"),
                            Status == ECLProjectionReadbackStatus::Complete))
            return true;
        if (!Test->TestEqual(TEXT("All world-anchor witnesses returned"), Results.Num(), W->Positions.Num()))
            return true;
        double PositionError = 0, NormalError = 0, VectorError = 0, AffineError = 0;
        for (int32 I = 0; I < Results.Num(); ++I)
        {
            VectorError = FMath::Max(VectorError, FVector::Distance(FVector(Results[I].Phase), W->Vectors[I]));
            AffineError = FMath::Max(AffineError, FVector(Results[I].MacroPhase).Length()/W->Inputs[I].Normal.W);
            PositionError =
                FMath::Max(PositionError, FVector::Distance(FVector(Results[I].Position), W->Positions[I])/W->PositionBudgets[I]);
            NormalError =
                FMath::Max(NormalError, 1 - FVector::DotProduct(FVector(Results[I].Normal).GetSafeNormal(),
                                                                W->Normals[I]));
        }
        Test->TestTrue(TEXT("Translated native presentation stays within original 1 mm budget near anchor and 1 ppm at wide angles"),
                       FMath::IsFinite(PositionError) && PositionError < 1.);
        Test->TestTrue(TEXT("Translated native normal matches CPU differential"),
                       FMath::IsFinite(NormalError) && NormalError < 1.e-5);
        Test->TestTrue(TEXT("Voxel affine Jacobian matches CPU differential"), FMath::IsFinite(VectorError) && VectorError < 1.e-5);
        Test->TestTrue(TEXT("Per-brick linearization error is below 1% of brick probe"), FMath::IsFinite(AffineError) && AffineError < .01);
        Test->AddInfo(FString::Printf(TEXT("Voxel differential max error %.12g; affine residual/probe %.12g"), VectorError, AffineError));
        Test->AddInfo(
            FString::Printf(TEXT("%d world-anchor GPU witnesses; max position/budget %.12g; normal %.12g"),
                            Results.Num(), PositionError, NormalError));
        return true;
    }
};
} // namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLWorldAnchorGPUCheck, "Catalyst.Landform.Presentation.WorldAnchorGPU",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLWorldAnchorGPUCheck::RunTest(const FString &)
{
    auto W = MakeShared<FWorldAnchorWitnesses>();
    for (double Radius : {1875., 7500., 75000., 750000., 338950000.})
        for (FVector Axis : {FVector::ZAxisVector, FVector(.3, -.4, .866025403784).GetSafeNormal()})
            for (FVector Shift :
                 {FVector(123.125, -91.375, 48.625), FVector(1.e9 + 123.125, -2.e9 - 91.375, 3.e9 + 48.625)})
                for (double Curvature : {0., .05, .5, 1.})
                {
                    FCLPlanetProjectionSettings S;
                    S.Center = Shift - Radius * Axis;
                    S.AnchorDirection = Axis;
                    S.ReferenceRadiusCm = Radius;
                    S.Curvature = Curvature;
                    S.MaxArcRadians = FMath::DegreesToRadians(89.);
                    S.MinAltitudeCm = -Radius * .4;
                    S.MaxAltitudeCm = Radius * .3;
                    FCLPlanetProjection P;
                    if (!TestTrue(TEXT("Create translated CPU chart"), FCLPlanetProjection::TryCreate(S, P)))
                        return false;
                    FVector X, Y;
                    Axis.FindBestAxisVectors(X, Y);
                    for (bool Wide : {false,true})
                    for (int32 I = 0; I < 8; ++I)
                    {
                        const double Arc = (Wide ? Radius * FMath::DegreesToRadians(88.) * (I - 3.5) / 3.5 : FMath::Min(Radius * .1, 10000.) * (I - 3.5) / 4.);
                        const FVector Direction =
                            Axis * FMath::Cos(Arc / Radius) + X * FMath::Sin(Arc / Radius);
                        const FVector Source = S.Center + Direction * (Radius + I * .17 - 1.25),
                                      N = (Direction + Y * .13).GetSafeNormal();
                        FVector Pos, Normal, Vector;
                        if (!P.TryProjectPosition(Source, Pos) || !P.TryProjectNormal(Source, N, Normal) || !P.TryProjectVector(Source,N,Vector))
                            return false;
                        const FVector3f High(Source), AnchorHigh(Shift);
                        FCLAnchoredGPUInput Q;
                        Q.SourceCurvature = FVector4f(High, Curvature);
                        Q.AnchorRadius = FVector4f(FVector3f(Axis), Radius);
                        Q.OriginHighScale = FVector4f(AnchorHigh, 1);
                        Q.OriginLow = FVector4f(FVector3f(Shift - FVector(AnchorHigh)), 0);
                        Q.BodyFromLocal = FVector4f(FVector3f(Source - FVector(High)), 0);
                        Q.Normal = FVector4f(FVector3f(N), Radius*.0001);
                        Q.ProjectionOrigin = FVector4f(0, 0, 0, 9150);
                        W->Inputs.Add(Q);
                        W->Positions.Add(Pos - Shift);
                        W->PositionBudgets.Add(Wide ? FMath::Max(.1,Radius*1.e-6) : .1);
                        W->Normals.Add(Normal);
                        W->Vectors.Add(Vector);
                    }
                }
    FString Error;
    W->Job = FCLAnchoredGPUReadback::TryStart(W->Inputs, Error);
    if (!W->Job)
    {
        AddError(Error);
        return false;
    }
    ADD_LATENT_AUTOMATION_COMMAND(FWorldAnchorWait(this, W));
    return true;
}
#endif
