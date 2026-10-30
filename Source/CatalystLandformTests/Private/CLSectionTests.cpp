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
        const FVector P = G.SourceSurfacePoints[I] / 100;
        const double Field = P.Size() - Plan->RadiusMeters - Plan->SampleHeightMeters(P);
        MaximumFieldError = FMath::Max(MaximumFieldError, FMath::Abs(Field));
    }
    AddInfo(FString::Printf(TEXT("Section maximum surface/field error: %.6f cm"), MaximumFieldError * 100));
    // UE's float Perlin coordinates quantize the field; qualify against a
    // centimetre, rather than an unattainable sub-float root tolerance.
    TestTrue(TEXT("Source samples lie on the compiled radial field within 1 cm"), MaximumFieldError < .01);
    bool bCore = false, bTop = false;
    TMap<FIntVector, int32> Welded;
    TArray<int32> Ids;
    for (const FVector& V : G.Vertices)
    {
        bCore |= V.IsNearlyZero(); bTop |= V.Z > 0 && V.Z <= FCLSectionGeometry::DisplaySize * 100;
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
    const FVector Source = FCLSectionGeometry::SourceSurfacePoint(*Constant, 30, 40, 0);
    TestTrue(TEXT("Surface-distance chart samples the actual sphere"), FMath::Abs(Source.Z / 100 - 100 * FMath::Cos(.5)) < 1.e-5);
    const FVector Centre = FCLSectionGeometry::SurfacePoint(*Constant, 0, 0, 0, 100);
    const FVector Display = FCLSectionGeometry::SurfacePoint(*Constant, 30, 40, 0, 100);
    TestTrue(TEXT("Designer keeps a fixed one-third schematic curvature"),
        FMath::Abs((Centre.Z - Display.Z) * 3 - (Centre.Z - Source.Z * (FCLSectionGeometry::DisplaySize / Constant->RadiusMeters))) < .001);
    TestTrue(TEXT("Physical coordinates map into the fixed display frame"), FMath::Abs(Display.X - 30000) < .001 && FMath::Abs(Display.Y - 40000) < .001);
    const FVector2D Edge(FCLSectionGeometry::DisplaySize / 2, FCLSectionGeometry::DisplaySize / 2);
    for (double Perspective : {0.0, .00001, -.00001})
    {
        FMatrix Projection = FMatrix::Identity;
        Projection.M[2][1] = .0001; Projection.M[3][1] = -.5; Projection.M[2][3] = Perspective;
        for (double N : {0.0, .05, .2, .3, .4, .6, .9, 1.0})
        {
            const FVector GuidePosition = FCLSectionGeometry::StratumPoint(Edge, N);
            const FVector4 Clip = Projection.TransformFVector4(FVector4(GuidePosition, 1));
            const double ScreenY = .5 - .5 * Clip.Y / Clip.W;
            TestTrue(TEXT("Dragging inverts the exact rendered guide height under perspective"),
                FMath::Abs(FCLSectionGeometry::StratumAtScreenY(Edge, Projection, ScreenY) - N) < 1.e-8);
        }
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLSectionAreaTest, "Catalyst.Landform.Designer.SurfaceArea",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLSectionAreaTest::RunTest(const FString&)
{
    FCLPlanetRecipe Recipe = FCLPlanetCompiler::RollingPlains(); Recipe.RadiusMeters = 1000;
    FString Error; const auto Plan = FCLPlanetCompiler::Compile(Recipe, Error);
    const double Reference = FCLSectionGeometry::ReferenceSurfaceRadius(*Plan);
    for (double Width : {100.0, 500.0, 1000.0})
    {
        const auto G = FCLSectionGeometry::Build(*Plan, .7, Width);
        TestEqual(TEXT("Physical width and length use the requested square footprint"), G.HalfWidthMeters * 2, Width);
        TestEqual(TEXT("Every top vertex retains its actual spherical source"), G.SourceSurfacePoints.Num(), G.SurfaceVertexCount);
        const int32 N = FMath::RoundToInt(FMath::Sqrt(double(G.SurfaceVertexCount))) - 1;
        TestTrue(TEXT("Grid keeps five metre or finer spacing"), Width / N <= 5);
        TestEqual(TEXT("Left display edge stays fixed"), G.Vertices[0].X / 100, -FCLSectionGeometry::DisplaySize / 2);
        TestEqual(TEXT("Right display edge stays fixed"), G.Vertices[N].X / 100, FCLSectionGeometry::DisplaySize / 2);
        TestEqual(TEXT("Back display edge stays fixed"), G.Vertices[G.SurfaceVertexCount - 1].Y / 100, FCLSectionGeometry::DisplaySize / 2);
        for (const FVector& V : G.Vertices) TestTrue(TEXT("All displayed points are finite and above the core plane"), !V.ContainsNaN() && V.Z >= 0);
        // Axis arc lengths can be checked independently of the chart implementation.
        const FVector Centre = FCLSectionGeometry::SourceSurfacePoint(*Plan, 0, 0, .7).GetSafeNormal();
        const FVector Edge = FCLSectionGeometry::SourceSurfacePoint(*Plan, Width / 2, 0, .7).GetSafeNormal();
        const double Arc = FMath::Acos(FMath::Clamp(FVector::DotProduct(Centre, Edge), -1.0, 1.0)) * Reference;
        TestTrue(TEXT("Half-width is measured in actual great-circle metres"), FMath::Abs(Arc - Width / 2) < .00001);
        // A fixed physical location must not change when the view expands.
        const int32 CentreIndex = (N / 2) * (N + 1) + N / 2;
        TestTrue(TEXT("Changing viewing area preserves the sampled centre"), G.SourceSurfacePoints[CentreIndex].Equals(
            FCLSectionGeometry::SourceSurfacePoint(*Plan, 0, 0, .7), .001));
        const FVector A = FCLSectionGeometry::StratumPoint(FVector2D(FCLSectionGeometry::DisplaySize / 2, FCLSectionGeometry::DisplaySize / 2), .6);
        const FVector B = FCLSectionGeometry::StratumPoint(FVector2D(FCLSectionGeometry::DisplaySize / 2, FCLSectionGeometry::DisplaySize / 2), .5);
        TestTrue(TEXT("Vertical reference scale stays normalized to the full planet"), FMath::Abs((A.Z - B.Z) / 100 - 100) < .00001);
        FMatrix Projection = FMatrix::Identity; Projection.M[2][1] = .00001; Projection.M[2][3] = .000001;
        const FVector2D XY(FCLSectionGeometry::DisplaySize / 2, FCLSectionGeometry::DisplaySize / 2);
        for (double Height : {0.0, .05, .3, .6, .9, 1.0})
        {
            const FVector P = FCLSectionGeometry::StratumPoint(XY, Height);
            const FVector4 Clip = Projection.TransformFVector4(FVector4(P, 1));
            TestTrue(TEXT("Guide dragging remains invertible at every area size"), FMath::Abs(
                FCLSectionGeometry::StratumAtScreenY(XY, Projection, .5 - .5 * Clip.Y / Clip.W) - Height) < 1.e-8);
        }
    }
    return true;
}
#endif
