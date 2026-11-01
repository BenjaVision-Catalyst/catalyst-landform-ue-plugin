#include "Misc/AutomationTest.h"
#include "CLSurfaceInspection.h"
#include "Model/CLPlanetRecipe.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLSurfaceLandingTest,"Catalyst.Landform.Inspection.RadialLanding",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLSurfaceLandingTest::RunTest(const FString&)
{
    auto R=FCLPlanetCompiler::BasePlanet();R.RadiusMeters=1000;R.BaseSurfaceNormalized=.5;R.NoiseLayers.Reset();R.BaseStructure.Reset();R.RegionGeneration.bEnabled=false;
    FString Error;const auto P=FCLPlanetCompiler::Compile(R,Error);if(!TestTrue(TEXT("Sphere compiles"),P.IsValid()))return false;
    for(FVector D:{FVector::ZAxisVector,-FVector::ZAxisVector,FVector::XAxisVector,FVector::YAxisVector,FVector(1,1,0).GetSafeNormal()})
    {
        FCLSurfaceLanding L;TestTrue(TEXT("Poles, equator and patch boundary admit footing"),FCLSurfaceInspection::FindLanding(*P,D,L));
        TestTrue(TEXT("No displacement on a level surface"),L.Direction.Equals(D,1.e-8));TestEqual(TEXT("No adjustment"),L.AdjustmentMeters,0.);
        TestTrue(TEXT("Exact recipe surface in UE units"),FMath::IsNearlyEqual(L.PositionCm.Size(),50000.,.01));TestTrue(TEXT("Outward normal"),FVector::DotProduct(L.Normal,D)>.999);
    }
    FCLSurfaceLanding L;TestFalse(TEXT("Centre is not a surface direction"),FCLSurfaceInspection::FindLanding(*P,FVector::ZeroVector,L));
    R.Coordinates=ECLTerrainCoordinates::LocalPatch;const auto Patch=FCLPlanetCompiler::Compile(R,Error);if(Patch)TestFalse(TEXT("A local patch is not a globe"),FCLSurfaceInspection::FindLanding(*Patch,FVector::ZAxisVector,L));
    return true;
}
