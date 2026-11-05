#include "CLCaveMorphology.h"
#include "Model/CLPlanetRecipe.h"
#include "Math/RotationMatrix.h"
namespace
{
FVector BeddingUp(const FCLCompiledPlanetRecipe& Base,FVector Point)
{
    const auto D=Point.GetSafeNormal();FVector X,Y;D.FindBestAxisVectors(X,Y);
    const double Step=1./Point.Length();
    auto H=[&](FVector Q){return Base.Rock->SampleColumn(Q.GetSafeNormal()).DeformationMeters;};
    // One-metre samples of the existing deformed stratigraphic coordinate.
    return (D-X*((H(D+X*Step)-H(D-X*Step))*.5)-Y*((H(D+Y*Step)-H(D-Y*Step))*.5)).GetSafeNormal();
}
}
void CLDevelopCaveMorphology(const FCLCaveSettings& S,const FCLCompiledPlanetRecipe& Base,const FCLCompiledCaves::FNetwork& Info,TArray<FCLCompiledCaves::FNode>& Nodes,TArray<FCLCavePassage>& Paths,TArray<FCLCaveChamber>& Chambers,TArray<FCLCaveInfill>& Infills)
{
    const bool Karst=Info.Family==FCLCompiledCaves::EFamily::CarbonateKarst;
    FRandomStream Random(int32(GetTypeHash(Info.Id)^0x4D4F5250));int32 Route=0;
    for(auto& P:Paths)
    {
        P.Development=S.Character;
        if(!Karst)
        {
            const FVector A=P.Points[0],B=P.Points.Last(),Up=A.GetSafeNormal(),Along=(B-A).GetSafeNormal();
            const FVector Side=FVector::CrossProduct(Up,Along).GetSafeNormal();
            const FVector Offset=Side*(S.RadiusMeters*Random.FRandRange(-.6,.6));
            const auto First=P.Sections[0],Last=P.Sections.Last();
            // A fracture has planar reaches and discrete changes of direction,
            // rather than a repeated sinusoidal centreline.
            P.Points={A,FMath::Lerp(A,B,.38)+Offset,FMath::Lerp(A,B,.72)+Offset,B};
            P.Sections={First,FMath::Lerp(First,Last,.38),FMath::Lerp(First,Last,.72),Last};
            P.Form=P.EndForm=ECLCaveForm::JointCleft;
            for(auto& V:P.Sections){V.X*=.6;V.Y*=1.35;}
        }
        else if(P.Entrance)
        {
            P.Form=P.EndForm=ECLCaveForm::Phreatic;
            const auto A=P.Points[0],B=P.Points.Last();for(int32 I=0;I<P.Points.Num();++I)P.Points[I]=FMath::Lerp(A,B,double(I)/(P.Points.Num()-1));
        }
        else
        {
            // Declared development priors, not inferred palaeo-water tables.
            // The routes and bedding remain those of the existing karst builder.
            const int32 Kind=Route++%4;
            P.Form=Kind==0?ECLCaveForm::Keyhole:Kind==1?ECLCaveForm::BeddingSlot:Kind==2?ECLCaveForm::Canyon:ECLCaveForm::Phreatic;
            P.EndForm=Kind==0?ECLCaveForm::Canyon:Kind==1?ECLCaveForm::Phreatic:Kind==2?ECLCaveForm::Keyhole:ECLCaveForm::BeddingSlot;
            P.Incision=P.Form==ECLCaveForm::Keyhole||P.EndForm==ECLCaveForm::Keyhole?.7+.6*S.Character:0;
            if(Kind==1)
            {
                int32 BedIndex=0;for(int32 I=0;I<Base.Rock->GetOlderBedCount();++I){if(Base.Rock->GetBed(I).UpperMeters>Info.InceptionRadiusMeters+.01)break;BedIndex=I;}
                const auto Bed=Base.Rock->GetBed(BedIndex);
                const double Thickness=FMath::Clamp(Bed.UpperMeters-Bed.LowerMeters,1.,10.);
                for(auto& V:P.Sections){V.X*=1.55;V.Y=FMath::Clamp(Thickness*.45,1.05,FMath::Max(1.05,V.Y));}
            }
            if(Kind==2)for(auto& V:P.Sections){V.X*=.65;V.Y*=1.4;}
        }
        P.BeddingUps.Reset();for(const auto& V:P.Points)P.BeddingUps.Add(BeddingUp(Base,V));
    }
    Chambers.Reset();int32 Rooms=0;
    for(int32 I=0;I<Nodes.Num();++I)
    {
        if(!Nodes[I].Chamber||Nodes[I].Portal)continue;
        if(!Karst&&Rooms>0&&S.Character<.7)continue;
        const FVector Center=Nodes[I].Position,Up=BeddingUp(Base,Center);
        const auto* P=Paths.FindByPredicate([&](const auto& V){return !V.Entrance&&(V.From==I||V.To==I);});if(!P)continue;
        const FVector Along=(P->Points.Last()-P->Points[0]).GetSafeNormal();
        const FQuat Frame=FRotationMatrix::MakeFromXZ(Along,Up).ToQuat();
        const double R=S.RadiusMeters;
        FCLCaveChamber C;C.Network=Info.Id;C.Node=I;C.Center=Center;C.Rotation=Frame;
        C.HalfExtents=FVector(R*Random.FRandRange(2.4,3.4),R*Random.FRandRange(Karst?1.6:1.3,Karst?2.4:1.8),R*Random.FRandRange(1.6,2.1));
        C.Form=Karst?ECLCaveForm::KarstHall:ECLCaveForm::FractureHall;Chambers.Add(C);++Rooms;
        if(S.Character<.55)continue;
        const int32 Blocks=1+int32(S.Character*2);
        for(int32 K=0;K<Blocks;++K)
        {
            // Bounded static breakdown construction. No stress solution or
            // mass-conserving collapse history is claimed. Keep the core open.
            FCLCaveInfill B;B.Network=Info.Id;B.HalfExtents=FVector(R*Random.FRandRange(.28,.46),R*Random.FRandRange(.25,.4),R*Random.FRandRange(.25,.36));
            const FVector Side=Frame.GetAxisY()*C.HalfExtents.Y*(K%2?-.65:.65),Forward=Frame.GetAxisX()*C.HalfExtents.X*Random.FRandRange(-.45,.45);
            B.SourceCenter=Center+Forward+Side+Frame.GetAxisZ()*(C.HalfExtents.Z+B.HalfExtents.Z*.9);B.SourceRotation=Frame;
            B.Center=Center+Forward+Side-Frame.GetAxisZ()*(C.HalfExtents.Z-B.HalfExtents.Z*.1);
            B.Rotation=Frame*FRotator(Random.FRandRange(-18,18),Random.FRandRange(-24,24),Random.FRandRange(-18,18)).Quaternion();Infills.Add(B);
            FCLCaveChamber Scar;Scar.Network=Info.Id;Scar.Node=I;Scar.Center=B.SourceCenter-Frame.GetAxisZ()*C.HalfExtents.Z*.25;Scar.HalfExtents=B.HalfExtents;Scar.HalfExtents.Z+=C.HalfExtents.Z*.35;Scar.Rotation=Frame;Scar.Form=ECLCaveForm::RoofScar;Chambers.Add(Scar);
        }
    }
}
