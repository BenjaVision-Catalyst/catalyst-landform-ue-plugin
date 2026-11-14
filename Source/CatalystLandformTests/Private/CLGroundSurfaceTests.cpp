#include "Scene/CLGroundSurface.h"
#include "Scene/CLScene.h"
#include "Scene/CLVolumes.h"
#include "Model/CLPlanetRecipe.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLGroundSurfaceTest,"Catalyst.Landform.PCG.ComposedGroundContact",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLGroundSurfaceTest::RunTest(const FString&)
{
    FCLPlanetRecipe Recipe=FCLPlanetCompiler::BasePlanet();Recipe.SchemaVersion=23;Recipe.RadiusMeters=1000;Recipe.BaseSurfaceNormalized=.5;Recipe.Caves.bEnabled=false;
    FString Error;
    auto Check=[&](const TCHAR* Label,double Expected,const FVector& ExpectedNormal,bool Supported=true)
    {
        const auto Plan=FCLPlanetCompiler::Compile(Recipe,Error);if(!Plan){AddError(Error);return;}
        const auto Ground=FCLGroundSurfaceSampler::Make(Plan,Error);if(!Ground){AddError(Error);return;}
        const TArray<FVector> D={FVector::XAxisVector};TArray<FCLGroundSurfaceSample> Out;
        if(!TestTrue(Label,Ground->Sample(D,Out,Error))){AddError(Error);return;}
        TestEqual(TEXT("Explicit support state"),Out[0].bSupported,Supported);if(!Supported)return;
        TestTrue(TEXT("Contact agrees with independent analytic boundary within 1 mm"),FMath::Abs(Out[0].PositionMeters.X-Expected)<.001);
        TestTrue(TEXT("Normal belongs to the composed support surface"),FVector::DotProduct(Out[0].Normal,ExpectedNormal)>.999);
        const auto Scene=FCLScene::FromPlanet(Plan,Error);TArray<FVector> P={Out[0].PositionMeters+Out[0].Normal*.05,Out[0].PositionMeters-Out[0].Normal*.05};TArray<FCLSceneShapeSample> Shape;
        TestTrue(TEXT("Independent final-volume query accepts both sides"),Scene->SampleShape({P},Shape,Error));
        TestTrue(TEXT("Air above and solid below the resolved ground"),Shape.Num()==2&&Shape[0].FieldMeters>0&&Shape[1].FieldMeters<0);
    };
    Check(TEXT("Uncut outer surface"),500,FVector::XAxisVector);
    FCLSolidOperation Cut;Cut.Id=FGuid(1,2,3,4);Cut.Name=TEXT("Surface opening");Cut.Primitive=ECLSolidPrimitive::Sphere;Cut.Composition=ECLSolidComposition::Subtract;Cut.CenterMeters=FVector(500,0,0);Cut.RadiusMeters=10;
    Recipe.Volumes.bEnabled=true;Recipe.Volumes.Operations={Cut};Check(TEXT("Open cavity plants on the floor"),490,FVector::XAxisVector);
    Recipe.Volumes.Operations[0].CenterMeters=FVector(480,0,0);Recipe.Volumes.Operations[0].RadiusMeters=5;Check(TEXT("Closed cavity retains its exterior roof"),500,FVector::XAxisVector);
    Recipe.Volumes.Operations[0].CenterMeters=FVector(494.8,0,0);Recipe.Volumes.Operations[0].RadiusMeters=5;Check(TEXT("Sub-voxel roof cannot support vegetation above an opening"),489.8,FVector::XAxisVector);
    FCLSolidOperation Add;Add.Id=FGuid(5,6,7,8);Add.Name=TEXT("Added ledge");Add.Primitive=ECLSolidPrimitive::Box;Add.Composition=ECLSolidComposition::Union;Add.CenterMeters=FVector(510,0,0);Add.HalfExtentsMeters=FVector(12,4,4);
    Recipe.Volumes.Operations={Add};Check(TEXT("Union finds the higher exposed ledge"),522,FVector::XAxisVector);
    Cut.Primitive=ECLSolidPrimitive::Box;Cut.Rotation=FRotator(30,0,0);Cut.HalfExtentsMeters=FVector(10,20,20);Recipe.Volumes.Operations={Cut};
    const FVector N=Cut.Rotation.Quaternion().GetAxisX();Check(TEXT("Sloped cut updates support normal and root height"),500-10/N.X,N);
    Cut.Primitive=ECLSolidPrimitive::Sphere;Cut.Rotation=FRotator::ZeroRotator;Cut.RadiusMeters=600;Recipe.Volumes.Operations={Cut};Check(TEXT("Unresolved deep air emits no unsupported point"),0,FVector::XAxisVector,false);
    return !HasAnyErrors();
}
