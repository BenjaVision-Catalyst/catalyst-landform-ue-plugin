#pragma once
#include "CoreMinimal.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLCompileProgress.h"
#include "Tasks/Task.h"
struct FCLRecipeEvaluationResult
{
    uint64 Serial=0;
    FCLPlanetRecipe Recipe;
    TSharedPtr<const FCLCompiledPlanetRecipe> Plan;
    FString Error;
    TArray<FVector2D> Elevation;
    FString ElevationReadout;
    FCLRockBedStatistics Beds;
    TArray<double> TargetBeds;
    bool WorkerThread=false;
    double Seconds=0;
};
/** One UE task plus one latest pending input. Owns no editor, asset or widget. */
class FCLRecipeEvaluation
{
public:
    ~FCLRecipeEvaluation(){Cancel();}
    TSharedPtr<const FCLPreparedPlanetCompile> Prepare(const FCLPlanetRecipe&,FString&,const FBox2D* Region=nullptr);
    uint64 Request(FCLPlanetRecipe,TSharedPtr<const FCLPreparedPlanetCompile>);
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
    uint64 Latest()const{return Serial;}
    uint32 Started=0,Discarded=0;
    double LastPreparationMs=0,MaximumPreparationMs=0;
private:
    struct FRequest{uint64 Serial;FCLPlanetRecipe Recipe;TSharedPtr<const FCLPreparedPlanetCompile> Prepared;};
    uint64 Serial=0,NativeEpoch=0;
    TSharedPtr<const FCLPreparedPlanetCompile> NativeCache;
    TOptional<FRequest> Active,Pending;
    TSharedPtr<FCLCompileProgress> Progress;
    UE::Tasks::TTask<TSharedPtr<FCLRecipeEvaluationResult>> Task;
};
