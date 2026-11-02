#include "Misc/AutomationTest.h"
#include "Model/CLTectonics.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLSphericalPatches.h"
#include "CLPlanetStamp.h"
#include "VoxelLayers.h"
#include "VoxelQuery.h"
#include "Surface/VoxelSurfaceTypeTable.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLContinentalFieldsTest,"Catalyst.Landform.Continental.FieldsAndMotion",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLContinentalFieldsTest::RunTest(const FString&)
{
    FString Error;auto P=FCLPlanetCompiler::ContinentalBase().Tectonics;P.Activity=1;
    const auto Collision=FCLCompiledTectonics::Compile(P,1729,120,Error);if(!Collision){AddError(Error);return false;}
    FCLGeologicalSample Front,Ocean;TestTrue(TEXT("Geology is available"),Collision->SampleGeology(FVector::XAxisVector,Front));Collision->SampleGeology(-FVector::XAxisVector,Ocean);
    TestTrue(TEXT("Continental interiors sit above the deep oceanic foundation"),Front.Continentalness>.8&&Ocean.Continentalness<.2&&Front.BaseDeltaMeters>Ocean.BaseDeltaMeters+40);
    TestTrue(TEXT("Continental crust is thicker under the declared scaled approximation"),Front.CrustThicknessMeters>Ocean.CrustThicknessMeters);
    TestTrue(TEXT("The controlled front boundary converges and thickens a collision plateau"),Front.Convergence>0&&Front.DeformationMeters>5&&Front.Setting==ECLGeologicalSetting::ContinentalCollision);
    P.MotionDirection=0;const auto Still=FCLCompiledTectonics::Compile(P,1729,120,Error);FCLGeologicalSample Stable;Still->SampleGeology(FVector::XAxisVector,Stable);
    TestTrue(TEXT("Stationary plates create no fresh tectonic relief"),Stable.DeformationMeters==0&&Stable.Setting==ECLGeologicalSetting::Interior&&Stable.Continentalness==Front.Continentalness);
    P.MotionDirection=-1;const auto Reverse=FCLCompiledTectonics::Compile(P,1729,120,Error);FCLGeologicalSample Rift;Reverse->SampleGeology(FVector::XAxisVector,Rift);
    TestTrue(TEXT("Reversing motion opens a continental rift instead of uplifting a belt"),Rift.Convergence<0&&Rift.DeformationMeters<0&&Rift.Setting==ECLGeologicalSetting::ContinentalRift);
    P.MotionDirection=1;P.Setting=ECLContinentalSetting::Transform;const auto Transform=FCLCompiledTectonics::Compile(P,1729,120,Error);FCLGeologicalSample Fault;Transform->SampleGeology(FVector::XAxisVector,Fault);
    TestTrue(TEXT("Transform motion remains a modest fault corridor"),FMath::Abs(Fault.Convergence)<1.e-8&&FMath::Abs(Fault.Shear)>0&&FMath::Abs(Fault.DeformationMeters)<Front.DeformationMeters*.15&&Fault.Setting==ECLGeologicalSetting::Transform);
    P.Setting=ECLContinentalSetting::Subduction;const auto Subduction=FCLCompiledTectonics::Compile(P,1729,120,Error);FCLGeologicalSample Arc;Subduction->SampleGeology(FVector::XAxisVector,Arc);
    TestTrue(TEXT("Coastal crust selects subduction rather than a symmetric collision"),Arc.Setting==ECLGeologicalSetting::Subduction);
    AddInfo(FString::Printf(TEXT("Continental front/ocean base %.3f / %.3f m; collision %.3f m, reversed %.3f m, transform %.3f m"),Front.BaseDeltaMeters,Ocean.BaseDeltaMeters,Front.DeformationMeters,Rift.DeformationMeters,Fault.DeformationMeters));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLContinentalIdentityTest,"Catalyst.Landform.Continental.IdentityBoundsAndScale",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLContinentalIdentityTest::RunTest(const FString&)
{
    FString Error;auto P=FCLPlanetCompiler::ContinentalBase().Tectonics;TArray<FGuid> Ids;double Continent=-1;
    for(int32 K=0;K<=20;++K)
    {
        P.Activity=K/20.;const auto T=FCLCompiledTectonics::Compile(P,1729,120,Error);if(!T){AddError(Error);return false;}
        FCLGeologicalSample S;T->SampleGeology(FVector::XAxisVector,S);
        if(K==0){Continent=S.Continentalness;for(const auto& Plate:T->Lower.Plates)Ids.Add(Plate.Id);}
        TestEqual(TEXT("Activity preserves continental identity and coast layout"),S.Continentalness,Continent);
        for(int32 I=0;I<2;++I)TestEqual(TEXT("Controlled plate identities survive knob changes"),T->Lower.Plates[I].Id,Ids[I]);
    }
    int32 Samples=0;double WorstStep=0;
    for(int32 Seed:{0,1729,-531})for(ECLContinentalSetting Setting:{ECLContinentalSetting::Collision,ECLContinentalSetting::Subduction,ECLContinentalSetting::Rift,ECLContinentalSetting::Transform})
    {
        P.Activity=1;P.Setting=Setting;auto T=FCLCompiledTectonics::Compile(P,Seed,120,Error),Large=FCLCompiledTectonics::Compile(P,Seed,240,Error);
        for(int32 F=0;F<6;++F)for(int32 Y=0;Y<=16;++Y)for(int32 X=0;X<=16;++X)
        {
            const FVector D=FCLSphericalPatches::Direction(F,FVector2D(X/8.-1,Y/8.-1));FCLGeologicalSample S,L;T->SampleGeology(D,S);Large->SampleGeology(D,L);++Samples;
            TestTrue(TEXT("Shared geology has finite bounded heights and crust values"),FMath::IsFinite(S.TotalDeltaMeters)&&FMath::Abs(S.TotalDeltaMeters)<=T->MaximumDeltaMeters&&S.Continentalness>=0&&S.Continentalness<=1&&S.CrustThicknessMeters>0);
            TestTrue(TEXT("Proportional resizing preserves normalized geology"),FMath::IsNearlyEqual(L.TotalDeltaMeters,S.TotalDeltaMeters*2,1.e-9)&&L.Continentalness==S.Continentalness);
            const FVector N=FVector::CrossProduct(D,FMath::Abs(D.Z)<.9?FVector::ZAxisVector:FVector::XAxisVector).GetSafeNormal();FCLGeologicalSample Near;T->SampleGeology((D+N*1.e-6).GetSafeNormal(),Near);WorstStep=FMath::Max(WorstStep,FMath::Abs(S.TotalDeltaMeters-Near.TotalDeltaMeters));
        }
    }
    TestTrue(TEXT("Plate and coast transitions have no height jumps"),WorstStep<.02);
    P.FoundationOrientation=FRotator(37,61,12);auto Rotated=FCLCompiledTectonics::Compile(P,1729,120,Error);P.FoundationOrientation=FRotator::ZeroRotator;auto Original=FCLCompiledTectonics::Compile(P,1729,120,Error);
    const FVector D=FVector(.9,.2,.1).GetSafeNormal();FCLGeologicalSample A,B;Original->SampleGeology(D,A);Rotated->SampleGeology(FRotator(37,61,12).Quaternion().RotateVector(D),B);
    TestTrue(TEXT("Editorial orientation rotates the whole shared foundation"),FMath::IsNearlyEqual(A.TotalDeltaMeters,B.TotalDeltaMeters,1.e-4));
    B.TotalDeltaMeters=123;TestFalse(TEXT("Invalid geology requests are refused"),Original->SampleGeology(FVector::ZeroVector,B));TestEqual(TEXT("Refusal preserves output"),B.TotalDeltaMeters,123.);
    AddInfo(FString::Printf(TEXT("Continental contexts: %d probes; worst microstep %.9g m"),Samples,WorstStep));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLContinentalNativeTest,"Catalyst.Landform.Continental.NativeFieldAndPersistence",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLContinentalNativeTest::RunTest(const FString&)
{
    auto R=FCLPlanetCompiler::ContinentalBase();FString Error,Json;const auto Plan=FCLPlanetCompiler::Compile(R,Error);if(!Plan){AddError(Error);return false;}
    auto* A=NewObject<UCLPlanetRecipeAsset>();A->Recipe=R;TestTrue(TEXT("Continental recipe exports"),A->ExportJsonString(Json,Error));auto* B=NewObject<UCLPlanetRecipeAsset>();TestTrue(TEXT("Continental recipe imports"),B->ImportRecipeJson(Json,Error));TestTrue(TEXT("All versioned controls roundtrip"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&R,&B->Recipe,0));
    TArray<FVector> D;for(int32 F=0;F<6;++F)for(int32 Y=0;Y<=12;++Y)for(int32 X=0;X<=12;++X)D.Add(FCLSphericalPatches::Direction(F,FVector2D(X/6.-1,Y/6.-1)));
    TArray<double> H;Plan->SampleHeightsMeters(D,H);FCLPlanetStamp Stamp;Stamp.bUseRecipe=true;Stamp.Radius=100000;Stamp.Recipe=R;auto Ref=FVoxelStampRef::New(Stamp);auto Native=StaticCastSharedPtr<FCLPlanetStampRuntime>(FVoxelStampRuntime::Create(nullptr,Ref,nullptr));if(!TestTrue(TEXT("Native continental field compiles"),Native.IsValid()))return false;
    FVoxelDependencyCollector Dependencies(STATIC_FNAME("CL.Continental"));auto Table=FVoxelSurfaceTypeTable::Get();FVoxelQuery Query(0,FVoxelLayers::Empty(),*Table,Dependencies);
    FVoxelDoubleVectorBuffer Points;Points.Allocate(D.Num());for(int32 I=0;I<D.Num();++I)Points.Set(I,D[I]*(R.RadiusMeters+H[I])*100);const auto Dist=Native->SampleDistances(Query,Points);double Worst=0;for(int32 I=0;I<D.Num();++I)Worst=FMath::Max(Worst,FMath::Abs(double(Dist[I])));
    TestTrue(TEXT("VPP and the Builder share the continental zero set"),Worst<.02);AddInfo(FString::Printf(TEXT("Continental native parity: %d probes; %.9g cm residual"),D.Num(),Worst));return true;
}
