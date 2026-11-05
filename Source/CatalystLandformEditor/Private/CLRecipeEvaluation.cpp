#include "CLRecipeEvaluation.h"
#include "CLWatershedDebug.h"
#include "Model/CLSurfaceRange.h"
#include "Containers/Ticker.h"
#include "UObject/UObjectGlobals.h"
namespace
{
struct FLiveEvaluation{UE::Tasks::TTask<TSharedPtr<FCLRecipeEvaluationResult>> Task;TSharedRef<FCLCompileProgress> Control;};
TArray<FLiveEvaluation> LiveEvaluations;
FTSTicker::FDelegateHandle RetirementTicker;
FDelegateHandle ModifiedHandle,ChangedHandle;uint64 NativeInputEpoch=0;
void InvalidateNativeInputs(UObject* Object){if(Object&&!Object->IsA<UCLPlanetRecipeAsset>())++NativeInputEpoch;}
void RetireCompleted(){check(IsInGameThread());LiveEvaluations.RemoveAll([](const auto& R){return R.Task.IsCompleted();});}

void Measure(FCLRecipeEvaluationResult& R,const TSharedRef<FCLCompileProgress>& Control)
{
    const auto P=R.Plan;R.ElevationReadout=TEXT("Enable distribution to measure area fractions");
    if(P->SurfaceRange&&R.Recipe.SchemaVersion>=21)
    {
        TArray<FVector> D;for(int32 I=0;I<8192;++I){const double Z=1-2*(I+.5)/8192,A=(I+.719)*2.399963229728653;D.Add(FVector(FMath::Cos(A)*FMath::Sqrt(1-Z*Z),FMath::Sin(A)*FMath::Sqrt(1-Z*Z),Z));}
        TArray<double> H;P->SampleHeightsMeters(D,H);const auto& Range=*P->SurfaceRange;for(auto& V:H)V=FMath::Clamp((V-Range.TargetMin)/(Range.TargetMax-Range.TargetMin),0.,1.);H.Sort();
        const auto S=R.Recipe.ElevationDistribution;int32 Low=0,High=0;for(double V:H){Low+=V<=S.EndBandHeightPercent/100;High+=V>=1-S.EndBandHeightPercent/100;}
        R.Elevation.Add({0,0});for(int32 I=0;I<=128;++I){const int32 K=FMath::Min(H.Num()-1,I*(H.Num()-1)/128);R.Elevation.Add({H[K],double(K+1)/H.Num()});}R.Elevation.Add({1,1});
        R.ElevationReadout=FString::Printf(TEXT("Measured area · low %.1f%% · high %.1f%% · median %.0f%% height"),100.*Low/H.Num(),100.*High/H.Num(),100*H[H.Num()/2]);
    }
    if(Control->Cancelled.load())return;
    R.Beds=P->Rock?P->Rock->ThicknessStatistics():FCLRockBedStatistics();R.TargetBeds.Init(0,FCLRockBedStatistics::BinCount);
    if(R.Beds.Count){const auto S=R.Recipe.Rock.BedDistribution;const auto C=S.QuantileCurve();for(int32 I=0;I<8192;++I){const double T=C.Eval((I+.5f)/8192);const int32 B=FMath::Clamp(int32((T-S.MinimumMeters)/(S.MaximumMeters-S.MinimumMeters)*FCLRockBedStatistics::BinCount),0,FCLRockBedStatistics::BinCount-1);R.TargetBeds[B]+=1./8192;}}
}
}
TSharedPtr<const FCLPreparedPlanetCompile> FCLRecipeEvaluation::Prepare(const FCLPlanetRecipe& R,FString& E,const FBox2D* Region)
{
    const double Start=FPlatformTime::Seconds();const uint64 Epoch=NativeInputEpoch;auto P=FCLPlanetCompiler::PrepareBackground(R,E,Region,INDEX_NONE,NativeEpoch==Epoch?NativeCache.Get():nullptr);if(P){NativeCache=P;NativeEpoch=Epoch;}LastPreparationMs=(FPlatformTime::Seconds()-Start)*1000;MaximumPreparationMs=FMath::Max(MaximumPreparationMs,LastPreparationMs);return P;
}
uint64 FCLRecipeEvaluation::Request(FCLPlanetRecipe R,TSharedPtr<const FCLPreparedPlanetCompile> P)
{
    check(IsInGameThread());++Serial;if(Progress)Progress->Cancelled=true;Pending=FRequest{Serial,MoveTemp(R),MoveTemp(P)};return Serial;
}
void FCLRecipeEvaluation::Cancel(){++Serial;Pending.Reset();if(Progress)Progress->Cancelled=true;}
TSharedPtr<FCLRecipeEvaluationResult> FCLRecipeEvaluation::Pump()
{
    check(IsInGameThread());RetireCompleted();TSharedPtr<FCLRecipeEvaluationResult> Result;
    if(Active.IsSet()&&Task.IsCompleted())
    {
        auto Done=Task.GetResult();if(Done->Serial==Serial)Result=MoveTemp(Done);else ++Discarded;
        Active.Reset();Progress.Reset();Task={};
    }
    if(!Active.IsSet()&&Pending.IsSet())
    {
        Active=MoveTemp(Pending);Pending.Reset();Progress=MakeShared<FCLCompileProgress>();const auto Input=Active.GetValue();const auto Control=Progress.ToSharedRef();++Started;
        Task=UE::Tasks::Launch(UE_SOURCE_LOCATION,[Input,Control]()->TSharedPtr<FCLRecipeEvaluationResult>
        {
            const double Start=FPlatformTime::Seconds();auto R=MakeShared<FCLRecipeEvaluationResult>();R->Serial=Input.Serial;R->Recipe=Input.Recipe;R->WorkerThread=!IsInGameThread();
            if(Input.Prepared)R->Plan=FCLPlanetCompiler::CompleteBackground(*Input.Prepared,Control,R->Error);else R->Error=TEXT("Native input preparation failed");
            if(R->Plan&&!Control->Cancelled.load())Measure(*R,Control);
            if(R->Plan&&R->Plan->Watershed&&!Control->Cancelled.load())
            {Control->Stage=ECLCompileStage::Presentation;Control->Work=0;R->WatershedDebug=FCLWatershedDebugGeometry::Build(*R->Plan,&Control.Get());}
            R->Seconds=FPlatformTime::Seconds()-Start;return R;
        },UE::Tasks::ETaskPriority::BackgroundNormal);
        LiveEvaluations.Add({Task,Control});
    }
    return Result;
}

void FCLRecipeEvaluation::Startup()
{
    ModifiedHandle=FCoreUObjectDelegates::OnObjectModified.AddStatic(&InvalidateNativeInputs);
    ChangedHandle=FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda([](UObject* O,FPropertyChangedEvent&){InvalidateNativeInputs(O);});
    RetirementTicker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float){RetireCompleted();return true;}),.25f);
}
void FCLRecipeEvaluation::Shutdown()
{
    FCoreUObjectDelegates::OnObjectModified.Remove(ModifiedHandle);FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(ChangedHandle);
    FTSTicker::GetCoreTicker().RemoveTicker(RetirementTicker);RetirementTicker.Reset();
    check(IsInGameThread());for(const auto& R:LiveEvaluations)R.Control->Cancelled=true;
    for(const auto& R:LiveEvaluations)R.Task.Wait();LiveEvaluations.Reset();
}
#if WITH_DEV_AUTOMATION_TESTS
int32 FCLRecipeEvaluation::OutstandingTasks(){return LiveEvaluations.Num();}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Interfaces/IPluginManager.h"
#include "UObject/StrongObjectPtr.h"
#include "Stamps/CLStampLibrary.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLNativeInputReuseTest,"Catalyst.Landform.Background.NativeInputInvalidation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLNativeInputReuseTest::RunTest(const FString&)
{
    FString J,Error;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/GeneratedRegions.json")));
    TStrongObjectPtr<UCLPlanetRecipeAsset> Asset(NewObject<UCLPlanetRecipeAsset>());if(!Asset->ImportRecipeJson(J,Error)){AddError(Error);return false;}
    FCLRecipeEvaluation Evaluation;const auto First=Evaluation.Prepare(Asset->Recipe,Error);if(!First){AddError(Error);return false;}const double InitialMs=Evaluation.LastPreparationMs;
    const auto Reused=Evaluation.Prepare(Asset->Recipe,Error);if(!Reused){AddError(Error);return false;}const double ReusedMs=Evaluation.LastPreparationMs;
    TestTrue(TEXT("Unchanged native inputs are reused"),FCLCompileProgress::ReusedNativeInputs(*Reused)>0);
    Asset->PostEditChange();const auto OwnEdit=Evaluation.Prepare(Asset->Recipe,Error);if(!OwnEdit){AddError(Error);return false;}
    TestTrue(TEXT("Recipe notification alone does not invalidate matching native inputs"),FCLCompileProgress::ReusedNativeInputs(*OwnEdit)>0);
    // Broad invalidation covers native libraries/heightmaps/materials even when
    // their path/revision stays unchanged; the host assets need no mutation.
    TStrongObjectPtr<UCLStampLibraryAsset> Source(NewObject<UCLStampLibraryAsset>());Source->Modify();const auto Invalidated=Evaluation.Prepare(Asset->Recipe,Error);if(!Invalidated){AddError(Error);return false;}
    TestEqual(TEXT("An external object modification invalidates native input reuse"),FCLCompileProgress::ReusedNativeInputs(*Invalidated),0);
    auto Changed=Asset->Recipe;++Changed.Seed;const auto Upstream=Evaluation.Prepare(Changed,Error);if(!Upstream){AddError(Error);return false;}
    TestEqual(TEXT("Changed upstream recipe inputs cannot reuse old native fields"),FCLCompileProgress::ReusedNativeInputs(*Upstream),0);
    const FString Dir=FPaths::ProjectSavedDir()/TEXT("Verification/background");IFileManager::Get().MakeDirectory(*Dir,true);FFileHelper::SaveStringToFile(FString::Printf(TEXT("{\"initialMs\":%.6f,\"reusedMs\":%.6f,\"reusedInputs\":%d,\"invalidatedInputs\":%d,\"upstreamChangedInputs\":%d}\n"),InitialMs,ReusedMs,FCLCompileProgress::ReusedNativeInputs(*Reused),FCLCompileProgress::ReusedNativeInputs(*Invalidated),FCLCompileProgress::ReusedNativeInputs(*Upstream)),*(Dir/TEXT("native-input-cache.json")));
    return true;
}
#endif
