#pragma once
#include "CoreMinimal.h"
#include "CLHeightReference.generated.h"

UENUM(BlueprintType)
enum class ECLHeightReferenceMode : uint8 { Height, IntervalHeight, IntervalDelta };

/** Height uses an outer-radius offset; interval modes resolve planet-global guides. */
USTRUCT(BlueprintType)
struct CATALYSTLANDFORM_API FCLHeightReference
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) ECLHeightReferenceMode Mode = ECLHeightReferenceMode::Height;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Guide;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName UpperGuide;
    /** Height lerps [0,1]; Delta scales the guide span by a signed fraction [-1,1]. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite) double Fraction = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Offset", Units="m")) double OffsetMeters = 0;
    bool Validate(FString& Error) const;
};

