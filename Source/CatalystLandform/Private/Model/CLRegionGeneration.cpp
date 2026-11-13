#include "Model/CLRegionGeneration.h"
#include "Model/CLCompileProgress.h"
#include "Model/CLTerrainType.h"
#include "Model/CLSphericalPatches.h"
#include "Math/RotationMatrix.h"

namespace
{
uint32 MixRegion(uint32 V){V^=V>>16;V*=0x7feb352dU;V^=V>>15;V*=0x846ca68bU;return V^(V>>16);}
double RandomRegion(uint32 V){return (double(MixRegion(V))+.5)/4294967296.;}
double Quintic(double T){T=FMath::Clamp(T,0.,1.);return T*T*T*(T*(T*6-15)+10);}
double RangeFit(double V,const FVector2D& Range,double Softness)
{const double D=FMath::Max(FMath::Max(Range.X-V,V-Range.Y),0.)/Softness;return FMath::Exp(-D*D);}
FGuid CellId(int32 Seed,int32 Index){return FGuid(0x434C5247U,uint32(Seed),1,uint32(Index+1));}

// Minimum-cost cell-to-slot matching. Slots carry the balanced source capacities;
// optimizing the complete assignment avoids a source winning every local contest
// or an early cell consuming the only good location for another source.
bool AssignBalancedCells(const TArray<TArray<double>>& Costs,const TArray<int32>& Cells,
    const TArray<int32>& Slots,TArray<int32>& Assigned,FString& Error)
{
    const int32 N=Cells.Num();check(N==Slots.Num());
    TArray<double> U,V;U.Init(0,N+1);V.Init(0,N+1);
    TArray<int32> Row,Previous;Row.Init(0,N+1);Previous.Init(0,N+1);
    for(int32 I=1;I<=N;++I)
    {
        if(!FCLCompileProgress::Check(Error))return false;
        Row[0]=I;int32 J0=0;TArray<double> Minimum;Minimum.Init(MAX_dbl,N+1);TArray<bool> Used;Used.Init(false,N+1);
        do
        {
            Used[J0]=true;const int32 I0=Row[J0];double Delta=MAX_dbl;int32 J1=0;
            for(int32 J=1;J<=N;++J)if(!Used[J])
            {
                const double Cost=Costs[Cells[I0-1]][Slots[J-1]]-U[I0]-V[J];
                if(Cost<Minimum[J]){Minimum[J]=Cost;Previous[J]=J0;}
                if(Minimum[J]<Delta){Delta=Minimum[J];J1=J;}
            }
            for(int32 J=0;J<=N;++J)if(Used[J]){U[Row[J]]+=Delta;V[J]-=Delta;}else Minimum[J]-=Delta;
            J0=J1;
        }while(Row[J0]!=0);
        do{const int32 J1=Previous[J0];Row[J0]=Row[J1];J0=J1;}while(J0);
    }
    for(int32 J=1;J<=N;++J)Assigned[Cells[Row[J]-1]]=Slots[J-1];
    return true;
}
}
bool FCLLandformSuitability::Validate(FString& Error) const
{
    auto Range=[](const FVector2D& V,double Low,double High){return !V.ContainsNaN()&&V.X>=Low&&V.Y<=High&&V.X<=V.Y;};
    if(!FMath::IsFinite(SelectionWeight)||SelectionWeight<.01||SelectionWeight>100||
       !FMath::IsFinite(UpliftAffinity)||FMath::Abs(UpliftAffinity)>1||!FMath::IsFinite(BasinAffinity)||FMath::Abs(BasinAffinity)>1||
       !FMath::IsFinite(StructuralEdgeAffinity)||FMath::Abs(StructuralEdgeAffinity)>1||
       !Range(PreferredElevation,-4,4)||!Range(PreferredSlopeDegrees,0,90)||!Range(PreferredScale,.001,8)||
       (bUsePreferences&&(LowerElevationGuide.IsNone()||UpperElevationGuide.IsNone())))
    {Error=TEXT("Landform placement preferences require finite, ordered ranges, valid guide names, affinities -1..1 and weight 0.01..100");return false;}
    return Province.Validate(Error);
}
FVector FCLCompiledRegionGeneration::Signature(const FVector& D) const
{
    FVector V=FVector::ZeroVector;
    for(const auto& F:Structure)
    {
        const double W=F.Weight(D);
        if(F.Heightmap)V.Z+=W;
        else if(F.ReliefMeters>0)V.X+=W;
        else if(F.ReliefMeters<0)V.Y+=W;
    }
    V.X=1-FMath::Exp(-1.6*V.X);V.Y=1-FMath::Exp(-1.6*V.Y);V.Z=1-FMath::Exp(-1.6*V.Z);return V;
}
double FCLCompiledRegionGeneration::Cost(int32 I,const FVector& D,const FVector& S) const
{
    const auto& C=Cells[I];const FVector P=C.Frame.UnrotateVector(D-C.Centre);
    // Continuous chord metric on the sphere, elongated in the underlying feature's
    // tangent direction. Structural contrast bends neighbouring cellular boundaries.
    return FMath::Square(P.X/C.Stretch)+FMath::Square(P.Y*C.Stretch)+P.Z*P.Z+
        Guidance*(4*UE_DOUBLE_PI/Cells.Num())*.65*(S-C.StructureSignature).SizeSquared();
}
int32 FCLCompiledRegionGeneration::Owner(const FVector& Direction) const
{
    const FVector D=Direction.GetSafeNormal(),S=Signature(D);double Best=MAX_dbl;int32 Result=INDEX_NONE;
    for(int32 I=0;I<Cells.Num();++I){const double C=Cost(I,D,S);if(C<Best){Best=C;Result=I;}}return Result;
}
void FCLCompiledRegionGeneration::Weights(const FVector& Direction,double* CW,double* PW) const
{
    // Uniform assignment (including whole-planet Solo) has exact palette weight
    // one. Cell inspection still evaluates the unchanged spatial partition.
    if(PW&&!CW&&!Cells.IsEmpty())
    {
        const int32 P=Cells[0].PaletteIndex;bool Uniform=true;
        for(const auto& C:Cells)if(C.PaletteIndex!=P){Uniform=false;break;}
        if(Uniform){for(int32 I=0;I<Palette.Num();++I)PW[I]=I==P?1.:0.;return;}
    }
    const FVector D=Direction.GetSafeNormal(),S=Signature(D);double Costs[128],Best=MAX_dbl,Sum=0;
    // A C2 compact smooth minimum keeps the normalization differentiable at
    // nearest-cell ties, including triple junctions. Its accumulated depression
    // is bounded by Band/2, so at least one kernel always remains positive.
    const double K=3*Band/FMath::Max(1,Cells.Num()-1);
    for(int32 I=0;I<Cells.Num();++I)
    {
        Costs[I]=Cost(I,D,S);
        if(I==0)Best=Costs[I];
        else {const double H=FMath::Max(K-FMath::Abs(Best-Costs[I]),0.)/K;Best=FMath::Min(Best,Costs[I])-H*H*H*K/6;}
    }
    for(int32 I=0;I<Cells.Num();++I){Costs[I]=Quintic(1-(Costs[I]-Best)/Band);Sum+=Costs[I];}
    if(PW)for(int32 I=0;I<Palette.Num();++I)PW[I]=0;
    for(int32 I=0;I<Cells.Num();++I){const double W=Costs[I]/Sum;if(CW)CW[I]=W;if(PW)PW[Cells[I].PaletteIndex]+=W;}
}
void FCLCompiledRegionGeneration::CellWeights(const FVector& D,TArray<double>& W) const
{W.SetNumUninitialized(Cells.Num());Weights(D,W.GetData(),nullptr);}
void FCLCompiledRegionGeneration::PaletteWeights(const FVector& D,TArray<double>& W) const
{W.SetNumUninitialized(Palette.Num());Weights(D,nullptr,W.GetData());}
void FCLCompiledRegionGeneration::Blend(TConstArrayView<FVector> Directions,TArray<double>& Heights) const
{
    // Group queries by source, not cell. Shared sources are sampled once per point,
    // even when several neighbouring cells select them. Scratch is bounded to 512 points.
    if(Strength==0)return;
    for(int32 Start=0;Start<Directions.Num();Start+=512)
    {
        const int32 N=FMath::Min(512,Directions.Num()-Start);TArray<FVector> Queries[32];TArray<int32> Indices[32];TArray<double> Weight[32];
        for(int32 I=0;I<N;++I)
        {
            double W[32];Weights(Directions[Start+I],nullptr,W);
            for(int32 P=0;P<Palette.Num();++P)if(W[P]>0){Queries[P].Add(Directions[Start+I]);Indices[P].Add(Start+I);Weight[P].Add(W[P]);}
        }
        for(int32 P=0;P<Palette.Num();++P)if(!Queries[P].IsEmpty())
        {
            TArray<double> H;Fields[P]->SampleHeightsMeters(Queries[P],H);
            for(int32 I=0;I<H.Num();++I)Heights[Indices[P][I]]+=((H[I]-NeutralHeight)*(Rifts.IsValidIndex(P)&&Rifts[P]?Rifts[P]->SurfaceDetailStrength():Transforms.IsValidIndex(P)&&Transforms[P]?1.:VolcanicArcs.IsValidIndex(P)&&VolcanicArcs[P]?1.:2*Palette[P].Character)+(Rifts.IsValidIndex(P)&&Rifts[P]?Rifts[P]->SampleMeters(Queries[P][I]):0)+(Transforms.IsValidIndex(P)&&Transforms[P]?Transforms[P]->Sample(Queries[P][I]).Relief:0)+(VolcanicArcs.IsValidIndex(P)&&VolcanicArcs[P]?VolcanicArcs[P]->SampleMeters(Queries[P][I]):0))*Weight[P][I]*Strength;
        }
    }
}
bool FCLCompiledRegionGeneration::HasPhysicalRelief()const{return bHasPhysicalRelief;}
double FCLCompiledRegionGeneration::MaximumProvinceDisplacementMeters()const
{double Max=0;for(const auto& F:Rifts)if(F)Max=FMath::Max(Max,F->MaximumDisplacementMeters());for(const auto& F:Transforms)if(F)Max=FMath::Max(Max,F->MaximumDisplacementMeters());for(const auto& F:VolcanicArcs)if(F)Max=FMath::Max(Max,F->MaximumDisplacementMeters());return Max*Strength;}
double FCLCompiledRegionGeneration::ProvinceDeformationMeters(const FVector& D)const
{
    if(!bHasProvinceDeformation||Strength==0)return 0;double W[32];Weights(D,nullptr,W);double V=0;
    for(int32 P=0;P<Rifts.Num();++P)if(W[P]>0&&Rifts[P])V+=W[P]*Rifts[P]->SampleMeters(D);for(int32 P=0;P<Transforms.Num();++P)if(W[P]>0&&Transforms[P])V+=W[P]*Transforms[P]->Sample(D).Relief;for(int32 P=0;P<VolcanicArcs.Num();++P)if(W[P]>0&&VolcanicArcs[P])V+=W[P]*VolcanicArcs[P]->SampleMeters(D);return V*Strength;
}
FVector FCLCompiledRegionGeneration::ProvinceReferenceDirection(const FVector& D)const
{
    if(!bHasDirectionalDeformation||Strength==0)return D;
    const FVector Unit=D.GetSafeNormal();double W[32];Weights(Unit,nullptr,W);FVector Offset=FVector::ZeroVector;
    for(int32 P=0;P<Transforms.Num();++P)if(W[P]>0&&Transforms[P])Offset+=W[P]*Transforms[P]->Sample(Unit).TangentOffset/Transforms[P]->ReferenceRadius();
    return (Unit+Offset*Strength).GetSafeNormal();
}
bool FCLCompiledRegionGeneration::ValidateSettings(const FCLRegionGenerationSettings& Settings,FString& Error)
{
    auto Fail=[&](const FString& E){Error=E;return false;};
    if((Settings.AlgorithmVersion<1||Settings.AlgorithmVersion>4)||Settings.RegionCount<8||Settings.RegionCount>128||Settings.Palette.Num()>(Settings.AlgorithmVersion>=3?31:32)||Settings.Overrides.Num()>128||
       !FMath::IsFinite(Settings.StructureGuidance)||Settings.StructureGuidance<0||Settings.StructureGuidance>1||
       !FMath::IsFinite(Settings.BlendFraction)||Settings.BlendFraction<.05||Settings.BlendFraction>1||
       !FMath::IsFinite(Settings.ReliefStrength)||Settings.ReliefStrength<0||Settings.ReliefStrength>1)
        return Fail(TEXT("Region generation requires algorithm 1..4, 8..128 cells, at most 32 palette entries (31 in geological mode)/128 overrides and finite guidance, blend and strength values"));
    TSet<FGuid> Ids;
    for(const auto& Entry:Settings.Palette)
    {
        if(!Entry.Id.IsValid()||Ids.Contains(Entry.Id)||Entry.Name.TrimStartAndEnd().IsEmpty()||(Entry.AlgorithmVersion!=1&&Entry.AlgorithmVersion!=2)||!Entry.SurfaceStack.Validate(Error)||!Entry.Suitability.Validate(Error)||!FCLPlanetCompiler::ValidateNoiseLayers(Entry.NoiseLayers,Error))
            return Fail(Error.IsEmpty()?TEXT("Palette entries require unique IDs, names and valid source snapshots"):Error);
        if(!FCLLandformDeposit::ValidateList(Entry.Deposits,Error))return false;
        if(Entry.Suitability.Province.bPlaceholder&&!Entry.Deposits.IsEmpty())return Fail(TEXT("Province placeholders cannot introduce physical deposits"));
        if(Entry.Suitability.Province.IsActive()&&Settings.AlgorithmVersion<3)return Fail(TEXT("Province Landforms require geological region algorithm 3"));
        if(Entry.Suitability.Province.bPlaceholder&&(!Entry.NoiseLayers.IsEmpty()||Entry.SurfaceStack.IsActive()))return Fail(TEXT("Province placeholders cannot replace base relief or material"));
        if(!Entry.VolcanicArc.Validate(Error))return false;
        if(Entry.VolcanicArc.IsActive()&&(Settings.AlgorithmVersion!=4||Entry.Rift.IsActive()||Entry.Transform.IsActive()||Entry.Suitability.Province.Type!=ECLProvinceType::VolcanicArc||Entry.Suitability.Province.bPlaceholder||Entry.SurfaceStack.IsActive()))return Fail(TEXT("Volcanic arc requires geology-first mode 4 and its non-placeholder province without replacement materials"));
        if(Entry.VolcanicArc.IsActive()){FCLRiftLandform Additive;Additive.AlgorithmVersion=3;if(!Additive.ValidateNoise(Entry.NoiseLayers,Error))return false;}
        if(!Entry.Transform.Validate(Error))return false;
        if(Entry.Transform.IsActive()&&(Settings.AlgorithmVersion!=4||Entry.Rift.IsActive()||Entry.Suitability.Province.Type!=ECLProvinceType::TransformBelt||Entry.Suitability.Province.bPlaceholder||Entry.SurfaceStack.IsActive()))return Fail(TEXT("Transform requires geology-first mode 4 and a non-placeholder Transform Belt without replacement materials or Rift"));
        if(Entry.Transform.IsActive()){FCLRiftLandform Additive;Additive.AlgorithmVersion=3;if(!Additive.ValidateNoise(Entry.NoiseLayers,Error))return false;}
        if(!Entry.Rift.Validate(Error)||!Entry.Rift.ValidateNoise(Entry.NoiseLayers,Error))return false;
        if(Entry.Rift.IsActive()&&(Settings.AlgorithmVersion!=4||Entry.Suitability.Province.Type!=ECLProvinceType::ContinentalRift||Entry.Suitability.Province.bPlaceholder||Entry.SurfaceStack.IsActive()))return Fail(TEXT("Physical rifts require geology-first region algorithm 4 and a Continental Rift source without replacement noise/materials"));
        if(!FMath::IsFinite(Entry.Character)||Entry.Character<0||Entry.Character>1)return Fail(TEXT("Landform Character must be between zero and one"));
        Ids.Add(Entry.Id);
    }
    if(Settings.bEnabled&&!Settings.Palette.ContainsByPredicate([](const auto& P){return P.bEnabled;}))return Fail(TEXT("Select at least one enabled Landform for generated regions"));
    return true;
}
TSharedPtr<const FCLCompiledRegionGeneration> FCLCompiledRegionGeneration::Compile(const FCLPlanetRecipe& Recipe,const FCLCompiledPlanetRecipe& Base,UCLStampLibraryAsset* Library,FString& Error,const FCLCompiledRegionGeneration* NativeInputs)
{
    auto Fail=[&](const FString& E)->TSharedPtr<const FCLCompiledRegionGeneration>{Error=E;return nullptr;};
    const auto& Settings=Recipe.RegionGeneration;
    if(!ValidateSettings(Settings,Error))return nullptr;
    int32 EnabledCount=0;for(const auto& Entry:Settings.Palette)EnabledCount+=Entry.bEnabled;
    const int32 CellCount=FMath::Max(Settings.RegionCount,EnabledCount);
    auto Out=MakeShared<FCLCompiledRegionGeneration>();Out->Guidance=Settings.StructureGuidance;Out->Band=(4*UE_DOUBLE_PI/CellCount)*Settings.BlendFraction*.5;Out->Strength=Settings.ReliefStrength;Out->NeutralHeight=Base.BaseHeightMeters;Out->Structure=Base.BaseStructure;
    for(const auto& Entry:Settings.Palette)if(Entry.bEnabled)Out->Palette.Add(Entry);
    Out->Palette.Sort([](const auto& A,const auto& B){return A.Id.ToString(EGuidFormats::Digits)<B.Id.ToString(EGuidFormats::Digits);});
    if(Out->Palette.IsEmpty())return Fail(TEXT("Select at least one enabled Landform for generated regions"));
    const int32 BasePalette=Settings.AlgorithmVersion>=3?Out->Palette.Num():INDEX_NONE;
    if(BasePalette>=0){auto& Neutral=Out->Palette.AddDefaulted_GetRef();Neutral.Id=FGuid();Neutral.Name=TEXT("Base geology");}
    if(FCLCompileProgress::IsPreparing()||Settings.AlgorithmVersion==4)
    {
        // Resolve every possible native child on the game thread. An unused
        // child's failure remains deferred in legacy modes. Mode 4 requires
        // every included child to compile so its native input tape stays stable.
        Out->Fields.SetNum(Out->Palette.Num());Out->FieldErrors.SetNum(Out->Palette.Num());
        for(int32 P=0;P<Out->Palette.Num();++P)
        {
        auto Child=Recipe;Child.SchemaVersion=8;Child.RegionGeneration=FCLRegionGenerationSettings();Child.BaseStructure.Reset();if(Settings.AlgorithmVersion==4){Child.Rock=FCLRockLayer();Child.Watershed=FCLWatershedSettings();Child.Caves=FCLCaveSettings();Child.ElevationDistribution=FCLSurfaceDistributionSettings();Child.Volumes=FCLVolumeSettings();}Child.Tectonics=FCLTectonicPreset();Child.Continents=FCLContinentalLayer();Child.TerrainRegions.Reset();Child.NoiseLayers=Out->Palette[P].NoiseLayers;if(!Out->Palette[P].Rift.IsActive()&&!Out->Palette[P].Transform.IsActive()&&!Out->Palette[P].VolcanicArc.IsActive()&&Out->Palette[P].Seed)for(auto& L:Child.NoiseLayers)L.Seed=int32(uint32(L.Seed)^MixRegion(uint32(Out->Palette[P].Seed)));if(Out->Palette[P].Rift.AlgorithmVersion>=2)for(auto& L:Child.NoiseLayers)L.Seed=int32(uint32(L.Seed)^MixRegion(uint32(Out->Palette[P].Rift.Seed)));if(Out->Palette[P].Transform.IsActive())for(auto& L:Child.NoiseLayers)L.Seed=int32(uint32(L.Seed)^MixRegion(uint32(Out->Palette[P].Transform.Seed)));if(Out->Palette[P].VolcanicArc.IsActive())for(auto& L:Child.NoiseLayers)L.Seed=int32(uint32(L.Seed)^MixRegion(uint32(Out->Palette[P].VolcanicArc.Seed)));Child.AlgorithmVersion=Out->Palette[P].AlgorithmVersion;Child.SurfaceStack=Out->Palette[P].SurfaceStack;if(Child.SurfaceStack.IsActive())Child.SchemaVersion=13;Child.TerrainTypeId=Out->Palette[P].Id;Child.TerrainTypeName=Out->Palette[P].Name;
            Out->Fields[P]=FCLPlanetCompiler::Compile(Child,Out->FieldErrors[P],Library);
            if(Settings.AlgorithmVersion==4&&!Out->Fields[P])return Fail(Out->Palette[P].Name+TEXT(": ")+Out->FieldErrors[P]);
        }
        if(FCLCompileProgress::IsPreparing())return Out;
    }
    Out->Rifts.SetNum(Out->Palette.Num());
    for(int32 P=0;P<Out->Palette.Num();++P)if(Out->Palette[P].Rift.IsActive())
    {
        double R=Base.RadiusMeters+Base.BaseHeightMeters,Span=0;
        const auto* Lo=Base.Guides.FindByPredicate([](const auto& G){return G.Name==TEXT("SurfaceMin");});const auto* Hi=Base.Guides.FindByPredicate([](const auto& G){return G.Name==TEXT("SurfaceMax");});
        if(Lo&&Hi){Span=Hi->HeightMeters-Lo->HeightMeters;R=Base.RadiusMeters+(Lo->HeightMeters+Hi->HeightMeters)*.5;}
        Out->Rifts[P]=FCLCompiledRiftLandform::Compile(Out->Palette[P].Rift,Recipe.Seed,R,Span,Base.Tectonics,Base.Continents,Error);if(!Out->Rifts[P])return nullptr;
    }
    const uint32 Seed=MixRegion(uint32(Recipe.Seed))^MixRegion(uint32(Settings.Seed));
    const double U=RandomRegion(Seed),A=2*UE_DOUBLE_PI*RandomRegion(Seed+1),B=2*UE_DOUBLE_PI*RandomRegion(Seed+2);
    const FQuat Rotation(FMath::Sqrt(1-U)*FMath::Sin(A),FMath::Sqrt(1-U)*FMath::Cos(A),FMath::Sqrt(U)*FMath::Sin(B),FMath::Sqrt(U)*FMath::Cos(B));
    const int32 PoolCount=CellCount*16;TArray<FVector> Pool;TArray<double> Density,Distance;
    for(int32 I=0;I<PoolCount;++I)
    {
        const double Y=1-2*(I+.5)/PoolCount,R=FMath::Sqrt(1-Y*Y),Angle=I*2.399963229728653;
        const FVector D=Rotation.RotateVector(FVector(R*FMath::Sin(Angle),Y,R*FMath::Cos(Angle)));Pool.Add(D);
        const FVector S=Out->Signature(D);Density.Add(1+Settings.StructureGuidance*3*(S.X+S.Y+S.Z));Distance.Add(MAX_dbl);
    }
    // Weighted farthest-point sites retain whole-sphere coverage while placing more
    // regions around broad uplift/basins/scars. Site identity is independent of palette order.
    int32 Next=int32(MixRegion(Seed+3)%uint32(PoolCount));
    for(int32 I=0;I<CellCount;++I)
    {
        if(!FCLCompileProgress::Check(Error))return nullptr;
        FCLGeneratedRegionCell Cell;Cell.Id=CellId(Settings.Seed,I);Cell.Centre=Pool[Next];Cell.StructureSignature=Out->Signature(Cell.Centre);Cell.Uplift=Cell.StructureSignature.X;Cell.Basin=Cell.StructureSignature.Y;
        const double Influence=FMath::Max3(Cell.StructureSignature.X,Cell.StructureSignature.Y,Cell.StructureSignature.Z);Cell.StructuralEdge=4*Influence*(1-Influence);
        FVector Axis=FVector::VectorPlaneProject(FVector::XAxisVector,Cell.Centre).GetSafeNormal();if(Axis.IsNearlyZero())Axis=FVector::YAxisVector;
        double Strongest=0;
        for(const auto& F:Out->Structure){const double W=F.Weight(Cell.Centre);if(W>Strongest){Strongest=W;Axis=FVector::VectorPlaneProject(F.Orientation.RotateVector(F.HalfExtentRadians.X>=F.HalfExtentRadians.Y?FVector::XAxisVector:FVector::YAxisVector),Cell.Centre).GetSafeNormal();}}
        Cell.Frame=FRotationMatrix::MakeFromXZ(Axis,Cell.Centre).ToQuat();Cell.Stretch=1+Settings.StructureGuidance*1.25*Strongest;
        Cell.Scale=2*FMath::Sqrt(4*UE_DOUBLE_PI/CellCount)*(Base.RadiusMeters+Base.BaseHeightMeters)/Base.RadiusMeters;Out->Cells.Add(Cell);
        double Best=-1;int32 Winner=0;
        for(int32 J=0;J<PoolCount;++J){Distance[J]=FMath::Min(Distance[J],(Pool[J]-Cell.Centre).SizeSquared());const double Value=Distance[J]*Density[J];if(Value>Best){Best=Value;Winner=J;}}Next=Winner;
    }
    const double Epsilon=FMath::Clamp(FMath::Sqrt(4*UE_DOUBLE_PI/CellCount)*.06,.003,.04);TArray<FVector> Samples;
    for(const auto& Cell:Out->Cells){const FVector X=Cell.Frame.GetAxisX(),Y=Cell.Frame.GetAxisY(),N=Cell.Centre;Samples.Append({N,(N+X*Epsilon).GetSafeNormal(),(N-X*Epsilon).GetSafeNormal(),(N+Y*Epsilon).GetSafeNormal(),(N-Y*Epsilon).GetSafeNormal()});}
    TArray<double> Heights;Base.SampleHeightsMeters(Samples,Heights);
    struct FBaseSample {double Height,Slope;FVector Signature;FCLProvinceGeology Geology;};
    TArray<TArray<FBaseSample>> Character;Character.SetNum(Out->Cells.Num());
    if(Settings.AlgorithmVersion>=2)
    {
        // Equal-area sphere samples follow actual cell ownership, not a disc
        // approximation around the site. Fixed 128 samples/cell on average;
        // no surface-resolution or client mesh dependency. Bound field scratch.
        const int32 Count=CellCount*128;
        constexpr int32 Batch=512;constexpr double Step=.001;
        for(int32 Start=0;Start<Count;Start+=Batch)
        {
            if(!FCLCompileProgress::Check(Error))return nullptr;
            const int32 N=FMath::Min(Batch,Count-Start);TArray<FVector> D;
            for(int32 J=0;J<N;++J)
            {
                const int32 Index=Start+J;const double Y=1-2*(Index+.5)/Count,R=FMath::Sqrt(1-Y*Y),Angle=Index*2.399963229728653;
                const FVector P=Rotation.RotateVector(FVector(R*FMath::Sin(Angle),Y,R*FMath::Cos(Angle)));
                const FVector X=FVector::CrossProduct(P,FMath::Abs(P.Z)<.8?FVector::ZAxisVector:FVector::XAxisVector).GetSafeNormal(),Z=FVector::CrossProduct(P,X);
                D.Append({P,(P+X*Step).GetSafeNormal(),(P-X*Step).GetSafeNormal(),(P+Z*Step).GetSafeNormal(),(P-Z*Step).GetSafeNormal()});
            }
            TArray<double> H;Base.SampleHeightsMeters(D,H);
            for(int32 J=0;J<N;++J)
            {
                const int32 K=J*5;const FVector PX=D[K+1]*(Base.RadiusMeters+H[K+1]),NX=D[K+2]*(Base.RadiusMeters+H[K+2]),PY=D[K+3]*(Base.RadiusMeters+H[K+3]),NY=D[K+4]*(Base.RadiusMeters+H[K+4]);
                const FVector Normal=FVector::CrossProduct(PX-NX,PY-NY).GetSafeNormal();
                const double Slope=FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FMath::Abs(FVector::DotProduct(Normal,D[K])),0.,1.)));
                Character[Out->Owner(D[K])].Add({H[K],Slope,Out->Signature(D[K]),Settings.AlgorithmVersion>=3?FCLProvinceGeology::Sample(Base,D[K]):FCLProvinceGeology()});
            }
        }
        for(int32 I=0;I<Out->Cells.Num();++I)
        {
            auto& Cell=Out->Cells[I];auto& Values=Character[I];
            if(Values.IsEmpty())return Fail(TEXT("Regional characterization undersampled a cell; reduce region count or structure guidance"));
            Cell.CharacterSampleCount=Values.Num();Cell.SolidAngle=4*UE_DOUBLE_PI*Values.Num()/Count;
            Cell.HeightMeters=0;Cell.SlopeDegrees=0;Cell.Uplift=0;Cell.Basin=0;Cell.StructuralEdge=0;
            TArray<double> Elevations,Slopes;
            for(const auto& V:Values)
            {
                Cell.HeightMeters+=V.Height;Cell.SlopeDegrees+=V.Slope;Cell.Uplift+=V.Signature.X;Cell.Basin+=V.Signature.Y;
                const double Influence=FMath::Max3(V.Signature.X,V.Signature.Y,V.Signature.Z);Cell.StructuralEdge+=4*Influence*(1-Influence);Elevations.Add(V.Height);Slopes.Add(V.Slope);
            }
            const double Inv=1./Values.Num();Cell.HeightMeters*=Inv;Cell.SlopeDegrees*=Inv;Cell.Uplift*=Inv;Cell.Basin*=Inv;Cell.StructuralEdge*=Inv;
            Elevations.Sort();Slopes.Sort();Cell.MinimumBaseHeightMeters=Elevations[0];Cell.MaximumBaseHeightMeters=Elevations.Last();
            Cell.LowerHeightMeters=Elevations[FMath::FloorToInt(.1*(Values.Num()-1))];Cell.UpperHeightMeters=Elevations[FMath::FloorToInt(.9*(Values.Num()-1))];
            Cell.MedianSlopeDegrees=Slopes[(Values.Num()-1)/2];Cell.UpperSlopeDegrees=Slopes[FMath::FloorToInt(.9*(Values.Num()-1))];
            Cell.Scale=2*FMath::Sqrt(Cell.SolidAngle)*(Base.RadiusMeters+Base.BaseHeightMeters)/Base.RadiusMeters;
        }
    }
    struct FEligibility {bool Allowed=true,CoverageFallback=false;int32 Positive=0,Unknown=0,Tier=0;double Weight=1;FString Reason;};
    TArray<TArray<FEligibility>> Eligibility;
    auto Evaluate=[&](int32 I,int32 P,int32 Tier)
    {
        FEligibility Fit;Fit.Tier=Tier;const auto& Definition=Out->Palette[P].Suitability.Province;
        const double Coverage=Tier?Definition.Relaxations[Tier-1].MinimumCoverage:Definition.MinimumCoverage;
        Fit.Weight=Tier?Definition.Relaxations[Tier-1].PlacementWeight:1;FString Missing;
        for(const auto& Sample:Character[I])
        {FString Why;const auto Match=Definition.Match(Sample.Geology,&Why,Tier);Fit.Positive+=Match==ECLProvinceMatch::Eligible;Fit.Unknown+=Match==ECLProvinceMatch::Unknown;if(Missing.IsEmpty()&&Match!=ECLProvinceMatch::Eligible)Missing=Why;}
        double Preferred=0;
        for(const auto& Sample:Character[I])for(auto Evidence:Definition.PlacementPreferences)
        {const uint64 Bit=uint64(1)<<uint8(Evidence);if((Sample.Geology.Known&Bit)&&(Sample.Geology.Present&Bit))Preferred+=1.;}
        if(!Definition.PlacementPreferences.IsEmpty())Fit.Weight*=1+2*Preferred/(Character[I].Num()*Definition.PlacementPreferences.Num());
        Fit.Allowed=double(Fit.Positive)/Character[I].Num()+1.e-12>=Coverage;
        Fit.Reason=FString::Printf(TEXT("Geological prerequisites: %d/%d positive area samples, %d unknown; required %.0f%% coverage. %s"),Fit.Positive,Character[I].Num(),Fit.Unknown,100*Coverage,Fit.Allowed?TEXT("Compatible base geology."):*Missing);
        if(Tier)Fit.Reason+=FString::Printf(TEXT(" Gameplay fallback %d: %s; original geological requirements retained in source metadata."),Tier,*Definition.Relaxations[Tier-1].Name.ToString());
        return Fit;
    };
    if(Settings.AlgorithmVersion>=3)
    {
        Eligibility.SetNum(Out->Cells.Num());
        for(int32 I=0;I<Out->Cells.Num();++I)
        {
            Eligibility[I].SetNum(Out->Palette.Num());
            for(int32 P=0;P<Out->Palette.Num();++P)
            {
                const auto& Definition=Out->Palette[P].Suitability.Province;if(!Definition.IsActive())continue;
                auto Fit=Evaluate(I,P,0);
                if(!Fit.Allowed)for(int32 Tier=1;Tier<=Definition.Relaxations.Num();++Tier)
                {auto Relaxed=Evaluate(I,P,Tier);if(Relaxed.Allowed){Fit=MoveTemp(Relaxed);break;}}
                if(!Fit.Allowed)
                {
                    // Coverage is mandatory. Preserve strict source metadata and
                    // rank the remaining cells by partial evidence/preferences.
                    double Compatibility=0,Preferred=0;
                    for(const auto& Sample:Character[I])
                    {
                        for(auto Evidence:Definition.Requirements){const uint64 Bit=uint64(1)<<uint8(Evidence);Compatibility+=(Sample.Geology.Known&Bit)?((Sample.Geology.Present&Bit)?1.:0.):.25;}
                        for(auto Evidence:Definition.PlacementPreferences){const uint64 Bit=uint64(1)<<uint8(Evidence);Preferred+=bool((Sample.Geology.Known&Bit)&&(Sample.Geology.Present&Bit));}
                    }
                    Compatibility/=FMath::Max(1,Character[I].Num()*Definition.Requirements.Num());
                    const double Preference=Definition.PlacementPreferences.IsEmpty()?1.:1+2*Preferred/(Character[I].Num()*Definition.PlacementPreferences.Num());
                    Fit.Allowed=true;Fit.CoverageFallback=true;Fit.Tier=Definition.Relaxations.Num()+1;Fit.Weight=(.1+.9*Compatibility*Compatibility)*Preference;
                    Fit.Reason+=TEXT(" Coverage fallback: no declared tier fits this cell; closest selected geological preferences used. Strict requirements remain recorded and upstream geology is unchanged.");
                }
                Eligibility[I][P]=MoveTemp(Fit);
            }
        }
    }
    TSet<FGuid> OverrideIds;
    TArray<int32> Assigned,Locked,Targets,FreeCells,Order,Slots;
    Assigned.Init(INDEX_NONE,Out->Cells.Num());Locked.Init(0,EnabledCount);
    for(const auto& Override:Settings.Overrides)
    {
        const int32 I=Out->Cells.IndexOfByPredicate([&](const auto& C){return C.Id==Override.RegionId;});
        const int32 P=Out->Palette.IndexOfByPredicate([&](const auto& Entry){return Entry.Id==Override.LandformId;});
        if(!Override.RegionId.IsValid()||!Override.LandformId.IsValid()||OverrideIds.Contains(Override.RegionId)||I<0||P<0||P>=EnabledCount)
            return Fail(TEXT("Region override references a missing cell/enabled Landform, or repeats a cell"));
        OverrideIds.Add(Override.RegionId);Assigned[I]=P;++Locked[P];Out->Cells[I].bOverride=true;
    }
    // Equal shares are inclusion semantics, independent of Character or relative
    // suitability scores. Explicit overrides stay locked; water-fill the remaining
    // capacity fairly, with at least one cell reserved for every included source.
    Targets=Locked;int32 Reserved=0;
    for(int32 P=0;P<EnabledCount;++P){Targets[P]=FMath::Max(1,Targets[P]);Reserved+=Targets[P];Order.Add(P);}
    if(Reserved>Out->Cells.Num())return Fail(TEXT("Region overrides leave too few free cells to assign every included Landform. Clear an override or increase the region count."));
    auto SourceSeed=[&](int32 P){const auto Id=Out->Palette[P].Id;return MixRegion(Seed^MixRegion(Id.A)^MixRegion(Id.B)^MixRegion(Id.C)^MixRegion(Id.D));};
    Order.Sort([&](int32 AIndex,int32 BIndex){const uint32 ASeed=SourceSeed(AIndex),BSeed=SourceSeed(BIndex);return ASeed==BSeed?AIndex<BIndex:ASeed<BSeed;});
    while(Reserved<Out->Cells.Num())
    {int32 Best=Order[0];for(int32 P:Order)if(Targets[P]<Targets[Best])Best=P;++Targets[Best];++Reserved;}
    for(int32 P:Order)for(int32 K=Locked[P];K<Targets[P];++K)Slots.Add(P);
    for(int32 I=0;I<Assigned.Num();++I)if(Assigned[I]<0)FreeCells.Add(I);

    TArray<TArray<double>> Scores,Costs;Scores.SetNum(Out->Cells.Num());Costs.SetNum(Out->Cells.Num());
    int32 MaximumTier=1;for(int32 P=0;P<EnabledCount;++P)MaximumTier=FMath::Max(MaximumTier,Out->Palette[P].Suitability.Province.Relaxations.Num()+1);
    const double RelaxedPenalty=(Out->Cells.Num()+1.)*(MaximumTier+1);
    for(int32 I=0;I<Out->Cells.Num();++I)
    {
        auto& Cell=Out->Cells[I];if(Settings.AlgorithmVersion==1){Cell.HeightMeters=Heights[I*5];const double Span=2*Epsilon*(Base.RadiusMeters+Base.BaseHeightMeters);
        Cell.SlopeDegrees=FMath::RadiansToDegrees(FMath::Atan(FMath::Sqrt(FMath::Square((Heights[I*5+1]-Heights[I*5+2])/Span)+FMath::Square((Heights[I*5+3]-Heights[I*5+4])/Span))));}
        Scores[I].SetNumUninitialized(EnabledCount);Costs[I].SetNumUninitialized(EnabledCount);
        for(int32 P=0;P<EnabledCount;++P)
        {
            const auto& Entry=Out->Palette[P];const auto& S=Entry.Suitability;double Score=1;
            if(S.bUsePreferences)
            {
                const auto* Low=Base.Guides.FindByPredicate([&](const auto& G){return G.Name==S.LowerElevationGuide;});const auto* High=Base.Guides.FindByPredicate([&](const auto& G){return G.Name==S.UpperElevationGuide;});
                if(!Low||!High||High->HeightMeters<=Low->HeightMeters)return Fail(TEXT("Placement preferences require an ordered guide interval: ")+Entry.Name);
                const double Elevation=(Cell.HeightMeters-Low->HeightMeters)/(High->HeightMeters-Low->HeightMeters);
                if(Settings.AlgorithmVersion==1)
                {
                    Score=FMath::Max(.01,1+3*S.UpliftAffinity*Cell.Uplift+3*S.BasinAffinity*Cell.Basin+2*S.StructuralEdgeAffinity*Cell.StructuralEdge)*S.SelectionWeight;
                    Score*=FMath::Max(.001,RangeFit(Elevation,S.PreferredElevation,.12)*RangeFit(Cell.SlopeDegrees,S.PreferredSlopeDegrees,6)*RangeFit(Cell.Scale,S.PreferredScale,.15));
                }
                else
                {
                    // Rank the joint elevation/slope/structural fit over the
                    // region's area. A flat centre cannot conceal steep banks
                    // or a ridge elsewhere in the same region.
                    Score=0;
                    for(const auto& V:Character[I])
                    {
                        const double E=(V.Height-Low->HeightMeters)/(High->HeightMeters-Low->HeightMeters),Influence=FMath::Max3(V.Signature.X,V.Signature.Y,V.Signature.Z);
                        const double Affinity=FMath::Max(.01,1+3*S.UpliftAffinity*V.Signature.X+3*S.BasinAffinity*V.Signature.Y+2*S.StructuralEdgeAffinity*4*Influence*(1-Influence));
                        Score+=Affinity*RangeFit(E,S.PreferredElevation,.06)*RangeFit(V.Slope,S.PreferredSlopeDegrees,2);
                    }
                    Score=S.SelectionWeight*FMath::Max(.001,Score/Character[I].Num())*RangeFit(Cell.Scale,S.PreferredScale,.15);
                    // Protect gentle terrain from a steep minority hidden by
                    // the area average. Ridge crests can still be flat: only
                    // the preferred maximum constrains the upper decile.
                    Score*=RangeFit(Cell.UpperSlopeDegrees,FVector2D(0,S.PreferredSlopeDegrees.Y),2);
                }
            }
            int32 Tier=0;
            if(Settings.AlgorithmVersion>=3)
            {
                const auto& Fit=Eligibility[I][P];Tier=Fit.CoverageFallback?MaximumTier:Fit.Tier;Score*=Fit.Weight;
                if(Tier>0)Score*=FMath::Min(1.,double(Out->Palette[P].Suitability.Province.MaximumRelaxedRegions)/Targets[P]);
            }
            Scores[I][P]=Score;
            const double Jitter=RandomRegion(SourceSeed(P)^MixRegion(uint32(I)));
            const double Choice=Score*(.9+.2*Jitter);
            const double Preference=(30-FMath::Clamp(FMath::Loge(FMath::Max(Choice,1.e-30)),-30.,30.))/60+1.e-6*Jitter;
            // Across the complete balanced assignment: preserve as many strict
            // cells as possible, then prefer earlier relaxation tiers, then fit.
            Costs[I][P]=(Tier>0?RelaxedPenalty:0)+Tier+Preference/(Out->Cells.Num()+1.);
        }
    }
    if(!AssignBalancedCells(Costs,FreeCells,Slots,Assigned,Error))return nullptr;
    for(int32 I=0;I<Out->Cells.Num();++I)
    {
        auto& Cell=Out->Cells[I];const int32 P=Assigned[I];if(P<0)return Fail(TEXT("Every generated cell must select an included Landform"));
        Cell.PaletteIndex=P;Cell.Score=Scores[I][P];
        Cell.Reason=FString::Printf(TEXT("%s: %s, %d/%d cells. Balanced inclusion with geological fit optimized over the planet (score %.3f)."),Cell.bOverride?TEXT("Editorial assignment"):TEXT("Balanced assignment"),*Out->Palette[P].Name,Targets[P],Out->Cells.Num(),Cell.Score);
        if(Settings.AlgorithmVersion>=2)
        {
            const double Low=(Base.RadiusMeters+Cell.MinimumBaseHeightMeters)/Base.RadiusMeters,High=(Base.RadiusMeters+Cell.MaximumBaseHeightMeters)/Base.RadiusMeters;
            Cell.Reason+=FString::Printf(TEXT(" Area suitability: %d equal-area samples; base radial height %.3f–%.3f × radius, slope median %.1f°, 90th percentile %.1f°. Mean uplift %.2f, basin %.2f, edge %.2f."),Cell.CharacterSampleCount,Low,High,Cell.MedianSlopeDegrees,Cell.UpperSlopeDegrees,Cell.Uplift,Cell.Basin,Cell.StructuralEdge);
        }
        if(Settings.AlgorithmVersion>=3){Cell.ProvinceRelaxationTier=Eligibility[I][P].Tier;Cell.Reason+=TEXT(" ")+Eligibility[I][P].Reason;}
    }
    // An explicit evaluator option bypasses regional assignment for Solo.
    // It cannot be stored in, exported from, or enabled by a planet recipe.
    const FGuid Solo=FCLCompileProgress::SoloPreviewLandform();
    const int32 SoloIndex=Out->Palette.IndexOfByPredicate([&](const auto& P){return P.Id==Solo&&P.bEnabled;});
    if(Solo.IsValid()&&SoloIndex!=INDEX_NONE)for(auto& Cell:Out->Cells)
    {Cell.PaletteIndex=SoloIndex;Cell.bOverride=false;Cell.Reason=TEXT("Whole-planet Solo preview; generation settings are unchanged.");}
    Out->Transforms.SetNum(Out->Palette.Num());
    for(int32 P=0;P<Out->Palette.Num();++P)if(Out->Palette[P].Transform.IsActive())
    {
        double R=Base.RadiusMeters+Base.BaseHeightMeters;
        const auto* Lo=Base.Guides.FindByPredicate([](const auto& G){return G.Name==TEXT("SurfaceMin");});const auto* Hi=Base.Guides.FindByPredicate([](const auto& G){return G.Name==TEXT("SurfaceMax");});
        if(Lo&&Hi)R=Base.RadiusMeters+(Lo->HeightMeters+Hi->HeightMeters)*.5;
        Out->Transforms[P]=FCLCompiledTransformLandform::Compile(Out->Palette[P].Transform,Recipe.Seed,R,Out->Cells,Base.Tectonics,Error);if(!Out->Transforms[P])return nullptr;
    }
    Out->VolcanicArcs.SetNum(Out->Palette.Num());
    for(int32 P=0;P<Out->Palette.Num();++P)if(Out->Palette[P].VolcanicArc.IsActive())
    {
        double R=Base.RadiusMeters+Base.BaseHeightMeters;
        const auto* Lo=Base.Guides.FindByPredicate([](const auto& G){return G.Name==TEXT("SurfaceMin");});const auto* Hi=Base.Guides.FindByPredicate([](const auto& G){return G.Name==TEXT("SurfaceMax");});
        if(Lo&&Hi)R=Base.RadiusMeters+(Lo->HeightMeters+Hi->HeightMeters)*.5;
        Out->VolcanicArcs[P]=FCLCompiledVolcanicArcLandform::Compile(Out->Palette[P].VolcanicArc,Recipe.Seed,R,Out->Cells,Base.Tectonics,Error);if(!Out->VolcanicArcs[P])return nullptr;
    }
    for(const auto& Cell:Out->Cells)
    {
        const int32 P=Cell.PaletteIndex;
        const bool Arc=Out->VolcanicArcs.IsValidIndex(P)&&Out->VolcanicArcs[P].IsValid();
        Out->bHasProvinceDeformation|=Arc;Out->bHasPhysicalRelief|=Arc;
        const bool Transform=Out->Transforms.IsValidIndex(P)&&Out->Transforms[P].IsValid();
        Out->bHasDirectionalDeformation|=Transform;Out->bHasProvinceDeformation|=Transform;Out->bHasPhysicalRelief|=Transform;
        Out->bHasProvinceDeformation|=Out->Rifts.IsValidIndex(P)&&Out->Rifts[P].IsValid();
        Out->bHasPhysicalRelief|=(Out->Rifts.IsValidIndex(P)&&Out->Rifts[P].IsValid())||Out->Palette[P].NoiseLayers.ContainsByPredicate([](const auto& L){return L.bEnabled;});
    }
    Out->Fields.SetNum(Out->Palette.Num());
    for(int32 P=0;P<Out->Palette.Num();++P)if(Out->Cells.ContainsByPredicate([&](const auto& C){return C.PaletteIndex==P;}))
    {
        if(Settings.AlgorithmVersion==4){if(!Out->Fields[P])return Fail(Out->FieldErrors[P]);continue;}
        if(NativeInputs)
        {
            if(!NativeInputs->Fields.IsValidIndex(P)||!NativeInputs->Fields[P])return Fail(Out->Palette[P].Name+TEXT(": ")+(NativeInputs->FieldErrors.IsValidIndex(P)?NativeInputs->FieldErrors[P]:TEXT("Native palette snapshot is missing")));
            Out->Fields[P]=NativeInputs->Fields[P];continue;
        }
        auto Child=Recipe;Child.SchemaVersion=8;Child.RegionGeneration=FCLRegionGenerationSettings();Child.BaseStructure.Reset();if(Settings.AlgorithmVersion==4){Child.Rock=FCLRockLayer();Child.Watershed=FCLWatershedSettings();Child.Caves=FCLCaveSettings();Child.ElevationDistribution=FCLSurfaceDistributionSettings();Child.Volumes=FCLVolumeSettings();}Child.Tectonics=FCLTectonicPreset();Child.Continents=FCLContinentalLayer();Child.TerrainRegions.Reset();Child.NoiseLayers=Out->Palette[P].NoiseLayers;if(!Out->Palette[P].Rift.IsActive()&&!Out->Palette[P].Transform.IsActive()&&!Out->Palette[P].VolcanicArc.IsActive()&&Out->Palette[P].Seed)for(auto& L:Child.NoiseLayers)L.Seed=int32(uint32(L.Seed)^MixRegion(uint32(Out->Palette[P].Seed)));if(Out->Palette[P].Rift.AlgorithmVersion>=2)for(auto& L:Child.NoiseLayers)L.Seed=int32(uint32(L.Seed)^MixRegion(uint32(Out->Palette[P].Rift.Seed)));if(Out->Palette[P].Transform.IsActive())for(auto& L:Child.NoiseLayers)L.Seed=int32(uint32(L.Seed)^MixRegion(uint32(Out->Palette[P].Transform.Seed)));if(Out->Palette[P].VolcanicArc.IsActive())for(auto& L:Child.NoiseLayers)L.Seed=int32(uint32(L.Seed)^MixRegion(uint32(Out->Palette[P].VolcanicArc.Seed)));Child.AlgorithmVersion=Out->Palette[P].AlgorithmVersion;Child.SurfaceStack=Out->Palette[P].SurfaceStack;if(Child.SurfaceStack.IsActive())Child.SchemaVersion=13;Child.TerrainTypeId=Out->Palette[P].Id;Child.TerrainTypeName=Out->Palette[P].Name;
        Out->Fields[P]=FCLPlanetCompiler::Compile(Child,Error,Library);if(!Out->Fields[P])return Fail(Out->Palette[P].Name+TEXT(": ")+Error);
    }
    // A modest diagnostic contour atlas, independent of terrain mesh LOD. Shared
    // queries are globally spherical, including every cube-face boundary and pole.
    constexpr int32 Grid=48;
    auto Edge=[&](FVector A,FVector B,int32 IA,int32 IB)
    {for(int32 K=0;K<9;++K){const FVector M=(A+B).GetSafeNormal();if(Out->Owner(M)==IA)A=M;else B=M;}return (A+B).GetSafeNormal();};
    auto Junction=[&](FVector D,TArray<int32> Owners)
    {
        Owners.Sort();if(Owners.Num()!=3)return;
        const int32 A=Owners[0],B=Owners[1],C=Owners[2];
        if(Out->Cells[A].PaletteIndex==Out->Cells[B].PaletteIndex||Out->Cells[A].PaletteIndex==Out->Cells[C].PaletteIndex||Out->Cells[B].PaletteIndex==Out->Cells[C].PaletteIndex)return;
        auto F=[&](FVector P){const FVector S=Out->Signature(P);return FVector2D(Out->Cost(A,P,S)-Out->Cost(B,P,S),Out->Cost(A,P,S)-Out->Cost(C,P,S));};
        for(int32 Iter=0;Iter<20;++Iter)
        {
            const FVector2D V=F(D);if(V.Size()<1.e-11)break;
            const FVector X=FVector::CrossProduct(D,FMath::Abs(D.Z)<.8?FVector::ZAxisVector:FVector::XAxisVector).GetSafeNormal(),Y=FVector::CrossProduct(D,X);
            constexpr double E=.00001;const FVector2D DX=(F((D+X*E).GetSafeNormal())-F((D-X*E).GetSafeNormal()))/(2*E),DY=(F((D+Y*E).GetSafeNormal())-F((D-Y*E).GetSafeNormal()))/(2*E);
            const double Det=DX.X*DY.Y-DX.Y*DY.X;if(FMath::Abs(Det)<1.e-12)return;
            FVector2D Step((V.X*DY.Y-V.Y*DY.X)/Det,(DX.X*V.Y-DX.Y*V.X)/Det);if(Step.Size()>.04)Step*=.04/Step.Size();D=(D-X*Step.X-Y*Step.Y).GetSafeNormal();
        }
        if(F(D).Size()>1.e-8)return;const FVector S=Out->Signature(D);const double Cost=Out->Cost(A,D,S);
        for(int32 I=0;I<Out->Cells.Num();++I)if(Out->Cost(I,D,S)<Cost-1.e-8)return;
        if(Out->Junctions.ContainsByPredicate([&](const auto& J){return (J.Direction-D).Size()<.001;}))return;
        Out->Junctions.Add({D,A,B,C});
    };
    for(int32 Face=0;Face<6;++Face)for(int32 Y=0;Y<Grid;++Y)for(int32 X=0;X<Grid;++X)
    {
        if(X==0&&!FCLCompileProgress::Check(Error))return nullptr;
        FVector D[4]={FCLSphericalPatches::Direction(Face,FVector2D(2.*X/Grid-1,2.*Y/Grid-1)),FCLSphericalPatches::Direction(Face,FVector2D(2.*(X+1)/Grid-1,2.*Y/Grid-1)),FCLSphericalPatches::Direction(Face,FVector2D(2.*(X+1)/Grid-1,2.*(Y+1)/Grid-1)),FCLSphericalPatches::Direction(Face,FVector2D(2.*X/Grid-1,2.*(Y+1)/Grid-1))};
        int32 O[4];for(int32 K=0;K<4;++K)O[K]=Out->Owner(D[K]);TArray<FVector,TInlineAllocator<4>> Cross;TArray<TPair<int32,int32>,TInlineAllocator<4>> Pair;
        for(int32 K=0;K<4;++K){const int32 J=(K+1)%4;if(O[K]!=O[J]){Cross.Add(Edge(D[K],D[J],O[K],O[J]));Pair.Emplace(O[K],O[J]);}}
        if(Cross.Num()==2)Out->Boundaries.Add({Cross[0],Cross[1],Pair[0].Key,Pair[0].Value});
        else if(Cross.Num()>2){FVector Centre=FVector::ZeroVector;for(const auto& P:Cross)Centre+=P;Centre.Normalize();for(int32 K=0;K<Cross.Num();++K)Out->Boundaries.Add({Cross[K],Centre,Pair[K].Key,Pair[K].Value});TArray<int32> Owners;for(int32 K=0;K<4;++K)Owners.AddUnique(O[K]);Junction(Centre,Owners);}
    }
    return Out;
}

bool UCLPlanetRecipeAsset::AddPaletteSource(UCLTerrainTypeAsset* Source,FString& Error)
{
    if(!Source||Source->TerrainType.Coordinates!=ECLTerrainCoordinates::SphericalSurface||!Source->TerrainType.Validate(Error)){if(Error.IsEmpty())Error=TEXT("Select a valid spherical-surface Landform");return false;}
    auto Candidate=Recipe;Candidate.SchemaVersion=FMath::Max(12,Candidate.SchemaVersion);if(Candidate.RegionGeneration.Palette.IsEmpty()){Candidate.RegionGeneration.bEnabled=true;Candidate.RegionGeneration.AlgorithmVersion=2;}if(Source->TerrainType.PlacementSuitability.Province.IsActive())Candidate.RegionGeneration.AlgorithmVersion=FMath::Max(Candidate.RegionGeneration.AlgorithmVersion,(Source->TerrainType.Rift.IsActive()||Source->TerrainType.Transform.IsActive()||Source->TerrainType.VolcanicArc.IsActive())?4:3);auto* Entry=Candidate.RegionGeneration.Palette.FindByPredicate([&](const auto& P){return P.Id==Source->TerrainType.Id;});
    if(!Entry){if(Candidate.RegionGeneration.Palette.Num()>=32){Error=TEXT("The palette supports at most 32 Landforms");return false;}Entry=&Candidate.RegionGeneration.Palette.AddDefaulted_GetRef();}
    Entry->Id=Source->TerrainType.Id;Entry->Name=Source->TerrainType.Name;Entry->Description=Source->TerrainType.Description;Entry->AlgorithmVersion=Source->TerrainType.AlgorithmVersion;Entry->Suitability=Source->TerrainType.PlacementSuitability;Entry->Rift=Source->TerrainType.Rift;Entry->Transform=Source->TerrainType.Transform;Entry->VolcanicArc=Source->TerrainType.VolcanicArc;Entry->NoiseLayers=Source->TerrainType.NoiseLayers;Entry->SurfaceStack=Source->TerrainType.SurfaceStack;Entry->Deposits=Source->TerrainType.Deposits;
    if(!Entry->Deposits.IsEmpty()){Candidate.SchemaVersion=FMath::Max(13,Candidate.SchemaVersion);if(!Candidate.SurfaceStack.IsActive())Candidate.SurfaceStack=FCLSurfaceStack::MakeDefault();}
    if(Entry->SurfaceStack.IsActive()){Candidate.SchemaVersion=FMath::Max(13,Candidate.SchemaVersion);if(!Candidate.SurfaceStack.IsActive()){Candidate.SurfaceStack.DefaultSurfaceId=Entry->SurfaceStack.DefaultSurfaceId;Candidate.SurfaceStack.DefaultSurfaceRevision=Entry->SurfaceStack.DefaultSurfaceRevision;}}
    if(!FCLPlanetCompiler::Compile(Candidate,Error))return false;
    Modify();Recipe=MoveTemp(Candidate);PaletteSources.Add(Source->TerrainType.Id,Source);
#if WITH_EDITOR
    PostEditChange();MarkPackageDirty();
#endif
    return true;
}
bool UCLPlanetRecipeAsset::RefreshPaletteSource(FGuid Id,FString& Error)
{const auto* Source=PaletteSources.Find(Id);if(!Source||!Source->Get()){Error=TEXT("No authoring source is linked to this palette entry");return false;}return AddPaletteSource(Source->Get(),Error);}
