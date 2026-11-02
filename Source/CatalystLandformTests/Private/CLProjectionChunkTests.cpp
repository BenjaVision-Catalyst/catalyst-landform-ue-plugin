#if WITH_DEV_AUTOMATION_TESTS
#include "Presentation/CLProjectionChunk.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLProjectionChunkTest, "Catalyst.Landform.Presentation.ChunkResidualAndStretchBounds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLProjectionChunkTest::RunTest(const FString&)
{
    FRandomStream Random(0x434C4348);
    double MaxResidualFraction = 0, MaxObservedStretchFraction = 0;
    int32 Samples = 0;
    for (double R : {1000., 100000., 1000000.}) for (double C : {0., .1, .5, 1.})
    {
        FCLPlanetProjectionSettings S;
        S.Center = FVector(1.e6, -2.e6, 3.e6);
        S.ReferenceRadiusCm = R;
        S.Curvature = C;
        S.AnchorDirection = Random.VRand();
        S.MinAltitudeCm = -.4 * R;
        S.MaxAltitudeCm = .3 * R;
        FCLPlanetProjection P;
        TestTrue(TEXT("Chart accepted"), FCLPlanetProjection::TryCreate(S, P));
        FVector D, D2;
        S.AnchorDirection.FindBestAxisVectors(D, D2);
        for (double Theta : {0., .4, .9})
        {
            const FVector Pivot = S.Center + R * (FMath::Cos(Theta) * S.AnchorDirection + FMath::Sin(Theta) * D);
            const FVector Extent = R * FVector(.01, .02, .015);
            const FBox Source(Pivot - Extent, Pivot + Extent);
            FCLProjectionChunk Fit;
            if (!TestTrue(TEXT("Entire chunk box admitted"), FCLProjectionChunk::TryCreate(P, Source, Fit))) return false;
            if (C == 1) TestEqual(TEXT("Identity needs no residual"), Fit.GetMaxResidualCm(), 0.);
            TestTrue(TEXT("Residual remains local to chunk"), Fit.GetMaxResidualCm() < Source.GetExtent().Length());
            for (int32 I = 0; I < 136; ++I)
            {
                const FVector Q = I < 8 ? FVector(I & 1 ? Source.Max.X : Source.Min.X, I & 2 ? Source.Max.Y : Source.Min.Y, I & 4 ? Source.Max.Z : Source.Min.Z)
                    : Pivot + Extent * FVector(Random.FRandRange(-1, 1), Random.FRandRange(-1, 1), Random.FRandRange(-1, 1));
                FVector Presented;
                if (!TestTrue(TEXT("Admitted mesh point projects"), P.TryProjectPosition(Q, Presented))) return false;
                const double Residual = FVector::Distance(Presented, Fit.ProjectRigidPosition(Q));
                TestTrue(TEXT("Actual residual inside analytic bound"), Residual <= Fit.GetMaxResidualCm() + R * 1.e-9);
                TestTrue(TEXT("Projected geometry inside conservative fitted box"), Fit.GetPresentedBounds().IsInsideOrOn(Presented));
                if (Fit.GetMaxResidualCm() > 0) MaxResidualFraction = FMath::Max(MaxResidualFraction, Residual / Fit.GetMaxResidualCm());
                FVector Vector;
                P.TryProjectVector(Q, Random.VRand(), Vector);
                MaxObservedStretchFraction = FMath::Max(MaxObservedStretchFraction, Vector.Length() / Fit.GetMaxStretch());
                TestTrue(TEXT("Jacobian vector inside analytic stretch bound"), Vector.Length() <= Fit.GetMaxStretch() + 1.e-9);
                ++Samples;
            }
            const FTransform Canonical(FQuat(FVector::ZAxisVector, .37), Pivot + FVector(1, 2, 3), FVector(2));
            const FVector Local(4, 5, 6);
            TestTrue(TEXT("Instance transform preserves the fitted mapping"),
                Fit.ProjectRigidTransform(Canonical).TransformPosition(Local).Equals(Fit.ProjectRigidPosition(Canonical.TransformPosition(Local)), R * 1.e-9));
        }
        FCLProjectionChunk Sentinel;
        TestFalse(TEXT("Chunk crossing the planet centre refused"), FCLProjectionChunk::TryCreate(P, FBox(S.Center - FVector(R), S.Center + FVector(R)), Sentinel));
        const FVector Edge = S.Center + R * (FMath::Cos(S.MaxArcRadians) * S.AnchorDirection + FMath::Sin(S.MaxArcRadians) * D);
        TestFalse(TEXT("Boundary-crossing chunk refused rather than admitted by pivot alone"), FCLProjectionChunk::TryCreate(P, FBox(Edge - FVector(R * .01), Edge + FVector(R * .01)), Sentinel));
        TestFalse(TEXT("Uninitialized chart refused"), FCLProjectionChunk::TryCreate(FCLPlanetProjection(), FBox(FVector(-1, -1, R - 1), FVector(1, 1, R + 1)), Sentinel));
    }
    FCLPlanetProjection DefaultProjection;
    TestTrue(TEXT("Default chart accepted"), FCLPlanetProjection::TryCreate(FCLPlanetProjectionSettings(), DefaultProjection));
    FCLProjectionChunk ThinFit;
    const FBox ThinBox(FVector(-1000, -1000, 9950), FVector(1000, 1000, 10050));
    TestTrue(TEXT("Thin box admitted when its loose sphere crosses the altitude boundary"), FCLProjectionChunk::TryCreate(DefaultProjection, ThinBox, ThinFit));
    AddInfo(FString::Printf(TEXT("%d chunk samples; max residual/bound %.9g; max observed stretch/bound %.9g"), Samples, MaxResidualFraction, MaxObservedStretchFraction));
    return true;
}
#endif
