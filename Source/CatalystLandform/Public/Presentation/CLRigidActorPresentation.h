#pragma once
#include "CoreMinimal.h"
class AActor;
class FCLPlanetProjection;

/** Per-view rigid visual copy. Simulation components, animation, sockets and collision stay canonical.
 * One owner per source actor/view; contains skeletal leader-pose followers and attached groom/mesh copies.
 */
class CATALYSTLANDFORM_API FCLRigidActorPresentation
{
public:
    static TUniquePtr<FCLRigidActorPresentation> TryCreate(AActor& Source, FString& Error);
    ~FCLRigidActorPresentation();
    bool Update(const FCLPlanetProjection& Projection, FString& Error);
    int32 NumVisuals() const;
    AActor* GetVisualActor() const;
private:
    FCLRigidActorPresentation();
    struct FImpl;
    TUniquePtr<FImpl> Impl;
};
