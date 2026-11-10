#include "Model/CLDirectionalScalarField.h"

namespace
{
void Coefficients(const double* V,double* C)
{
    // Cubic through t={0,1/3,2/3,1}, evaluated with Horner's rule.
    C[0]=V[0];C[1]=-5.5*V[0]+9*V[1]-4.5*V[2]+V[3];
    C[2]=9*V[0]-22.5*V[1]+18*V[2]-4.5*V[3];
    C[3]=-4.5*V[0]+13.5*V[1]-13.5*V[2]+4.5*V[3];
}
}
double FCLDirectionalScalarField::FNode::Sample(const FVector2D& P)const
{
    const FVector2D T=(P-Bounds.Min)/(Bounds.Max-Bounds.Min);double Rows[4];
    for(int32 Y=0;Y<4;++Y){const double* C=&Coefficients[Y*4];Rows[Y]=C[0]+T.X*(C[1]+T.X*(C[2]+T.X*C[3]));}
    return Rows[0]+T.Y*(Rows[1]+T.Y*(Rows[2]+T.Y*Rows[3]));
}
void FCLDirectionalScalarField::Build(const FBox2D& Bounds,TFunction<double(const FVector2D&)> Function,TFunction<void(TConstArrayView<FVector2D>,TArray<double>&)> BatchFunction,double ToleranceMeters)
{
    Tolerance=ToleranceMeters;Exact=MoveTemp(Function);Batch=MoveTemp(BatchFunction);
    if(!Batch)Batch=[this](TConstArrayView<FVector2D> Points,TArray<double>& Values)
    {Values.Reset(Points.Num());for(const auto& P:Points)Values.Add(Exact(P));};
    Nodes.Reset();Nodes.AddDefaulted();Nodes[0].Bounds=Bounds;MaxError=0;
    // Breadth first refinement gives every direction a fair share of the fixed
    // budget. A difficult corner must not consume the budget for a hemisphere.
    for(int32 Index=0;Index<Nodes.Num();++Index)
    {
        const double Ratio=(Bounds.Max.X-Bounds.Min.X)/(Nodes[Index].Bounds.Max.X-Nodes[Index].Bounds.Min.X);
        BuildNode(Index,FMath::RoundToInt(FMath::Log2(Ratio)));
    }
    Nodes.Shrink();Batch={};
}
void FCLDirectionalScalarField::BuildNode(int32 Index,int32 Depth)
{
    // Do not retain a reference while recursively growing the backing array.
    FNode Node=Nodes[Index];double Rows[16];TArray<FVector2D> Points;TArray<double> Values;
    for(int32 Y=0;Y<4;++Y)for(int32 X=0;X<4;++X)Points.Add(Node.Bounds.Min+(Node.Bounds.Max-Node.Bounds.Min)*FVector2D(X/3.,Y/3.));
    Batch(Points,Values);
    for(int32 Y=0;Y<4;++Y)
    {
        double V[4];for(int32 X=0;X<4;++X)V[X]=Values[4*Y+X];
        Coefficients(V,&Rows[4*Y]);
    }
    for(int32 X=0;X<4;++X)
    {
        double V[4],C[4];for(int32 Y=0;Y<4;++Y)V[Y]=Rows[4*Y+X];Coefficients(V,C);
        for(int32 Y=0;Y<4;++Y)Node.Coefficients[4*Y+X]=C[Y];
    }
    double Error=0;
    // Independent off-knot positions; discontinuities or nonconvergent tiles
    // retain the exact evaluator. Accuracy is also checked by a separate test.
    Points.Reset();
    for(double Y:{.1,.2,.4,.5,.6,.8,.9})for(double X:{.1,.2,.4,.5,.6,.8,.9})
        Points.Add(Node.Bounds.Min+(Node.Bounds.Max-Node.Bounds.Min)*FVector2D(X,Y));
    Batch(Points,Values);
    for(int32 I=0;I<Points.Num();++I)Error=FMath::Max(Error,FMath::Abs(Node.Sample(Points[I])-Values[I]));
    if(Error<=Tolerance){MaxError=FMath::Max(MaxError,Error);Nodes[Index]=Node;return;}
    // Bound both compilation and storage; uncertain cells always use exact data.
    if(Depth>=10||Nodes.Num()+4>65533){Node.bExact=true;Nodes[Index]=Node;return;}
    Node.Children=Nodes.AddDefaulted(4);Nodes[Index]=Node;
    const FVector2D Middle=(Node.Bounds.Min+Node.Bounds.Max)*.5;
    for(int32 Y=0;Y<2;++Y)for(int32 X=0;X<2;++X)
    {
        const int32 Child=Node.Children+X+Y*2;
        Nodes[Child].Bounds=FBox2D(FVector2D(X?Middle.X:Node.Bounds.Min.X,Y?Middle.Y:Node.Bounds.Min.Y),FVector2D(X?Node.Bounds.Max.X:Middle.X,Y?Node.Bounds.Max.Y:Middle.Y));

    }
}
bool FCLDirectionalScalarField::TrySample(const FVector2D& P,double& Value)const
{
    if(!Nodes[0].Bounds.IsInside(P))return false;int32 Index=0;
    while(Nodes[Index].Children!=INDEX_NONE)
    {
        const auto& Node=Nodes[Index];const FVector2D Middle=(Node.Bounds.Min+Node.Bounds.Max)*.5;
        Index=Node.Children+int32(P.X>=Middle.X)+2*int32(P.Y>=Middle.Y);
    }
    const auto& Node=Nodes[Index];if(Node.bExact)return false;Value=Node.Sample(P);return true;
}
double FCLDirectionalScalarField::Sample(const FVector2D& P,bool* bUsedExact)const
{
    double Value;const bool Found=TrySample(P,Value);if(bUsedExact)*bUsedExact=!Found;return Found?Value:Exact(P);
}
int32 FCLDirectionalScalarField::NumExactLeaves()const
{int32 N=0;for(const auto& Node:Nodes)N+=Node.bExact;return N;}
