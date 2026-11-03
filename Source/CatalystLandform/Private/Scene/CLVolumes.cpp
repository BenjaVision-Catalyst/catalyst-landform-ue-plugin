#include "Scene/CLVolumes.h"
#include "Surface/CLSurfaceCompiler.h"

double FCLCompiledVolumes::FOperation::Field(const FVector3d& Position) const
{
    const FVector3d P=Transform.InverseTransformPosition(Position);double D=0;
    switch(Source.Primitive)
    {
    case ECLSolidPrimitive::Sphere:D=P.Length()-Source.RadiusMeters;break;
    case ECLSolidPrimitive::Capsule:
        D=(P-FVector3d(0,0,FMath::Clamp(P.Z,-Source.HalfLengthMeters,Source.HalfLengthMeters))).Length()-Source.RadiusMeters;break;
    case ECLSolidPrimitive::Box:
    {const FVector3d Q=P.GetAbs()-Source.HalfExtentsMeters;D=Q.ComponentMax(FVector3d::ZeroVector).Length()+FMath::Min(0.,Q.GetMax());break;}
    }
    // Conservative scalar under nonuniform transforms; correct zero set, not
    // an assertion of Euclidean distance for the composed scene.
    return D*MinimumScale;
}

TSharedPtr<const FCLCompiledVolumes> FCLCompiledVolumes::Compile(const FCLVolumeSettings& S,double Radius,
    const FCLCompiledSurfaceLibrary* Surfaces,FString& Error)
{
    Error.Reset();auto Fail=[&](const TCHAR* Text)->TSharedPtr<const FCLCompiledVolumes>{Error=Text;return nullptr;};
    if(S.AlgorithmVersion!=1||S.HeightPolicy!=ECLVolumeHeightPolicy::BaseTerrainOnly||S.Operations.Num()>64||S.GeologicalUnits.Num()>16||!FMath::IsFinite(Radius)||Radius<=0)
        return Fail(TEXT("Volume profile requires algorithm 1, base-terrain-only height policy, at most 64 operations and 16 geological units"));
    auto Out=MakeShared<FCLCompiledVolumes>();TSet<FGuid> Ids;
    for(const auto& V:S.Operations)
    {
        if(!V.Id.IsValid()||Ids.Contains(V.Id)||V.Name.IsNone()||V.AlgorithmVersion!=1||uint8(V.Primitive)>uint8(ECLSolidPrimitive::Box)||uint8(V.Composition)>uint8(ECLSolidComposition::Subtract)||
           V.CenterMeters.ContainsNaN()||V.Rotation.ContainsNaN()||V.Scale.ContainsNaN()||V.Scale.GetMin()<.01||V.Scale.GetMax()>100||
           !FMath::IsFinite(V.RadiusMeters)||V.RadiusMeters<.01||!FMath::IsFinite(V.HalfLengthMeters)||V.HalfLengthMeters<0||
           V.HalfExtentsMeters.ContainsNaN()||V.HalfExtentsMeters.GetMin()<.01||!FMath::IsFinite(V.SmoothnessMeters)||V.SmoothnessMeters<0||V.SmoothnessMeters>Radius*.1)
            return Fail(TEXT("Invalid volume operation identity, version, primitive, transform or dimensions"));
        Ids.Add(V.Id);if(!V.bEnabled)continue;
        FOperation O;O.Source=V;O.Transform=FTransform3d(V.Rotation.Quaternion(),V.CenterMeters,V.Scale);O.MinimumScale=V.Scale.GetMin();
        FVector3d Extent=V.Primitive==ECLSolidPrimitive::Box?V.HalfExtentsMeters:FVector3d(V.RadiusMeters);
        if(V.Primitive==ECLSolidPrimitive::Capsule)Extent.Z+=V.HalfLengthMeters;
        const FBox3d Solid=FBox3d(-Extent,Extent).TransformBy(O.Transform.ToMatrixWithScale());
        const double Expansion=V.SmoothnessMeters*V.Scale.GetMax()/O.MinimumScale;
        O.Bounds=Solid.ExpandBy(Expansion+1.);
        // Certify additions against the sky without deforming them. A cutter
        // may extend beyond the planet: subtraction cannot create exterior rock.
        const FBox3d Supported=Solid.ExpandBy(Expansion);
        for(int32 I=0;I<8;++I)
        {
            const FVector3d Corner(I&1?Supported.Max.X:Supported.Min.X,I&2?Supported.Max.Y:Supported.Min.Y,I&4?Supported.Max.Z:Supported.Min.Z);
            if(!FMath::IsFinite(Corner.SizeSquared())||Corner.Length()>Radius*10||
               (V.Composition==ECLSolidComposition::Union&&Corner.Length()>Radius))
                return Fail(TEXT("Solid additions must fit inside the sky; all operation support must fit the bounded ten-radius domain"));
        }
        if(!V.SurfaceId.IsNone())
        {O.SurfaceIndex=Surfaces?Surfaces->Find(V.SurfaceId,V.SurfaceRevision):INDEX_NONE;if(O.SurfaceIndex==INDEX_NONE)return Fail(TEXT("Volume contact surface is missing from the client library"));}
        Out->Operations.Add(MoveTemp(O));
    }
    for(const auto& U:S.GeologicalUnits)
    {
        if(U.Name.IsNone()||U.RadiusInterval.ContainsNaN()||U.RadiusInterval.X<0||U.RadiusInterval.Y>1||U.RadiusInterval.X>=U.RadiusInterval.Y||!FMath::IsFinite(U.FeatherMeters)||U.FeatherMeters<0)
            return Fail(TEXT("Geological units require ordered normalized radial intervals and finite feather widths"));
        const int32 Material=Surfaces?Surfaces->Find(U.SurfaceId,U.SurfaceRevision):INDEX_NONE;
        if(Material==INDEX_NONE)return Fail(TEXT("Geological unit surface is missing from the client library"));
        Out->Units.Add({U.RadiusInterval.X*Radius,U.RadiusInterval.Y*Radius,U.FeatherMeters,Material});
    }
    if(!Out->Operations.IsEmpty()){TArray<int32> Items;for(int32 I=0;I<Out->Operations.Num();++I)Items.Add(I);Out->BuildIndex(MoveTemp(Items));}
    return Out;
}

int32 FCLCompiledVolumes::BuildIndex(TArray<int32> Items)
{
    FNode Node;Node.Bounds=FBox3d(ForceInit);for(int32 I:Items){Node.Bounds+=Operations[I].Bounds;Node.Mask|=uint64(1)<<I;}
    const int32 N=Index.Add(Node);if(Items.Num()<=4)return N;
    const FVector3d Size=Node.Bounds.GetSize();const int32 Axis=Size.X>Size.Y?(Size.X>Size.Z?0:2):(Size.Y>Size.Z?1:2);
    Items.Sort([&](int32 A,int32 B){return Operations[A].Bounds.GetCenter()[Axis]<Operations[B].Bounds.GetCenter()[Axis];});
    TArray<int32> Left,Right;for(int32 I=0;I<Items.Num();++I)(I<Items.Num()/2?Left:Right).Add(Items[I]);
    const int32 L=BuildIndex(MoveTemp(Left)),R=BuildIndex(MoveTemp(Right));Index[N].Left=L;Index[N].Right=R;return N;
}
uint64 FCLCompiledVolumes::Candidates(const FVector3d& P) const
{
    if(Index.IsEmpty())return 0;uint64 Mask=0;TArray<int32,TInlineAllocator<16>> Stack;Stack.Add(0);
    while(!Stack.IsEmpty())
    {
        const auto& N=Index[Stack.Pop(EAllowShrinking::No)];if(!N.Bounds.IsInsideOrOn(P))continue;
        if(N.Left==INDEX_NONE)Mask|=N.Mask;else{Stack.Add(N.Left);Stack.Add(N.Right);}
    }
    return Mask;
}
double FCLCompiledVolumes::Compose(const FVector3d& P,double A,FCLSurfaceWeights* Weights) const
{
    const uint64 Mask=Candidates(P);
    for(int32 I=0;I<Operations.Num();++I)
    {
        const auto& O=Operations[I];
        if(!(Mask&(uint64(1)<<I))||!O.Bounds.IsInsideOrOn(P))
        {
            // A primitive's positive field continues outside its zero-set box.
            // Dropping it there corrupts native interpolation and can erase
            // detached solids. Skip only when a conservative lower bound proves
            // that neither the field nor the smooth blend can change.
            const double Lower=FMath::Sqrt(O.Bounds.ComputeSquaredDistanceToPoint(P))*O.MinimumScale/O.Source.Scale.GetMax();
            const double Threshold=(O.Source.Composition==ECLSolidComposition::Subtract?-A:A)+O.Source.SmoothnessMeters;
            if(Lower>=Threshold)continue;
        }
        const double B=O.Field(P),K=O.Source.SmoothnessMeters;const bool Sub=O.Source.Composition==ECLSolidComposition::Subtract;
        const double Compared=Sub?-B:B;const bool Wins=Sub?Compared>A:Compared<A;
        const double Alpha=K>0?FMath::Clamp(.5+.5*(Sub?Compared-A:A-Compared)/K,0.,1.):double(Wins);
        const double Blend=K>0?FMath::Square(FMath::Max(K-FMath::Abs(A-Compared),0.))/(4*K):0;
        A=Sub?FMath::Max(A,Compared)+Blend:FMath::Min(A,Compared)-Blend;
        if(Weights&&O.SurfaceIndex!=INDEX_NONE)
        {for(double& V:Weights->Values)V*=1-Alpha;Weights->Values[O.SurfaceIndex]+=Alpha;}
    }
    return A;
}
