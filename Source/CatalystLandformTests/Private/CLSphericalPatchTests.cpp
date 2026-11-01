#if WITH_DEV_AUTOMATION_TESTS
#include "Model/CLTerrainType.h"
#include "Model/CLSphericalPatches.h"
#include "CLPlanetStamp.h"
#include "CLSectionGeometry.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Interfaces/IPluginManager.h"
#include "Surface/VoxelSurfaceTypeTable.h"
#include "VoxelLayers.h"
#include "VoxelQuery.h"
#include "Async/Async.h"

namespace
{
bool PatchRecipe(FCLPlanetRecipe& R,FString& Error)
{
    FString Json;FFileHelper::LoadFileToString(Json,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/SphericalPatchComposition.json")));
    auto* A=NewObject<UCLPlanetRecipeAsset>();if(!A->ImportRecipeJson(Json,Error))return false;R=A->Recipe;return true;
}
TArray<double> PatchSamples(const FCLCompiledPlanetRecipe& P,const TArray<FVector>& D)
{TArray<double> H;P.SampleHeightsMeters(D,H);return H;}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLPatchIdentityTest,"Catalyst.Landform.SphericalPatches.CoverageAndIdentity",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLPatchIdentityTest::RunTest(const FString&)
{
    FString Error;FCLPlanetRecipe R;if(!PatchRecipe(R,Error)){AddError(Error);return false;}
    const auto Atlas=FCLPlanetCompiler::Compile(R,Error);const auto Whole=FCLPlanetCompiler::Compile(R,Error,nullptr,nullptr,FCLSphericalPatches::WholeSphere);
    if(!Atlas||!Whole){AddError(Error);return false;}
    TestEqual(TEXT("Six independent spherical patch plans cover the globe"),Atlas->SurfacePatches.Num(),6);
    TArray<FCLResolvedStampPlacement> All,Union;Whole->GetStampPlacements(All);Atlas->GetStampPlacements(Union);
    TestEqual(TEXT("Patch halo union preserves every global placement"),Union.Num(),All.Num());
    int32 Retained=0,External=0;
    for(int32 Face=0;Face<6;++Face)
    {
        const auto& Patch=*Atlas->SurfacePatches[Face];TArray<FCLResolvedStampPlacement> P;Patch.GetStampPlacements(P);Retained+=P.Num();
        TestTrue(TEXT("Patch compiles a bounded subset of the global registry"),P.Num()>0&&P.Num()<All.Num());
        for(const auto& Placement:P)
        {
            const auto* A=All.FindByPredicate([&](const auto& V){return V.Id==Placement.Id;});
            if(!TestNotNull(TEXT("Identity is global, never patch-local"),A))continue;
            TestTrue(TEXT("Native PCG choice, heading, size and relief are invariant across patches"),A->SourceId==Placement.SourceId&&A->Anchor.Equals(Placement.Anchor,1.e-12)&&A->TangentX.Equals(Placement.TangentX,1.e-12)&&A->WidthMeters==Placement.WidthMeters&&A->ReliefMeters==Placement.ReliefMeters&&A->ChartRadiusMeters==Placement.ChartRadiusMeters);
            if(FCLSphericalPatches::Owner(Placement.Anchor)!=Face)++External;
        }
        for(int32 Y=0;Y<=16;++Y)for(int32 X=0;X<=16;++X)
        {
            const FVector2D UV(X/8.-1,Y/8.-1);const FVector D=FCLSphericalPatches::Direction(Face,UV);FVector2D Back;
            TestTrue(TEXT("Each face has its own invertible spherical chart"),FCLSphericalPatches::Coordinates(Face,D,Back)&&Back.Equals(UV,1.e-12));
        }
    }
    TestTrue(TEXT("Neighbour-anchored stamps are included in patch halos"),External>0);
    TestTrue(TEXT("Culling reduces native source references below six complete copies"),Retained<6*All.Num());
    auto Bad=R;Bad.SchemaVersion=7;TestFalse(TEXT("Old recipes cannot silently acquire spherical-surface semantics"),FCLPlanetCompiler::Compile(Bad,Error).IsValid());
    Bad=R;Bad.TerrainRegions[0].bSphericalMask=false;TestFalse(TEXT("Local masks cannot be reinterpreted as spherical masks"),FCLPlanetCompiler::Compile(Bad,Error).IsValid());
    Bad=R;Bad.NoiseLayers[1].Distribution.DensityPerSquareKilometer=10000;TestFalse(TEXT("Global registry budget fails explicitly rather than reseeding/truncating per patch"),FCLPlanetCompiler::Compile(Bad,Error).IsValid());
    TestFalse(TEXT("Invalid patch request is refused"),FCLPlanetCompiler::Compile(R,Error,nullptr,nullptr,8).IsValid());
    AddInfo(FString::Printf(TEXT("Spherical atlas: %d unique global stamps, %d patch references, %d neighbour-anchored references"),All.Num(),Retained,External));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLPatchEdgesTest,"Catalyst.Landform.SphericalPatches.EdgeCornerAndNormalContinuity",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLPatchEdgesTest::RunTest(const FString&)
{
    FString Error;FCLPlanetRecipe R;if(!PatchRecipe(R,Error)){AddError(Error);return false;}
    const auto Atlas=FCLPlanetCompiler::Compile(R,Error);const auto Whole=FCLPlanetCompiler::Compile(R,Error,nullptr,nullptr,FCLSphericalPatches::WholeSphere);
    if(!Atlas||!Whole){AddError(Error);return false;}
    double MaxError=0,MinNormalDot=1;int32 Edges=0,CornerSamples=0,Points=0;
    // All twelve cube edges, including their three-way corners. Each side compiles independently.
    for(int32 A=0;A<6;++A)for(int32 B=A+1;B<6;++B)
    {
        FVector NA,XA,YA,NB,XB,YB;FCLSphericalPatches::Frame(A,NA,XA,YA);FCLSphericalPatches::Frame(B,NB,XB,YB);
        if(FVector::DotProduct(NA,NB)!=0)continue;++Edges;
        const FVector Along=FVector::CrossProduct(NA,NB);TArray<FVector> D;
        for(int32 I=0;I<=128;++I)
        {
            const FVector Center=(NA+NB+Along*(I/64.-1)).GetSafeNormal();
            const FVector T=FVector::CrossProduct(Center,Along).GetSafeNormal();const FVector S=FVector::CrossProduct(Center,T).GetSafeNormal();
            D.Append({Center,(Center+T*.0001).GetSafeNormal(),(Center-T*.0001).GetSafeNormal(),(Center+S*.0001).GetSafeNormal(),(Center-S*.0001).GetSafeNormal()});
        }
        const auto H1=PatchSamples(*Atlas->SurfacePatches[A],D),H2=PatchSamples(*Atlas->SurfacePatches[B],D),HW=PatchSamples(*Whole,D),HR=PatchSamples(*Atlas,D);
        for(int32 I=0;I<D.Num();++I)
        {
            MaxError=FMath::Max(MaxError,FMath::Max(FMath::Abs(H1[I]-HW[I]),FMath::Max(FMath::Abs(H2[I]-HW[I]),FMath::Abs(HR[I]-HW[I]))));++Points;
        }
        for(int32 I=0;I<D.Num();I+=5)
        {
            FVector P1[4],P2[4];for(int32 J=0;J<4;++J){P1[J]=D[I+1+J]*(R.RadiusMeters+H1[I+1+J]);P2[J]=D[I+1+J]*(R.RadiusMeters+H2[I+1+J]);}
            const FVector N1=FVector::CrossProduct(P1[0]-P1[1],P1[2]-P1[3]).GetSafeNormal();
            const FVector N2=FVector::CrossProduct(P2[0]-P2[1],P2[2]-P2[3]).GetSafeNormal();
            MinNormalDot=FMath::Min(MinNormalDot,FVector::DotProduct(N1,N2));
        }
    }
    for(int32 X:{-1,1})for(int32 Y:{-1,1})for(int32 Z:{-1,1})
    {
        const FVector D=FVector(X,Y,Z).GetSafeNormal();const double H=Whole->SampleHeightMeters(D);
        for(int32 Face:{X>0?0:1,Y>0?2:3,Z>0?4:5}){MaxError=FMath::Max(MaxError,FMath::Abs(Atlas->SurfacePatches[Face]->SampleHeightMeters(D)-H));++CornerSamples;}
    }
    TestEqual(TEXT("All twelve patch boundaries are checked"),Edges,12);
    TestEqual(TEXT("All eight triple corners are checked from all three patches"),CornerSamples,24);
    TestTrue(TEXT("No patch-induced height jump above 0.1mm"),MaxError<.0001);
    TestTrue(TEXT("Patch switch preserves the same surface normal"),MinNormalDot>.999999);
    AddInfo(FString::Printf(TEXT("Patch continuity: %d edge probes, %d corner probes, %.12g m maximum height disagreement; minimum normal dot %.12f"),Points,CornerSamples,MaxError,MinNormalDot));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLPatchNativeTest,"Catalyst.Landform.SphericalPatches.NativeVolumeAndRoundTrip",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLPatchNativeTest::RunTest(const FString&)
{
    FString Error;FCLPlanetRecipe R;if(!PatchRecipe(R,Error)){AddError(Error);return false;}
    const auto Atlas=FCLPlanetCompiler::Compile(R,Error);if(!Atlas){AddError(Error);return false;}
    FCLPlanetStamp Stamp;Stamp.bUseRecipe=true;Stamp.Radius=R.RadiusMeters*100;Stamp.Recipe=R;
    const auto Ref=FVoxelStampRef::New(Stamp);const auto Runtime=StaticCastSharedPtr<FCLPlanetStampRuntime>(FVoxelStampRuntime::Create(nullptr,Ref,nullptr));
    if(!TestTrue(TEXT("One native VPP volume owns all spherical patches"),Runtime.IsValid()))return false;
    FVoxelDependencyCollector Dependencies(STATIC_FNAME("CL.SphericalPatches"));const auto Surfaces=FVoxelSurfaceTypeTable::Get();const FVoxelQuery Query(0,FVoxelLayers::Empty(),*Surfaces,Dependencies);
    TArray<FVector> D;
    for(int32 Face=0;Face<6;++Face)for(int32 Y=0;Y<=40;++Y)for(int32 X=0;X<=40;++X)D.Add(FCLSphericalPatches::Direction(Face,FVector2D(X/20.-1,Y/20.-1)));
    TArray<double> H;Atlas->SampleHeightsMeters(D,H);FVoxelDoubleVectorBuffer P;P.Allocate(D.Num());
    for(int32 I=0;I<D.Num();++I)P.Set(I,D[I]*(R.RadiusMeters+H[I])*100);
    const auto Distances=Runtime->SampleDistances(Query,P);double MaxError=0;
    for(int32 I=0;I<D.Num();++I)MaxError=FMath::Max(MaxError,FMath::Abs(double(Distances[I])));
    TestTrue(TEXT("Native volume matches all six patch surfaces within 0.1mm"),MaxError<.01);
    auto* Asset=NewObject<UCLPlanetRecipeAsset>();Asset->Recipe=R;FString Json;TestTrue(TEXT("Full spherical recipe exports"),Asset->ExportJsonString(Json,Error));
    auto* Copy=NewObject<UCLPlanetRecipeAsset>();TestTrue(TEXT("Full spherical recipe imports"),Copy->ImportRecipeJson(Json,Error));
    const auto Reloaded=FCLPlanetCompiler::Compile(Copy->Recipe,Error);if(!Reloaded){AddError(Error);return false;}
    const auto Again=PatchSamples(*Reloaded,D);for(int32 I=0;I<H.Num();++I)TestTrue(TEXT("Serialization preserves the planet-wide field"),FMath::Abs(H[I]-Again[I])<1.e-7);
    const auto Worker=Async(EAsyncExecution::ThreadPool,[Atlas,D]{return PatchSamples(*Atlas,D);});const auto Frozen=Worker.Get();
    for(int32 I=0;I<H.Num();++I)TestTrue(TEXT("Frozen atlas evaluates on workers"),FMath::Abs(H[I]-Frozen[I])<1.e-7);
    AddInfo(FString::Printf(TEXT("Native spherical atlas: %d source points; %.9f cm residual"),D.Num(),MaxError));return true;
}
#endif
