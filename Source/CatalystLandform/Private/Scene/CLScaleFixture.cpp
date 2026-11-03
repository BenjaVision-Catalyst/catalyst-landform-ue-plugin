#include "Scene/CLScaleFixture.h"

double FCLScaleFixture::Radius(int32 I)
{const double Values[]={100,1000,3389500,6371000};return Values[FMath::Clamp(I,0,3)];}
FString FCLScaleFixture::RadiusName(int32 I)
{const TCHAR* Names[]={TEXT("100 m"),TEXT("1 km"),TEXT("Mars scale"),TEXT("Earth scale")};return Names[FMath::Clamp(I,0,3)];}
FVector FCLScaleFixture::SiteDirection(int32 I)
{
    const FVector2D Degrees[]={FVector2D(0,0),FVector2D(89.999,38),FVector2D(-89.999,-73),FVector2D(35,179.999)};
    const auto D=Degrees[FMath::Clamp(I,0,3)];const double P=FMath::DegreesToRadians(D.X),L=FMath::DegreesToRadians(D.Y);
    return FVector(FMath::Cos(P)*FMath::Cos(L),FMath::Cos(P)*FMath::Sin(L),FMath::Sin(P));
}
FCLScaleFixture FCLScaleFixture::Make(double Radius,int32 Site)
{
    FCLScaleFixture F;F.SurfaceRadiusMeters=Radius;auto& R=F.Recipe;R=FCLPlanetCompiler::BasePlanet();R.SchemaVersion=18;
    R.Name=TEXT("Fixed-metre scale proof");R.RadiusMeters=Radius+100;R.BaseSurfaceNormalized=Radius/R.RadiusMeters;
    R.NoiseLayers.Reset();R.Tectonics.bEnabled=false;R.Continents.bEnabled=false;
    for(auto& G:R.RadialGuides.Guides)
    {if(G.Name==TEXT("SurfaceMin")||G.Name==TEXT("SurfaceMax")||G.Name==TEXT("OceanHeight"))G.NormalizedHeight=R.BaseSurfaceNormalized;}
    R.SurfaceStack=FCLSurfaceStack::MakeDefault();R.SurfaceStack.DefaultSurfaceId=TEXT("Soil");R.SurfaceStack.Layers.Reset();R.Volumes.bEnabled=true;
    const FVector Up=SiteDirection(Site);const FVector East=FVector(-Up.Y,Up.X,0).GetSafeNormal(1.e-12,FVector::YAxisVector);
    F.FeatureFrame.AnchorMeters=Up*Radius;F.FeatureFrame.BodyFromLocal=FRotationMatrix::MakeFromXZ(East,Up).ToQuat();
    F.Window.bEnabled=true;F.Window.AnchorMeters=F.FeatureFrame.AnchorMeters;F.Window.BodyFromLocal=F.FeatureFrame.BodyFromLocal;
    auto Add=[&](int32 Id,const TCHAR* Name,FVector Center,ECLSolidPrimitive Shape,ECLSolidComposition Mode,FVector Size,double Length=0,FRotator Rotation=FRotator::ZeroRotator)
    {
        FCLSolidOperation O;O.Id=FGuid(0x434C5343,0,0,Id);O.Name=Name;O.Primitive=Shape;O.Composition=Mode;
        O.CenterMeters=F.FeatureFrame.NativeToBodyMeters(Center*100);O.Rotation=(F.FeatureFrame.BodyFromLocal*Rotation.Quaternion()).Rotator();
        O.HalfExtentsMeters=Size;O.RadiusMeters=Size.X;O.HalfLengthMeters=Length;O.SurfaceId=TEXT("Rock");R.Volumes.Operations.Add(O);
    };
    Add(1,TEXT("West pier"),FVector(-5,-5,2.5),ECLSolidPrimitive::Box,ECLSolidComposition::Union,FVector(.75,1,3.5));
    Add(2,TEXT("East pier"),FVector(5,-5,2.5),ECLSolidPrimitive::Box,ECLSolidComposition::Union,FVector(.75,1,3.5));
    Add(3,TEXT("Arch crown"),FVector(0,-5,6),ECLSolidPrimitive::Capsule,ECLSolidComposition::Union,FVector(1),5,FRotator(90,0,0));
    Add(4,TEXT("One metre wall"),FVector(12,0,1),ECLSolidPrimitive::Box,ECLSolidComposition::Union,FVector(.5,4,2));
    Add(5,TEXT("Upper cave"),FVector(0,10,-6),ECLSolidPrimitive::Box,ECLSolidComposition::Subtract,FVector(4,4,1.5));
    Add(6,TEXT("Lower cave"),FVector(0,10,-12),ECLSolidPrimitive::Box,ECLSolidComposition::Subtract,FVector(4,4,1.5));
    Add(7,TEXT("Detached rock"),FVector(14,-10,9),ECLSolidPrimitive::Sphere,ECLSolidComposition::Union,FVector(2));
    FCLGeologicalUnit Rock;Rock.Name=TEXT("Bedrock");Rock.RadiusInterval=FVector2D(0,(Radius-3)/R.RadiusMeters);Rock.FeatherMeters=.25;R.Volumes.GeologicalUnits.Add(Rock);
    return F;
}
FVector FCLScaleFixture::Footing(int32 I) const
{
    const double Z=-25/(SurfaceRadiusMeters+FMath::Sqrt(SurfaceRadiusMeters*SurfaceRadiusMeters-25));
    const FVector Points[]={FVector(0,-5,Z),FVector(0,10,-7.5),FVector(0,10,-13.5),FVector(14,-10,11)};
    return FeatureFrame.NativeToBodyMeters(Points[FMath::Clamp(I,0,3)]*100);
}
