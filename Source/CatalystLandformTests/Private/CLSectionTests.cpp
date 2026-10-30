#if WITH_DEV_AUTOMATION_TESTS
#include "CLSectionGeometry.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLSectionTest, "Catalyst.Landform.Designer.CoreToSkySection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLSectionTest::RunTest(const FString&)
{
    FString Error;
    const auto Plan = FCLPlanetCompiler::Compile(FCLPlanetCompiler::RollingPlains(), Error);
    const double Heading = .73;
    const auto G = FCLSectionGeometry::Build(*Plan, Heading);
    TestTrue(TEXT("A populated solid section is generated"), G.SurfaceVertexCount > 100 && G.Triangles.Num() > 100);
    TestEqual(TEXT("Every vertex has a preview color"), G.Vertices.Num(), G.Colors.Num());
    double MaximumFieldError = 0;
    for (int32 I = 0; I < G.SurfaceVertexCount; ++I)
    {
        const FVector P = G.Vertices[I] / 100;
        const double Field = P.Size() - Plan->RadiusMeters - Plan->SampleHeightMeters(FQuat(FVector::ZAxisVector, Heading).RotateVector(P));
        MaximumFieldError = FMath::Max(MaximumFieldError, FMath::Abs(Field));
    }
    AddInfo(FString::Printf(TEXT("Section maximum surface/field error: %.6f cm"), MaximumFieldError * 100));
    // UE's float Perlin coordinates quantize the field; qualify against a
    // centimetre, rather than an unattainable sub-float root tolerance.
    TestTrue(TEXT("Top surface intersects the compiled radial field within 1 cm"), MaximumFieldError < .01);
    bool bCore = false, bTop = false;
    TMap<FIntVector, int32> Welded;
    TArray<int32> Ids;
    for (const FVector& V : G.Vertices)
    {
        bCore |= V.IsNearlyZero(); bTop |= V.Z > Plan->RadiusMeters * 100;
        TestTrue(TEXT("Solid cutaway never extends below the centre plane"), V.Z >= 0);
        const FIntVector Key(FMath::RoundToInt(V.X * 10), FMath::RoundToInt(V.Y * 10), FMath::RoundToInt(V.Z * 10));
        if (!Welded.Contains(Key)) Welded.Add(Key, Welded.Num());
        Ids.Add(Welded[Key]);
    }
    TestTrue(TEXT("Cutaway reaches core and terrain"), bCore && bTop);
    TMap<uint64, int32> Edges;
    for (int32 I = 0; I < G.Triangles.Num(); I += 3)
    {
        const int32 A = G.Triangles[I], B = G.Triangles[I + 1], C = G.Triangles[I + 2];
        const FVector Normal = FVector::CrossProduct(G.Vertices[B] - G.Vertices[C], G.Vertices[A] - G.Vertices[C]);
        TestTrue(TEXT("No degenerate section triangles"), Normal.SizeSquared() > .001);
        if (A < G.SurfaceVertexCount && B < G.SurfaceVertexCount && C < G.SurfaceVertexCount)
            TestTrue(TEXT("Terrain faces point outward with UE winding"), Normal.Z > 0);
        for (int32 K = 0; K < 3; ++K)
        {
            const uint32 U = Ids[G.Triangles[I + K]], V = Ids[G.Triangles[I + (K + 1) % 3]];
            ++Edges.FindOrAdd((uint64(FMath::Min(U, V)) << 32) | FMath::Max(U, V));
        }
    }
    for (const auto& Edge : Edges) TestEqual(TEXT("Solid section is closed, including core floor"), Edge.Value, 2);
    FCLPlanetRecipe Sphere; const auto Constant = FCLPlanetCompiler::Compile(Sphere, Error);
    const FVector P = FCLSectionGeometry::SurfacePoint(*Constant, 30, 40, 0);
    TestTrue(TEXT("Vertical intersection preserves actual sphere curvature"), FMath::Abs(P.Z / 100 - FMath::Sqrt(10000.0 - 900 - 1600)) < 1.e-5);
    return true;
}
#endif
