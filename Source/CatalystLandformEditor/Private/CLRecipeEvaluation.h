#pragma once
#include "CoreMinimal.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLCompileProgress.h"
#include "Tasks/Task.h"
struct FCLWatershedDebugGeometry;
struct FCLRecipeEvaluationResult
{
    uint64 Serial=0;
    FCLPlanetRecipe Recipe;
    TSharedPtr<const FCLCompiledPlanetRecipe> Plan;
    TSharedPtr<const FCLWatershedDebugGeometry> WatershedDebug;
    FString Error;
    TArray<FVector2D> Elevation;
    FString ElevationReadout;
    FCLRockBedStatistics Beds;
    TArray<double> TargetBeds;
    bool WorkerThread=false;
    bool SurfaceOnly=false;
    double Seconds=0;
};
/** One UE task plus one latest pending input. Owns no editor, asset or widget. */
class FCLRecipeEvaluation
{
public:
    ~FCLRecipeEvaluation(){Cancel();}
    TSharedPtr<const FCLPreparedPlanetCompile> Prepare(const FCLPlanetRecipe&,FString&,const FBox2D* Region=nullptr,FGuid SoloPreviewLandform=FGuid());
    uint64 Request(FCLPlanetRecipe,TSharedPtr<const FCLPreparedPlanetCompile>,bool SurfaceFirst=false);
    /** Called only after the viewport has published the checkpoint's native surface. */
    void ResumeAfterSurface(uint64 PublishedSerial);
    bool AwaitingSurface()const{return WaitingSurface.IsValid();}
    TSharedPtr<FCLRecipeEvaluationResult> Pump();
    void Cancel();
    static void Startup();
    /** Cancel and join native jobs before the editor module unloads. */
    static void Shutdown();
#if WITH_DEV_AUTOMATION_TESTS
    static int32 OutstandingTasks();
#endif
    bool IsBusy()const{return Active.IsSet()||Pending.IsSet();}
    ECLCompileStage Stage()const{return Progress?Progress->Stage.load():ECLCompileStage::Preparing;}
    uint64 Work()const{return Progress?Progress->Work.load():0;}
    TOptional<float> Fraction()const{const uint64 W=Work();return uint32(W>>32)?TOptional<float>(float(uint32(W))/uint32(W>>32)):TOptional<float>();}
    uint64 Latest()const{return Serial;}
    uint32 Started=0,Discarded=0;
    double LastPreparationMs=0,MaximumPreparationMs=0;
private:
    struct FRequest{uint64 Serial;FCLPlanetRecipe Recipe;TSharedPtr<const FCLPreparedPlanetCompile> Prepared;bool SurfaceFirst=false;};
    void Launch(TSharedPtr<FCLRecipeEvaluationResult> Surface=nullptr);
    uint64 Serial=0,NativeEpoch=0;
    TSharedPtr<const FCLPreparedPlanetCompile> NativeCache;
    TOptional<FRequest> Active,Pending;
    TSharedPtr<FCLCompileProgress> Progress;
    TSharedPtr<FCLRecipeEvaluationResult> WaitingSurface;
    UE::Tasks::TTask<TSharedPtr<FCLRecipeEvaluationResult>> Task;
};
