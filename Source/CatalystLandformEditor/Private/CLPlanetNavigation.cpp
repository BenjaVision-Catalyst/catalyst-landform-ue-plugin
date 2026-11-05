#include "CLPlanetNavigation.h"
void FCLPlanetNavigation::Focus(const FBox& Bounds)
{FocusBounds=Bounds;Zoom=1;Pan=FVector2D::ZeroVector;Pivot=Bounds.GetCenter();}
void FCLPlanetNavigation::ResetFrame()
{FocusBounds.Reset();Zoom=1;Pan=FVector2D::ZeroVector;Pivot=FVector::ZeroVector;}
void FCLPlanetNavigation::Orbit(FVector2D Pixels)
{Yaw+=Pixels.X*.3;Elevation=FMath::Clamp(Elevation+Pixels.Y*.3,-85.,85.);}
void FCLPlanetNavigation::PanBy(FVector2D Pixels)
{
    if(!FocusBounds.IsSet()&&Zoom>=1)return;
    Pan.X=FMath::Clamp(Pan.X-Pixels.X*PanScale.X,-PanLimit.X,PanLimit.X);
    Pan.Y=FMath::Clamp(Pan.Y+Pixels.Y*PanScale.Y,-PanLimit.Y,PanLimit.Y);
}
void FCLPlanetNavigation::Dolly(double Steps)
{
    if(FocusBounds.IsSet()){Zoom=FMath::Clamp(Zoom*FMath::Exp(FMath::Clamp(-Steps*.12,-10.,10.)),.05,4.);return;}
    const double Previous=Zoom;Zoom=FMath::Clamp(Zoom*FMath::Exp(FMath::Clamp(-Steps*.12,-10.,10.)),.25,1.);
    if(Zoom>=1){Pan=FVector2D::ZeroVector;Pivot=FVector::ZeroVector;}
    else if(Zoom>Previous)Pan*=((1-Zoom)/(1-Previous));
}
FCLSectionCamera FCLPlanetNavigation::Camera(double H,double V)
{
    H=FMath::Max(.01,H);V=FMath::Max(.01,V);
    const double R=FocusBounds.IsSet()?FMath::Max(10.,FocusBounds->GetExtent().Size()):Radius;
    const double Distance=FMath::Max(FocusBounds.IsSet()?R*.05:Radius*1.1,R/FMath::Sin(FMath::Atan(FMath::Min(H*.72,V*.85)))*Zoom);
    const FVector Back=FRotator(Elevation,Yaw,0).Vector();
    FCLSectionCamera C;C.Rotation=(-Back).Rotation();const FRotationMatrix Axes(C.Rotation);
    Right=Axes.GetUnitAxis(EAxis::Y);Up=Axes.GetUnitAxis(EAxis::Z);
    PanLimit=FocusBounds.IsSet()?FVector2D(R*4):Zoom<1?FVector2D(FMath::Max(0.,Radius-Distance*H),FMath::Max(0.,Radius-Distance*V)):FVector2D::ZeroVector;
    Pan.X=FMath::Clamp(Pan.X,-PanLimit.X,PanLimit.X);Pan.Y=FMath::Clamp(Pan.Y,-PanLimit.Y,PanLimit.Y);
    Pivot=(FocusBounds.IsSet()?FocusBounds->GetCenter():FVector::ZeroVector)+Right*Pan.X+Up*Pan.Y;
    PanScale=FVector2D(Distance*H,Distance*V)*.002;
    C.Target=Pivot;C.Eye=Pivot+Back*Distance;return C;
}
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLPlanetNavigationTest,"Catalyst.Landform.PlanetBuilder.Navigation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLPlanetNavigationTest::RunTest(const FString&)
{
    for(double Aspect:{.5,1.,2.5})
    {
        FCLPlanetNavigation N;const auto Initial=N.Camera(.577,.577/Aspect);const double Distance=Initial.Eye.Size();
        N.PanBy({10000,10000});TestTrue(TEXT("Fit view stays centred"),N.Pivot.IsNearlyZero());
        for(int32 I=0;I<100;++I){N.Orbit({13,3});const auto C=N.Camera(.577,.577/Aspect);TestTrue(TEXT("Orbit preserves fitted distance and centre"),FMath::IsNearlyEqual(C.Eye.Size(),Distance,1.e-7)&&C.Target.IsNearlyZero());}
        N.Dolly(100);N.Camera(.577,.577/Aspect);N.PanBy({10000,-10000});const auto C=N.Camera(.577,.577/Aspect);
        TestTrue(TEXT("Crop pan cannot leave outer sphere"),C.Target.Size()<FCLPlanetNavigation::Radius);
        const double PannedDistance=C.Eye.Size();
        for(int32 I=0;I<50;++I){N.Orbit({13,2});const auto Orbit=N.Camera(.577,.577/Aspect);TestTrue(TEXT("Cropped orbit also preserves scale"),FMath::IsNearlyEqual(Orbit.Eye.Size(),PannedDistance,1.e-7));}
        N.Dolly(-100);const auto Reset=N.Camera(.577,.577/Aspect);TestTrue(TEXT("Zooming out restores the centre"),Reset.Target.IsNearlyZero()&&N.Zoom==1);
    }
    FCLPlanetNavigation N;const FBox B(FVector(90000,0,0),FVector(90100,400,50));N.Focus(B);const auto Focus=N.Camera(.577,.577);
    TestTrue(TEXT("Cave framing targets the network bounds"),Focus.Target.Equals(B.GetCenter()));TestTrue(TEXT("Cave framing removes the globe distance floor"),(Focus.Eye-Focus.Target).Size()<2000);
    N.Orbit({30,10});N.Dolly(3);N.PanBy({10,5});const auto Close=N.Camera(.577,.577);TestTrue(TEXT("Focused zoom and pan stay finite"),!Close.Eye.ContainsNaN()&&(Close.Eye-Close.Target).Size()<(Focus.Eye-Focus.Target).Size());
    N.ResetFrame();const auto Globe=N.Camera(.577,.577);TestTrue(TEXT("Frame restores whole planet"),Globe.Target.IsNearlyZero()&&!N.FocusBounds.IsSet());return true;
}
#endif
