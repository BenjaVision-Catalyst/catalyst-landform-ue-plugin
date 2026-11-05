#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Surface/CLSurfaceCompiler.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLSurfaceNormalScaleTest,"Catalyst.Landform.Surfaces.NormalScale",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLSurfaceNormalScaleTest::RunTest(const FString&)
{
    for(const FVector Direction:{FVector::ZAxisVector,FVector(1,2,3).GetSafeNormal()})
    {
        FVector Reference=FVector::ZeroVector;
        for(double Radius:{1.,10.,25.,100.,1000.,3425000.})
        {
            TArray<FVector> D;CLSurfaceProbe(Direction,D);TArray<double> H;
            for(const auto& P:D)H.Add(-.25*Radius);
            const auto Sphere=CLSurfaceNormal(D,H,Radius,0);
            TestTrue(TEXT("A spherical floor has a radial normal at every physical scale"),FVector::DotProduct(Sphere,Direction)>1-1.e-8);
            for(int32 I=0;I<H.Num();++I)H[I]+=Radius*.05*D[I].X;
            const auto Relief=CLSurfaceNormal(D,H,Radius,0);
            if(Reference.IsNearlyZero())Reference=Relief;
            TestTrue(TEXT("Uniform rescaling preserves relief slope"),Relief.Equals(Reference,1.e-8));
        }
    }
    return !HasAnyErrors();
}
#endif
