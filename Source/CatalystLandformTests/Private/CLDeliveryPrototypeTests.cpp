#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Interfaces/IPluginManager.h"
#include "CLDeliveryPrototype.h"
#include "CLDeliveryGraphTools.h"
#include "Graphs/VoxelVolumeGraph.h"
#include "Graphs/VoxelVolumeGraphStamp.h"
#include "VoxelLinearColorMetadata.h"
#include "VoxelLayers.h"
#include "VoxelQuery.h"
#include "Surface/VoxelSurfaceTypeTable.h"
#include "UObject/StrongObjectPtr.h"
#include "Async/Async.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLDeliveryPrototypeTest,"Catalyst.Landform.Caves.DeliveryPrototype.Parity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLDeliveryPrototypeTest::RunTest(const FString&)
{
    FString E,J;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/OrganicKarst.json")));
    TStrongObjectPtr<UCLPlanetRecipeAsset> Asset(NewObject<UCLPlanetRecipeAsset>());if(!Asset->ImportRecipeJson(J,E)){AddError(E);return false;}
    FCLPlanetStamp Original;Original.bUseRecipe=true;Original.Recipe=Asset->Recipe;Original.Radius=Asset->Recipe.RadiusMeters*100;Original.Behavior=EVoxelStampBehavior::AffectAll;
    auto Native=StaticCastSharedPtr<FCLPlanetStampRuntime>(FVoxelStampRuntime::Create(nullptr,FVoxelStampRef::New(Original),nullptr));
    if(!Native){AddError(TEXT("Missing published runtime"));return false;}
    FCLDeliveryBinding B;B.Snapshot=FGuid::NewGuid();B.Source=Native;TestTrue(TEXT("Qualified immutable binding"),B.IsValid());
    FCLDeliveryStamp Cpp;Cpp.Binding=B;Cpp.Behavior=Original.Behavior;
    auto CppRuntime=FVoxelStampRuntime::Create(nullptr,FVoxelStampRef::New(Cpp),nullptr);
    TStrongObjectPtr<UVoxelVolumeGraph> Graph;
    if(FParse::Param(FCommandLine::Get(),TEXT("CLCreateDeliveryGraph")))Graph.Reset(CLCreateDeliveryGraph(E,true));
    else Graph.Reset(LoadObject<UVoxelVolumeGraph>(nullptr,TEXT("/CatalystLandform/Prototype/VG_Delivery.VG_Delivery")));
    if(!Graph.IsValid()){AddError(E);return false;}
    FVoxelVolumeGraphStamp G;G.Graph=Graph.Get();G.Behavior=Original.Behavior;
    auto& Override=G.ParameterOverrides.GuidToValueOverride.FindOrAdd(CLDeliveryPrototype::ParameterGuid());Override.bEnable=true;Override.Value=FVoxelPinValue::Make(B);
    auto GraphRuntime=FVoxelStampRuntime::Create(nullptr,FVoxelStampRef::New(G),nullptr);
    if(!CppRuntime||!GraphRuntime){AddError(TEXT("Native prototype runtime initialization failed"));return false;}
    FVoxelDependencyCollector Dependencies(STATIC_FNAME("CL.Delivery.Parity"));const auto Table=FVoxelSurfaceTypeTable::Get();FVoxelQuery Q(0,FVoxelLayers::Empty(),*Table,Dependencies);
    TArray<FVector> Points;
    for(const auto& V:Native->RecipePlan->Caves->Passages)
    for(int32 I=0;I<V.Points.Num();I+=2)
    for(double X:{-12.,-4.,0.,4.,12.})for(double Z:{-3.,-.01,0.,.01,1.62,6.,14.})
    {const auto Up=V.Points[I].GetSafeNormal();FVector Side,Other;Up.FindBestAxisVectors(Side,Other);Points.Add(V.Points[I]+Side*X+Up*Z);}
    for(const auto& R:Native->RecipePlan->Caves->Chambers)
    for(double X=-35;X<=35;X+=7)for(double Y=-35;Y<=35;Y+=7)for(double Z:{-8.,-1.,0.,2.,15.,30.})Points.Add(R.Center+R.Rotation.RotateVector(FVector(X,Y,Z)));
    for(int32 I=0;I<4096;++I){const double Z=1-2*(I+.5)/4096,A=I*2.399963229728653;Points.Add(FVector(FMath::Cos(A)*FMath::Sqrt(1-Z*Z),FMath::Sin(A)*FMath::Sqrt(1-Z*Z),Z)*(I%2?600+I%201:double(I%800)));}
    FVoxelDoubleVectorBuffer XYZ;XYZ.Allocate(Points.Num());for(int32 I=0;I<Points.Num();++I)XYZ.Set(I,Points[I]*100);
    const auto Reference=Native->SampleDistances(Q,XYZ);const auto Optimized=B.Distances(Q,XYZ);double Worst=0;int32 Different=0;
    for(int32 I=0;I<Points.Num();++I){Worst=FMath::Max(Worst,FMath::Abs(double(Reference[I])-Optimized[I]));Different+=Reference[I]!=Optimized[I];}
    TestEqual(TEXT("Optimized complete field is bit-identical at dense floor, junction, room and whole-body probes"),Different,0);
    const auto Worker=Async(EAsyncExecution::ThreadPool,[B,&Q,XYZ]{return B.Distances(Q,XYZ);});const auto OnWorker=Worker.Get();for(int32 I=0;I<Points.Num();++I)if(OnWorker[I]!=Optimized[I]){AddError(TEXT("Worker result differs"));break;}
    // Saved native probes exposed regrouping-dependent noise rounding that the
    // dense lattice missed. Preserve exact partial-packet evaluation as well.
    const TArray<FVector> SavedPacket={
        {1471.6788181437457,-30385.591934679534,41571.080206995364},
        {1431.6754070914849,-31393.149152772316,40805.29881468118},
        {1392.4156070158617,-32394.79452627032,40007.68464175345},
        {-29396.559598480173,-248.8002032302682,-42287.52979992962},
        {-29862.527544405548,1167.289491534085,-41947.720729407505},
        {-30321.26476849565,2627.4153045625803,-41558.44761834656}};
    for(int32 Packet:{1,3,6})for(int32 StartIndex=0;StartIndex<SavedPacket.Num();StartIndex+=Packet)
    {
        const int32 Count=FMath::Min(Packet,SavedPacket.Num()-StartIndex);FVoxelDoubleVectorBuffer P;P.Allocate(Count);
        for(int32 I=0;I<Count;++I)P.Set(I,SavedPacket[StartIndex+I]);
        const auto A=Native->SampleDistances(Q,P),C=B.Distances(Q,P);
        for(int32 I=0;I<Count;++I)TestEqual(TEXT("Native saved-position partial packets remain bit-identical"),C[I],A[I]);
    }
    // Actual native sparse delivery includes geology and requested rock metadata.
    const int32 N=FMath::Min(2048,Points.Num());FVoxelDoubleVectorBuffer Probes;Probes.Allocate(N);for(int32 I=0;I<N;++I)Probes.Set(I,XYZ[I]);
    const auto Meta=Native->RockMetadata;
    TArray<TArray<float>> Fields;TArray<TVoxelArray<FVoxelSurfaceTypeBlend>> Surfaces;TArray<TSharedRef<FVoxelBuffer>> Properties;
    for(const auto& Runtime:{StaticCastSharedPtr<FVoxelStampRuntime>(Native),CppRuntime,GraphRuntime})
    {
        auto& F=Fields.AddDefaulted_GetRef();F.Init(FVoxelUtilities::NaNf(),N);auto& Types=Surfaces.AddDefaulted_GetRef();Types.SetNum(N);
        auto Values=Meta.MakeDefaultBuffer(N);Properties.Add(Values);TVoxelMap<FVoxelMetadataRef,TSharedRef<FVoxelBuffer>> Buffers;Buffers.Add_CheckNew(Meta,Values);
        StaticCastSharedPtr<FVoxelVolumeStampRuntime>(Runtime)->Apply(FVoxelVolumeSparseQuery::Create(Q,MakeVoxelArrayView(F),Types,FVoxelMetadataMap(Buffers),Probes,true,{Meta}),FVoxelVolumeTransform());
    }
    bool HasSolid=false,HasAir=false;for(const float Value:Fields[0]){HasSolid|=Value<-1;HasAir|=Value>1;}
    TestTrue(TEXT("Native sparse control contains both solid and cave air, rather than an empty or constant result"),HasSolid&&HasAir);
    for(int32 K=1;K<3;++K)
    {
        int32 Errors=0;for(int32 I=0;I<N;++I)Errors+=Fields[K][I]!=Fields[0][I]||!(Surfaces[K][I]==Surfaces[0][I])||Properties[K]->AsChecked<FVoxelLinearColorBuffer>()[I]!=Properties[0]->AsChecked<FVoxelLinearColorBuffer>()[I];
        TestEqual(*FString::Printf(TEXT("Native %s sparse field, surfaces and rock metadata match"),K==1?TEXT("C++"):TEXT("graph")),Errors,0);
    }
    const FVector Start=Native->RecipePlan->Caves->Chambers[0].Center*100-FVector(800);const FIntVector Size(17);const double Step=100.;TArray<float> Expected;
    for(const auto& Runtime:{StaticCastSharedPtr<FVoxelStampRuntime>(Native),CppRuntime,GraphRuntime})
    {
        TArray<float> F;F.Init(FVoxelUtilities::NaNf(),Size.X*Size.Y*Size.Z);StaticCastSharedPtr<FVoxelVolumeStampRuntime>(Runtime)->Apply(FVoxelVolumeBulkQuery::Create(Q,MakeVoxelArrayView(F),Start,Size,Step),FVoxelVolumeTransform());
        if(Expected.IsEmpty())Expected=F;else TestTrue(TEXT("Actual native bulk lattice is bit-identical"),F==Expected);
    }
    // Ordered authored cuts/backfill must remain after cave/floor composition.
    FCLPlanetStamp Edited=Original;Edited.Recipe.Volumes.bEnabled=true;
    FCLSolidOperation Fill;Fill.Id=FGuid(1,2,3,4);Fill.Name=TEXT("Delivery backfill");Fill.CenterMeters=Native->RecipePlan->Caves->Passages[0].Points[0]+Native->RecipePlan->Caves->Passages[0].Points[0].GetSafeNormal()*1.62;Fill.RadiusMeters=3;Fill.SmoothnessMeters=.4;Fill.SurfaceId=Native->RecipePlan->Surfaces->Ids[0];
    FCLSolidOperation Cut=Fill;Cut.Id=FGuid(5,6,7,8);Cut.Name=TEXT("Delivery excavation");Cut.Composition=ECLSolidComposition::Subtract;Cut.CenterMeters+=FVector(2,0,0);Cut.RadiusMeters=1.;Cut.SmoothnessMeters=.2;
    Edited.Recipe.Volumes.Operations={Fill,Cut};
    auto EditedNative=StaticCastSharedPtr<FCLPlanetStampRuntime>(FVoxelStampRuntime::Create(nullptr,FVoxelStampRef::New(Edited),nullptr));
    if(!EditedNative){AddError(TEXT("Authored edit control failed"));return false;}
    FCLDeliveryBinding EB=B;EB.Source=EditedNative;EB.Snapshot=FGuid::NewGuid();FCLDeliveryStamp EC=Cpp;EC.Binding=EB;auto EditedCpp=FVoxelStampRuntime::Create(nullptr,FVoxelStampRef::New(EC),nullptr);
    auto EG=G;EG.ParameterOverrides.GuidToValueOverride.FindChecked(CLDeliveryPrototype::ParameterGuid()).Value=FVoxelPinValue::Make(EB);auto EditedGraph=FVoxelStampRuntime::Create(nullptr,FVoxelStampRef::New(EG),nullptr);
    if(!EditedCpp||!EditedGraph){AddError(TEXT("Authored edit prototype activation failed"));return false;}
    const auto Before=Native->SampleDistances(Q,Probes),After=EditedNative->SampleDistances(Q,Probes);bool Changed=false;for(int32 I=0;I<N;++I)Changed|=Before[I]!=After[I];TestTrue(TEXT("Authored backfill/excavation actually changes this cave"),Changed);
    Fields.Reset();Surfaces.Reset();Properties.Reset();
    for(const auto& Runtime:{StaticCastSharedPtr<FVoxelStampRuntime>(EditedNative),EditedCpp,EditedGraph})
    {
        auto& F=Fields.AddDefaulted_GetRef();F.Init(FVoxelUtilities::NaNf(),N);auto& Types=Surfaces.AddDefaulted_GetRef();Types.SetNum(N);
        auto Values=Meta.MakeDefaultBuffer(N);Properties.Add(Values);TVoxelMap<FVoxelMetadataRef,TSharedRef<FVoxelBuffer>> Buffers;Buffers.Add_CheckNew(Meta,Values);
        StaticCastSharedPtr<FVoxelVolumeStampRuntime>(Runtime)->Apply(FVoxelVolumeSparseQuery::Create(Q,MakeVoxelArrayView(F),Types,FVoxelMetadataMap(Buffers),Probes,true,{Meta}),FVoxelVolumeTransform());
    }
    for(int32 K=1;K<3;++K)
    {
        int32 Errors=0;for(int32 I=0;I<N;++I)Errors+=Fields[K][I]!=Fields[0][I]||!(Surfaces[K][I]==Surfaces[0][I])||Properties[K]->AsChecked<FVoxelLinearColorBuffer>()[I]!=Properties[0]->AsChecked<FVoxelLinearColorBuffer>()[I];
        TestEqual(TEXT("Authored cuts/backfill keep exact native fields, contact materials and geology"),Errors,0);
    }
    AddInfo(FString::Printf(TEXT("DELIVERY_PARITY points=%d bitDifferences=%d maxErrorCm=%.12g; native sparse=%d bulk=4913; same compiled geology"),Points.Num(),Different,Worst,N));
    FString BenchmarkReport;
    if(!HasAnyErrors()&&FParse::Value(FCommandLine::Get(),TEXT("CLDeliveryBenchmarkReport="),BenchmarkReport))
    {
        TArray<FVector> Centers;
        for(const auto& V:Native->RecipePlan->Caves->Passages)if(!V.Entrance&&Centers.Num()<8)Centers.Add(V.Points[V.Points.Num()/2]*100);
        for(const auto& R:Native->RecipePlan->Caves->Chambers)Centers.Add(Native->RecipePlan->Caves->Nodes[R.Node].Position*100);
        for(int32 I=0;I<12;++I){const double Z=1-2*(I+.5)/12,A=I*2.399963229728653;const FVector D(FMath::Cos(A)*FMath::Sqrt(1-Z*Z),FMath::Sin(A)*FMath::Sqrt(1-Z*Z),Z);Centers.Add(D*(Native->RecipePlan->RadiusMeters+Native->RecipePlan->SampleHeightMeters(D))*100);}
        const TArray<TSharedPtr<FVoxelStampRuntime>> Runtimes={Native,CppRuntime,GraphRuntime};const TCHAR* Names[]={TEXT("legacy"),TEXT("cpp"),TEXT("graph")};
        FVoxelDoubleVectorBuffer Scatter;Scatter.Allocate(2048);
        for(int32 I=0;I<2048;++I){const double Z=1-2*(I+.5)/2048,A=I*2.399963229728653;const FVector D(FMath::Cos(A)*FMath::Sqrt(1-Z*Z),FMath::Sin(A)*FMath::Sqrt(1-Z*Z),Z);Scatter.Set(I,D*(Native->RecipePlan->RadiusMeters+Native->RecipePlan->SampleHeightMeters(D))*100);}
        TArray<TSharedPtr<FJsonValue>> Runs;TMap<int32,double> ExpectedChecksums;
        for(int32 Scenario=0;Scenario<3;++Scenario)
        for(int32 Round=-1;Round<3;++Round)for(int32 Order=0;Order<3;++Order)
        {
            const int32 K=(Order+FMath::Max(Round,0))%3;const auto R=StaticCastSharedPtr<FVoxelVolumeStampRuntime>(Runtimes[K]);
            FVoxelQuery Replay(Scenario==1?4:0,FVoxelLayers::Empty(),*Table,Dependencies);
            const double Started=FPlatformTime::Seconds();double Checksum=0;int32 Count=0;
            if(Scenario<2)for(const auto& Center:Centers)
            {
                const double ReplayStep=Scenario==0?20.:320.;
                TArray<float> F;F.Init(FVoxelUtilities::NaNf(),4913);R->Apply(FVoxelVolumeBulkQuery::Create(Replay,MakeVoxelArrayView(F),Center-FVector(ReplayStep*8),FIntVector(17),ReplayStep),FVoxelVolumeTransform());
                for(int32 I=0;I<F.Num();++I)Checksum+=double(F[I])*(1+I%13);Count+=F.Num();
            }
            else for(int32 Repeat=0;Repeat<4;++Repeat)
            {
                TArray<float> F;F.Init(FVoxelUtilities::NaNf(),2048);TVoxelArray<FVoxelSurfaceTypeBlend> Types;Types.SetNum(2048);
                auto Values=Meta.MakeDefaultBuffer(2048);TVoxelMap<FVoxelMetadataRef,TSharedRef<FVoxelBuffer>> Buffers;Buffers.Add_CheckNew(Meta,Values);
                R->Apply(FVoxelVolumeSparseQuery::Create(Replay,MakeVoxelArrayView(F),Types,FVoxelMetadataMap(Buffers),Scatter,true,{Meta}),FVoxelVolumeTransform());
                const auto& Colors=Values->AsChecked<FVoxelLinearColorBuffer>();
                for(int32 I=0;I<F.Num();++I)Checksum+=(double(F[I])+Colors[I].R+Colors[I].G)*(1+I%13);Count+=F.Num();
            }
            const double Elapsed=FPlatformTime::Seconds()-Started;
            if(!ExpectedChecksums.Contains(Scenario))ExpectedChecksums.Add(Scenario,Checksum);
            else TestEqual(TEXT("Benchmark queries retain identical field and metadata checksums"),Checksum,ExpectedChecksums[Scenario]);
            if(Round>=0){auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("scenario"),Scenario==0?TEXT("fine_bulk"):Scenario==1?TEXT("coarse_bulk"):TEXT("scattered_attributes"));Row->SetStringField(TEXT("backend"),Names[K]);Row->SetNumberField(TEXT("round"),Round);Row->SetNumberField(TEXT("points"),Count);Row->SetNumberField(TEXT("seconds"),Elapsed);Row->SetNumberField(TEXT("checksum"),Checksum);Runs.Add(MakeShared<FJsonValueObject>(Row));}
        }
        auto Report=MakeShared<FJsonObject>();Report->SetNumberField(TEXT("profile"),7);Report->SetNumberField(TEXT("bulkGridEdge"),17);Report->SetNumberField(TEXT("stepCm"),20);Report->SetNumberField(TEXT("centers"),Centers.Num());Report->SetArrayField(TEXT("runs"),Runs);
        FString Json;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));FFileHelper::SaveStringToFile(Json,*BenchmarkReport);
    }
    return !HasAnyErrors();
}
#endif
