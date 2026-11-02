#include "Misc/AutomationTest.h"
#include "Model/CLTectonics.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLSphericalPatches.h"
#include "CLPlanetStamp.h"
#include "VoxelLayers.h"
#include "VoxelQuery.h"
#include "Surface/VoxelSurfaceTypeTable.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLTectonicProgressionTest,"Catalyst.Landform.Tectonics.ProgressionAndTopology",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLTectonicProgressionTest::RunTest(const FString&)
{
    FString Error;FCLTectonicPreset P;P.bEnabled=true;P.AlgorithmVersion=2;
    for(int32 Seed:{0,1729,-531})for(int32 Count=1;Count<=12;++Count)
    {
        P.bOverridePlateCount=true;P.PlateCount=Count;const auto T=FCLCompiledTectonics::Compile(P,Seed,300,Error);if(!T){AddError(Error);return false;}
        TestEqual(TEXT("Pinned count is exact"),T->Lower.Plates.Num(),Count);
        TestEqual(TEXT("Closed generic spherical Voronoi graph has expected edges"),T->Lower.Boundaries.Num(),Count==1?0:Count==2?4:3*Count-6);
        for(const auto& E:T->Lower.Boundaries)
        {
            const double Score=FVector::DotProduct(E.Mid,T->Lower.Plates[E.A].Site);
            TestTrue(TEXT("Boundary lies on shared spherical bisector"),FMath::Abs(FVector::DotProduct(E.Mid,E.Normal))<1.e-8);
            for(const auto& V:T->Lower.Plates)TestTrue(TEXT("Boundary is not an extension through a third plate"),FVector::DotProduct(E.Mid,V.Site)<=Score+1.e-7);
            const FVector Relative=FVector::CrossProduct(T->Lower.Plates[E.A].AngularVelocity-T->Lower.Plates[E.B].AngularVelocity,E.Mid);
            TestTrue(TEXT("Convergence comes from tangent relative motion"),FMath::IsNearlyEqual(E.Convergence,-FVector::DotProduct(Relative,E.Normal),1.e-10));
        }
    }
    P=FCLTectonicPreset();P.Activity=0;FCLTectonicControls Quiet,Intense;TestTrue(TEXT("Quiet mapping"),FCLTectonicControls::Resolve(P,Quiet,Error));P.Activity=1;FCLTectonicControls::Resolve(P,Intense,Error);
    TestTrue(TEXT("One knob coordinates count, motion, crust, width and relief"),Quiet.LowerPlateCount==1&&Intense.LowerPlateCount==12&&Quiet.Motion==0&&Intense.Motion>Quiet.Motion&&Intense.CrustThickness>Quiet.CrustThickness&&Intense.BeltWidth>Quiet.BeltWidth&&Intense.Relief>Quiet.Relief);
    P.bOverridePlateCount=true;P.PlateCount=4;P.bOverrideMotion=true;P.Motion=.3;P.bOverrideRelief=true;P.Relief=.2;FCLTectonicControls C;FCLTectonicControls::Resolve(P,C,Error);
    TestTrue(TEXT("Advanced pins survive knob changes"),C.LowerPlateCount==4&&C.UpperPlateCount==4&&C.Motion==.3&&C.Relief==.2);
    P.Activity=2;TestFalse(TEXT("Out of range activity is rejected"),FCLTectonicControls::Resolve(P,C,Error));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLTectonicContinuityTest,"Catalyst.Landform.Tectonics.KnobContinuityAndIdentity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLTectonicContinuityTest::RunTest(const FString&)
{
    FString Error;auto R=FCLPlanetCompiler::TectonicBase();double RMS[3]={};
    TArray<FVector> D;for(int32 F=0;F<6;++F)for(int32 Y=0;Y<=16;++Y)for(int32 X=0;X<=16;++X)D.Add(FCLSphericalPatches::Direction(F,FVector2D(X/8.-1,Y/8.-1)));
    TArray<FGuid> LastIds;
    for(int32 K=0;K<3;++K)
    {
        R.Tectonics.Activity=K*.5;const auto T=FCLCompiledTectonics::Compile(R.Tectonics,R.Seed,300,Error);if(!T){AddError(Error);return false;}
        for(int32 I=0;I<LastIds.Num();++I)TestTrue(TEXT("Existing plate identities survive activity changes"),T->Lower.Plates.IsValidIndex(I)&&T->Lower.Plates[I].Id==LastIds[I]);
        LastIds.Reset();for(const auto& V:T->Lower.Plates)LastIds.Add(V.Id);
        for(const auto& V:D){const double H=T->SampleDeltaMeters(V);RMS[K]+=H*H;TestTrue(TEXT("Analytic terrain bound contains every sample"),FMath::Abs(H)<=T->MaximumDeltaMeters+1.e-8);}
        RMS[K]=FMath::Sqrt(RMS[K]/D.Num());
    }
    TestTrue(TEXT("Quiet, moderate, intense have progressively greater global relief"),RMS[0]<RMS[1]&&RMS[1]<RMS[2]);double Worst=0;
    for(int32 N=1;N<12;++N)
    {
        R.Tectonics.Activity=FMath::Max(0.,N/11.-1.e-7);auto A=FCLCompiledTectonics::Compile(R.Tectonics,R.Seed,300,Error);
        R.Tectonics.Activity=FMath::Min(1.,N/11.+1.e-7);auto B=FCLCompiledTectonics::Compile(R.Tectonics,R.Seed,300,Error);
        for(const auto& V:D)Worst=FMath::Max(Worst,FMath::Abs(A->SampleDeltaMeters(V)-B->SampleDeltaMeters(V)));
    }
    TestTrue(TEXT("New plates have no height pop at activation thresholds"),Worst<.005);
    AddInfo(FString::Printf(TEXT("Tectonics RMS relief: %.4f / %.4f / %.4f m; threshold change %.9g m"),RMS[0],RMS[1],RMS[2],Worst));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLTectonicNativeTest,"Catalyst.Landform.Tectonics.NativeFieldAndPersistence",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLTectonicNativeTest::RunTest(const FString&)
{
    FString Error,Json;auto R=FCLPlanetCompiler::TectonicBase();
    const auto Plan=FCLPlanetCompiler::Compile(R,Error);if(!Plan){AddError(Error);return false;}
    auto* Asset=NewObject<UCLPlanetRecipeAsset>();Asset->Recipe=R;TestTrue(TEXT("Preset exports"),Asset->ExportJsonString(Json,Error));auto* Reload=NewObject<UCLPlanetRecipeAsset>();TestTrue(TEXT("Portable preset imports"),Reload->ImportRecipeJson(Json,Error));
    TestTrue(TEXT("Full control bindings roundtrip"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&R,&Reload->Recipe,0));
    R.SchemaVersion=13;TestFalse(TEXT("Old schema cannot gain tectonic semantics"),FCLPlanetCompiler::Compile(R,Error).IsValid());R.SchemaVersion=14;
    const auto Renamed=R.Tectonics.LowerGuide;R.RenameGuideReferences(Renamed,TEXT("LowerGround"));TestTrue(TEXT("Global guide renaming includes tectonic inputs"),R.Tectonics.LowerGuide==TEXT("LowerGround"));R=Asset->Recipe;
    TArray<FVector> D;for(int32 F=0;F<6;++F)for(int32 Y=0;Y<=12;++Y)for(int32 X=0;X<=12;++X)D.Add(FCLSphericalPatches::Direction(F,FVector2D(X/6.-1,Y/6.-1)));
    TArray<double> H;Plan->SampleHeightsMeters(D,H);const auto Whole=FCLPlanetCompiler::Compile(R,Error,nullptr,nullptr,FCLSphericalPatches::WholeSphere);TArray<double> HW;Whole->SampleHeightsMeters(D,HW);
    FCLPlanetStamp Stamp;Stamp.bUseRecipe=true;Stamp.Radius=100000;Stamp.Recipe=R;const auto Ref=FVoxelStampRef::New(Stamp);const auto Native=StaticCastSharedPtr<FCLPlanetStampRuntime>(FVoxelStampRuntime::Create(nullptr,Ref,nullptr));
    if(!TestTrue(TEXT("Native terrain compiles"),Native.IsValid()))return false;
    FVoxelDependencyCollector Dependencies(STATIC_FNAME("CL.Tectonics"));const auto Table=FVoxelSurfaceTypeTable::Get();const FVoxelQuery Query(0,FVoxelLayers::Empty(),*Table,Dependencies);
    FVoxelDoubleVectorBuffer Points;Points.Allocate(D.Num());for(int32 I=0;I<D.Num();++I)Points.Set(I,D[I]*(R.RadiusMeters+H[I])*100);const auto Dist=Native->SampleDistances(Query,Points);double Worst=0;
    for(int32 I=0;I<D.Num();++I){TestTrue(TEXT("Atlas and whole-sphere sampling agree"),FMath::IsNearlyEqual(H[I],HW[I],1.e-9));Worst=FMath::Max(Worst,FMath::Abs(double(Dist[I])));}
    TestTrue(TEXT("VPP zero set matches the Builder field"),Worst<.02);AddInfo(FString::Printf(TEXT("Native tectonics: %d probes, %.9g cm residual"),D.Num(),Worst));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLTectonicBeltsTest,"Catalyst.Landform.Tectonics.OrganicBeltsAndLegacy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLTectonicBeltsTest::RunTest(const FString&)
{
    FString Error;FCLTectonicPreset P;P.bEnabled=true;P.AlgorithmVersion=1;
    auto Legacy=FCLCompiledTectonics::Compile(P,1729,120,Error);
    // Retained algorithm-1 reference samples, recorded before the geometry change.
    const double Recorded[]={-18.59694152632665,-17.99704511739856,-17.05154952513567};
    for(int32 X=0;X<3;++X)TestTrue(TEXT("Algorithm 1 retains its published field"),FMath::IsNearlyEqual(Legacy->SampleDeltaMeters(FCLSphericalPatches::Direction(0,FVector2D(X/6.-1,-1))),Recorded[X],1.e-8));
    P.AlgorithmVersion=3;FCLTectonicControls Controls;TestFalse(TEXT("Unknown geometry version is refused"),FCLTectonicControls::Resolve(P,Controls,Error));
    double Changes[2]={};int32 Edges=0;double WorstJump=0;
    for(int32 Seed:{0,1729,-531})
    {
        P.Activity=1;P.AlgorithmVersion=1;Legacy=FCLCompiledTectonics::Compile(P,Seed,120,Error);
        P.AlgorithmVersion=2;const auto Organic=FCLCompiledTectonics::Compile(P,Seed,120,Error);
        P.bOverrideMotion=true;P.Motion=0;const auto Crust=FCLCompiledTectonics::Compile(P,Seed,120,Error);P.bOverrideMotion=false;
        for(const auto& E:Organic->Lower.Boundaries)
        {
            const double Half=FMath::Acos(FMath::Clamp(E.HalfCos,-1.,1.));if(Half<.25)continue;
            const FVector Axis=FVector::CrossProduct(E.Normal,E.Mid).GetSafeNormal();double Min[2]={1,1},Max[2]={-1,-1};
            // Track the cross-belt centre of relief along each long boundary.
            for(int32 K=0;K<=12;++K)
            {
                const double T=Half*(K/6.-1)*.7;const FVector Center=E.Mid*FMath::Cos(T)+Axis*FMath::Sin(T);
                for(int32 Version=0;Version<2;++Version)
                {
                    const auto& Field=Version==0?Legacy:Organic;double Sum=0,Weighted=0;
                    for(int32 J=-48;J<=48;++J)
                    {
                        const double Offset=J/16.*Organic->Controls.BeltWidth;
                        const FVector D=Center*FMath::Cos(Offset)+E.Normal*FMath::Sin(Offset);
                        const double Weight=FMath::Abs(Field->SampleDeltaMeters(D)-Crust->SampleDeltaMeters(D));
                        Sum+=Weight;Weighted+=Weight*Offset;
                    }
                    const double Position=Sum>1.e-6?Weighted/Sum:0;Min[Version]=FMath::Min(Min[Version],Position);Max[Version]=FMath::Max(Max[Version],Position);
                }
            }
            for(int32 V=0;V<2;++V)Changes[V]+=(Max[V]-Min[V])/Organic->Controls.BeltWidth;++Edges;
        }
        for(int32 F=0;F<6;++F)for(int32 Y=0;Y<=24;++Y)for(int32 X=0;X<=24;++X)
        {
            const FVector D=FCLSphericalPatches::Direction(F,FVector2D(X/12.-1,Y/12.-1));
            const FVector T=FVector::CrossProduct(D,FMath::Abs(D.Z)<.9?FVector::ZAxisVector:FVector::XAxisVector).GetSafeNormal();
            const double H=Organic->SampleDeltaMeters(D),Near=Organic->SampleDeltaMeters((D+T*1.e-6).GetSafeNormal());
            TestTrue(TEXT("Organic relief remains inside the native field bound"),FMath::IsFinite(H)&&FMath::Abs(H)<=Organic->MaximumDeltaMeters);
            WorstJump=FMath::Max(WorstJump,FMath::Abs(H-Near));
        }
    }
    TestTrue(TEXT("Deformation removes the straight belt centre lines"),Edges>30&&Changes[1]>Changes[0]*1.2);
    TestTrue(TEXT("No discontinuity across spherical sampling directions"),WorstJump<.01);
    AddInfo(FString::Printf(TEXT("Long belts %d; mean lateral variation legacy/organic %.4f / %.4f belt widths; worst microstep %.9g m"),Edges,Changes[0]/Edges,Changes[1]/Edges,WorstJump));return true;
}
