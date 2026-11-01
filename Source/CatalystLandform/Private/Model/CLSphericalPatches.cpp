#include "Model/CLSphericalPatches.h"

void FCLSphericalPatches::Frame(int32 Face,FVector& N,FVector& X,FVector& Y)
{
    static const FVector Normals[]={FVector(1,0,0),FVector(-1,0,0),FVector(0,1,0),FVector(0,-1,0),FVector(0,0,1),FVector(0,0,-1)};
    N=Normals[FMath::Clamp(Face,0,5)];X=Face<4?FVector::ZAxisVector:FVector::XAxisVector;Y=FVector::CrossProduct(N,X);
}
FVector FCLSphericalPatches::Direction(int32 Face,const FVector2D& UV)
{FVector N,X,Y;Frame(Face,N,X,Y);return (N+X*UV.X+Y*UV.Y).GetSafeNormal();}
bool FCLSphericalPatches::Coordinates(int32 Face,const FVector& D,FVector2D& UV)
{
    UV=FVector2D::ZeroVector;if(!IsFace(Face)||D.ContainsNaN())return false;
    FVector N,X,Y;Frame(Face,N,X,Y);const double Den=FVector::DotProduct(D,N);if(Den<=1.e-12)return false;
    UV=FVector2D(FVector::DotProduct(D,X),FVector::DotProduct(D,Y))/Den;return true;
}
int32 FCLSphericalPatches::Owner(const FVector& D)
{
    const FVector A=D.GetAbs();
    if(A.X>=A.Y&&A.X>=A.Z)return D.X>=0?0:1;
    if(A.Y>=A.Z)return D.Y>=0?2:3;
    return D.Z>=0?4:5;
}
bool FCLSphericalPatches::MayAffect(int32 Face,const FVector& Anchor,double SupportAngle)
{
    if(!IsFace(Face))return true;
    FVector N,X,Y;Frame(Face,N,X,Y);
    const double Angle=FMath::Min(UE_PI,FMath::Acos(1/FMath::Sqrt(3.))+SupportAngle+.04+1.e-9);
    return FVector::DotProduct(N,Anchor)>=FMath::Cos(Angle);
}
