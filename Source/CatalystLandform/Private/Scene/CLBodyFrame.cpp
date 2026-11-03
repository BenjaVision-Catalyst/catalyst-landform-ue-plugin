#include "Scene/CLBodyFrame.h"

bool FCLBodyFrame::Validate(FString& Error) const
{
    Error.Reset();
    // UE's SIMD quaternion normalization is approximate. At this tolerance its
    // scale error is far below a millimetre over the bounded native window.
    if (AnchorMeters.ContainsNaN() || !FMath::IsFinite(AnchorMeters.SizeSquared()) || BodyFromLocal.ContainsNaN() || FMath::Abs(BodyFromLocal.SizeSquared()-1)>1.e-8)
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
