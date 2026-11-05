#include "CLCaveKarst.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLWatershed.h"
#include "Math/RotationMatrix.h"

bool CLBuildKarst(const FCLCaveSettings& S,const FCLCompiledPlanetRecipe& Base,FVector D,FCLCompiledCaves::FNetwork& Info,FRandomStream& Random,TArray<FCLCompiledCaves::FNode>& Nodes,TArray<FCLCavePassage>& Paths,TArray<FCLCaveChamber>& Chambers)
{
    if(!Base.Watershed||!Base.Rock->HasKarstHosts())return false;
    const auto& Cells=Base.Watershed->GetCells();const int32 Head=Base.Watershed->FindCell(D);
    if(!Cells.IsValidIndex(Head)||Cells[Head].bOcean||Cells[Head].bLake)return false;
    const double Reference=Base.Watershed->ReferenceRadiusMeters,R=S.RadiusMeters;
    auto Length=[&](int32 A,int32 B){return FMath::Acos(FMath::Clamp(FVector::DotProduct(Cells[A].Direction,Cells[B].Direction),-1.,1.))*Reference;};
    TArray<int32> Trunk;Trunk.Add(Head);double Run=0;
    for(int32 I=0;I<256&&Run<S.LengthMeters;++I)
    {
        const int32 Current=Trunk.Last(),Next=Cells[Current].Receiver;
        if(!Cells.IsValidIndex(Next)||Cells[Next].Rank>=Cells[Current].Rank||Cells[Next].bOcean||Cells[Next].bLake)break;
        Run+=Length(Current,Next);Trunk.Add(Next);
    }
    if(Trunk.Num()<4||Run<S.LengthMeters*.35)return false;
    // An actual receiver confluence supplies the secondary recharge route;
    // never invent a second surface drainage solver for cave placement.
    TArray<int32> Branch;int32 Junction=INDEX_NONE;
    for(int32 K=1;K<Trunk.Num()-1;++K)
    {
        int32 Best=INDEX_NONE;double Weight=-1;
        for(int32 N:Cells[Trunk[K]].Neighbours)
            if(Cells[N].Receiver==Trunk[K]&&!Trunk.Contains(N)&&!Cells[N].bOcean&&!Cells[N].bLake&&Cells[N].AccumulatedAreaSquareMeters>Weight){Best=N;Weight=Cells[N].AccumulatedAreaSquareMeters;}
        if(Best!=INDEX_NONE){Junction=K;Branch={Trunk[K],Best};break;}
    }
    if(Junction==INDEX_NONE)return false;
    double BranchLength=Length(Branch[0],Branch[1]);
    for(int32 K=0;K<64&&BranchLength<S.LengthMeters*.3;++K)
    {
        const int32 Current=Branch.Last();int32 Best=INDEX_NONE;double Weight=-1;
        for(int32 N:Cells[Current].Neighbours)
            if(Cells[N].Receiver==Current&&!Trunk.Contains(N)&&!Branch.Contains(N)&&!Cells[N].bOcean&&!Cells[N].bLake&&Cells[N].AccumulatedAreaSquareMeters>Weight){Best=N;Weight=Cells[N].AccumulatedAreaSquareMeters;}
        if(Best==INDEX_NONE)break;BranchLength+=Length(Current,Best);Branch.Add(Best);
    }
    // Select a real older bedding contact below every route's roof. The fixed
    // restored level is then deformed by the canonical Rock field, rather than
    // tracing hills or randomly moving a water table as rainfall changes.
    double Limit=MAX_dbl;
    for(const auto* Route:{&Trunk,&Branch})for(int32 I:*Route)
    {
        const auto Q=Cells[I].Direction;const auto Column=Base.Rock->SampleColumn(Q);if(!Column.bValid)return false;
        Limit=FMath::Min(Limit,Base.RadiusMeters+Base.SampleHeightMeters(Q)-Column.DeformationMeters-S.MinimumCoverMeters-R*7-S.DepthMeters*.45);
    }
    int32 Begin=0,End=Base.Rock->GetOlderBedCount();
    while(Begin<End){const int32 Mid=(Begin+End)/2;if(Base.Rock->GetBed(Mid).UpperMeters<=Limit)Begin=Mid+1;else End=Mid;}
    if(Begin==0)return false;
    Info.RechargeCell=Head;Info.ReceivingCell=Trunk.Last();Info.InceptionRadiusMeters=Base.Rock->GetBed(Begin-1).UpperMeters;
    auto Body=[&](int32 I){const auto Q=Cells[I].Direction;return Q*(Info.InceptionRadiusMeters+Base.Rock->SampleColumn(Q).DeformationMeters);};
    auto Node=[&](int32 Cell,FVector2D Section,bool Chamber=false)
    {return Nodes.Add({Info.Id,Body(Cell),Section,Chamber,false});};
    const int32 A=Node(Head,{R*.7,R*.8}),B=Node(Trunk[Junction],{R*1.3,R*.85},true),C=Node(Trunk.Last(),{R,R*.7}),Recharge=Node(Branch.Last(),{R*.65,R*.75});
    auto Passage=[&](int32 From,int32 To,const TArray<int32>& Route,int32 First,int32 Last,double Incision)
    {
        FCLCavePassage P;P.Network=Info.Id;P.From=From;P.To=To;P.Incision=Incision;
        const double Phase=Random.FRandRange(0,2*UE_DOUBLE_PI);
        for(int32 K=First;K<=Last;++K)
        {
            const double T=double(K-First)/FMath::Max(1,Last-First);const double E=FMath::Sin(UE_DOUBLE_PI*T);
            const FVector Center=Body(Route[K]),Along=Body(Route[FMath::Min(K+1,Last)])-Body(Route[FMath::Max(First,K-1)]);
            const FVector Side=FVector::CrossProduct(Center.GetSafeNormal(),Along).GetSafeNormal();
            P.Points.Add(Center+Side*(R*.35*E*FMath::Sin(T*5+Phase)));
            auto Section=FMath::Lerp(Nodes[From].Section,Nodes[To].Section,T);
            Section.X*=1+.35*E*FMath::Sin(T*6+Phase);Section.Y*=1+.25*E*FMath::Cos(T*5+Phase);P.Sections.Add(Section);
        }
        Paths.Add(MoveTemp(P));
    };
    Passage(A,B,Trunk,0,Junction,.8*S.Character);Passage(B,C,Trunk,Junction,Trunk.Num()-1,0);
    Passage(B,Recharge,Branch,0,Branch.Num()-1,1.1*S.Character);
    if(S.Character>.5)
    {
        // An abandoned higher conduit reconnects to the same junctions. Its
        // course is a development prior, not a simulated palaeo-water-table.
        auto P=Paths[1];P.Incision=0;const FVector Along=(P.Points.Last()-P.Points[0]).GetSafeNormal(),Side=FVector::CrossProduct(D,Along).GetSafeNormal();
        for(int32 K=0;K<P.Points.Num();++K)
        {const double E=FMath::Sin(UE_DOUBLE_PI*K/(P.Points.Num()-1));P.Points[K]+=(D*R*2.2+Side*R*3)*E;P.Sections[K]*=.8;}
        // Two-cell reaches need an intermediate control to separate the loop.
        if(P.Points.Num()==2){P.Points.Insert((P.Points[0]+P.Points[1])*.5+D*R*2.2+Side*R*3,1);P.Sections.Insert((P.Sections[0]+P.Sections[1])*.4,1);}
        P.Sections[0]=Nodes[B].Section;P.Sections.Last()=Nodes[C].Section;Paths.Add(MoveTemp(P));
    }
    const FVector Along=(Nodes[C].Position-Nodes[A].Position).GetSafeNormal(),Across=FVector::CrossProduct(D,Along).GetSafeNormal();
    const auto Frame=FRotationMatrix::MakeFromXZ(Along,Nodes[B].Position.GetSafeNormal()).ToQuat();
    for(int32 K=-1;K<=1;++K)
    {
        const double W=R*Random.FRandRange(2.8,3.8);
        Chambers.Add({Info.Id,B,Nodes[B].Position+Along*(K*R*1.8)+Across*(K?Random.FRandRange(-.4,.4)*R:0),FVector(W,R*Random.FRandRange(2,2.7),R*Random.FRandRange(1.4,2.1)),Frame});
    }
    if(S.bSurfaceEntrances)
    {
        // A genuine recharge shaft intersects the real surface. No spring is
        // claimed when the receiving continuation remains below ground.
        const FVector Q=Cells[Head].Direction;const int32 Portal=Nodes.Add({Info.Id,Q*(Base.RadiusMeters+Base.SampleHeightMeters(Q)+R*.5),{R*.75,R*.85},false,true});
        FCLCavePassage P;P.Network=Info.Id;P.From=A;P.To=Portal;P.Entrance=true;
        for(int32 K=0;K<=6;++K){const double T=K/6.;P.Points.Add(FMath::Lerp(Nodes[A].Position,Nodes[Portal].Position,T)+Across*(R*.5*FMath::Sin(UE_DOUBLE_PI*T)));P.Sections.Add(FMath::Lerp(Nodes[A].Section,Nodes[Portal].Section,T));}Paths.Add(MoveTemp(P));
    }
    return true;
}
