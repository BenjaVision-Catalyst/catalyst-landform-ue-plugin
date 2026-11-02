#include "CLRadialGuideMeasurements.h"

FText FCLRadialGuideMeasurements::Meters(double Value)
{
    FNumberFormattingOptions Format; Format.SetMaximumFractionalDigits(0);
    return FText::Format(NSLOCTEXT("CL", "RadialMeters", "{0} m"), FText::AsNumber(Value, &Format));
}
FText FCLRadialGuideMeasurements::Height(const FCLPlanetRecipe& Recipe, int32 Index)
{
    const double Fraction=Index==-3?1:Recipe.RadialGuides.Guides.IsValidIndex(Index)?Recipe.RadialGuides.Guides[Index].NormalizedHeight:0;
    return Meters(Recipe.RadiusMeters*Fraction);
}
TArray<FCLRadialGuideMeasurements::FSpan> FCLRadialGuideMeasurements::SurfaceSpans(const FCLPlanetRecipe& Recipe)
{
    const auto& Guides=Recipe.RadialGuides.Guides;
    const int32 Low=Guides.IndexOfByPredicate([](const auto& G){return G.Name==TEXT("SurfaceMin");});
    const int32 High=Guides.IndexOfByPredicate([](const auto& G){return G.Name==TEXT("SurfaceMax");});
    const int32 Sea=Guides.IndexOfByPredicate([](const auto& G){return G.Name==TEXT("OceanHeight");});
    TArray<FSpan> Result;
    if(Low==INDEX_NONE||High==INDEX_NONE||Recipe.RadiusMeters<=0||Guides[Low].NormalizedHeight>=Guides[High].NormalizedHeight)return Result;
    auto Add=[&](int32 A,int32 B,bool Total){Result.Add({A,B,(Guides[B].NormalizedHeight-Guides[A].NormalizedHeight)*Recipe.RadiusMeters,Total});};
    Add(Low,High,true);
    if(Sea!=INDEX_NONE&&Guides[Sea].NormalizedHeight>Guides[Low].NormalizedHeight&&Guides[Sea].NormalizedHeight<Guides[High].NormalizedHeight)
    {Add(Low,Sea,false);Add(Sea,High,false);}
    return Result;
}
