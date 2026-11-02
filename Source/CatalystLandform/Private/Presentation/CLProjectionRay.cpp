#include "Presentation/CLProjectionRay.h"

bool FCLProjectionRay::TryCreate(const FCLPlanetProjection& P,const FVector& O,const FVector& D,
    double LengthCm,FCLProjectionRay& Out)
{
    const double DirectionLength=D.Length();
    if(O.ContainsNaN()||D.ContainsNaN()||!FMath::IsFinite(LengthCm)||LengthCm<=0||
       !FMath::IsFinite(DirectionLength)||DirectionLength<1.e-12)return false;
    FCLProjectionRay Ray;Ray.Projection=P;Ray.Origin=O;Ray.Direction=D/DirectionLength;Ray.Length=LengthCm;
    const auto& S=P.GetSettings();const double R=S.ReferenceRadiusCm,C=S.Curvature;
    const FVector A=O-(S.Center+R*S.AnchorDirection),B=A+LengthCm*Ray.Direction;
    if(B.ContainsNaN())return false;
    const double ZA=FVector::DotProduct(A,S.AnchorDirection),ZB=FVector::DotProduct(B,S.AnchorDirection);
    const double ZMin=FMath::Min(ZA,ZB),ZMax=FMath::Max(ZA,ZB);
    const double X=FMath::Max((A-ZA*S.AnchorDirection).Length(),(B-ZB*S.AnchorDirection).Length());
    const double Amin=R+C*ZMin;
    if(!FMath::IsFinite(Amin)||Amin<=0||!FMath::IsFinite(X))return false;
    const double Y=C*X/Amin;
    const double AtanRatio=Y>1.e-4?FMath::Atan(Y)/Y:1-Y*Y/3+Y*Y*Y*Y/5;
    const double Theta=X/Amin*AtanRatio;
    // h = z + c*x^2/(sqrt((R+c*z)^2+c^2*x^2)+R+c*z).
    // Convex tangent norm and affine z bound every point of this whole ray.
    const double HUpper=ZMax+.5*C*X*X/Amin;
    if(!FMath::IsFinite(Theta)||!FMath::IsFinite(HUpper)||Theta>S.MaxArcRadians||
       ZMin<S.MinAltitudeCm||HUpper>S.MaxAltitudeCm)return false;
    FVector Start,End;
    if(!P.TryUnprojectPosition(O,Start)||!P.TryUnprojectPosition(O+LengthCm*Ray.Direction,End))return false;
    if(C!=1)
    {
        const double Sec=FMath::Sqrt(1+Y*Y);
        // Inverse = (R+h) Exp_n(a), a = atan(|y|)*y/(c*|y|),
        // y=c*tangent/(R+c*z). ||D atan-vector||<=1;
        // ||D^2 atan-vector||<=2Y+8Y^3/5 (integral of y/(1+t^2*y^2)).
        // ||D Exp||<=1 and ||D^2 Exp||<=1+theta. Also |h'|<=1,
        // |h''|<=c/Amin. These give a bound for any unit ray direction.
        const double HAtan=2*Y+1.6*Y*Y*Y;
        Ray.SecondDerivativeBound=(C+2*Sec)/Amin+(R+HUpper)/(Amin*Amin)*
            ((1+Theta)*Sec*Sec+C*(HAtan*Sec*Sec+2*Sec));
        const double Sinc=Theta>1.e-8?FMath::Sin(Theta)/Theta:1;
        Ray.MaxForwardStretch=FMath::Max(1.,(R+C*ZMin)/(R+ZMin)/Sinc);
    }
    if(!FMath::IsFinite(Ray.SecondDerivativeBound)||!FMath::IsFinite(Ray.MaxForwardStretch))return false;
    Ray.bValid=true;Out=Ray;return true;
}
bool FCLProjectionRay::TryGetCanonicalPoint(double DistanceCm,FVector& Out) const
{
    return bValid&&FMath::IsFinite(DistanceCm)&&DistanceCm>=0&&DistanceCm<=Length&&
        Projection.TryUnprojectPosition(Origin+DistanceCm*Direction,Out);
}
double FCLProjectionRay::GetMaxChordErrorCm(double IntervalLengthCm) const
{
    if(!bValid||!FMath::IsFinite(IntervalLengthCm)||IntervalLengthCm<0||IntervalLengthCm>Length)return MAX_dbl;
    // Green-function bound for a C2 curve and its endpoint chord.
    return SecondDerivativeBound*IntervalLengthCm*IntervalLengthCm/8;
}
