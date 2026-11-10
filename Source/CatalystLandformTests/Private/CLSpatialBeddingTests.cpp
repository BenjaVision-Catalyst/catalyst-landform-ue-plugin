#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Interfaces/IPluginManager.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLSpatialCave.h"
#include "Model/CLWatershed.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLSpatialBeddingTest,"Catalyst.Landform.Caves.SpatialPrototype.CompiledBedding",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLSpatialBeddingTest::RunTest(const FString&)
{
    FString J,E;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/SpatialCave.json")));
    auto* Asset=NewObject<UCLPlanetRecipeAsset>();if(!Asset->ImportRecipeJson(J,E)){AddError(E);return false;}
    const double Started=FPlatformTime::Seconds();const auto Plan=FCLPlanetCompiler::Compile(Asset->Recipe,E);
    if(!Plan||!Plan->Caves||!Plan->Caves->Spatial){AddError(E);return false;}
    const double Compile=FPlatformTime::Seconds()-Started;const auto C=Plan->Caves->Spatial;
    TArray<FVector> Points;FRandomStream Random(914036);
    const FBox Bounds=C->Bounds();
    // Independent world-space points, not the compiler's chart/validation knots.
    for(int32 I=0;I<100000;++I)Points.Add(Bounds.Min+(Bounds.Max-Bounds.Min)*FVector(Random.FRand(),Random.FRand(),Random.FRand()));
    // Fine floor/contact/roof probes preserve the actual geological coordinate
    // and independently exercise directions near both sides of each boundary.
    for(double X=-125;X<=85;X+=.71)for(double Y=-50;Y<=50;Y+=.83)
    {
        const double Floor=C->FloorHeight(X,Y);
        for(double Z:{Floor-.001,Floor+.001,5.8,16.,26.,40.})Points.Add(C->BodyPoint(FVector(X,Y,Z)));
    }
    double BeddingError=0;
    for(const FVector& P:Points)BeddingError=FMath::Max(BeddingError,FMath::Abs(C->BeddingPoint(P).Z-C->BeddingPoint(P,false).Z));
    TArray<double> Reference,Compiled;double Begin=FPlatformTime::Seconds();C->Sample(Points,Reference,false);const double ReferenceSeconds=FPlatformTime::Seconds()-Begin;
    Begin=FPlatformTime::Seconds();C->Sample(Points,Compiled,true);const double CompiledSeconds=FPlatformTime::Seconds()-Begin;
    double FieldError=0;int32 SignErrors=0;
    for(int32 I=0;I<Points.Num();++I)
    {
        FieldError=FMath::Max(FieldError,FMath::Abs(Reference[I]-Compiled[I]));
        if(FMath::Abs(Reference[I])>.0005&&(Reference[I]>0)!=(Compiled[I]>0))++SignErrors;
    }
    TArray<FVector> SurfacePoints;for(int32 I=0;I<120000;++I)
    {
        const double Z=1-2*(I+.5)/120000.,Angle=I*2.399963229728653;
        SurfacePoints.Add(FVector(FMath::Cos(Angle)*FMath::Sqrt(1-Z*Z),FMath::Sin(Angle)*FMath::Sqrt(1-Z*Z),Z));
    }
    for(int32 I=0;I<Points.Num();I+=3)SurfacePoints.Add(Points[I]);
    // Both sides of all cube seams/corners and the poles, independently of
    // the chart compiler's knots and deterministic dominant-axis tie handling.
    for(double V=-1;V<=1;V+=.017)for(double Epsilon:{-1.e-8,0.,1.e-8})for(double Sign:{-1.,1.})
    {
        SurfacePoints.Add(FVector(1,Sign*(1+Epsilon),V));SurfacePoints.Add(FVector(V,1,Sign*(1+Epsilon)));SurfacePoints.Add(FVector(Sign*(1+Epsilon),V,1));
    }
    for(const FVector D:{FVector::XAxisVector,-FVector::XAxisVector,FVector::YAxisVector,-FVector::YAxisVector,FVector::ZAxisVector,-FVector::ZAxisVector})SurfacePoints.Add(D);
    TArray<double> Heights,CompiledHeights;Plan->SampleHeightsMeters(SurfacePoints,Heights);C->SampleSurfaceHeights(*Plan,SurfacePoints,CompiledHeights);double HeightError=0;
    for(int32 I=0;I<Heights.Num();++I)HeightError=FMath::Max(HeightError,FMath::Abs(Heights[I]-CompiledHeights[I]));
    TestTrue(TEXT("Independent whole sphere and local heights agree within one millimetre"),HeightError<.001);
    TArray<FVector> Coherent;const auto& W=*Plan->Watershed;const auto& Cells=W.GetCells();const auto& Triangles=W.GetTriangles();
    for(int32 I=0;I<Triangles.Num();I+=FMath::Max(1,Triangles.Num()/256))
    {
        const auto T=Triangles[I];const FVector D=(Cells[T.X].Direction*.31+Cells[T.Y].Direction*.27+Cells[T.Z].Direction*.42).GetSafeNormal();
        for(int32 K=0;K<32;++K)Coherent.Add(D+FVector(K*1.e-8,-K*1.e-8,K*2.e-8));
    }
    // Include exact mesh vertices/edges, chart seams, random and invalid input.
    TArray<FVector> Boundary=SurfacePoints;Boundary.Add(FVector::ZeroVector);
    for(int32 I=0;I<Cells.Num();I+=127)Boundary.Add(Cells[I].Direction);
    auto CheckWater=[&](TConstArrayView<FVector> Q,int32& Searches)
    {
        TArray<double> Original,Fast;Original.Init(-205.,Q.Num());Fast=Original;
        for(int32 I=0;I<Q.Num();++I)Original[I]=W.MapHeight(Q[I],Original[I]);
        W.MapHeights(Q,Fast,&Searches);double Error=0;for(int32 I=0;I<Q.Num();++I)Error=FMath::Max(Error,FMath::Abs(Original[I]-Fast[I]));return Error;
    };
    int32 Searches,BoundarySearches;double WaterError=CheckWater(Coherent,Searches);WaterError=FMath::Max(WaterError,CheckWater(Boundary,BoundarySearches));
    TestEqual(TEXT("Batched watershed matches canonical tree queries exactly, including seams"),WaterError,0.);
    TestTrue(TEXT("Coherent voxel packets bypass repeated tree traversal"),Searches<Coherent.Num()/10);
    int32 Nodes,ExactLeaves;uint64 Bytes;double Validation;C->GetBeddingCompilationStats(Nodes,ExactLeaves,Bytes,Validation);
    TestTrue(TEXT("Independent bedding agreement within 0.1 millimetre"),BeddingError<.0001);
    TestTrue(TEXT("Independent field agreement within 0.1 millimetre"),FieldError<.0001);
    TestEqual(TEXT("No solid/air changes outside the 0.5 mm comparison band"),SignErrors,0);
    TestTrue(TEXT("Compiled bedding storage bounded to sixteen MiB"),Bytes<16*1024*1024);
    TestTrue(TEXT("Directional input reduces field evaluation CPU"),CompiledSeconds<ReferenceSeconds*.5);
    auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("points"),Points.Num());O->SetNumberField(TEXT("watershedCoherentPoints"),Coherent.Num());O->SetNumberField(TEXT("watershedTreeQueries"),Searches);O->SetNumberField(TEXT("watershedMaximumErrorMeters"),WaterError);O->SetNumberField(TEXT("surfacePoints"),SurfacePoints.Num());O->SetNumberField(TEXT("maximumSurfaceHeightErrorMeters"),HeightError);int32 SN,SL;uint64 SB,SI,SE;C->GetSurfaceCompilationStats(SN,SL,SB,SI,SE);O->SetNumberField(TEXT("surfaceNodes"),SN);O->SetNumberField(TEXT("surfaceExactLeaves"),SL);O->SetNumberField(TEXT("surfaceBytes"),SB);O->SetNumberField(TEXT("surfaceInterpolatedPoints"),SI);O->SetNumberField(TEXT("surfaceExactPoints"),SE);O->SetNumberField(TEXT("maximumBeddingErrorMeters"),BeddingError);O->SetNumberField(TEXT("maximumFieldErrorMeters"),FieldError);O->SetNumberField(TEXT("signErrorsOutsideTolerance"),SignErrors);O->SetNumberField(TEXT("nodes"),Nodes);O->SetNumberField(TEXT("exactLeaves"),ExactLeaves);O->SetNumberField(TEXT("bytes"),Bytes);O->SetNumberField(TEXT("maximumCompilerValidationErrorMeters"),Validation);O->SetNumberField(TEXT("wholeRecipeCompileSeconds"),Compile);O->SetNumberField(TEXT("referenceSeconds"),ReferenceSeconds);O->SetNumberField(TEXT("compiledSeconds"),CompiledSeconds);
    FString Path;if(FParse::Value(FCommandLine::Get(),TEXT("CLBeddingReport="),Path)){FString Json;FJsonSerializer::Serialize(O,TJsonWriterFactory<>::Create(&Json));FFileHelper::SaveStringToFile(Json,*Path);}
    AddInfo(FString::Printf(TEXT("BEDDING points=%d nodes=%d exact=%d error=%.9fm fieldError=%.9fm reference=%.3fs compiled=%.3fs"),Points.Num(),Nodes,ExactLeaves,BeddingError,FieldError,ReferenceSeconds,CompiledSeconds));return !HasAnyErrors();
}
#endif
