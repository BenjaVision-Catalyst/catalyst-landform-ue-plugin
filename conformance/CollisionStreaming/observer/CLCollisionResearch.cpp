#include "CLCollisionResearch.h"
#include "CLPlanetProof.h"
#include "CLPlanet.h"
#include "CLPlanetStamp.h"
#include "CLRadialGravityMovement.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "VoxelRuntime.h"
#include "VoxelState.h"
#include "VoxelQuery.h"
#include "VoxelLayers.h"
#include "Surface/VoxelSurfaceTypeTable.h"
#include "Collision/VoxelInvokerCollisionSubsystem.h"
#include "Collision/VoxelCollisionComponent.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

void CLCollisionResearchSnapshot(ACLPlanet& Planet,ACLProofCharacter& Walker,float Delta,const FString& Report)
{
    static int32 LastWait=0;static bool Holding=false;static double LastCapture=0,Started=FPlatformTime::Seconds();
    static TArray<TSharedPtr<FJsonValue>> Records;
    auto* Movement=Walker.RadialMovement();const int32 Wait=Movement->GetCollisionWaitFrames();
    const bool Held=Wait>LastWait;LastWait=Wait;const double Now=FPlatformTime::Seconds();
    const bool Changed=Held!=Holding;Holding=Held;
    if(!Changed&&(!Held||Now-LastCapture<1.))return;LastCapture=Now;
    const auto Runtime=Planet.GetRuntime();const auto Newest=Runtime->GetNewestState(),New=Runtime->GetNewState(),Old=Runtime->GetState();
    const FVector Here=Walker.GetActorLocation(),Up=Here.GetSafeNormal();const double Half=Walker.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const double Look=FMath::Clamp(Movement->GetMaxSpeed()*double(Delta)+Walker.GetCapsuleComponent()->GetScaledCapsuleRadius()*2.,150.,300.);
    const FVector Next=Here+FVector::VectorPlaneProject(Walker.GetActorForwardVector(),Up).GetSafeNormal()*Look,NextUp=Next.GetSafeNormal();
    const FVector A=Next+NextUp*Movement->MaxStepHeight,B=Next-NextUp*(Half+Movement->MaxStepHeight*2.);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(CLCollisionResearch),false);Params.AddIgnoredActor(&Walker);
    FHitResult Hit,Current;const bool HitNext=Planet.GetWorld()->LineTraceSingleByChannel(Hit,A,B,ECC_Visibility,Params);
    const bool HitCurrent=Planet.GetWorld()->LineTraceSingleByChannel(Current,Here+Up*Movement->MaxStepHeight,Here-Up*(Half+Movement->MaxStepHeight*2.),ECC_Visibility,Params);
    auto Computed=[&](const FVector& P){TVoxelArray<FVector> V;V.Add(P);return Newest->GetSubsystem<FVoxelInvokerCollisionSubsystem>().HasCollision(V);};
    auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("seconds"),Now-Started);O->SetBoolField(TEXT("held"),Held);O->SetBoolField(TEXT("transition"),Changed);O->SetNumberField(TEXT("guardWaitSeconds"),Movement->GetCollisionWaitSeconds());
    O->SetNumberField(TEXT("pendingTasks"),Planet.GetNumPendingTasks());O->SetNumberField(TEXT("oldState"),Old?Old->GetStateIndex():-1);O->SetNumberField(TEXT("newState"),New?New->GetStateIndex():-1);
    O->SetBoolField(TEXT("newReadyToRender"),New&&New->GetTaskContext()&&New->IsReadyToRender());O->SetBoolField(TEXT("stateBusy"),Planet.IsProcessingNewState());
    O->SetNumberField(TEXT("lookAheadCm"),Look);O->SetStringField(TEXT("positionCm"),Here.ToString());O->SetStringField(TEXT("nextCm"),Next.ToString());
    O->SetBoolField(TEXT("currentPhysicsHit"),HitCurrent);O->SetBoolField(TEXT("nextPhysicsHit"),HitNext);O->SetBoolField(TEXT("nextPhysicsWalkable"),HitNext&&Movement->IsWalkable(Hit));
    if(HitNext){O->SetStringField(TEXT("hitComponent"),GetNameSafe(Hit.GetComponent()));O->SetStringField(TEXT("hitNormal"),Hit.ImpactNormal.ToString());}
    O->SetBoolField(TEXT("currentCenterComputed"),Computed(Here));O->SetBoolField(TEXT("nextCenterComputed"),Computed(Next));O->SetBoolField(TEXT("nextFootComputed"),Computed(Next-NextUp*Half));
    FVoxelDependencyCollector Dependencies(STATIC_FNAME("CLCollisionResearch"));FVoxelQuery Q(0,FVoxelLayers::Empty(),*FVoxelSurfaceTypeTable::Get(),Dependencies);
    FVoxelDoubleVectorBuffer Probes;Probes.Allocate(25);for(int32 I=0;I<25;++I)Probes.Set(I,FMath::Lerp(A,B,double(I)/24));
    const auto D=Planet.GetSamplingRuntime()->SampleDistances(Q,Probes);int32 Crossing=-1;
    for(int32 I=1;I<25;++I)if(D[I-1]>0&&D[I]<=0){Crossing=I;break;}
    O->SetBoolField(TEXT("generatedFloorCrossingInShortTrace"),Crossing>=0);
    if(Crossing>=0)
    {
        const FVector Cross=FMath::Lerp(Probes[Crossing-1],Probes[Crossing],double(D[Crossing-1])/(D[Crossing-1]-D[Crossing]));
        O->SetStringField(TEXT("crossingCm"),Cross.ToString());O->SetBoolField(TEXT("crossingComputed"),Computed(Cross));O->SetBoolField(TEXT("crossingBelowComputed"),Computed(Cross-NextUp*10));
        TInlineComponentArray<UVoxelCollisionComponent*> Components;Planet.GetComponents(Components);int32 Near=0,Physics=0;
        for(auto* C:Components)if(C->Bounds.GetBox().ExpandBy(20).IsInside(Cross)){++Near;Physics+=C->IsPhysicsStateCreated();}
        O->SetNumberField(TEXT("nearbyCollisionComponents"),Near);O->SetNumberField(TEXT("nearbyPhysicsStates"),Physics);
    }
    Records.Add(MakeShared<FJsonValueObject>(O));auto Root=MakeShared<FJsonObject>();Root->SetStringField(TEXT("scope"),TEXT("Read-only witness of unedited generated entrance geometry; not a timing comparison"));Root->SetArrayField(TEXT("records"),Records);
    FString Json;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Json));FFileHelper::SaveStringToFile(Json,*(FPaths::GetPath(Report)/TEXT("collision-witness.json")));
    UE_LOG(LogTemp,Display,TEXT("COLLISION_WITNESS held=%d state=%d->%d pending=%d computed=%d physics=%d generatedFloor=%d"),Held,Old?Old->GetStateIndex():-1,New?New->GetStateIndex():-1,Planet.GetNumPendingTasks(),Computed(Next-NextUp*Half),HitNext,Crossing>=0);
}
