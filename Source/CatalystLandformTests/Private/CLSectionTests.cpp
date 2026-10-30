#if WITH_DEV_AUTOMATION_TESTS
#include "CLSectionGeometry.h"
#include "CLSectionNavigation.h"
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
    TestEqual(TEXT("Every Designer opens at 500m coverage"), FCLSectionGeometry::DefaultWidth(*Plan), 500.0);
    TestEqual(TEXT("Default generated patch samples 500m"), FCLSectionGeometry::Build(*Plan, 0).HalfWidthMeters * 2, 500.0);
    FCLPlanetRecipe Small = Recipe; Small.RadiusMeters = 100;
    TestEqual(TEXT("Default coverage is independent of planet radius"), FCLSectionGeometry::DefaultWidth(*FCLPlanetCompiler::Compile(Small, Error)), 500.0);
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLSectionNavigationTest, "Catalyst.Landform.Designer.BoundedNavigation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLSectionNavigationTest::RunTest(const FString&)
{
    const double S = FCLSectionGeometry::DisplaySize * 100;
    auto Bounds = [&](const FCLSectionCamera& C, double H, double V)
    {
        const FRotationMatrix Axes(C.Rotation);
        FBox2D B(ForceInit);
        for (double X : {-.5 * S, .5 * S}) for (double Y : {-.5 * S, .5 * S}) for (double Z : {0.0, S})
        {
            const FVector P = FVector(X, Y, Z) - C.Eye;
            const double Depth = FVector::DotProduct(P, Axes.GetUnitAxis(EAxis::X));
            TestTrue(TEXT("Every core-to-sky corner remains in front of the camera"), Depth > 0);
            B += FVector2D(FVector::DotProduct(P, Axes.GetUnitAxis(EAxis::Y)) / (Depth * H),
                FVector::DotProduct(P, Axes.GetUnitAxis(EAxis::Z)) / (Depth * V));
        }
        return B;
    };
    FCLSectionNavigation N;
    N.PanBy(FVector2D(1000, 1000));
    TestTrue(TEXT("Default fitted view cannot pan"), N.Pan.IsNearlyZero());
    N.Orbit(FVector2D(1.e8, 1.e8)); N.Dolly(1.e8);
    TestTrue(TEXT("Orbit elevation is bounded"), N.Elevation >= 15 && N.Elevation <= 75);
    TestEqual(TEXT("Zoom allows close inspection"), N.Zoom, .3);
    N.Dolly(-1.e8); TestEqual(TEXT("Zoom out has a finite limit"), N.Zoom, 2.2);
    for (double Aspect : {.45, 1.0, 1.6, 3.0}) for (double Yaw : {-180.0, -120.0, -60.0, 0.0, 60.0, 120.0, 180.0})
        for (double Elevation : {15.0, 32.0, 75.0}) for (double PanSign : {-1.0, 1.0}) for (double Zoom : {.3, .5, .75, 1.0, 2.2})
        {
            N.Yaw = Yaw; N.Elevation = Elevation; N.Pan = FVector2D::ZeroVector; N.Zoom = Zoom;
            const double H = FMath::Tan(FMath::DegreesToRadians(30.0)), V = H / Aspect;
            const auto Centred = Bounds(N.Camera(H, V), H, V);
            TestTrue(TEXT("Neutral projected bounds are horizontally centred"), FMath::Abs(Centred.Min.X + Centred.Max.X) < 1.e-8);
            TestTrue(TEXT("Neutral projected bounds are vertically centred"), FMath::Abs(Centred.Min.Y + Centred.Max.Y) < 1.e-8);
            N.PanBy(FVector2D(PanSign * 1.e8, PanSign * 1.e8));
            const auto Panned = Bounds(N.Camera(H, V), H, V);
            if (N.bCanPanX)
            {
                TestTrue(TEXT("Horizontal panning requires clipped edges"), Centred.Min.X < -1 && Centred.Max.X > 1);
                TestTrue(TEXT("Horizontal pan cannot move the cutaway out of the viewport"), Panned.Min.X <= -1 + 1.e-8 && Panned.Max.X >= 1 - 1.e-8);
            }
            else
            {
                TestEqual(TEXT("Horizontal pan resets when the axis fits"), N.Pan.X, 0.0);
                TestTrue(TEXT("Fitted horizontal view remains centred"), FMath::Abs(Panned.Min.X + Panned.Max.X) < 1.e-8);
            }
            if (N.bCanPanY)
            {
                TestTrue(TEXT("Vertical panning requires clipped edges"), Centred.Min.Y < -1 && Centred.Max.Y > 1);
                TestTrue(TEXT("Vertical pan cannot move the cutaway out of the viewport"), Panned.Min.Y <= -1 + 1.e-8 && Panned.Max.Y >= 1 - 1.e-8);
            }
            else
            {
                TestEqual(TEXT("Vertical pan resets when the axis fits"), N.Pan.Y, 0.0);
                TestTrue(TEXT("Fitted vertical view remains centred"), FMath::Abs(Panned.Min.Y + Panned.Max.Y) < 1.e-8);
            }
            if (Zoom >= 1) TestTrue(TEXT("Zoomed out view fits completely and cannot pan"),
                !N.bCanPanX && !N.bCanPanY && Panned.Min.X >= -1 && Panned.Max.X <= 1 && Panned.Min.Y >= -1 && Panned.Max.Y <= 1);
        }
    FCLSectionNavigation Return;
    const double H = FMath::Tan(FMath::DegreesToRadians(30.0)), V = H / 1.6;
    Return.Dolly(100); Return.Camera(H, V); Return.PanBy(FVector2D(1000, 1000));
    TestTrue(TEXT("Close inspection enables at least one pan axis"), Return.bCanPanX || Return.bCanPanY);
    double PreviousOffset = TNumericLimits<double>::Max();
    for (int32 I = 0; I < 20; ++I)
    {
        const auto Projected = Bounds(Return.Camera(H, V), H, V);
        // Centre is a screen-space requirement. Perspective can increase the
        // world-space target offset while its visible displacement decreases.
        const double Offset = ((Projected.Min + Projected.Max) * .5).Size();
        TestTrue(TEXT("Zooming out progressively recentres a panned view"), Offset <= PreviousOffset + 1.e-8);
        PreviousOffset = Offset; Return.Dolly(-1);
    }
    TestTrue(TEXT("Returning to a fitted view clears pan"), Return.Pan.IsNearlyZero() && !Return.bCanPanX && !Return.bCanPanY);

    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLStableOrbitTest, "Catalyst.Landform.Designer.StableOrbitZoom",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCLStableOrbitTest::RunTest(const FString&)
{
    const FVector Pivot(0, 0, FCLSectionGeometry::DisplaySize * 50);
    for (double Aspect : {.45, 1.0, 1.6, 3.0}) for (double Zoom : {.3, .5, .75, 1.0, 2.2})
    {
        FCLSectionNavigation Nav; Nav.Zoom = Zoom;
        const double H = FMath::Tan(FMath::DegreesToRadians(30.0)), V = H / Aspect;
        const auto Initial = Nav.Camera(H, V);
        const double Distance = (Initial.Eye - Initial.Target).Size();
        const double ExpectedXScale = 1000 / (Distance * H), ExpectedYScale = 1000 / (Distance * V);
        // Exercise the real orbit operation over complete yaw circuits and the
        // full elevation range, including close zoom's near-distance guard.
        for (double Elevation : {15.0, 32.0, 45.0, 60.0, 75.0})
        {
            Nav.Orbit(FVector2D(0, (Elevation - Nav.Elevation) / .3));
            for (int32 I = 0; I < 24; ++I)
            {
                Nav.Orbit(FVector2D(50, 0));
                auto C = Nav.Camera(H, V);
                Nav.PanBy(FVector2D(20, -10)); C = Nav.Camera(H, V);
                const FRotationMatrix Axes(C.Rotation);
                const FVector Forward = Axes.GetUnitAxis(EAxis::X), Right = Axes.GetUnitAxis(EAxis::Y), Up = Axes.GetUnitAxis(EAxis::Z);
                const double Depth = FVector::DotProduct(Pivot - C.Eye, Forward);
                TestTrue(TEXT("Orbit preserves camera-to-target distance"), FMath::Abs((C.Eye - C.Target).Size() - Distance) < 1.e-7);
                TestTrue(TEXT("Orbit preserves depth of the planet display centre"), FMath::Abs(Depth - Distance) < 1.e-7);
                TestEqual(TEXT("Orbit never changes the chosen zoom"), Nav.Zoom, Zoom);
                auto Project = [&](FVector P)
                {
                    P -= C.Eye; const double Z = FVector::DotProduct(P, Forward);
                    return FVector2D(FVector::DotProduct(P, Right) / (Z * H), FVector::DotProduct(P, Up) / (Z * V));
                };
                // View-aligned reference segments isolate magnification from
                // the natural foreshortening of a rotating terrain surface.
                const double XScale = (Project(Pivot + Right * 500) - Project(Pivot - Right * 500)).Size();
                const double YScale = (Project(Pivot + Up * 500) - Project(Pivot - Up * 500)).Size();
                TestTrue(TEXT("Horizontal magnification stays fixed throughout orbit"), FMath::Abs(XScale - ExpectedXScale) < 1.e-10);
                TestTrue(TEXT("Vertical magnification stays fixed throughout orbit"), FMath::Abs(YScale - ExpectedYScale) < 1.e-10);
            }
        }
    }
    return true;
}
#endif
