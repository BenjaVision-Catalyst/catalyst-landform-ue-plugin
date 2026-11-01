#pragma once
#include "CoreMinimal.h"
#include "Model/CLPlanetRecipe.h"

enum class ECLGlobePlacementMode : uint8 { Move, Rotate, Resize };
enum class ECLGlobePlacementHandle : uint8 { None, Centre, Rotation, Width, Length, Uniform };
struct FCLGlobeSelection
{
    FGuid Id;
    bool bStructure=false;
    bool IsValid() const {return Id.IsValid();}
    bool operator==(const FCLGlobeSelection& Other) const {return Id==Other.Id&&bStructure==Other.bStructure;}
    bool operator!=(const FCLGlobeSelection& Other) const {return !(*this==Other);}
};
/** A resolved editor snapshot, separate from the source definition and live UObject state. */
struct CATALYSTLANDFORMEDITOR_API FCLGlobePlacementTarget
{
    FCLGlobeSelection Key;
    FString Name;
    FQuat Frame=FQuat::Identity;
    FVector2D HalfExtentDegrees;
    bool bRectangle=false,bEnabled=true;
    double Transition=.4;
    int32 Priority=0;
    double Weight(const FVector& Direction) const;
};
class CATALYSTLANDFORMEDITOR_API FCLGlobePlacementEditing
{
public:
    static TArray<FCLGlobePlacementTarget> Resolve(const FCLPlanetRecipe& Recipe);
    static FVector Direction(const FVector2D& AngularRadians);
    static FVector2D Chart(const FQuat& Frame,const FVector& Direction);
    static void Coordinates(const FQuat& Frame,FVector2D& LocationDegrees,double& HeadingDegrees);
    /** Changes only planet placement fields; callers own transactions and full compilation. */
    static bool Apply(FCLPlanetRecipe& Recipe,const FCLGlobePlacementTarget& Target,bool bResize,FString& Error);
    static FCLGlobePlacementTarget Drag(const FCLGlobePlacementTarget& Start,ECLGlobePlacementHandle Handle,
        const FVector& StartDirection,const FVector& CurrentDirection);
};
