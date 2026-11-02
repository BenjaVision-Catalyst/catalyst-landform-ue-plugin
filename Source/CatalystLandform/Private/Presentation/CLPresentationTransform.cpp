#include "Presentation/CLPresentationTransform.h"
#include "Presentation/CLPlanetProjection.h"
namespace {
bool Frame(const FCLPlanetProjection& P,const FVector& Position,const FQuat& Q,FVector& Mapped,FQuat& Rotation,FVector& Scale)
{
    FVector X,Y,Z;
    if(Position.ContainsNaN()||Q.ContainsNaN()||!Q.IsNormalized()||
       !P.TryProjectPosition(Position,Mapped)||!P.TryProjectVector(Position,Q.GetAxisX(),X)||
       !P.TryProjectVector(Position,Q.GetAxisY(),Y)||!P.TryProjectVector(Position,Q.GetAxisZ(),Z))return false;
    Scale=FVector(X.Length(),Y.Length(),Z.Length());
    if(Scale.GetMin()<1.e-8||FVector::CrossProduct(X,Z).IsNearlyZero())return false;
    Rotation=FRotationMatrix::MakeFromXZ(X,Z).ToQuat();return !Rotation.ContainsNaN();
}}
bool FCLPresentationTransform::TryProject(const FCLPlanetProjection& P,const FTransform& Source,
    const FBox& Bounds,FTransform& Out,double& Error)
{
    if(!Bounds.IsValid||Source.GetScale3D().ContainsNaN()||Source.GetScale3D().GetMin()<=0)return false;
    FVector Position,Scale;FQuat Rotation;
    if(!Frame(P,Source.GetLocation(),Source.GetRotation(),Position,Rotation,Scale))return false;
    const FTransform Candidate(Rotation,Position,Source.GetScale3D()*Scale);double Maximum=0;
    for(int32 I=0;I<8;++I)
    {
        const FVector Local(I&1?Bounds.Max.X:Bounds.Min.X,I&2?Bounds.Max.Y:Bounds.Min.Y,I&4?Bounds.Max.Z:Bounds.Min.Z);
        FVector Exact;if(!P.TryProjectPosition(Source.TransformPosition(Local),Exact))return false;
        Maximum=FMath::Max(Maximum,FVector::Distance(Exact,Candidate.TransformPosition(Local)));
    }
    Out=Candidate;Error=Maximum;return true;
}
bool FCLPresentationTransform::TryProjectCamera(const FCLPlanetProjection& P,const FVector& Position,
    const FQuat& Rotation,FVector& OutPosition,FQuat& OutRotation)
{
    FVector Mapped,Scale;FQuat Q;if(!Frame(P,Position,Rotation,Mapped,Q,Scale))return false;
    OutPosition=Mapped;OutRotation=Q;return true;
}
