#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Interfaces/IPluginManager.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLSpatialCave.h"
#include "CLPlanetStamp.h"
#include "CLDeliveryPrototype.h"
#include "VoxelQuery.h"
#include "VoxelLayers.h"
#include "Surface/VoxelSurfaceTypeTable.h"
#include "UObject/StrongObjectPtr.h"
#include "Async/Async.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLSpatialCaveTest,"Catalyst.Landform.Caves.SpatialPrototype.GeometryAndDelivery",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLSpatialCaveTest::RunTest(const FString&)
{
    FString E,J;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/SpatialCave.json")));
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>());
    if(!A->ImportRecipeJson(J,E)){AddError(E);return false;}
    FCLPlanetStamp Stamp;Stamp.bUseRecipe=true;Stamp.Recipe=A->Recipe;Stamp.Radius=A->Recipe.RadiusMeters*100;Stamp.Behavior=EVoxelStampBehavior::AffectAll;
    const auto R=StaticCastSharedPtr<FCLPlanetStampRuntime>(FVoxelStampRuntime::Create(nullptr,FVoxelStampRef::New(Stamp),nullptr));
    if(!R||!R->RecipePlan||!R->RecipePlan->Caves||!R->RecipePlan->Caves->Spatial){AddError(TEXT("Missing spatial cave runtime: ")+E);return false;}
    const auto& P=*R->RecipePlan;const auto& C=*P.Caves->Spatial;
    TestEqual(TEXT("Existing UI default is preserved"),FCLCaveSettings().AlgorithmVersion,7);
    TestEqual(TEXT("One connected composition"),P.Caves->Networks,1);
    double MinFloor=MAX_dbl,MaxFloor=-MAX_dbl,MaxGrade=0,MinClearance=MAX_dbl,InverseError=0;
    auto Floor=[&](double X,double Y)
    {
        double Low=-8,High=8;for(int32 K=0;K<19;++K)
        {const double Z=(Low+High)*.5;const auto B=C.BodyPoint({X,Y,Z});if(P.Caves->Cutters->Compose(B,-1000)>0)High=Z;else Low=Z;}
        return (Low+High)*.5;
    };
    TArray<FVector> Points;TArray<double> Widths,Ceilings;int32 Contacts=0;
    for(double X=-100;X<=25;X+=.5)
    {
        const double Y=-3+2*FMath::Sin(X*.045),F=Floor(X,Y);MinFloor=FMath::Min(MinFloor,F);MaxFloor=FMath::Max(MaxFloor,F);
        const double Next=Floor(X+.1,Y);MaxGrade=FMath::Max(MaxGrade,FMath::Abs(Next-F)/.1);
        for(double Z:{-.35,0.,.5,1.92,3.})Points.Add(C.BodyPoint({X,Y,F+Z}));
        const auto B=C.BodyPoint({X,Y,F+1.92});MinClearance=FMath::Min(MinClearance,P.Caves->Cutters->Compose(B,-1000));
        InverseError=FMath::Max(InverseError,FVector::Distance(C.BeddingPoint(B),FVector(X,Y,F+1.92)));
        TestTrue(TEXT("Continuous foundation below actual walk floor"),P.Caves->Cutters->Compose(C.BodyPoint({X,Y,F-.35}),-1000)<-.10);
        // A full capsule-width band, including the passage-to-room threshold.
        for(double Side:{-.5,0.,.5})TestTrue(TEXT("Head and shoulders clear all threshold transitions"),P.Caves->Cutters->Compose(C.BodyPoint({X,Y+Side,F+1.92}),-1000)>.35);
        ++Contacts;
    }
    for(double X:{-90.,-65.,-40.,-15.,0.,20.})
    {
        double L=0,H=0,Roof=0;const double F=Floor(X,-3);
        for(double Y=-3;Y>-60;Y-=.25){if(P.Caves->Cutters->Compose(C.BodyPoint({X,Y,F+3}),-1000)<=0)break;L=-3-Y;}
        for(double Y=-3;Y<60;Y+=.25){if(P.Caves->Cutters->Compose(C.BodyPoint({X,Y,F+3}),-1000)<=0)break;H=Y+3;}
        for(double Z=F+2;Z<50;Z+=.25){if(P.Caves->Cutters->Compose(C.BodyPoint({X,-3,Z}),-1000)<=0)break;Roof=Z-F;}
        Widths.Add(L+H);Ceilings.Add(Roof);
    }
    TestTrue(TEXT("Visible rolling floor relief"),MaxFloor-MinFloor>1.0);
    TestTrue(TEXT("Walking grade below 30 degrees"),MaxGrade<FMath::Tan(FMath::DegreesToRadians(30.)));
    TestTrue(TEXT("Canonical bedding inverse agrees within a millimetre"),InverseError<.001);
    TestTrue(TEXT("Chamber opens substantially wider than passage"),Widths.Last()>Widths[0]*2);
    TestTrue(TEXT("Chamber roof rises independently of passage section"),Ceilings.Last()>Ceilings[0]*1.7);
    for(double X=-125;X<=85;X+=5)for(double Y=-50;Y<=50;Y+=5)for(double Z:{-12.,0.,4.,12.,26.,45.})Points.Add(C.BodyPoint({X,Y,Z}));
    // Compare cave-first classification to the unpruned complete planet field.
    TArray<double> Height,Reference,Cave,Fast;P.SampleHeightsMeters(Points,Height);P.SampleRadialFields(Points,Height,Reference);C.Sample(Points,Cave);
    int32 HeightPoints=0;C.SamplePlanet(P,Points,Fast,&HeightPoints);double Worst=0;
    for(int32 I=0;I<Points.Num();++I){Worst=FMath::Max(Worst,FMath::Abs(Fast[I]-FMath::Max(Reference[I],Cave[I])));TestTrue(TEXT("In-place cave rock retains canonical geological coordinates"),P.RockPositionMeters(Points[I]).Equals(Points[I],1.e-8));}
    TestTrue(TEXT("Conservative terrain pruning preserves the full field"),Worst<.001);
    TestTrue(TEXT("Deep cave queries bypass redundant terrain height"),HeightPoints<Points.Num()/10);
    TArray<FVector> Global;
    for(int32 I=0;I<128;++I)
    {
        const double Z=1-2*(I+.5)/128.,Angle=I*2.399963229728653;
        const FVector Direction(FMath::Cos(Angle)*FMath::Sqrt(1-Z*Z),FMath::Sin(Angle)*FMath::Sqrt(1-Z*Z),Z);
        for(double Fraction:{.50,.74,.75,.76,.90})Global.Add(Direction*(P.RadiusMeters*Fraction));
    }
    TArray<double> GH,GN,GC,GF;P.SampleHeightsMeters(Global,GH);P.SampleRadialFields(Global,GH,GN);C.Sample(Global,GC);C.SamplePlanet(P,Global,GF);
    for(int32 I=0;I<Global.Num();++I)TestTrue(TEXT("Whole-body and exterior field agree within the compiled input tolerance"),FMath::Abs(GF[I]-FMath::Max(GN[I],GC[I]))<.001);
    auto Edited=A->Recipe;Edited.Volumes.bEnabled=true;
    FCLSolidOperation Fill;Fill.Name=TEXT("Spatial backfill");Fill.Id=FGuid(1,2,3,8);Fill.Composition=ECLSolidComposition::Union;Fill.CenterMeters=C.BodyPoint({-40,-3,2});Fill.RadiusMeters=3;Edited.Volumes.Operations.Add(Fill);
    FCLSolidOperation Cut;Cut.Name=TEXT("Spatial excavation");Cut.Id=FGuid(1,2,3,9);Cut.Composition=ECLSolidComposition::Subtract;Cut.CenterMeters=Fill.CenterMeters;Cut.RadiusMeters=1;Edited.Volumes.Operations.Add(Cut);
    const auto EP=FCLPlanetCompiler::Compile(Edited,E);if(!EP){AddError(E);return false;}
    TArray<FVector> EditPoints={Fill.CenterMeters,Fill.CenterMeters+Fill.CenterMeters.GetSafeNormal()*2};TArray<double> EF;EP->Caves->Spatial->SamplePlanet(*EP,EditPoints,EF);
    TestTrue(TEXT("Ordered authored cut remains air after filling cave"),EF[0]>0);
    TestTrue(TEXT("Ordered authored backfill restores solid cave rock"),EF[1]<0);
    const auto Worker=Async(EAsyncExecution::ThreadPool,[&C,Points]{TArray<double> V;C.Sample(Points,V);return V;}).Get();double WorkerError=0;for(int32 I=0;I<Cave.Num();++I)WorkerError=FMath::Max(WorkerError,FMath::Abs(Worker[I]-Cave[I]));TestEqual(TEXT("Immutable worker field parity"),WorkerError,0.);
    // Native delivery uses the unchanged planet stamp, not diagnostic meshes.
    FVoxelDependencyCollector D(STATIC_FNAME("CL.SpatialPrototype"));FVoxelQuery Q(0,FVoxelLayers::Empty(),*FVoxelSurfaceTypeTable::Get(),D);
    FCLDeliveryBinding Binding;Binding.Source=R;Binding.Snapshot=FGuid::NewGuid();FVoxelDoubleVectorBuffer XYZ;XYZ.Allocate(Points.Num());for(int32 I=0;I<Points.Num();++I)XYZ.Set(I,Points[I]*100);
    const auto Native=R->SampleDistances(Q,XYZ),Cpp=Binding.Distances(Q,XYZ);double DeliveryError=0;
    for(int32 I=0;I<Points.Num();++I)DeliveryError=FMath::Max(DeliveryError,FMath::Abs(double(Native[I])-Cpp[I]));TestEqual(TEXT("Native default and direct stamp delivery parity"),DeliveryError,0.);
    auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("floorProbes"),Contacts);O->SetNumberField(TEXT("floorReliefMeters"),MaxFloor-MinFloor);O->SetNumberField(TEXT("maximumRouteGrade"),MaxGrade);O->SetNumberField(TEXT("minimumHeadClearanceFieldMeters"),MinClearance);O->SetNumberField(TEXT("beddingInverseErrorMeters"),InverseError);O->SetNumberField(TEXT("heightPoints"),HeightPoints);O->SetNumberField(TEXT("fieldProbes"),Points.Num());O->SetNumberField(TEXT("pruningMaxErrorMeters"),Worst);O->SetNumberField(TEXT("deliveryMaxErrorCm"),DeliveryError);
    TArray<TSharedPtr<FJsonValue>> W,Hs;for(double V:Widths)W.Add(MakeShared<FJsonValueNumber>(V));for(double V:Ceilings)Hs.Add(MakeShared<FJsonValueNumber>(V));O->SetArrayField(TEXT("openingWidthsMeters"),W);O->SetArrayField(TEXT("ceilingHeightsMeters"),Hs);
    FString Path;if(FParse::Value(FCommandLine::Get(),TEXT("CLSpatialReport="),Path)){FString Json;FJsonSerializer::Serialize(O,TJsonWriterFactory<>::Create(&Json));FFileHelper::SaveStringToFile(Json,*Path);}
    AddInfo(FString::Printf(TEXT("SPATIAL_CAVE floors=%d relief=%.3fm grade=%.3f clearance=%.3fm fieldProbes=%d terrainNeeded=%d"),Contacts,MaxFloor-MinFloor,MaxGrade,MinClearance,Points.Num(),HeightPoints));return !HasAnyErrors();
}
#endif
