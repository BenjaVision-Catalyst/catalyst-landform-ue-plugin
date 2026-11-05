#include "Model/CLCaves.h"
#include "Model/CLPlanetRecipe.h"
#include "Model/CLCompileProgress.h"
#include "Model/CLRegionGeneration.h"
#include "Model/CLProvinceDefinition.h"
#include "Model/CLTectonics.h"

void FCLCaveSettings::ApplyProgression(double Value)
{
    Character=FMath::Clamp(Value,0.,1.);
    NetworksPerSquareKilometer=.8*Character;
    LengthMeters=FMath::Lerp(35.,140.,Character);
    RadiusMeters=FMath::Lerp(1.1,3.2,Character);
    DepthMeters=FMath::Lerp(10.,42.,Character);
    Branching=FMath::Lerp(.15,1.,Character);
}
bool FCLCaveSettings::Validate(FString& Error)const
{
    auto In=[](double V,double A,double B){return FMath::IsFinite(V)&&V>=A&&V<=B;};
    if(AlgorithmVersion!=1||!In(Character,0,1)||!In(NetworksPerSquareKilometer,0,10)||!In(LengthMeters,1,500)||!In(RadiusMeters,.1,10)||!In(MinimumCoverMeters,1,100)||!In(DepthMeters,1,500)||!In(Branching,0,1))
    {Error=TEXT("Caves require algorithm 1 and finite bounded physical density, dimensions, cover and branching");return false;}return true;
}
TSharedPtr<const FCLCompiledCaves> FCLCompiledCaves::Compile(const FCLCaveSettings& S,const FCLCompiledPlanetRecipe& Base,int32 Seed,FString& Error)
{
    Error.Reset();if(!S.Validate(Error))return nullptr;
    auto Out=MakeShared<FCLCompiledCaves>();
    if(!S.bEnabled){Out->Readiness=TEXT("No cave development requested");return Out;}
    if(!Base.Rock||!Base.Tectonics||!Base.GeneratedRegions)
    {Error=TEXT("Enabled caves require Rock, tectonics and selected rift/shear Landforms");return nullptr;}
    if(!Base.Rock->HasFractureHosts())
    {Error=TEXT("Enabled fissure caves require all three rock units to declare a known non-ice lithology and positive fracture potential");return nullptr;}
    const double Reference=Base.RadiusMeters+(Base.MinimumHeightMeters+Base.MaximumHeightMeters)*.5;
    if(S.DepthMeters+S.MinimumCoverMeters+S.RadiusMeters*3>Reference*.3||S.LengthMeters>Reference*.5)
    {Error=TEXT("Cave dimensions exceed this body's bounded physical profile; reduce depth/length or enlarge the body");return nullptr;}
    const double Area=4*UE_DOUBLE_PI*Reference*Reference/1.e6;
    constexpr double CandidateDensity=16;
    const double Requested=FMath::CeilToDouble(Area*CandidateDensity);
    if(Requested>16384){Error=TEXT("Cave placement exceeds the 16384-candidate physical-area proof budget; no coarsening or truncated generation was applied");return nullptr;}
    Out->Candidates=FMath::Max(16,int32(Requested));
    FRandomStream Random(int32(uint32(Seed)^uint32(S.Seed)^0x43415645));
    const FQuat Rotation(FRotator(Random.FRandRange(-90,90),Random.FRandRange(-180,180),Random.FRandRange(-180,180)));

    auto Profile=[&](FVector D)->ECLProvinceType
    {
        const auto& G=*Base.GeneratedRegions;const auto& P=G.Palette[G.Cells[G.Owner(D)].PaletteIndex];
        const auto Type=P.Suitability.Province.Type;
        if(Type!=ECLProvinceType::ContinentalRift&&Type!=ECLProvinceType::TransformBelt)return ECLProvinceType::None;
        return P.Suitability.Province.Match(FCLProvinceGeology::Sample(Base,D))==ECLProvinceMatch::Eligible?Type:ECLProvinceType::None;
    };
    auto Drive=[&](FVector D,ECLProvinceType Type){const auto V=Base.Tectonics->SampleRockDeformation(D,1,0,1);return Type==ECLProvinceType::ContinentalRift?V.Extension:V.Shear;};
    struct FCandidate {int32 Index;FVector Direction;ECLProvinceType Type;double Occurrence,Phase,Depth,Width,Branch,Entrance;};
    TArray<FCandidate> Eligible;
    auto Place=[&](const FCandidate& C)->bool
    {
        FRandomStream ShapeRandom(int32(HashCombineFast(uint32(Seed)^uint32(S.Seed),uint32(C.Index+1))));
        const FVector D=C.Direction;
        const FVector X=FVector::CrossProduct(D,FMath::Abs(D.Z)<.8?FVector::ZAxisVector:FVector::XAxisVector).GetSafeNormal(),Y=FVector::CrossProduct(D,X);
        constexpr double Step=.001;
        const FVector Gradient=X*(Drive((D+X*Step).GetSafeNormal(),C.Type)-Drive((D-X*Step).GetSafeNormal(),C.Type))+Y*(Drive((D+Y*Step).GetSafeNormal(),C.Type)-Drive((D-Y*Step).GetSafeNormal(),C.Type));
        FVector Along=FVector::CrossProduct(D,Gradient).GetSafeNormal();if(Along.IsNearlyZero())Along=X;
        Along=FQuat(D,C.Phase).RotateVector(Along);
        // An explicit structural approximation: primary and conjugate joints,
        // oriented by extension/shear evidence. No dissolution or flow history
        // is inferred for these declared fractured clastic/crystalline hosts.
        const FVector Across=FVector::CrossProduct(D,Along).GetSafeNormal();
        const double JointAngle=ShapeRandom.FRandRange(.85,1.35)*(ShapeRandom.RandRange(0,1)?1:-1);
        const FVector Joint=Along*FMath::Cos(JointAngle)+Across*FMath::Sin(JointAngle);
        const double L=S.LengthMeters,R=S.RadiusMeters*C.Width;
        const double Depth=S.MinimumCoverMeters+R*6+S.DepthMeters*C.Depth*.65;
        const double RoofRadius=Base.RadiusMeters+Base.SampleHeightMeters(D);
        const FGuid Id(0x43415645,uint32(Seed),uint32(S.Seed),C.Index+1);
        auto Body=[&](FVector P)
        {
            const FVector Direction=(D+(Along*P.X+Across*P.Y)/Reference).GetSafeNormal();
            // One structural level across the network, with deliberate steps;
            // underground paths do not trace every hill of the exterior.
            return Direction*(RoofRadius-Depth+P.Z);
        };
        TArray<FCLCompiledCaves::FNode> Nodes;
        auto Node=[&](FVector P,double Width,double Height,bool Chamber=false,bool Portal=false)
        {return Nodes.Add({Id,Body(P),FVector2D(R*Width,R*Height),Chamber,Portal});};
        const double Tilt=ShapeRandom.FRandRange(-.035,.035)*L;
        const int32 A=Node(FVector(-L*.43,0,-Tilt),.75,1.35);
        const int32 B=Node(FVector(-L*.14,L*.025,0),1.05,1.5,true);
        const int32 Cn=Node(FVector(L*.15,-L*.035,Tilt*.4),1.,1.35,S.Character>.25);
        const int32 End=Node(FVector(L*.43,L*.025,Tilt),.6,1.15);
        TArray<FCLCavePassage> Paths;
        auto Passage=[&](int32 From,int32 To,double Spread,bool Entrance=false)
        {
            FCLCavePassage P;P.Network=Id;P.From=From;P.To=To;P.Entrance=Entrance;
            const auto& N=Nodes[From];const auto& M=Nodes[To];const FVector Delta=M.Position-N.Position;
            const FVector Bend=FVector::CrossProduct(D,Delta).GetSafeNormal();
            const double Phase=ShapeRandom.FRandRange(0,2*UE_DOUBLE_PI),Curve=ShapeRandom.FRandRange(.65,1.25);
            constexpr int32 Steps=6;
            for(int32 K=0;K<=Steps;++K)
            {
                const double T=double(K)/Steps,Envelope=FMath::Sin(UE_DOUBLE_PI*T);
                // Correlated changes in orientation, section and slope. End
                // positions/sections are shared at every graph junction.
                P.Points.Add(FMath::Lerp(N.Position,M.Position,T)+Bend*(Envelope*Spread*Curve*FMath::Sin(T*2*UE_DOUBLE_PI+Phase))+D*(Envelope*R*.4*FMath::Sin(T*UE_DOUBLE_PI+Phase)));
                auto Section=FMath::Lerp(N.Section,M.Section,T);
                Section.X*=1+Envelope*.3*FMath::Sin(T*2*UE_DOUBLE_PI+Phase);
                Section.Y*=1+Envelope*.22*FMath::Cos(T*2*UE_DOUBLE_PI+Phase*.7);
                P.Sections.Add(Section);
            }
            Paths.Add(MoveTemp(P));
        };
        Passage(A,B,L*.035);Passage(B,Cn,L*.04);Passage(Cn,End,L*.035);
        // Even a modest cave includes a secondary opening. The one-knob
        // progression changes topology and level structure as well as size.
        const double SideLength=L*ShapeRandom.FRandRange(.16,.25);
        const FVector Side=Joint*SideLength;
        const auto LocalSide=[&](double X0,double Sign,double Z)
        {return FVector(X0+FVector::DotProduct(Side,Along)*Sign,FVector::DotProduct(Side,Across)*Sign,Z);};
        const int32 Upper=Node(LocalSide(-L*.14,1,-R),.7,1.6);
        Passage(B,Upper,L*.025);
        if(S.Character>.3||C.Branch<S.Branching)
        {
            const int32 Lower=Node(LocalSide(L*.15,-1,-S.DepthMeters*.22),.85,1.3,true);
            Passage(Cn,Lower,L*.03);
            if(S.Character>.5)Passage(Upper,Cn,L*.045); // reconnecting joint loop
            if(S.Character>.7)
            {
                const FVector P=LocalSide(-L*.14,1,-S.DepthMeters*.32-R*2);
                const int32 Deep=Node(P,.8,1.15);
                Passage(Upper,Deep,R*.25); // steep structural connector
                Passage(Deep,Lower,L*.03); // lower-level route
            }
        }
        if(S.bSurfaceEntrances)
        {
            // A real surface intersection, approached obliquely along the
            // structural corridor. A connected cave is inspectable from outside.
            auto PortalLocal=FVector(-L*.5,L*.025,0);const FVector Q=Body(PortalLocal).GetSafeNormal();
            const int32 Portal=Node(PortalLocal,.85,1.2,false,true);
            Nodes[Portal].Position=Q*(Base.RadiusMeters+Base.SampleHeightMeters(Q)+R*.5);
            const auto InteriorRandom=ShapeRandom;Passage(A,Portal,L*.02,true);ShapeRandom=InteriorRandom;
        }
        TArray<FCLCaveChamber> Chambers;
        for(int32 I=0;I<Nodes.Num();++I)if(Nodes[I].Chamber)
        {
            const double Width=R*ShapeRandom.FRandRange(2.5,3.8),Height=R*ShapeRandom.FRandRange(1.8,2.6);
            const FQuat Rotation=FRotationMatrix::MakeFromXZ(Along,Nodes[I].Position.GetSafeNormal()).ToQuat();
            // Overlapping irregular structural pockets, elongated along the
            // main joint. Native smooth union makes one traversable space.
            for(int32 K=-1;K<=1;++K)
            {
                const FVector Offset=Along*(K*Width*.65)+Across*(K==0?0:ShapeRandom.FRandRange(-.3,.3)*Width);
                Chambers.Add({Id,I,Nodes[I].Position+Offset,FVector(Width*(K==0?1.:.72),Width*ShapeRandom.FRandRange(.55,.85),Height*ShapeRandom.FRandRange(.75,1.)),Rotation});
            }
        }
        // Validate the whole sampled footprint before publishing any part of
        // this candidate. Bounds and roof safety use full widths, not axes alone.
        for(const auto& P:Paths)for(int32 I=0;I<P.Points.Num();++I)
        {
            const auto Direction=P.Points[I].GetSafeNormal();if(Profile(Direction)!=C.Type)return false;
            if(!P.Entrance)
            {
                const double Support=FMath::Max(P.Sections[I].X,P.Sections[I].Y)*1.5+1;
                if(Base.RadiusMeters+Base.SampleHeightMeters(Direction)-P.Points[I].Length()<S.MinimumCoverMeters+Support)return false;
            }
        }
        for(const auto& Chamber:Chambers)
        {
            const double Support=Chamber.HalfExtents.GetMax()*1.2;
            for(const FVector Axis:{D,Along,-Along,Across,-Across})
            {
                const auto Q=Chamber.Center+Axis*Support;const auto Direction=Q.GetSafeNormal();
                if(Profile(Direction)!=C.Type||Base.RadiusMeters+Base.SampleHeightMeters(Direction)-Q.Length()<S.MinimumCoverMeters)return false;
            }
        }
        if(Out->Passages.Num()+Paths.Num()>4096||Out->Chambers.Num()+Chambers.Num()>2048)
        {Error=TEXT("Cave construction exceeds 4096 native spline passages or 2048 chamber lobes; no partial networks were published");return false;}
        const int32 Offset=Out->Nodes.Num();Out->Nodes.Append(Nodes);
        for(auto& P:Paths)
        {
            P.From+=Offset;P.To+=Offset;
            for(int32 I=1;I<P.Points.Num();++I)
            {
                const auto Section=FVector2D(FMath::Max(P.Sections[I-1].X,P.Sections[I].X),FMath::Max(P.Sections[I-1].Y,P.Sections[I].Y));
                Out->Segments.Add({Id,P.Points[I-1],P.Points[I],1,FVector(Section.X,Section.Y,1),P.Entrance});
            }
            Out->Entrances+=P.Entrance;Out->Passages.Add(MoveTemp(P));
        }
        for(auto& Chamber:Chambers){Chamber.Node+=Offset;Out->Chambers.Add(MoveTemp(Chamber));}
        ++Out->Networks;return true;
    };
    for(int32 I=0;I<Out->Candidates;++I)
    {
        if(!FCLCompileProgress::Check(Error))return nullptr;
        FCLCompileProgress::Count(I,Out->Candidates);
        const double Z=1-2*(I+.5)/Out->Candidates,A=I*2.399963229728653;
        const FVector D=Rotation.RotateVector(FVector(FMath::Sqrt(1-Z*Z)*FMath::Cos(A),FMath::Sqrt(1-Z*Z)*FMath::Sin(A),Z));
        const double Occurrence=Random.FRand(),Phase=Random.FRandRange(-.35,.35),DepthJitter=Random.FRandRange(.8,1.2),WidthJitter=Random.FRandRange(.8,1.2),BranchDraw=Random.FRand(),EntranceDraw=Random.FRand();
        const auto Type=Profile(D);if(Type==ECLProvinceType::None)continue;++Out->EligibleCandidates;
        const FCandidate C{I,D,Type,Occurrence,Phase,DepthJitter,WidthJitter,BranchDraw,EntranceDraw};Eligible.Add(C);
        if(Occurrence>S.NetworksPerSquareKilometer*Area/Out->Candidates*Base.Rock->FracturePotential())continue;
        Place(C);if(!Error.IsEmpty())return nullptr;
    }
    bool MinimumApplied=false;
    if(Out->Networks==0)
    {
        // Preserve the density draws and identities. An enabled layer guarantees
        // representation only where the existing complete network is admissible.
        Eligible.Sort([](const auto& A,const auto& B){return A.Occurrence==B.Occurrence?A.Index<B.Index:A.Occurrence<B.Occurrence;});
        for(const auto& C:Eligible)
        {
            if(!FCLCompileProgress::Check(Error))return nullptr;
            if(Place(C)){MinimumApplied=true;break;}if(!Error.IsEmpty())return nullptr;
        }
    }
    if(Out->Networks==0){Error=TEXT("Caves are enabled but no complete fissure network fits the selected eligible geology at the requested dimensions; select compatible Landforms/hosts or reduce the cave dimensions");return nullptr;}
    FCLCompileProgress::Count(Out->Candidates,Out->Candidates);
    Out->Cutters=FCLCompiledCaveVolume::Compile(Out->Passages,Out->Chambers,Base.NativePerlin,Seed^S.Seed,Error);if(!Out->Cutters)return nullptr;
    Out->Readiness=FString::Printf(TEXT("%d structural cave networks, %d curved passages, %d entrances; %d/%d candidates have selected local rift/shear evidence"),Out->Networks,Out->Passages.Num(),Out->Entrances,Out->EligibleCandidates,Out->Candidates);
    if(MinimumApplied)Out->Readiness+=TEXT("; enabled-layer minimum applied");
    return Out;
}
