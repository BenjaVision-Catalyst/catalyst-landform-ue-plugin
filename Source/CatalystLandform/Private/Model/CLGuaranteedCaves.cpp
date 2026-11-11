#include "Model/CLCaves.h"
#include "Model/CLPlanetRecipe.h"
#include "CLNativeNoiseKernel.h"
#include "Math/RotationMatrix.h"

// The enabled minimum is a connected spatial volume, not invented lithology,
// a preview mesh or a swept tube. Reuse the existing native vault/lobe kernels.
TSharedPtr<const FCLCompiledCaves> FCLCompiledCaves::CompileMinimum(const FCLCaveSettings& S,const FCLCompiledPlanetRecipe& Base,int32 Seed,FString& Error)
{
    auto Out=MakeShared<FCLCompiledCaves>();
    const auto Noise=Base.NativePerlin?Base.NativePerlin:FCLNativeNoiseKernel::Create(Error);
    if(!Noise)return nullptr;
    FRandomStream Random(int32(uint32(Seed)^uint32(S.Seed)^0x43415645));
    const FVector Up=Random.VRand();FVector X,Y;Up.FindBestAxisVectors(X,Y);
    const FQuat Spin(Up,Random.FRandRange(-PI,PI));X=Spin.RotateVector(X);Y=Spin.RotateVector(Y);
    // This sphere is inside the accepted terrain's radial lower bound, even
    // without Rock, plates, provinces, drainage or any declared host capability.
    const double SolidRadius=Base.RadiusMeters+FMath::Min(0.,Base.MinimumHeightMeters);
    const double Run=FMath::Min(S.LengthMeters*.25,SolidRadius*.25);
    const double Width=FMath::Min(S.RadiusMeters*2.2,SolidRadius*.10);
    const double Height=FMath::Min(S.RadiusMeters*1.6,SolidRadius*.10);
    const double Depth=FMath::Max(S.DepthMeters,S.MinimumCoverMeters+Height*3);
    const FVector Origin=Up*FMath::Max(SolidRadius*.55,SolidRadius-Depth);
    const FQuat Frame=FRotationMatrix::MakeFromXZ(X,Up).ToQuat();
    const FGuid Id(0x47454e43,uint32(Seed),uint32(S.Seed),1);
    auto Chamber=[&](FVector Center,FVector Extents,FQuat Rotation,bool Portal=false)
    {
        const int32 Node=Out->Nodes.Add({Id,Center,{Extents.Y,Extents.Z},true,Portal});
        FCLCaveChamber C;C.Network=Id;C.Node=Node;C.Center=Center;C.HalfExtents=Extents;C.Rotation=Rotation;C.Form=ECLCaveForm::KarstChamber;
        Out->Chambers.Add(C);
    };
    Chamber(Origin-X*(Run*.45),{Run*.65,Width*.85,Height},Frame);
    Chamber(Origin,{Run*.70,Width,Height},Frame);
    Chamber(Origin+X*(Run*.45)+Y*(Width*.12),{Run*.62,Width*.9,Height},Frame);
    for(int32 I=1;I<3;++I)Out->Segments.Add({Id,Out->Nodes[I-1].Position,Out->Nodes[I].Position,FMath::Min(Width,Height)});
    if(S.bSurfaceEntrances)
    {
        const double AccessRun=FMath::Min(S.LengthMeters*.3,SolidRadius*.1);
        const FVector Direction=(Up+X*(AccessRun/SolidRadius)).GetSafeNormal();
        const FVector Mouth=Direction*(Base.RadiusMeters+Base.SampleHeightMeters(Direction)+Height*.5);
        const FVector Delta=Mouth-Origin;
        Chamber((Mouth+Origin)*.5,{Delta.Length()*.70,Width*.7,Height},FRotationMatrix::MakeFromXZ(Delta,Up).ToQuat(),true);
        Out->Segments.Add({Id,Origin,Mouth,FMath::Min(Width*.7,Height),FVector::OneVector,true});Out->Entrances=1;
    }
    Out->Cutters=FCLCompiledCaveVolume::Compile({},Out->Chambers,Noise,Seed^S.Seed,Error);
    if(!Out->Cutters)return nullptr;
    Out->Networks=Out->RequestedNetworks=Out->Candidates=Out->EligibleCandidates=1;
    FNetwork Info;Info.Id=Id;Info.Family=EFamily::General;Info.OriginDirection=Up;Info.InceptionRadiusMeters=Origin.Length();Info.InspectionBounds=Out->Cutters->Bounds();Out->NetworkInfo.Add(Info);
    Out->ResolveInspectionDomains();Out->Readiness=TEXT("1 connected cave network; enabled-layer minimum");
    return Out;
}
