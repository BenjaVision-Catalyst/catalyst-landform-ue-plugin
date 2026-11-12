#include "Model/CLCompileProgress.h"
#include "Model/CLRegionGeneration.h"
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
            for(int32 I=0;I<H.Num();++I)Heights[Indices[P][I]]+=((H[I]-NeutralHeight)*(Rifts.IsValidIndex(P)&&Rifts[P]?Rifts[P]->SurfaceDetailStrength():1)+(Rifts.IsValidIndex(P)&&Rifts[P]?Rifts[P]->SampleMeters(Queries[P][I]):0))*Weight[P][I]*Strength;
        }
    }
}
bool FCLCompiledRegionGeneration::HasPhysicalRelief()const{return bHasPhysicalRelief;}
double FCLCompiledRegionGeneration::MaximumProvinceDisplacementMeters()const
{double Max=0;for(const auto& F:Rifts)if(F)Max=FMath::Max(Max,F->MaximumDisplacementMeters());return Max*Strength;}
double FCLCompiledRegionGeneration::ProvinceDeformationMeters(const FVector& D)const
{
    if(!bHasProvinceDeformation||Strength==0)return 0;double W[32];Weights(D,nullptr,W);double V=0;
    for(int32 P=0;P<Rifts.Num();++P)if(W[P]>0&&Rifts[P])V+=W[P]*Rifts[P]->SampleMeters(D);return V*Strength;
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
        if(Entry.Suitability.Province.IsActive()&&Settings.AlgorithmVersion<3)return Fail(TEXT("Province Landforms require geological region algorithm 3"));
        if(Entry.Suitability.Province.bPlaceholder&&(!Entry.NoiseLayers.IsEmpty()||Entry.SurfaceStack.IsActive()))return Fail(TEXT("Province placeholders cannot replace base relief or material"));
        if(!Entry.Rift.Validate(Error)||!Entry.Rift.ValidateNoise(Entry.NoiseLayers,Error))return false;
        if(Entry.Rift.IsActive()&&(Settings.AlgorithmVersion!=4||Entry.Suitability.Province.Type!=ECLProvinceType::ContinentalRift||Entry.Suitability.Province.bPlaceholder||Entry.SurfaceStack.IsActive()))return Fail(TEXT("Physical rifts require geology-first region algorithm 4 and a Continental Rift source without replacement noise/materials"));
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
    auto Out=MakeShared<FCLCompiledRegionGeneration>();Out->Guidance=Settings.StructureGuidance;Out->Band=(4*UE_DOUBLE_PI/Settings.RegionCount)*Settings.BlendFraction*.5;Out->Strength=Settings.ReliefStrength;Out->NeutralHeight=Base.BaseHeightMeters;Out->Structure=Base.BaseStructure;
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
        auto Child=Recipe;Child.SchemaVersion=8;Child.RegionGeneration=FCLRegionGenerationSettings();Child.BaseStructure.Reset();if(Settings.AlgorithmVersion==4){Child.Rock=FCLRockLayer();Child.Watershed=FCLWatershedSettings();Child.Caves=FCLCaveSettings();Child.ElevationDistribution=FCLSurfaceDistributionSettings();Child.Volumes=FCLVolumeSettings();}Child.Tectonics=FCLTectonicPreset();Child.Continents=FCLContinentalLayer();Child.TerrainRegions.Reset();Child.NoiseLayers=Out->Palette[P].NoiseLayers;if(Out->Palette[P].Rift.AlgorithmVersion==2)for(auto& L:Child.NoiseLayers)L.Seed=int32(uint32(L.Seed)^MixRegion(uint32(Out->Palette[P].Rift.Seed)));Child.AlgorithmVersion=Out->Palette[P].AlgorithmVersion;Child.SurfaceStack=Out->Palette[P].SurfaceStack;if(Child.SurfaceStack.IsActive())Child.SchemaVersion=13;Child.TerrainTypeId=Out->Palette[P].Id;Child.TerrainTypeName=Out->Palette[P].Name;
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
    const int32 PoolCount=Settings.RegionCount*16;TArray<FVector> Pool;TArray<double> Density,Distance;
    for(int32 I=0;I<PoolCount;++I)
    {
        const double Y=1-2*(I+.5)/PoolCount,R=FMath::Sqrt(1-Y*Y),Angle=I*2.399963229728653;
        const FVector D=Rotation.RotateVector(FVector(R*FMath::Sin(Angle),Y,R*FMath::Cos(Angle)));Pool.Add(D);
        const FVector S=Out->Signature(D);Density.Add(1+Settings.StructureGuidance*3*(S.X+S.Y+S.Z));Distance.Add(MAX_dbl);
    }
    // Weighted farthest-point sites retain whole-sphere coverage while placing more
    // regions around broad uplift/basins/scars. Site identity is independent of palette order.
    int32 Next=int32(MixRegion(Seed+3)%uint32(PoolCount));
    for(int32 I=0;I<Settings.RegionCount;++I)
    {
        if(!FCLCompileProgress::Check(Error))return nullptr;
        FCLGeneratedRegionCell Cell;Cell.Id=CellId(Settings.Seed,I);Cell.Centre=Pool[Next];Cell.StructureSignature=Out->Signature(Cell.Centre);Cell.Uplift=Cell.StructureSignature.X;Cell.Basin=Cell.StructureSignature.Y;
        const double Influence=FMath::Max3(Cell.StructureSignature.X,Cell.StructureSignature.Y,Cell.StructureSignature.Z);Cell.StructuralEdge=4*Influence*(1-Influence);
        FVector Axis=FVector::VectorPlaneProject(FVector::XAxisVector,Cell.Centre).GetSafeNormal();if(Axis.IsNearlyZero())Axis=FVector::YAxisVector;
        double Strongest=0;
        for(const auto& F:Out->Structure){const double W=F.Weight(Cell.Centre);if(W>Strongest){Strongest=W;Axis=FVector::VectorPlaneProject(F.Orientation.RotateVector(F.HalfExtentRadians.X>=F.HalfExtentRadians.Y?FVector::XAxisVector:FVector::YAxisVector),Cell.Centre).GetSafeNormal();}}
        Cell.Frame=FRotationMatrix::MakeFromXZ(Axis,Cell.Centre).ToQuat();Cell.Stretch=1+Settings.StructureGuidance*1.25*Strongest;
        Cell.Scale=2*FMath::Sqrt(4*UE_DOUBLE_PI/Settings.RegionCount)*(Base.RadiusMeters+Base.BaseHeightMeters)/Base.RadiusMeters;Out->Cells.Add(Cell);
        double Best=-1;int32 Winner=0;
        for(int32 J=0;J<PoolCount;++J){Distance[J]=FMath::Min(Distance[J],(Pool[J]-Cell.Centre).SizeSquared());const double Value=Distance[J]*Density[J];if(Value>Best){Best=Value;Winner=J;}}Next=Winner;
    }
    const double Epsilon=FMath::Clamp(FMath::Sqrt(4*UE_DOUBLE_PI/Settings.RegionCount)*.06,.003,.04);TArray<FVector> Samples;
    for(const auto& Cell:Out->Cells){const FVector X=Cell.Frame.GetAxisX(),Y=Cell.Frame.GetAxisY(),N=Cell.Centre;Samples.Append({N,(N+X*Epsilon).GetSafeNormal(),(N-X*Epsilon).GetSafeNormal(),(N+Y*Epsilon).GetSafeNormal(),(N-Y*Epsilon).GetSafeNormal()});}
    TArray<double> Heights;Base.SampleHeightsMeters(Samples,Heights);
    struct FBaseSample {double Height,Slope;FVector Signature;FCLProvinceGeology Geology;};
    TArray<TArray<FBaseSample>> Character;Character.SetNum(Out->Cells.Num());
    if(Settings.AlgorithmVersion>=2)
    {
        // Equal-area sphere samples follow actual cell ownership, not a disc
        // approximation around the site. Fixed 128 samples/cell on average;
        // no surface-resolution or client mesh dependency. Bound field scratch.
        const int32 Count=Settings.RegionCount*128;
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
    struct FEligibility {bool Allowed=true;int32 Positive=0,Unknown=0,Tier=0;double Weight=1;FString Reason;};
    TArray<TArray<FEligibility>> Eligibility;
    auto Evaluate=[&](int32 I,int32 P,int32 Tier)
    {
        FEligibility Fit;Fit.Tier=Tier;const auto& Definition=Out->Palette[P].Suitability.Province;
        const double Coverage=Tier?Definition.Relaxations[Tier-1].MinimumCoverage:Definition.MinimumCoverage;
        Fit.Weight=Tier?Definition.Relaxations[Tier-1].PlacementWeight:1;FString Missing;
        for(const auto& Sample:Character[I])
        {FString Why;const auto Match=Definition.Match(Sample.Geology,&Why,Tier);Fit.Positive+=Match==ECLProvinceMatch::Eligible;Fit.Unknown+=Match==ECLProvinceMatch::Unknown;if(Missing.IsEmpty()&&Match!=ECLProvinceMatch::Eligible)Missing=Why;}
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
                Eligibility[I][P]=Evaluate(I,P,0);
            }
        }
    }
    // Relax a source only when the entire planet offers no strict candidate.
    // A small metadata-owned fallback budget prevents an unsupported province
    // from filling every cell merely because it was included in the palette.
    if(Settings.AlgorithmVersion>=3)for(int32 P=0;P<BasePalette;++P)
    {
        const auto& Definition=Out->Palette[P].Suitability.Province;
        if(Definition.AlgorithmVersion!=2||Definition.Relaxations.IsEmpty())continue;
        bool Strict=false;for(int32 I=0;I<Eligibility.Num();++I)Strict|=Eligibility[I][P].Allowed;
        if(Strict)continue;
        for(int32 Tier=1;Tier<=Definition.Relaxations.Num();++Tier)
        {
            TArray<FEligibility> Fits;TArray<int32> Candidates;
            for(int32 I=0;I<Eligibility.Num();++I){Fits.Add(Evaluate(I,P,Tier));if(Fits.Last().Allowed)Candidates.Add(I);}
            if(Candidates.IsEmpty())continue;
            auto Rank=[&](int32 I){return double(Fits[I].Positive)/Character[I].Num()+.001*RandomRegion(Seed^uint32(I)^GetTypeHash(Out->Palette[P].Id));};
            Candidates.Sort([&](int32 A,int32 B){return Rank(A)>Rank(B);});
            for(int32 I:Candidates)Fits[I].Allowed=false;
            for(int32 K=0;K<FMath::Min(Definition.MaximumRelaxedRegions,Candidates.Num());++K)Fits[Candidates[K]].Allowed=true;
            for(int32 I=0;I<Eligibility.Num();++I)Eligibility[I][P]=MoveTemp(Fits[I]);
            break;
        }
    }
    TSet<FGuid> OverrideIds;
    for(const auto& Override:Settings.Overrides)
    {
        if(!Override.RegionId.IsValid()||OverrideIds.Contains(Override.RegionId)||!Out->Cells.ContainsByPredicate([&](const auto& C){return C.Id==Override.RegionId;})||!Out->Palette.ContainsByPredicate([&](const auto& P){return P.Id==Override.LandformId;}))return Fail(TEXT("Region override references a missing cell/enabled Landform, or repeats a cell"));OverrideIds.Add(Override.RegionId);
    }
    for(int32 I=0;I<Out->Cells.Num();++I)
    {
        auto& Cell=Out->Cells[I];if(Settings.AlgorithmVersion==1){Cell.HeightMeters=Heights[I*5];const double Span=2*Epsilon*(Base.RadiusMeters+Base.BaseHeightMeters);
        Cell.SlopeDegrees=FMath::RadiansToDegrees(FMath::Atan(FMath::Sqrt(FMath::Square((Heights[I*5+1]-Heights[I*5+2])/Span)+FMath::Square((Heights[I*5+3]-Heights[I*5+4])/Span))));}
        if(Out->Palette.Num()==1&&Settings.AlgorithmVersion<3){Cell.PaletteIndex=0;Cell.Score=1;Cell.Reason=TEXT("Only selected Landform; used in every region. Placement preferences do not exclude regions.");}
        else
        {
            double Best=-1;
            for(int32 P=0;P<Out->Palette.Num();++P)
            {
                if(P==BasePalette||(Settings.AlgorithmVersion>=3&&!Eligibility[I][P].Allowed))continue;
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
                if(Settings.AlgorithmVersion>=3)Score*=Eligibility[I][P].Weight;
                const double Choice=Score*(.9+.2*RandomRegion(Seed^MixRegion(uint32(I))^MixRegion(Entry.Id.A)^MixRegion(Entry.Id.B)^MixRegion(Entry.Id.C)^MixRegion(Entry.Id.D)));
                if(Choice>Best){Best=Choice;Cell.PaletteIndex=P;Cell.Score=Score;}
            }
            if(Settings.AlgorithmVersion>=3&&Cell.PaletteIndex<0){Cell.PaletteIndex=BasePalette;Cell.Score=0;}
            Cell.Reason=FString::Printf(TEXT("Best seeded suitability match (score %.3f). Uplift %.2f, basin %.2f, structural edge %.2f; base slope %.1f°, region scale %.2f × radius. %s"),Cell.Score,Cell.Uplift,Cell.Basin,Cell.StructuralEdge,Cell.SlopeDegrees,Cell.Scale,Out->Palette[Cell.PaletteIndex].Suitability.bUsePreferences?TEXT("Named elevation guides and placement preferences applied."):TEXT("Neutral placement preferences."));
        }
        if(Settings.AlgorithmVersion>=3&&Cell.PaletteIndex<0){Cell.PaletteIndex=BasePalette;Cell.Score=0;}
        if(Settings.AlgorithmVersion>=2)
        {
            const double Low=(Base.RadiusMeters+Cell.MinimumBaseHeightMeters)/Base.RadiusMeters,High=(Base.RadiusMeters+Cell.MaximumBaseHeightMeters)/Base.RadiusMeters;
            Cell.Reason=FString::Printf(TEXT("Area suitability: %s (score %.3f), %d equal-area samples. Sampled base radial height %.3f–%.3f × radius; slope median %.1f°, 90th percentile %.1f°. Mean uplift %.2f, basin %.2f, edge %.2f. %s"),*Out->Palette[Cell.PaletteIndex].Name,Cell.Score,Cell.CharacterSampleCount,Low,High,Cell.MedianSlopeDegrees,Cell.UpperSlopeDegrees,Cell.Uplift,Cell.Basin,Cell.StructuralEdge,Out->Palette.Num()==1?TEXT("Only selected source fills every region."):TEXT("Joint elevation, slope and structural fit averaged across the cell."));
        }
        if(Settings.AlgorithmVersion>=3)
        {
            if(Cell.PaletteIndex==BasePalette)
            {
                Cell.PaletteIndex=BasePalette;Cell.Score=0;Cell.Reason=TEXT("Base geology retained: no selected Landform meets its geological prerequisites.");
                for(int32 P=0;P<FMath::Min(4,BasePalette);++P)Cell.Reason+=TEXT(" ")+Out->Palette[P].Name+TEXT(": ")+Eligibility[I][P].Reason;
            }
            else {Cell.ProvinceRelaxationTier=Eligibility[I][Cell.PaletteIndex].Tier;Cell.Reason+=TEXT(" ")+Eligibility[I][Cell.PaletteIndex].Reason;}
        }
        if(const auto* Override=Settings.Overrides.FindByPredicate([&](const auto& O){return O.RegionId==Cell.Id;}))
        {const int32 Assigned=Out->Palette.IndexOfByPredicate([&](const auto& P){return P.Id==Override->LandformId;});
            if(Settings.AlgorithmVersion>=3&&!Eligibility[I][Assigned].Allowed&&Out->Palette[Assigned].Suitability.Province.AlgorithmVersion==2)
                for(int32 Tier=0;Tier<=Out->Palette[Assigned].Suitability.Province.Relaxations.Num();++Tier){auto Fit=Evaluate(I,Assigned,Tier);if(Fit.Allowed){Eligibility[I][Assigned]=MoveTemp(Fit);break;}}
            if(Settings.AlgorithmVersion>=3&&!Eligibility[I][Assigned].Allowed)return Fail(TEXT("Editorial province assignment is incompatible or lacks base geology: ")+Out->Palette[Assigned].Name+TEXT(". ")+Eligibility[I][Assigned].Reason);
            Cell.PaletteIndex=Assigned;Cell.ProvinceRelaxationTier=Settings.AlgorithmVersion>=3?Eligibility[I][Assigned].Tier:0;Cell.bOverride=true;Cell.Reason=FString::Printf(TEXT("Editorial assignment: %s. Automatic evaluation: %s"),*Out->Palette[Cell.PaletteIndex].Name,*Cell.Reason);if(Settings.AlgorithmVersion>=3)Cell.Reason+=TEXT(" ")+Eligibility[I][Assigned].Reason;}
    }
    for(const auto& Cell:Out->Cells)
    {
        const int32 P=Cell.PaletteIndex;
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
        auto Child=Recipe;Child.SchemaVersion=8;Child.RegionGeneration=FCLRegionGenerationSettings();Child.BaseStructure.Reset();if(Settings.AlgorithmVersion==4){Child.Rock=FCLRockLayer();Child.Watershed=FCLWatershedSettings();Child.Caves=FCLCaveSettings();Child.ElevationDistribution=FCLSurfaceDistributionSettings();Child.Volumes=FCLVolumeSettings();}Child.Tectonics=FCLTectonicPreset();Child.Continents=FCLContinentalLayer();Child.TerrainRegions.Reset();Child.NoiseLayers=Out->Palette[P].NoiseLayers;if(Out->Palette[P].Rift.AlgorithmVersion==2)for(auto& L:Child.NoiseLayers)L.Seed=int32(uint32(L.Seed)^MixRegion(uint32(Out->Palette[P].Rift.Seed)));Child.AlgorithmVersion=Out->Palette[P].AlgorithmVersion;Child.SurfaceStack=Out->Palette[P].SurfaceStack;if(Child.SurfaceStack.IsActive())Child.SchemaVersion=13;Child.TerrainTypeId=Out->Palette[P].Id;Child.TerrainTypeName=Out->Palette[P].Name;
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
    auto Candidate=Recipe;Candidate.SchemaVersion=FMath::Max(12,Candidate.SchemaVersion);if(Candidate.RegionGeneration.Palette.IsEmpty()){Candidate.RegionGeneration.bEnabled=true;Candidate.RegionGeneration.AlgorithmVersion=2;}if(Source->TerrainType.PlacementSuitability.Province.IsActive())Candidate.RegionGeneration.AlgorithmVersion=FMath::Max(Candidate.RegionGeneration.AlgorithmVersion,Source->TerrainType.Rift.IsActive()?4:3);auto* Entry=Candidate.RegionGeneration.Palette.FindByPredicate([&](const auto& P){return P.Id==Source->TerrainType.Id;});
    if(!Entry){if(Candidate.RegionGeneration.Palette.Num()>=32){Error=TEXT("The palette supports at most 32 Landforms");return false;}Entry=&Candidate.RegionGeneration.Palette.AddDefaulted_GetRef();}
    Entry->Id=Source->TerrainType.Id;Entry->Name=Source->TerrainType.Name;Entry->AlgorithmVersion=Source->TerrainType.AlgorithmVersion;Entry->Suitability=Source->TerrainType.PlacementSuitability;Entry->Rift=Source->TerrainType.Rift;Entry->NoiseLayers=Source->TerrainType.NoiseLayers;Entry->SurfaceStack=Source->TerrainType.SurfaceStack;
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
