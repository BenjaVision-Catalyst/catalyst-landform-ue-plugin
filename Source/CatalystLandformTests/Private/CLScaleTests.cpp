#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Scene/CLScaleFixture.h"
#include "Scene/CLScene.h"
#include "CLPlanetStamp.h"
#include "VoxelQuery.h"
#include "VoxelLayers.h"
#include "Surface/VoxelSurfaceTypeTable.h"
#include "CLSurfaceInspection.h"

namespace
{
double Oracle(FVector P,double R)
{
    // Stable, independent local sphere equation; no subtraction of radii.
    double A=(2*R*P.Z+P.SizeSquared())/(FMath::Sqrt(FMath::Square(R+P.Z)+P.X*P.X+P.Y*P.Y)+R);
    auto Box=[](FVector Q,FVector E){const FVector V=Q.GetAbs()-E;return V.ComponentMax(FVector::ZeroVector).Length()+FMath::Min(0.,V.GetMax());};
    A=FMath::Min(A,Box(P-FVector(-5,-5,2.5),FVector(.75,1,3.5)));
    A=FMath::Min(A,Box(P-FVector(5,-5,2.5),FVector(.75,1,3.5)));
    const FVector Crown=P-FVector(0,-5,6);A=FMath::Min(A,(Crown-FVector(FMath::Clamp(Crown.X,-5.,5.),0,0)).Length()-1);
    A=FMath::Min(A,Box(P-FVector(12,0,1),FVector(.5,4,2)));
    A=FMath::Max(A,-Box(P-FVector(0,10,-6),FVector(4,4,1.5)));
    A=FMath::Max(A,-Box(P-FVector(0,10,-12),FVector(4,4,1.5)));
    return FMath::Min(A,(P-FVector(14,-10,9)).Length()-2);
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLScaleFieldTest,"Catalyst.Landform.Scale.CanonicalAndNativeWindows",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLScaleFieldTest::RunTest(const FString&)
{
    FString Error;double WorstCanonical=0,WorstNative=0,WorstFrame=0;int32 Samples=0;
    FVoxelDependencyCollector Dependencies(STATIC_FNAME("CL.ScaleOracle"));const auto Table=FVoxelSurfaceTypeTable::Get();FVoxelQuery Query(0,FVoxelLayers::Empty(),*Table,Dependencies);
    for(int32 Scale=0;Scale<4;++Scale)for(int32 Site=0;Site<4;++Site)
    {
        auto F=FCLScaleFixture::Make(FCLScaleFixture::Radius(Scale),Site);
        const auto Plan=FCLPlanetCompiler::Compile(F.Recipe,Error);if(!Plan){AddError(Error);return false;}
        const auto Scene=FCLScene::FromPlanet(Plan,Error);
        for(int32 Variant=0;Variant<3;++Variant)
        {
            auto W=F.Window;
            if(Variant){W.AnchorMeters=F.FeatureFrame.NativeToBodyMeters(FVector(625,-450,175)*Variant);W.BodyFromLocal=(F.FeatureFrame.BodyFromLocal*FQuat(FVector::ZAxisVector,FMath::DegreesToRadians(17.*Variant))).GetNormalized();}
            FCLPlanetStamp Stamp;Stamp.Radius=F.Recipe.RadiusMeters*100;Stamp.bUseRecipe=true;Stamp.Recipe=F.Recipe;Stamp.Window=W;
            const auto Native=StaticCastSharedPtr<FCLPlanetStampRuntime>(FVoxelStampRuntime::Create(nullptr,FVoxelStampRef::New(Stamp),nullptr));
            if(!Native||!Native->Scene){W.Validate(Error);AddError(FString::Printf(TEXT("Framed runtime refused scale %d / site %d / frame %d; quaternion %.17g; radius %.17g / %.17g; %s"),Scale,Site,Variant,W.BodyFromLocal.SizeSquared(),Stamp.Radius,Plan->RadiusMeters*100,*Error));return false;}
            TArray<FVector3d> Body;TArray<double> Expected;FVoxelDoubleVectorBuffer Local;
            for(int32 Z=-16;Z<=12;Z+=2)for(int32 Y=-12;Y<=16;Y+=2)for(int32 X=-8;X<=16;X+=2)
            {const FVector P(X+.173,Y+.317,Z+.239);Body.Add(F.FeatureFrame.NativeToBodyMeters(P*100));Expected.Add(Oracle(P,F.SurfaceRadiusMeters));}
            Local.Allocate(Body.Num());for(int32 I=0;I<Body.Num();++I){Local.Set(I,W.Frame().BodyToNativeCentimeters(Body[I]));WorstFrame=FMath::Max(WorstFrame,FVector::Distance(W.Frame().NativeToBodyMeters(Local[I]),Body[I]));}
            const auto Values=Native->SampleDistances(Query,Local);TArray<FCLSceneShapeSample> Shape;Scene->SampleShape({Body},Shape,Error);
            for(int32 I=0;I<Body.Num();++I){WorstCanonical=FMath::Max(WorstCanonical,FMath::Abs(Shape[I].FieldMeters-Expected[I]));WorstNative=FMath::Max(WorstNative,FMath::Abs(Values[I]*.01-Expected[I]));++Samples;}
        }
        for(int32 I=0;I<4;++I)
        {
            TArray<FVector> Point{F.Footing(I)};TArray<FCLSceneShapeSample> Zero;
            TestTrue(TEXT("Known analytic zero interface retains one-millimetre accuracy"),Scene->SampleShape({Point},Zero,Error)&&Zero.Num()==1&&FMath::Abs(Zero[0].FieldMeters)<.001);
            FCLSurfaceLanding Landing;TestTrue(TEXT("Each fixed-metre footing remains reachable at every radius and site"),FCLSurfaceInspection::FindLandingPoint(*Scene,F.Footing(I),Landing));
        }
    }
    TestTrue(TEXT("Canonical zero/field error stays below one millimetre"),WorstCanonical<.001);
    TestTrue(TEXT("Native float fields stay below one millimetre in the declared local band"),WorstNative<.001);
    TestTrue(TEXT("Reanchored and rotated frame round trips retain submillimetre canonical position"),WorstFrame<.001);
    AddInfo(FString::Printf(TEXT("%d fixed-metre samples / four radii / four sites / three frames: canonical %.9g m; native %.9g m; frame %.9g m"),Samples,WorstCanonical,WorstNative,WorstFrame));
    auto Invalid=FCLScaleFixture::Make(100).Window;Invalid.BodyFromLocal.W*=.999;TestFalse(TEXT("A scaled quaternion is not a rigid precision frame"),Invalid.Validate(Error));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLScaleNoiseTest,"Catalyst.Landform.Scale.NoiseFrameContinuity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLScaleNoiseTest::RunTest(const FString&)
{
    FString Error;double Worst=0;int32 Count=0;
    FVoxelDependencyCollector Dependencies(STATIC_FNAME("CL.ScaleNoise"));const auto Table=FVoxelSurfaceTypeTable::Get();FVoxelQuery Query(0,FVoxelLayers::Empty(),*Table,Dependencies);
    for(int32 Scale=0;Scale<4;++Scale)for(int32 Site=0;Site<4;++Site)
    {
        auto F=FCLScaleFixture::Make(FCLScaleFixture::Radius(Scale),Site);auto& R=F.Recipe;R.Volumes.bEnabled=false;
        for(auto& G:R.RadialGuides.Guides){if(G.Name==TEXT("SurfaceMin"))G.NormalizedHeight=(F.SurfaceRadiusMeters-2)/R.RadiusMeters;if(G.Name==TEXT("SurfaceMax"))G.NormalizedHeight=(F.SurfaceRadiusMeters+2)/R.RadiusMeters;}
        FCLNoiseLayer L;L.Name=TEXT("Canonical 3D phase witness");L.Seed=73;L.WavelengthMeters=F.SurfaceRadiusMeters/64;L.Minimum.OffsetMeters=-102;L.Maximum.OffsetMeters=-98;L.Octaves=3;R.NoiseLayers.Add(L);
        const auto Plan=FCLPlanetCompiler::Compile(R,Error);if(!Plan){AddError(Error);return false;}const auto Scene=FCLScene::FromPlanet(Plan,Error);
        TArray<FVector> Body;for(int32 Y=0;Y<8;++Y)for(int32 X=0;X<8;++X)Body.Add(F.FeatureFrame.NativeToBodyMeters(FVector(X*83.7-300,Y*71.3-260,17.3)));
        TArray<FCLSceneShapeSample> Expected;if(!Scene->SampleShape({Body},Expected,Error)){AddError(Error);return false;}
        for(int32 Variant=0;Variant<3;++Variant)
        {
            auto W=F.Window;W.AnchorMeters=F.FeatureFrame.NativeToBodyMeters(FVector(625,-450,175)*Variant);W.BodyFromLocal=(F.FeatureFrame.BodyFromLocal*FQuat(FVector::ZAxisVector,FMath::DegreesToRadians(17.*Variant))).GetNormalized();
            FCLPlanetStamp Stamp;Stamp.Radius=R.RadiusMeters*100;Stamp.bUseRecipe=true;Stamp.Recipe=R;Stamp.Window=W;
            const auto Native=StaticCastSharedPtr<FCLPlanetStampRuntime>(FVoxelStampRuntime::Create(nullptr,FVoxelStampRef::New(Stamp),nullptr));if(!Native){AddError(TEXT("Noise window refused"));return false;}
            FVoxelDoubleVectorBuffer Local;Local.Allocate(Body.Num());for(int32 I=0;I<Body.Num();++I)Local.Set(I,W.Frame().BodyToNativeCentimeters(Body[I]));const auto Values=Native->SampleDistances(Query,Local);
            for(int32 I=0;I<Body.Num();++I){Worst=FMath::Max(Worst,FMath::Abs(Values[I]*.01-Expected[I].FieldMeters));++Count;}
        }
    }
    AddInfo(FString::Printf(TEXT("%d canonical noise/frame witnesses; maximum field difference %.9g m; wavelength radius/64, three octaves"),Count,Worst));return TestTrue(TEXT("Existing global noise retains its canonical phase across native frame changes"),Worst<.001);
}
#endif
