#include "Misc/AutomationTest.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Presentation/CLRigidActorPresentation.h"
#include "Presentation/CLPlanetProjection.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLRigidActorPresentationTest,"Catalyst.Landform.Presentation.RigidActors",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLRigidActorPresentationTest::RunTest(const FString&)
{
    UWorld* World=GEditor->GetEditorWorldContext().World();
    auto* Source=World->SpawnActor<AActor>();
    auto* Root=NewObject<USceneComponent>(Source);Source->SetRootComponent(Root);Root->SetMobility(EComponentMobility::Movable);Root->RegisterComponent();
    auto* Mesh=NewObject<UStaticMeshComponent>(Source);Source->AddInstanceComponent(Mesh);Mesh->SetupAttachment(Root);
    Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);Mesh->RegisterComponent();
    FCLPlanetProjectionSettings S;S.ReferenceRadiusCm=100000;S.Curvature=.15;S.MinAltitudeCm=-10000;S.MaxAltitudeCm=200000;
    FCLPlanetProjection P;TestTrue(TEXT("Curvature chart constructs"),FCLPlanetProjection::TryCreate(S,P));
    FString Error;auto Visual=FCLRigidActorPresentation::TryCreate(*Source,Error);
    TestTrue(TEXT("Rigid copy constructs"),Visual.IsValid());
    if(Visual)
    {
        for(double Angle:{0.,.3,.6})
        {
            const FVector Up(FMath::Sin(Angle),0,FMath::Cos(Angle));
            Source->SetActorLocationAndRotation(Up*100100,FQuat::FindBetweenNormals(FVector::ZAxisVector,Up));
            const auto Pose=Source->GetActorTransform();
            TestTrue(TEXT("Moving source is accepted"),Visual->Update(P,Error));
            TestTrue(TEXT("Canonical transform is unchanged"),Source->GetActorTransform().Equals(Pose,0));
            TestTrue(TEXT("Canonical collision stays enabled"),Mesh->GetCollisionEnabled()==ECollisionEnabled::QueryAndPhysics);
            FVector Expected;P.TryProjectPosition(Pose.GetLocation(),Expected);
            TestTrue(TEXT("Visual follows the same chart"),Visual->GetVisualActor()->GetActorLocation().Equals(Expected,.001));
            auto* Copy=Visual->GetVisualActor()->FindComponentByClass<UStaticMeshComponent>();
            TestTrue(TEXT("Visual has no collision"),Copy&&Copy->GetCollisionEnabled()==ECollisionEnabled::NoCollision);
        }
        TestFalse(TEXT("Canonical source does not render twice"),bool(Mesh->bRenderInMainPass));
        Visual.Reset();
        TestTrue(TEXT("Retirement restores main pass"),bool(Mesh->bRenderInMainPass));
        TestTrue(TEXT("Retirement restores shadows"),bool(Mesh->CastShadow));
    }
    if(!Error.IsEmpty())AddError(Error);
    Source->Destroy();return true;
}

#include "Presentation/CLPlanetCurvature.h"
#include "Model/CLPlanetRecipe.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLGuideCurvatureTest,"Catalyst.Landform.Presentation.GuideCurvature",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLGuideCurvatureTest::RunTest(const FString&)
{
    FCLCompiledPlanetRecipe Plan;Plan.RadiusMeters=1000;
    FCLHeightGuide Guide;Guide.Name=TEXT("SurfaceMax");Guide.HeightMeters=-200;Plan.Guides.Add(Guide);
    double C=0,Surface=0,Sky=0;FString Error;
    auto Value=[&](double R){TestTrue(TEXT("Authored boundaries resolve"),FCLPlanetCurvature::Evaluate(Plan,R,.15,C,Surface,Sky,Error));return C;};
    TestEqual(TEXT("Ground is .15 below SurfaceMax"),Value(700),.15);
    TestEqual(TEXT("SurfaceMax endpoint is .15"),Value(800),.15);
    TestEqual(TEXT("Sky endpoint is natural"),Value(1000),1.);
    TestEqual(TEXT("Space bypass stays natural"),Value(1200),1.);
    TestTrue(TEXT("Midpoint is half blended"),FMath::IsNearlyEqual(Value(900),.575,1.e-12));
    TestTrue(TEXT("SurfaceMax eases with near-zero slope"),Value(800.01)-.15<1.e-9);
    TestTrue(TEXT("Sky eases with near-zero slope"),1-Value(999.99)<1.e-9);
    TArray<double> Ascent;for(int32 I=0;I<=100;++I)Ascent.Add(Value(800+I*2));
    for(int32 I=100;I>=0;--I)TestEqual(TEXT("Descent exactly reverses ascent"),Value(800+I*2),Ascent[I]);
    Plan.Guides.Reset();TestFalse(TEXT("Missing SurfaceMax refuses a fabricated altitude"),FCLPlanetCurvature::Evaluate(Plan,900,.15,C,Surface,Sky,Error));
    return true;
}
