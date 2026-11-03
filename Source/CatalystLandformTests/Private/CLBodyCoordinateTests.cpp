#if WITH_DEV_AUTOMATION_TESTS
#include "Scene/CLBodyCoordinates.h"
#include "Misc/AutomationTest.h"
#include "Async/Async.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GeoReferencingSystem.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/ReplicatedState.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLBodyCoordinateContractTest,"Catalyst.Landform.Coordinates.Admission",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLBodyCoordinateContractTest::RunTest(const FString&)
{
    const FCLReferenceFigure Valid{TEXT("earth"),TEXT("earth-fixed"),TEXT("ellipsoid-only"),6378137,6356752.314245179};
    FString Error;auto Context=FCLBodyCoordinates::TryCreate(Valid,Error);
    if(!Context){AddError(Error);return false;}
    for(int I=0;I<9;++I)
    {
        auto F=Valid;
        switch(I){case 0:F.BodyId.Reset();break;case 1:F.FrameId=TEXT(" ");break;case 2:F.DatumId.Reset();break;
        case 3:F.EquatorialRadiusMeters=std::numeric_limits<double>::infinity();break;case 4:F.PolarRadiusMeters=std::numeric_limits<double>::quiet_NaN();break;
        case 5:F.PolarRadiusMeters=0;break;case 6:F.PolarRadiusMeters=F.EquatorialRadiusMeters+1;break;case 7:F.PolarRadiusMeters=F.EquatorialRadiusMeters*.98;break;case 8:F.EquatorialRadiusMeters=1.e10;break;}
        TestFalse(TEXT("Invalid figure or missing identity refuses native context"),FCLBodyCoordinates::TryCreate(F,Error).IsValid());TestFalse(TEXT("Refusal has a reason"),Error.IsEmpty());
    }
    const FVector Sentinel(11,22,33);FVector P=Sentinel,N=Sentinel;
    for(int I=0;I<9;++I)
    {
        FCLGeodeticCoordinate G;
        switch(I){case 0:G.HeightReference=ECLHeightReference::AreoidMeters;break;case 1:G.HeightReference=ECLHeightReference::RadialMeters;break;case 2:G.HeightReference=ECLHeightReference::NormalizedGuide;break;
        case 3:G.LatitudeDegrees=90.00001;break;case 4:G.LongitudeEastDegrees=180.00001;break;case 5:G.HeightMeters=10001;break;case 6:G.HeightMeters=-10001;break;
        case 7:G.LongitudeEastDegrees=std::numeric_limits<double>::infinity();break;case 8:G.HeightMeters=std::numeric_limits<double>::quiet_NaN();break;}
        TestFalse(TEXT("Unknown height or invalid coordinate refuses conversion"),Context->ToBody(G,P,N,Error));
        TestTrue(TEXT("Failure preserves both outputs"),P==Sentinel&&N==Sentinel);TestFalse(TEXT("Conversion refusal has a reason"),Error.IsEmpty());
    }
    FCLGeodeticCoordinate G{12,34,56};
    for(const FVector Bad:{FVector::ZeroVector,FVector(1,2,3),FVector(1.e10,0,0),FVector(std::numeric_limits<double>::infinity(),0,0)})
    {
        TestFalse(TEXT("Undefined/interior/out-of-band body point refuses"),Context->FromBody(Bad,G,N,Error));
        TestTrue(TEXT("Failed inverse preserves outputs"),G.LatitudeDegrees==12&&G.LongitudeEastDegrees==34&&G.HeightMeters==56&&N==Sentinel);
    }
    TestTrue(TEXT("Valid call clears prior error"),Context->ToBody({},P,N,Error)&&Error.IsEmpty());
    const bool OtherThread=Async(EAsyncExecution::Thread,[&](){FVector A,B;FString E;return Context->ToBody({},A,B,E);}).Get();
    TestFalse(TEXT("A context is not shared across query threads"),OtherThread);
    TestTrue(TEXT("Owner remains usable after refusal"),Context->ToBody({},P,N,Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLNativeGeoAuditTest,"Catalyst.Landform.Coordinates.NativeGeoAudit",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLNativeGeoAuditTest::RunTest(const FString&)
{
    auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,TEXT("CLNativeGeoAudit"),nullptr,true,ERHIFeatureLevel::Num,&Values);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Native=World->SpawnActor<AGeoReferencingSystem>();Native->PlanetShape=EPlanetShape::RoundPlanet;Native->bOriginAtPlanetCenter=true;Native->ApplySettings();
    FString Error;auto Context=FCLBodyCoordinates::TryCreate({TEXT("earth"),TEXT("earth-fixed"),TEXT("wgs84-figure"),6378137,6356752.314245179},Error);
    for(const FCLGeodeticCoordinate G:{FCLGeodeticCoordinate{0,0,0},{45,45,10},{-80,-179.9999,-20},{89.999999,120,100}})
    {
        FVector NativeP,P,N;Native->GeographicToECEF(FGeographicCoordinates(G.LongitudeEastDegrees,G.LatitudeDegrees,G.HeightMeters),NativeP);
        TestTrue(TEXT("Native Earth actor agrees with direct bundled PROJ"),Context&&Context->ToBody(G,P,N,Error)&&FVector::Distance(P,NativeP)<.001);
        FGeographicCoordinates Back;Native->ECEFToGeographic(P,Back);TestTrue(TEXT("Native Earth actor retains ellipsoid height"),FMath::Abs(Back.Altitude-G.HeightMeters)<.001);
    }
    TGuardValue<int32> RebasePolicy(FRepMovement::EnableMultiplayerWorldOriginRebasing,1);
    for(const FIntVector Origin:{FIntVector(50000,-30000,10000),FIntVector::ZeroValue})
    {
        TestTrue(TEXT("Native world applies the Earth audit origin"),World->SetNewWorldOrigin(Origin));
        FVector ECEF(3180000,3190000,4480000),Engine,Back;
        Native->ECEFToEngine(ECEF,Engine);
        const FVector ZeroOrigin=FVector(ECEF.X,-ECEF.Y,ECEF.Z)*100;
        TestTrue(TEXT("Pinned native GeoReferencing returns zero-origin centimetres with Y parity"),Engine.Equals(ZeroOrigin,.001));
        { TGuardValue<int32> Disabled(FRepMovement::EnableMultiplayerWorldOriginRebasing,0);
          TestTrue(TEXT("Native rebase helpers explicitly depend on the multiplayer rebasing policy"),UGameplayStatics::RebaseZeroOriginOntoLocal(World,Engine)==Engine); }
        const FVector Local=UGameplayStatics::RebaseZeroOriginOntoLocal(World,Engine);
        TestTrue(TEXT("Native UE rebase helper supplies the explicit local-origin boundary"),Local.Equals(ZeroOrigin-FVector(Origin),.001));
        Native->EngineToECEF(UGameplayStatics::RebaseLocalOriginOntoZero(World,Local),Back);
        TestTrue(TEXT("Native Earth conversion plus native rebase helpers preserves ECEF"),FVector::Distance(ECEF,Back)<.001);
        AddInfo(FString::Printf(TEXT("Origin %s: raw zero/local difference %.9g cm; native rebased ECEF round trip %.9g m"),*Origin.ToString(),FVector::Distance(Engine,Local),FVector::Distance(ECEF,Back)));
    }
    // Use the actor's own database/file-system context, not a second raw CRS context.
    const FString Mars=TEXT("+proj=longlat +a=3396190 +b=3376200 +type=crs");
    TestTrue(TEXT("Native actor recognizes the custom Mars geographic CRS"),Native->IsCRSStringValid(Mars,Error));
    AddExpectedErrorPlain(TEXT("Source and target ellipsoid do not belong to the same celestial body"),EAutomationExpectedErrorFlags::Contains,2);
    AddExpectedErrorPlain(TEXT("AGeoReferencingSystem::BuildProjection failed in proj_create_crs_to_crs"),EAutomationExpectedErrorFlags::Contains,2);
    Native->GeographicCRS=Mars;Native->ProjectedCRS=TEXT("+proj=eqc +a=3396190 +b=3376200 +type=crs");Native->ApplySettings();
    TestTrue(TEXT("The native actor retains the Mars figure; the failure is its fixed Earth target"),FMath::Abs(Native->GetGeographicEllipsoidMaxRadius()-3396190)<.001&&FMath::Abs(Native->GetGeographicEllipsoidMinRadius()-3376200)<.001);
    AddInfo(FString::Printf(TEXT("Bundled PROJ %s; native Earth conversion/parity and explicit native rebase composition pass; actor custom Mars configuration requires exactly two celestial-body refusals"),*FCLBodyCoordinates::NativeVersion()));
    Native->Destroy();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    return true;
}
#endif
