#pragma once
#if WITH_DEV_AUTOMATION_TESTS
#include "Model/CLPlanetRecipe.h"
namespace CLTesting
{
/** Controlled native-operator junction fixture; not a generated recipe claim. */
inline TSharedPtr<const FCLCompiledPlanetRecipe> IntersectingCaves(TSharedPtr<const FCLCompiledPlanetRecipe> Base,FString& Error)
{
    auto P=MakeShared<FCLCompiledPlanetRecipe>(*Base);auto C=MakeShared<FCLCompiledCaves>();
    const FVector D=Base->Caves->Nodes[0].Position.GetSafeNormal();
    const FVector X=FVector::CrossProduct(D,FVector::ZAxisVector).GetSafeNormal(),Y=FVector::CrossProduct(D,X);
    const FVector Center=D*(Base->RadiusMeters+Base->SampleHeightMeters(D)-100);
    const FVector Centers[]={Center,Center,Center+X*10,Center+D*8,Center+X*100};
    const FVector Directions[]={X,Y,(X+Y).GetSafeNormal(),X,X};
    for(int32 I=0;I<5;++I)
    {
        const FGuid Id(0x4D495843,1,0,I+1);const FVector A=Centers[I]-Directions[I]*20,B=Centers[I]+Directions[I]*20;
        FCLCompiledCaves::FNetwork N;N.Id=Id;N.Family=(I==1||I==3)?FCLCompiledCaves::EFamily::CarbonateKarst:FCLCompiledCaves::EFamily::Structural;C->NetworkInfo.Add(N);
        const int32 From=C->Nodes.Add({Id,A,{2,2},false,false}),To=C->Nodes.Add({Id,B,{2,2},false,false});
        FCLCavePassage Path;Path.Network=Id;Path.From=From;Path.To=To;Path.Points={A,B};Path.Sections={{2,2},{2,2}};Path.Incision=I==1||I==3?.8:0;C->Passages.Add(Path);
        C->Segments.Add({Id,A,B,2,FVector::OneVector,false});
    }
    C->Networks=5;C->RequestedNetworks=5;C->Cutters=FCLCompiledCaveVolume::Compile(C->Passages,{},nullptr,0,Error);if(!C->Cutters)return nullptr;C->ResolveInspectionDomains();P->Caves=C;return P;
}
}
#endif
