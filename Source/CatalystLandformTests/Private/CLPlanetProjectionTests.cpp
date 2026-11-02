#if WITH_DEV_AUTOMATION_TESTS
#include "Presentation/CLPlanetProjection.h"
#include "Misc/AutomationTest.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLProjectionEndpointsTest, "Catalyst.Landform.Presentation.Endpoints",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLProjectionEndpointsTest::RunTest(const FString&)
{
    FCLPlanetProjectionSettings S;
    S.Center = FVector(12345, -67890, 13579);
    S.AnchorDirection = FVector(2, 3, 4).GetSafeNormal();
    S.ReferenceRadiusCm = 100000;
    S.MinAltitudeCm = -20000;
    S.MaxAltitudeCm = 30000;
    FVector D, Other;
    S.AnchorDirection.FindBestAxisVectors(D, Other);
    for (const double C : {0., 1.})
    {
        S.Curvature = C;
        FCLPlanetProjection Projection;
        if (!TestTrue(TEXT("Endpoint settings accepted"), FCLPlanetProjection::TryCreate(S, Projection))) return false;
        for (const double Theta : {0., .2, .7, S.MaxArcRadians})
            for (const double H : {-10000., 0., 20000.})
            {
                const FVector Source = S.Center + (S.ReferenceRadiusCm + H) *
                    (FMath::Cos(Theta) * S.AnchorDirection + FMath::Sin(Theta) * D);
                const FVector Expected = C == 1 ? Source : S.Center + (S.ReferenceRadiusCm + H) *
                    S.AnchorDirection + S.ReferenceRadiusCm * Theta * D;
                FVector Presented, Recovered;
                TestTrue(TEXT("Endpoint projects"), Projection.TryProjectPosition(Source, Presented));
                TestTrue(TEXT("Identity or arc-distance plane with retained altitude"), Presented.Equals(Expected, 1.e-7));
                TestTrue(TEXT("Endpoint inverse accepted"), Projection.TryUnprojectPosition(Presented, Recovered));
                TestTrue(TEXT("Endpoint inverse matches source"), Recovered.Equals(Source, 1.e-7));
                const FVector U = (Source - S.Center).GetSafeNormal();
                FVector Normal;
                TestTrue(TEXT("Radial normal projects"), Projection.TryProjectNormal(Source, U, Normal));
                TestTrue(TEXT("Endpoint normal is radial or anchor up"), Normal.Equals(C == 1 ? U : S.AnchorDirection, 1.e-10));
            }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLProjectionDifferentialTest, "Catalyst.Landform.Presentation.RoundTripDifferentialAndBounds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLProjectionDifferentialTest::RunTest(const FString&)
{
    FRandomStream Random(0x434C5052);
    double MaxRoundTripError = 0, MaxVectorError = 0, MinNormalDot = 1;
    int32 Count = 0;
    for (const double R : {1000., 100000., 1000000.})
        for (const double C : {0., 1.e-12, 1.e-6, .1, .5, 1.})
        {
            FCLPlanetProjectionSettings S;
            S.Center = R * FVector(3, -2, 1);
            S.AnchorDirection = Random.VRand();
            S.ReferenceRadiusCm = R;
            S.Curvature = C;
            S.MinAltitudeCm = -.4 * R;
            S.MaxAltitudeCm = .3 * R;
            FCLPlanetProjection Projection;
            if (!TestTrue(TEXT("Random chart accepted"), FCLPlanetProjection::TryCreate(S, Projection))) return false;
            FBox Bounds;
            TestTrue(TEXT("Chart bounds available"), Projection.TryGetChartBounds(Bounds));
            FVector D1, D2;
            S.AnchorDirection.FindBestAxisVectors(D1, D2);
            for (int32 I = 0; I < 100; ++I)
            {
                const double Theta = Random.FRandRange(0, .8) * S.MaxArcRadians;
                const double Phi = Random.FRandRange(-UE_PI, UE_PI);
                const FVector D = FMath::Cos(Phi) * D1 + FMath::Sin(Phi) * D2;
                const double H = R * Random.FRandRange(-.3, .2);
                const FVector Source = S.Center + (R + H) * (FMath::Cos(Theta) * S.AnchorDirection + FMath::Sin(Theta) * D);
                FVector Presented, Recovered;
                if (!Projection.TryProjectPosition(Source, Presented) || !Projection.TryUnprojectPosition(Presented, Recovered))
                { AddError(TEXT("Interior round trip rejected")); return false; }
                MaxRoundTripError = FMath::Max(MaxRoundTripError, FVector::Distance(Source, Recovered));
                TestTrue(TEXT("Projected point inside conservative chart box"), Bounds.IsInsideOrOn(Presented));
                const FVector Normal = Random.VRand();
                FVector T1, T2;
                Normal.FindBestAxisVectors(T1, T2);
                // Independent geometric oracle: transform two small surface tangents and cross them.
                const double E = R * 1.e-6;
                FVector P1, M1, P2, M2, AnalyticVector, AnalyticNormal;
                if (!Projection.TryProjectPosition(Source + E * T1, P1) || !Projection.TryProjectPosition(Source - E * T1, M1) ||
                    !Projection.TryProjectPosition(Source + E * T2, P2) || !Projection.TryProjectPosition(Source - E * T2, M2) ||
                    !Projection.TryProjectVector(Source, T1, AnalyticVector) || !Projection.TryProjectNormal(Source, Normal, AnalyticNormal))
                { AddError(TEXT("Interior differential query rejected")); return false; }
                const FVector V1 = (P1 - M1) / (2 * E), V2 = (P2 - M2) / (2 * E);
                FVector GeometricNormal = FVector::CrossProduct(V1, V2).GetSafeNormal();
                if (FVector::DotProduct(FVector::CrossProduct(T1, T2), Normal) < 0) GeometricNormal *= -1;
                MaxVectorError = FMath::Max(MaxVectorError, FVector::Distance(V1, AnalyticVector));
                MinNormalDot = FMath::Min(MinNormalDot, FVector::DotProduct(GeometricNormal, AnalyticNormal));
                ++Count;
            }
            // Include the outer angular and altitude edges in the bounds oracle.
            for (const double H : {S.MinAltitudeCm, S.MaxAltitudeCm})
                for (int32 I = 0; I < 64; ++I)
                {
                    const double Phi = 2 * UE_PI * I / 64;
                    const FVector U = FMath::Cos(S.MaxArcRadians) * S.AnchorDirection +
                        FMath::Sin(S.MaxArcRadians) * (FMath::Cos(Phi) * D1 + FMath::Sin(Phi) * D2);
                    FVector P;
                    TestTrue(TEXT("Chart edge accepted"), Projection.TryProjectPosition(S.Center + (R + H) * U, P));
                    TestTrue(TEXT("Chart edge inside box"), Bounds.IsInsideOrOn(P));
                }
        }
    TestTrue(TEXT("Round trips accurate to 0.00001 cm"), MaxRoundTripError < 1.e-5);
    TestTrue(TEXT("Jacobian agrees with finite differences"), MaxVectorError < 1.e-7);
    TestTrue(TEXT("Inverse-transpose normals agree with geometric oracle"), MinNormalDot > 1 - 1.e-9);
    AddInfo(FString::Printf(TEXT("%d round trips and differential oracles; max inverse %.12g cm, vector %.12g, min normal dot %.12g"),
        Count, MaxRoundTripError, MaxVectorError, MinNormalDot));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLProjectionDomainTest, "Catalyst.Landform.Presentation.DomainAndFailureIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLProjectionDomainTest::RunTest(const FString&)
{
    FCLPlanetProjectionSettings S;
    FCLPlanetProjection Projection, Invalid;
    TestTrue(TEXT("Default chart accepted"), FCLPlanetProjection::TryCreate(S, Projection));
    const FVector Sentinel(123, 456, 789);
    FVector Out = Sentinel;
    TestFalse(TEXT("Unconstructed chart rejected"), Invalid.TryProjectPosition(FVector(0, 0, 10000), Out));
    TestTrue(TEXT("Rejected query preserves output"), Out == Sentinel);
    for (const FVector P : {FVector::ZeroVector, FVector(0, 0, -10000), FVector(0, 0, 12000), FVector(0, 0, 8000),
        FVector(std::numeric_limits<double>::quiet_NaN(), 0, 0), FVector(0, std::numeric_limits<double>::infinity(), 0)})
    {
        TestFalse(TEXT("Invalid/outside source rejected"), Projection.TryProjectPosition(P, Out));
        TestTrue(TEXT("Failed source preserves output"), Out == Sentinel);
    }
    TestFalse(TEXT("Outside presented chart rejected"), Projection.TryUnprojectPosition(FVector(1.e6, 0, 10000), Out));
    TestFalse(TEXT("Ambiguous inverse hemisphere rejected"), Projection.TryUnprojectPosition(FVector(0, 0, -10000), Out));
    TestFalse(TEXT("Zero normal rejected"), Projection.TryProjectNormal(FVector(0, 0, 10000), FVector::ZeroVector, Out));
    TestTrue(TEXT("Failed inverse/normal preserves output"), Out == Sentinel);
    for (int32 I = 0; I < 11; ++I)
    {
        auto Bad = S;
        switch (I)
        {
        case 0: Bad.Curvature = -.1; break;
        case 1: Bad.Curvature = 1.1; break;
        case 2: Bad.Curvature = std::numeric_limits<double>::quiet_NaN(); break;
        case 3: Bad.ReferenceRadiusCm = 0; break;
        case 4: Bad.AnchorDirection = FVector::ZeroVector; break;
        case 5: Bad.MaxArcRadians = UE_HALF_PI; break;
        case 6: Bad.MinAltitudeCm = -Bad.ReferenceRadiusCm; break;
        case 7: Bad.MaxAltitudeCm = Bad.MinAltitudeCm - 1; break;
        case 8: Bad.Center.X = std::numeric_limits<double>::infinity(); break;
        case 9: Bad.MaxArcRadians = 0; break;
        case 10: Bad.Center.X = 1.5e308; Bad.ReferenceRadiusCm = 1.e308; break;
        }
        TestFalse(TEXT("Invalid settings rejected"), FCLPlanetProjection::TryCreate(Bad, Projection));
        TestEqual(TEXT("Rejected settings preserve previous chart"), Projection.GetSettings().Curvature, S.Curvature);
    }
    TestTrue(TEXT("Previously valid chart remains usable"), Projection.TryProjectPosition(FVector(0, 0, 10000), Out));
    return true;
}
#endif
