#if WITH_DEV_AUTOMATION_TESTS
#include "Scene/CLScene.h"
#include "Scene/CLVolumes.h"
#include "Model/CLPlanetRecipe.h"
#include "CLPlanetStamp.h"
#include "CLPlanetShape.h"
#include "VoxelQuery.h"
#include "VoxelLayers.h"
#include "Surface/VoxelSurfaceTypeTable.h"
#include "Misc/AutomationTest.h"
#include "Algo/Reverse.h"
#include "CLSurfaceInspection.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
FCLSolidOperation Solid(int32 Id,FVector P,ECLSolidComposition Mode=ECLSolidComposition::Union)
{FCLSolidOperation V;V.Id=FGuid(0x434C564F,0,0,Id);V.Name=FName(*FString::Printf(TEXT("Solid%d"),Id));V.CenterMeters=P;V.Composition=Mode;return V;}
FCLPlanetRecipe VolumeRecipe()
{auto R=FCLPlanetCompiler::BasePlanet();R.SchemaVersion=18;R.BaseSurfaceNormalized=.7;R.Volumes.bEnabled=true;return R;}
double Field(const FCLScene& Scene,FVector P)
{TArray<FCLSceneShapeSample> S;FString E;Scene.SampleShape({MakeArrayView(&P,1)},S,E);return S[0].FieldMeters;}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLVolumeAnalyticTest,"Catalyst.Landform.Volumes.AnalyticAndOrdering",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLVolumeAnalyticTest::RunTest(const FString&)
{
    FString Error;auto R=VolumeRecipe();auto A=Solid(1,FVector(0,0,730));A.RadiusMeters=10;A.Scale=FVector(2,1,.5);A.Rotation=FRotator(0,35,0);R.Volumes.Operations={A};
    auto P=FCLPlanetCompiler::Compile(R,Error);if(!P){AddError(Error);return false;}
    const auto S=FCLScene::FromPlanet(P,Error);TestFalse(TEXT("A general scene does not claim height-only coverage"),S->IsHeightOnly());
    TestFalse(TEXT("The radial provider refuses a volume plan"),FCLScene::FromLegacyRadial(P,Error).IsValid());
    const FTransform Pose(A.Rotation,A.CenterMeters,A.Scale);
    for(const FVector Local:{FVector(10,0,0),FVector(0,10,0),FVector(0,0,10)})
        TestTrue(TEXT("Nonuniform analytic solid keeps its transformed zero set"),FMath::Abs(Field(*S,Pose.TransformPosition(Local)))<1.e-9);
    TestTrue(TEXT("Detached solid contains rock while surrounding gap is air"),Field(*S,A.CenterMeters)<0&&Field(*S,FVector(0,0,710))>0);
    auto Cut=Solid(2,A.CenterMeters,ECLSolidComposition::Subtract);Cut.RadiusMeters=2;R.Volumes.Operations.Add(Cut);
    P=FCLPlanetCompiler::Compile(R,Error);auto CutScene=FCLScene::FromPlanet(P,Error);
    TestTrue(TEXT("Subtraction creates a cavity"),Field(*CutScene,A.CenterMeters)>0);
    Algo::Reverse(R.Volumes.Operations);P=FCLPlanetCompiler::Compile(R,Error);auto Reversed=FCLScene::FromPlanet(P,Error);
    TestTrue(TEXT("Authored operation order is semantic"),Field(*Reversed,A.CenterMeters)<0);
    R.Volumes.Operations[1].CenterMeters.Z=999;TestFalse(TEXT("Solid beyond outer sky is refused"),FCLPlanetCompiler::Compile(R,Error).IsValid());
    R=VolumeRecipe();auto LargeCut=Solid(5,FVector(0,0,1100),ECLSolidComposition::Subtract);LargeCut.RadiusMeters=900;R.Volumes.Operations={LargeCut};
    P=FCLPlanetCompiler::Compile(R,Error);TestTrue(TEXT("A planet-sized cutter may extend outside the sky without adding rock"),P.IsValid());
    R=VolumeRecipe();R.SchemaVersion=17;R.Volumes.Operations.Add(A);TestFalse(TEXT("An old schema cannot silently acquire volume semantics"),FCLPlanetCompiler::Compile(R,Error).IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLVolumeIndexTest,"Catalyst.Landform.Volumes.IndexAndSmoothContacts",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLVolumeIndexTest::RunTest(const FString&)
{
    FString Error;FCLVolumeSettings V;V.bEnabled=true;
    for(int32 I=0;I<64;++I){auto O=Solid(I+1,FVector((I%8)*30-110,(I/8)*30-110,700));O.SmoothnessMeters=2;V.Operations.Add(O);}
    const auto C=FCLCompiledVolumes::Compile(V,1000,nullptr,Error);if(!C){AddError(Error);return false;}
    TestTrue(TEXT("The index is bounded by the operation count"),C->IndexedOperationCount()==64);
    for(int32 X=-130;X<130;X+=7)for(int32 Y=-130;Y<130;Y+=11)
    {
        const FVector Q(X,Y,700);const uint64 Mask=C->Candidates(Q);
        for(int32 I=0;I<C->Operations.Num();++I)if(C->Operations[I].Bounds.IsInsideOrOn(Q))
            TestTrue(TEXT("Index never omits an influencing operation"),(Mask&(uint64(1)<<I))!=0);
    }
    TestEqual(TEXT("Remote point is outside zero-set support"),C->Candidates(FVector(0,0,-700)),uint64(0));
    // Compare pruning with an independently evaluated full ordered composition,
    // including air far outside primitive bounds and nonuniform smooth cutters.
    FCLVolumeSettings Mixed;Mixed.bEnabled=true;
    for(int32 I=0;I<8;++I){auto O=Solid(100+I,FVector(I*12-40,0,720),I%3==0?ECLSolidComposition::Subtract:ECLSolidComposition::Union);O.Scale=FVector(2,.5,1);O.SmoothnessMeters=3;Mixed.Operations.Add(O);}
    const auto Full=FCLCompiledVolumes::Compile(Mixed,1000,nullptr,Error);if(!Full){AddError(Error);return false;}
    double Worst=0;
    for(int32 Z=650;Z<780;Z+=3)for(int32 X=-100;X<100;X+=7)
    {
        const FVector Q(X,17,Z);double A=Q.Length()-700;
        for(const auto& Op:Full->Operations){const double B=Op.Field(Q),K=Op.Source.SmoothnessMeters;const bool Sub=Op.Source.Composition==ECLSolidComposition::Subtract;const double Compared=Sub?-B:B;const double Blend=FMath::Square(FMath::Max(K-FMath::Abs(A-Compared),0.))/(4*K);A=Sub?FMath::Max(A,Compared)+Blend:FMath::Min(A,Compared)-Blend;}
        Worst=FMath::Max(Worst,FMath::Abs(A-Full->Compose(Q,Q.Length()-700)));
    }
    TestTrue(TEXT("Native float blending stays within the 1 mm field budget against the independent double composition"),Worst<.001);
    const auto O=C->Operations[0];const FVector Join=O.Source.CenterMeters+FVector(5,0,0);
    const double A=O.Field(Join),D=C->Compose(Join,A);
    TestTrue(TEXT("Smooth union deepens equal contacts by k/4"),FMath::IsNearlyEqual(D,A-.5,1.e-9));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLVolumeNativeTest,"Catalyst.Landform.Volumes.NativeBulkSparseAndMaterials",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLVolumeNativeTest::RunTest(const FString&)
{
    FString Error;auto R=VolumeRecipe();R.SurfaceStack=FCLSurfaceStack::MakeDefault();R.SurfaceStack.DefaultSurfaceId=TEXT("Soil");R.SurfaceStack.Layers.Reset();
    auto O=Solid(1,FVector(0,0,680),ECLSolidComposition::Subtract);O.RadiusMeters=8;R.Volumes.Operations={O};
    FCLGeologicalUnit Unit;Unit.Name=TEXT("Bedrock");Unit.RadiusInterval=FVector2D(0,.695);R.Volumes.GeologicalUnits.Add(Unit);
    FCLPlanetStamp Stamp;Stamp.bUseRecipe=true;Stamp.Radius=R.RadiusMeters*100;Stamp.Recipe=R;Stamp.Behavior=EVoxelStampBehavior::AffectAll;Stamp.bApplyOnVoid=true;
    const auto Native=StaticCastSharedPtr<FCLPlanetStampRuntime>(FVoxelStampRuntime::Create(nullptr,FVoxelStampRef::New(Stamp),nullptr));
    if(!Native||!Native->Scene){AddError(TEXT("Volume native runtime did not compile"));return false;}
    FVoxelDependencyCollector Dependencies(STATIC_FNAME("CL.VolumeParity"));const auto Table=FVoxelSurfaceTypeTable::Get();FVoxelQuery Query(0,FVoxelLayers::Empty(),*Table,Dependencies);
    const FIntVector Size(3,3,33);const int32 Count=Size.X*Size.Y*Size.Z;const FVector Start(-100,-100,66500);constexpr float Step=100;
    TVoxelArray<float> Bulk,Sparse;Bulk.Init(FVoxelUtilities::NaNf(),Count);Sparse.Init(FVoxelUtilities::NaNf(),Count);
    TVoxelArray<FVoxelSurfaceTypeBlend> Materials;Materials.SetNum(Count);FVoxelDoubleVectorBuffer Points;Points.Allocate(Count);TArray<FVector3d> Body;
    for(int32 Z=0;Z<Size.Z;++Z)for(int32 Y=0;Y<Size.Y;++Y)for(int32 X=0;X<Size.X;++X){const FVector P=Start+FVector(X,Y,Z)*Step;Points.Set(Body.Num(),P);Body.Add(P*.01);}
    Native->Apply(FVoxelVolumeBulkQuery::Create(Query,Bulk,Start,Size,Step),FVoxelVolumeTransform());
    Native->Apply(FVoxelVolumeSparseQuery::Create(Query,Sparse,Materials,FVoxelMetadataMap(),Points,true,{}),FVoxelVolumeTransform());
    TArray<FCLSceneShapeSample> Shape;TArray<FCLSceneSurfaceSample> Surfaces;Native->Scene->SampleShape({Body},Shape,Error);Native->Scene->SampleSurfaces({Body},Surfaces,Error);
    double FieldError=0,QuantizationError=0,MaterialError=0;int32 Transitions=0;
    for(int32 I=0;I<Count;++I)
    {
        FieldError=FMath::Max(FieldError,FMath::Abs(double(Bulk[I])-Sparse[I]));FieldError=FMath::Max(FieldError,FMath::Abs(double(Bulk[I])-float(100*Shape[I].FieldMeters)));
        const double RoundedError=FMath::Abs(double(Bulk[I])-100*Shape[I].FieldMeters);QuantizationError=FMath::Max(QuantizationError,RoundedError);
        TestTrue(TEXT("Native float conversion stays within its relative rounding bound"),RoundedError<=FMath::Max(1.e-6,FMath::Abs(100*Shape[I].FieldMeters)*1.e-7));
        for(const auto& W:Surfaces[I].Weights)
        {const int32 K=Native->RecipePlan->Surfaces->Find(W.Id,W.Revision);double Actual=0;for(const auto& M:Materials[I].GetLayers())if(M.Type==Native->RecipePlan->Surfaces->NativeTypes[K])Actual+=M.Weight.ToFloat();MaterialError=FMath::Max(MaterialError,FMath::Abs(Actual-W.Weight));}
        if(I>=9&&I%9==4&&(Bulk[I]<0)!=(Bulk[I-9]<0))++Transitions;
    }
    TestTrue(TEXT("Native bulk and sparse geometry use the same volume field"),FieldError<.0001);
    TestTrue(TEXT("Native volume materials retain logical XYZ attribution"),MaterialError<.0001);
    TestEqual(TEXT("A radial ray crosses both cavity interfaces"),Transitions,2);
    const FVector3d P(0,0,672);TArray<FCLSceneSurfaceSample> W;Native->Scene->SampleSurfaces({MakeArrayView(&P,1)},W,Error);
    TestTrue(TEXT("An underground floor exposes its bedrock unit"),W[0].Weights.ContainsByPredicate([](const auto& M){return M.Id==TEXT("Rock")&&M.Weight>.99;}));
    AddInfo(FString::Printf(TEXT("%d native samples: field %.9g cm, float rounding %.9g cm, material %.9g, cavity crossings %d"),Count,FieldError,QuantizationError,MaterialError,Transitions));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLVolumeFixtureTest,"Catalyst.Landform.Volumes.GuidePolicyAndTwoFloors",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLVolumeFixtureTest::RunTest(const FString&)
{
    FString Json,Error;const auto Plugin=IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"));
    TStrongObjectPtr<UCLPlanetRecipeAsset> Asset(NewObject<UCLPlanetRecipeAsset>());
    if(!FFileHelper::LoadFileToString(Json,*(Plugin->GetBaseDir()/TEXT("Examples/VolumetricWorld.json")))||!Asset->ImportRecipeJson(Json,Error)){AddError(Error);return false;}
    const auto P=FCLPlanetCompiler::Compile(Asset->Recipe,Error);if(!P){AddError(Error);return false;}const auto S=FCLScene::FromPlanet(P,Error);
    auto Base=Asset->Recipe;Base.Volumes=FCLVolumeSettings();const auto B=FCLPlanetCompiler::Compile(Base,Error);if(!B){AddError(Error);return false;}
    TestEqual(TEXT("Volume composition preserves the fitted base minimum"),P->MinimumHeightMeters,B->MinimumHeightMeters);
    TestEqual(TEXT("Volume composition preserves the fitted base maximum"),P->MaximumHeightMeters,B->MaximumHeightMeters);
    for(double Z:{631.,604.})
    {
        FCLSurfaceLanding Landing;TestTrue(TEXT("A selected XYZ floor has valid independent footing"),FCLSurfaceInspection::FindLandingPoint(*S,FVector(0,45,Z),Landing));
        TestTrue(TEXT("The selected cave floor is retained instead of snapping to the exterior"),FMath::Abs(Landing.PositionCm.Z-Z*100)<.001);
        TestTrue(TEXT("Cave floors are permitted below SurfaceMin"),Landing.PositionCm.Size()<65000);
    }
    TestTrue(TEXT("Rock remains between the two floors"),Field(*S,FVector(0,45,623))<0);
    TestTrue(TEXT("Detached rock is permitted above base SurfaceMax"),Field(*S,FVector(85,-20,815))<0&&FVector(85,-20,815).Size()>750);
    TestTrue(TEXT("The detached rock has a genuine air gap"),Field(*S,FVector(85,-20,790))>0);
    const double Before=Field(*S,FVector(85,-20,815));Asset->Recipe.Volumes.Operations.Reset();
    TestEqual(TEXT("A captured scene survives authoring edits"),Field(*S,FVector(85,-20,815)),Before);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLVolumeSectionTest,"Catalyst.Landform.Volumes.NativeSectionDomain",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLVolumeSectionTest::RunTest(const FString&)
{
    auto R=VolumeRecipe();auto Cut=Solid(1,FVector(0,45,638),ECLSolidComposition::Subtract);Cut.RadiusMeters=8;R.Volumes.Operations={Cut};
    FCLPlanetStamp Stamp;Stamp.bUseRecipe=true;Stamp.Radius=R.RadiusMeters*100;Stamp.Recipe=R;Stamp.bSectionPreview=true;Stamp.SectionWidthMeters=500;Stamp.SectionReferenceRadiusMeters=700;
    const auto Native=StaticCastSharedPtr<FCLPlanetStampRuntime>(FVoxelStampRuntime::Create(nullptr,FVoxelStampRef::New(Stamp),nullptr));
    if(!Native||!Native->Scene){AddError(TEXT("Native section runtime did not compile"));return false;}
    FVoxelDependencyCollector Dependencies(STATIC_FNAME("CL.VolumeSection"));const auto Table=FVoxelSurfaceTypeTable::Get();FVoxelQuery Query(0,FVoxelLayers::Empty(),*Table,Dependencies);
    TArray<FVector3d> Flat,Body;FCLPlanetShape Chart;Chart.Radius=700;Chart.AnchorDirection=FVector::ZAxisVector;Chart.ChartXAxis=FVector::XAxisVector;
    double RoundTrip=0;
    for(double Z:{-2.,0.,604.,631.,700.,800.})for(double X:{-260.,0.,35.,250.})for(double Y:{-250.,0.,45.,260.})
    {const FVector P(X,Y,Z),Q=FCLPlanetStampRuntime::SectionToBodyMeters(P,700);Flat.Add(P);Body.Add(Q);RoundTrip=FMath::Max(RoundTrip,FVector::Distance(Q,Chart.FromChart(FVector2D(X,Y))*Z));}
    FVoxelDoubleVectorBuffer Positions;Positions.Allocate(Flat.Num());for(int32 I=0;I<Flat.Num();++I)Positions.Set(I,Flat[I]*100);
    const auto FieldValues=Native->SampleDistances(Query,Positions);TArray<FCLSceneShapeSample> Shape;FString Error;Native->Scene->SampleShape({Body},Shape,Error);double Worst=0;
    bool Signs=true;int32 Faces=0;
    for(int32 I=0;I<Flat.Num();++I)
    {
        const auto& P=Flat[I];const double Clip=FMath::Max(FMath::Max(FMath::Abs(P.X),FMath::Abs(P.Y))-250.,FMath::Max(-P.Z,P.Z-1000.));
        const double Expected=FMath::Max(Shape[I].FieldMeters,Clip)*100;
        Signs&=(Expected<=0)==(FieldValues[I]<=0);
        // On a face or inside the box, both independent clipping definitions
        // have the same field. Exterior corners only require the same sign.
        if(FMath::Abs(P.X)<=250&&FMath::Abs(P.Y)<=250&&P.Z>=0&&P.Z<=1000)
        {Worst=FMath::Max(Worst,FMath::Abs(double(FieldValues[I])-Expected));++Faces;}
    }
    TestTrue(TEXT("Flat section uses the existing spherical chart convention"),RoundTrip<1.e-10);
    TestTrue(TEXT("Native box intersection retains solid/air signs and planar cuts"),Signs&&Faces>0&&Worst<.01);
    AddInfo(FString::Printf(TEXT("%d native section points: field error %.9g cm, chart error %.9g m"),Flat.Num(),Worst,RoundTrip));return true;
}
#endif
