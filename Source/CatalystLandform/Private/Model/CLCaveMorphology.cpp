#include "Model/CLCaveFloor.h"
#include "CLNativeNoiseKernel.h"
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
    if(Karst&&S.AlgorithmVersion>=6)
    {
        // Mammoth's broad bed-controlled trunk passages and Carlsbad's irregular
        // room complexes are scale/anatomy references, not reconstructed surveys.
        FRandomStream Shape(int32(GetTypeHash(Info.Id)^0x4B415236));
        const double R=S.RadiusMeters,Eye=FMath::Min(1.6,R*.65);
        for(auto& P:Paths)
        {
            P.GeometryProfile=S.AlgorithmVersion;P.FloorReliefScale=S.AlgorithmVersion>=7?FMath::Min(1.,R*.22):0;
            P.Form=P.EndForm=ECLCaveForm::BeddedPassage;P.Incision=0;P.Development=S.Character;
            const double Phase=Shape.FRandRange(-PI,PI);
            // Replace the recharge shaft with a gently rising walk-in branch.
            // Its mouth intersects the actual surface; it is not a floating cut.
            if(P.Entrance)
            {
                const FVector Start=P.Points[0],D=Start.GetSafeNormal();
                const auto* Trunk=Paths.FindByPredicate([&](const auto& V){return !V.Entrance&&V.From==P.From;});
                FVector Out=Trunk?FVector::VectorPlaneProject(Trunk->Points[0]-Trunk->Points[1],D).GetSafeNormal():FVector::ZeroVector;
                if(Out.IsNearlyZero()){FVector Other;D.FindBestAxisVectors(Out,Other);}
                const double Rise=Base.RadiusMeters+Base.SampleHeightMeters(D)-Start.Length()+Eye;
                const double Run=FMath::Clamp(Rise*3.7,25.,S.LengthMeters*1.4);
                P.Points.Reset();P.Sections.Reset();
                const FVector PortalD=(D+Out*(Run/Start.Length())).GetSafeNormal();
                const double Top=Base.RadiusMeters+Base.SampleHeightMeters(PortalD)+Eye;
                for(int32 I=0;I<=24;++I)
                {
                    const double T=I/24.;const FVector Q=FMath::Lerp(D,PortalD,T).GetSafeNormal();
                    // Smooth grade changes at the underground junction.
                    const double Grade=T*T*(2-T);
                    const double Radius=FMath::Lerp(Start.Length(),Top,Grade);
                    P.Points.Add(Q*Radius);P.Sections.Add(FVector2D(R*1.7,R*1.45));
                }
                Nodes[P.To].Position=P.Points.Last();
            }
            for(int32 I=0;I<P.Points.Num();++I)
            {
                const double T=double(I)/(P.Points.Num()-1),E=FMath::Square(FMath::Sin(PI*T));
                if(!P.Entrance)
                {
                    // This dry family develops on one bedding floor. Preserve
                    // the reconnecting route in plan without hanging its mouth
                    // above the room on the former raised loop's floor.
                    P.Points[I]+=P.Points[I].GetSafeNormal()*(Info.InceptionRadiusMeters-Base.Rock->RestoredRadiusMeters(P.Points[I]));
                    const double Throat=(Nodes[P.From].Chamber?1-FMath::SmoothStep(0.,.4,T):0)+
                        (Nodes[P.To].Chamber?FMath::SmoothStep(.6,1.,T):0);
                    P.Sections[I].X*=2.05*(1+.3*Throat+E*.14*FMath::Sin(2.1*PI*T+Phase));
                    P.Sections[I].Y*=1.75*(1+E*.12*FMath::Cos(1.7*PI*T+Phase));
                }
                P.BeddingUps.Add(BeddingUp(Base,P.Points[I]));
                P.FloorRadii.Add(Base.Rock->RestoredRadiusMeters(P.Points[I])-Eye);
            }
            P.FloorRock=Base.Rock;P.FloorOffsetMeters=Eye;
        }
        Chambers.Reset();Infills.Reset();
        for(int32 I=0;I<Nodes.Num();++I)
        {
            if(!Nodes[I].Chamber||Nodes[I].Portal)continue;
            const auto* P=Paths.FindByPredicate([&](const auto& V){return !V.Entrance&&(V.From==I||V.To==I);});if(!P)continue;
            const FVector Center=Nodes[I].Position,Up=BeddingUp(Base,Center);
            const FQuat Frame=FRotationMatrix::MakeFromXZ(P->Points.Last()-P->Points[0],Up).ToQuat();
            FCLCaveChamber C;C.Network=Info.Id;C.Node=I;C.Rotation=Frame;
            C.HalfExtents=R*FVector(Shape.FRandRange(7.5,9.0),Shape.FRandRange(4.7,5.6),Shape.FRandRange(2.6,3.3));
            C.Center=Center+Up*(C.HalfExtents.Z-Eye);C.Form=ECLCaveForm::BeddedChamber;
            C.GeometryProfile=S.AlgorithmVersion;C.FloorReliefScale=S.AlgorithmVersion>=7?FMath::Min(1.,R*.22):0;
            C.FloorRadius=Base.Rock->RestoredRadiusMeters(Center)-Eye;C.FloorRock=Base.Rock;Chambers.Add(C);
            // Native solid bedrock buttresses provide foreground/middle-distance
            // scale. Keep the central passage mouths and crossing unobstructed.
            for(int32 K=0;K<3;++K)
            {
                FCLCaveInfill B;B.Network=Info.Id;B.BedrockPier=true;
                B.HalfExtents=R*FVector(Shape.FRandRange(.55,.85),Shape.FRandRange(.45,.7),Shape.FRandRange(2.8,3.5));
                B.Center=Center+Frame.RotateVector(FVector(C.HalfExtents.X*(K==0?-.38:K==1?.32:.55),C.HalfExtents.Y*(K==1?-.60:.62),B.HalfExtents.Z-Eye));
                const FVector D=B.Center.GetSafeNormal();B.Center=D*(Info.InceptionRadiusMeters-Eye+B.HalfExtents.Z+Base.Rock->SampleColumn(D).DeformationMeters);
                B.Rotation=B.SourceRotation=Frame;B.SourceCenter=B.Center;Infills.Add(B);
            }
            for(int32 K=0;K<12;++K)
            {
                // Partly embedded limestone breakdown at the margins, leaving
                // the broad crossing clear. This is static construction with
                // roof-rock provenance, not a simulated collapse history.
                FCLCaveInfill B;B.Network=Info.Id;B.BeddedBreakdown=true;
                B.HalfExtents=R*FVector(Shape.FRandRange(.14,.40),Shape.FRandRange(.12,.32),Shape.FRandRange(.10,.23));
                const FVector Offset=Frame.RotateVector(FVector(C.HalfExtents.X*Shape.FRandRange(-.7,.7),C.HalfExtents.Y*(K%2?-.68:.70),0));
                B.Center=Center+Offset+Up*(B.HalfExtents.Z*.5-Eye);
                B.Rotation=Frame*FRotator(Shape.FRandRange(-25,25),Shape.FRandRange(-80,80),Shape.FRandRange(-20,20)).Quaternion();
                B.SourceCenter=Center+Offset+Up*(C.HalfExtents.Z*2+B.HalfExtents.Z);B.SourceRotation=Frame;
                const FVector Local=Frame.UnrotateVector(Offset);const double Edge=FVector2D(Local.X/C.HalfExtents.X,Local.Y/C.HalfExtents.Y).Length();
                const double Bank=FMath::Min(1.2,C.HalfExtents.GetMin()*.10)*FMath::Square(FMath::SmoothStep(.55,.95,Edge));
                const FVector D=B.Center.GetSafeNormal();B.Center=D*(Info.InceptionRadiusMeters-Eye+B.HalfExtents.Z*.5+Bank+Base.Rock->SampleColumn(D).DeformationMeters);Infills.Add(B);
            }
        }
        // Broken, non-periodic wall beds use the real succession, rather than
        // wrapping a sine-wave groove around the whole passage. Jointed blocks
        // remain attached to the surrounding bedrock and retain its identity.
        FRandomStream Wall(int32(GetTypeHash(Info.Id)^0x57414C4C));
        int32 BedStart=0;while(BedStart<Base.Rock->GetOlderBedCount()&&Base.Rock->GetBed(BedStart).UpperMeters<=Info.InceptionRadiusMeters)++BedStart;
        for(const auto& P:Paths)if(!P.Entrance)
        {
            double Run=0,Next=Wall.FRandRange(8,18);
            for(int32 K=1;K<P.Points.Num()-1;++K)
            {
                Run+=FVector::Distance(P.Points[K-1],P.Points[K]);if(Run<Next)continue;Run=0;Next=Wall.FRandRange(8,18);
                bool InRoom=false;for(const auto& C:Chambers)if(FVector::Distance(P.Points[K],Nodes[C.Node].Position)<C.HalfExtents.GetMax()+R*2){InRoom=true;break;}
                if(InRoom)continue;
                const FVector Up=P.BeddingUps[K],Along=(P.Points[K+1]-P.Points[K-1]).GetSafeNormal();const FQuat Frame=FRotationMatrix::MakeFromXZ(Along,Up).ToQuat();
                TArray<int32> Beds;for(int32 I=BedStart;I<Base.Rock->GetOlderBedCount();++I){const auto Bed=Base.Rock->GetBed(I);if(Bed.LowerMeters-Info.InceptionRadiusMeters>P.Sections[K].Y*1.23)break;Beds.Add(I);}
                if(Beds.IsEmpty())continue;
                for(double Sign:{-1.,1.})
                {
                    if(Wall.FRand()<.2)continue;
                    const int32 Pieces=S.AlgorithmVersion>=7?FMath::Min(3,Beds.Num()):1;
                    for(int32 Piece=0;Piece<Pieces;++Piece)
                    {
                    const auto Bed=Base.Rock->GetBed(Beds[Wall.RandRange(0,Beds.Num()-1)]);
                    const double Mid=(Bed.LowerMeters+Bed.UpperMeters)*.5-Info.InceptionRadiusMeters;
                    FCLCaveInfill B;B.Network=Info.Id;B.BeddedRemnant=true;B.WallOutward=S.AlgorithmVersion>=7?Frame.GetAxisY()*Sign:FVector::ZeroVector;
                    B.HalfExtents=FVector(Wall.FRandRange(1.5,3.5),Wall.FRandRange(.6,1.5),FMath::Clamp((Bed.UpperMeters-Bed.LowerMeters)*Wall.FRandRange(.15,.28),.25,.9));
                    const double Level=FMath::Clamp((Mid+Eye)/(P.Sections[K].Y*1.23),0.,1.);
                    const double Side=P.Sections[K].X*(.87-.18*Level)+B.HalfExtents.Y*.3;
                    B.Center=P.Points[K]+Frame.GetAxisY()*(Sign*Side)+Up*Mid;
                    const FVector D=B.Center.GetSafeNormal();B.Center=D*((Bed.LowerMeters+Bed.UpperMeters)*.5+Base.Rock->SampleColumn(D).DeformationMeters);
                    B.Rotation=Frame*FRotator(Wall.FRandRange(-4,4),Wall.FRandRange(-9,9),Wall.FRandRange(-8,8)).Quaternion();
                    B.SourceCenter=B.Center;B.SourceRotation=B.Rotation;Infills.Add(B);
                    }
                }
            }
        }
        if(S.AlgorithmVersion>=7)for(auto& B:Infills)if(!B.BeddedRemnant)
        {
            B.Center+=B.Center.GetSafeNormal()*CLCaveFloorRelief(B.Center,*Base.NativePerlin,Base.SourceRecipe->Seed^S.Seed,FMath::Min(1.,R*.22));
            if(B.BedrockPier)B.SourceCenter=B.Center;
        }
        // Solid features belong at the margins. Reserve half the passage
        // width plus capsule clearance throughout the actual graph crossing.
        Infills.RemoveAll([&](const FCLCaveInfill& B)
        {
            const FVector D=B.Center.GetSafeNormal();const FVector Foot=D*(Info.InceptionRadiusMeters+Base.Rock->SampleColumn(D).DeformationMeters);
            const double RockRadius=B.BeddedRemnant?B.HalfExtents.Y+B.HalfExtents.X*.25:FVector2D(B.HalfExtents.X,B.HalfExtents.Y).Length();
            if(B.BeddedRemnant&&Base.Rock->RestoredRadiusMeters(B.Center)-Info.InceptionRadiusMeters+Eye-B.HalfExtents.Z>3.)return false;
            for(const auto& P:Paths)if(!P.Entrance)for(int32 K=1;K<P.Points.Num();++K)
            {
                const double Clear=FMath::Max(P.Sections[K-1].X,P.Sections[K].X)*.5+RockRadius+1.;
                if(FMath::PointDistToSegment(Foot,P.Points[K-1],P.Points[K])<Clear)return true;
            }
            return false;
        });
        return;
    }
    if(Karst&&S.AlgorithmVersion>=5)
    {
        // One declared dry-karst construction, rather than cycling unrelated
        // section labels along every network. Routing and host evidence stay
        // with the existing geological builder.
        FRandomStream Shape(int32(GetTypeHash(Info.Id)^0x4B415235));
        for(auto& P:Paths)
        {
            P.Form=P.EndForm=P.Entrance?ECLCaveForm::Phreatic:ECLCaveForm::KarstPassage;
            P.Development=S.Character;P.Incision=0;
            const double Phase=Shape.FRandRange(-PI,PI);
            for(int32 I=0;I<P.Points.Num();++I)
            {
                const double T=double(I)/(P.Points.Num()-1),Envelope=FMath::Square(FMath::Sin(PI*T));
                // Broad correlated changes form constrictions and enlargements;
                // the protected graph core and endpoint sections remain shared.
                if(!P.Entrance)
                {
                    const double Throat=(Nodes[P.From].Chamber?1-FMath::SmoothStep(0.,.3,T):0)+
                        (Nodes[P.To].Chamber?FMath::SmoothStep(.7,1.,T):0);
                    P.Sections[I].X*=1+.20*Throat+Envelope*.20*FMath::Sin(2.4*PI*T+Phase);
                    P.Sections[I].Y*=1+.12*Throat+Envelope*.13*FMath::Cos(1.7*PI*T+Phase);
                }
                P.BeddingUps.Add(BeddingUp(Base,P.Points[I]));
            }
        }
        Chambers.Reset();Infills.Reset();
        for(int32 I=0;I<Nodes.Num();++I)
        {
            if(!Nodes[I].Chamber||Nodes[I].Portal)continue;
            const auto* P=Paths.FindByPredicate([&](const auto& V){return !V.Entrance&&(V.From==I||V.To==I);});
            if(!P)continue;
            const FVector Along=(P->Points.Last()-P->Points[0]).GetSafeNormal();
            FCLCaveChamber C;C.Network=Info.Id;C.Node=I;C.Center=Nodes[I].Position;
            C.Rotation=FRotationMatrix::MakeFromXZ(Along,BeddingUp(Base,C.Center)).ToQuat();
            C.HalfExtents=S.RadiusMeters*FVector(Shape.FRandRange(3.4,4.4),Shape.FRandRange(2.0,2.7),Shape.FRandRange(1.7,2.2));
            // Enlargement develops mostly above the shared route floor, rather
            // than dropping a spherical bowl beneath elevated passage mouths.
            C.Center+=C.Rotation.GetAxisZ()*(C.HalfExtents.Z*.95-Nodes[I].Section.Y*1.20);
            C.Form=ECLCaveForm::KarstChamber;Chambers.Add(C);
        }
        return;
    }
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
