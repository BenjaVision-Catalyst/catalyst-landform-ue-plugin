#include "Scene/CLBodyFrame.h"

bool FCLBodyFrame::Validate(FString& Error) const
{
    Error.Reset();
    if (AnchorMeters.ContainsNaN() || BodyFromLocal.ContainsNaN() || !BodyFromLocal.IsNormalized())
    {
        Error = TEXT("Body frame requires a finite anchor and normalized rigid rotation");
        return false;
    }
    return true;
}

FVector3d FCLBodyFrame::NativeToBodyMeters(const FVector3d& LocalCentimeters) const
{
    return AnchorMeters + BodyFromLocal.RotateVector(LocalCentimeters * .01);
}

FVector3d FCLBodyFrame::BodyToNativeCentimeters(const FVector3d& BodyMeters) const
{
    return BodyFromLocal.UnrotateVector(BodyMeters - AnchorMeters) * 100.;
}
