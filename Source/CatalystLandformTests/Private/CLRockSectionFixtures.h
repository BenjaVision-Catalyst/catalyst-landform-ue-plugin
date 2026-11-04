#pragma once
#include "Model/CLTectonics.h"

// Controlled native model fixture, shared by both versioned geological proofs.
inline TSharedPtr<const FCLCompiledTectonics> CLRockSectionPlates(bool Compression,FString& Error,double Motion=1,double Relief=.7,double RiftStrength=1)
{
    FCLTectonicPreset P;P.ApplyProgression(.8);P.BoundaryWarp=0;P.InteriorRelief=0;
    P.BeltWidthDegrees=10;P.Motion=Motion;P.Relief=Relief;P.CollisionStrength=1;P.RiftStrength=RiftStrength;
    const auto Model=FCLCompiledTectonics::Compile(P,1729,160,Error);
    if(!Model)return nullptr;
    auto T=ConstCastSharedPtr<FCLCompiledTectonics>(Model);
    const double Sign=Compression?1.:-1.;
    T->Lower.Plates={{FGuid(1,0,0,1),FVector::XAxisVector,FVector(0,-Sign,0)},
                     {FGuid(1,0,0,2),-FVector::XAxisVector,FVector(0,Sign,0)}};
    FCLCompiledTectonics::FBoundary B;B.Id=FGuid(1,0,0,3);B.A=0;B.B=1;
    B.Normal=FVector::XAxisVector;B.Mid=FVector::ZAxisVector;
    B.Start=FVector(0,-.7071067811865475,.7071067811865475);
    B.End=FVector(0,.7071067811865475,.7071067811865475);
    B.HalfCos=.7071067811865475;B.CapCos=0;
    T->Lower.Boundaries={B};T->Upper={};T->Controls.PlateTransition=0;
    return Model;
}
