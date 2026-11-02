#pragma once

#include "CoreMinimal.h"

/** Known is distinguished from missing input and the identity of a contribution.
 * Known shape values are negative in solid, zero at an interface, positive in air. */
enum class ECLSceneSampleState : uint8
{
    Known,
    NoContribution,
    NotReady,
    OutsideDomain,
    InvalidQuery
};

enum class ECLSceneFieldMetric : uint8 { SignedImplicit, EuclideanDistance };

/** A query is bound to the immutable scene on which it is invoked.
 * A nonzero footprint requests filtering; unsupported filtering must be refused. */
struct FCLSceneQuery
{
    TConstArrayView<FVector3d> PositionsMeters;
    double FootprintMeters = 0;
};

struct FCLSceneShapeSample
{
    ECLSceneSampleState State = ECLSceneSampleState::InvalidQuery;
    double FieldMeters = 0;
};

/** Logical material identity; native slots and their capacity are adapter concerns. */
struct FCLSceneSurfaceWeight
{
    FName Id;
    int32 Revision = 0;
    double Weight = 0;
};

struct FCLSceneSurfaceSample
{
    ECLSceneSampleState State = ECLSceneSampleState::NoContribution;
    TArray<FCLSceneSurfaceWeight> Weights;
};
