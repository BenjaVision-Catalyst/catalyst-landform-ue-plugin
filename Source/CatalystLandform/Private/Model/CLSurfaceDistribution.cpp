#include "Model/CLSurfaceDistribution.h"
#include "Model/CLPlanetRecipe.h"

void FCLSurfaceDistributionSettings::ApplyProgression(double Position)
{
    Character=FMath::Clamp(Position,0.,1.);
    const double T=Character*Character*(3-2*Character);
    TypicalHeightPercent=FMath::Lerp(22.,78.,T);
    SpreadPercent=35+25*FMath::Square(2*Character-1);
}
bool FCLSurfaceDistributionSettings::Validate(FString& Error) const
{
    auto In=[](double V,double A,double B){return FMath::IsFinite(V)&&V>=A&&V<=B;};
    if(AlgorithmVersion!=1||!In(Character,0,1)||!In(LowEndAreaPercent,.2,20)||!In(HighEndAreaPercent,.2,20)||!In(EndBandHeightPercent,2,20)||
       !In(TypicalHeightPercent,21,79)||!In(SpreadPercent,5,95)||CalibrationSamples<8192||CalibrationSamples>262144)
    {Error=TEXT("Elevation distribution needs finite ordered quantiles, 0.2–20% tail areas and 8192–262144 calibration samples");return false;}
    return true;
}
TArray<FVector2D> FCLSurfaceDistributionSettings::Quantiles() const
{
    const double L=LowEndAreaPercent/100,U=HighEndAreaPercent/100,B=EndBandHeightPercent/100,M=TypicalHeightPercent/100,S=SpreadPercent/100;
    return {{0,0},{L,B},{.25,M-(M-B)*S},{.5,M},{.75,M+(1-B-M)*S},{1-U,1-B},{1,1}};
}
namespace
{
template<typename R,typename F> void DetailLayers(R& Recipe,F Visit)
{
    // Placed Landforms have an explicit placement transform. Their internal
    // source scale remains owned by their reusable terrain/placement editor.
    for(auto& L:Recipe.NoiseLayers)Visit(L);
    for(auto& Region:Recipe.TerrainRegions)if(!Region.bPlacedLandform)for(auto& L:Region.NoiseLayers)Visit(L);
    for(auto& Entry:Recipe.RegionGeneration.Palette)for(auto& L:Entry.NoiseLayers)Visit(L);
}
}
double FCLPhysicalDetailScale::MaximumMeters(const FCLPlanetRecipe& R)
{
    double Maximum=0;DetailLayers(R,[&](const FCLNoiseLayer& L)
    {
        if(L.Operation==ECLTerrainOperation::Noise)Maximum=FMath::Max(Maximum,L.Scale==ECLNoiseScale::Meters?L.WavelengthMeters:L.WavelengthRadiusFraction*R.RadiusMeters);
        else if(L.Operation==ECLTerrainOperation::StampDistribution)Maximum=FMath::Max(Maximum,L.Distribution.SizeMode==ECLStampSizeMode::Absolute?L.Distribution.MaximumSizeMeters:L.Distribution.MaximumSizeRadiusFraction*R.RadiusMeters);
    });return Maximum;
}
bool FCLPhysicalDetailScale::UsesMeters(const FCLPlanetRecipe& R)
{
    bool Metric=true;DetailLayers(R,[&](const FCLNoiseLayer& L){if(L.Operation==ECLTerrainOperation::Noise)Metric&=L.Scale==ECLNoiseScale::Meters;else if(L.Operation==ECLTerrainOperation::StampDistribution)Metric&=L.Distribution.SizeMode==ECLStampSizeMode::Absolute;});return Metric;
}
bool FCLPhysicalDetailScale::SetMaximumMeters(FCLPlanetRecipe& R,double Maximum,FString& Error)
{
    const double Before=MaximumMeters(R);
    if(!FMath::IsFinite(Maximum)||Maximum<.1||Maximum>100000||Before<=0)
    {Error=TEXT("Detail scale needs existing noise/stamp layers and a maximum size of 0.1–100000 m");return false;}
    const double Factor=Maximum/Before;
    DetailLayers(R,[&](FCLNoiseLayer& L)
    {
        if(L.Operation==ECLTerrainOperation::Noise){L.WavelengthMeters=(L.Scale==ECLNoiseScale::Meters?L.WavelengthMeters:L.WavelengthRadiusFraction*R.RadiusMeters)*Factor;L.Scale=ECLNoiseScale::Meters;}
        else if(L.Operation==ECLTerrainOperation::StampDistribution)
        {
            auto& S=L.Distribution;S.MinimumSizeMeters=(S.SizeMode==ECLStampSizeMode::Absolute?S.MinimumSizeMeters:S.MinimumSizeRadiusFraction*R.RadiusMeters)*Factor;
            S.MaximumSizeMeters=(S.SizeMode==ECLStampSizeMode::Absolute?S.MaximumSizeMeters:S.MaximumSizeRadiusFraction*R.RadiusMeters)*Factor;S.SizeMode=ECLStampSizeMode::Absolute;
        }
    });return true;
}
void FCLPhysicalDetailScale::AddNativeDetail(FCLPlanetRecipe& R)
{
    for(auto& Existing:R.NoiseLayers)if(Existing.Name==TEXT("Physical surface detail")&&Existing.Operation==ECLTerrainOperation::Noise){Existing.bEnabled=true;return;}
    const double Before=MaximumMeters(R);
    FCLNoiseLayer L;L.Name=TEXT("Physical surface detail");L.Operation=ECLTerrainOperation::Noise;L.Scale=ECLNoiseScale::Meters;
    L.WavelengthMeters=Before>0?FMath::Clamp(Before*.25,.1,250.):250;L.Blend=ECLNoiseBlend::Add;L.Octaves=3;
    L.Minimum.Mode=L.Maximum.Mode=ECLHeightReferenceMode::IntervalDelta;L.Minimum.Guide=L.Maximum.Guide=TEXT("SurfaceMin");
    L.Minimum.UpperGuide=L.Maximum.UpperGuide=TEXT("SurfaceMax");L.Minimum.Fraction=-.04;L.Maximum.Fraction=.04;
    R.NoiseLayers.Add(L);R.AlgorithmVersion=2;
}

FRichCurve FCLSurfaceDistributionSettings::InterpolateMonotone(TConstArrayView<FVector2D> Points)
{
    FRichCurve Curve;for(const auto& P:Points)Curve.AddKey(float(P.X),float(P.Y));
    for(int32 I=0;I<Curve.Keys.Num();++I)
    {
        auto& K=Curve.Keys[I];K.InterpMode=RCIM_Cubic;K.TangentMode=RCTM_User;float Slope=0;
        if(I>0&&I+1<Curve.Keys.Num())
        {
            const auto& A=Curve.Keys[I-1];const auto& B=Curve.Keys[I+1];
            ComputeCurveTangent(A.Time,A.Value,K.Time,K.Value,B.Time,B.Value,0.f,true,Slope);
        }
        K.ArriveTangent=K.LeaveTangent=Slope;
    }
    return Curve;
}
