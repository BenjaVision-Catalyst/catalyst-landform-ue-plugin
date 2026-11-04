#include "Model/CLRockLayer.h"

bool FCLRockFolding::Resolve(double H,double Contrast,double Strain,double Limit,FCLRockFolding& Out,FString& Error)
{
    auto In=[](double V,double A,double B){return FMath::IsFinite(V)&&V>=A&&V<=B;};
    if(!In(H,0,1.e7)||!(Contrast==1||In(Contrast,10,1000))||!In(Strain,0,.15)||!In(Limit,0,5.e5))
    {Error=TEXT("Early folding needs finite thickness, neutral viscosity contrast 1 or thin-layer contrast 10..1000, shortening 0..0.15 and a bounded amplitude limit");return false;}
    FCLRockFolding F;F.CompetentThicknessMeters=H;F.ViscosityContrast=Contrast;F.ShorteningStrain=Strain;
    if(H>0&&Contrast>1)
    {
        // Biot thin-layer linear-viscous dominant wavelength and dimensionless
        // amplification rate; Schmalholz & Mancktelow (2016), equations 13/14.
        F.WavelengthMeters=2*UE_DOUBLE_PI*H*FMath::Pow(Contrast/6.,1./3.);
        F.AmplificationRate=FMath::Pow(4*Contrast/3.,2./3.);
        F.AmplitudeLimitMeters=FMath::Min(Limit,.05*F.WavelengthMeters);
        F.bActive=Strain>0&&F.AmplitudeLimitMeters>0;
    }
    Out=F;return true;
}
double FCLRockFolding::AmplitudeMeters(double Compression) const
{
    if(!bActive||!FMath::IsFinite(Compression)||Compression<=0)return 0;
    const double Seed=.03*CompetentThicknessMeters;
    // Added growth relative to the initial imperfection. Bound the exponent
    // before evaluation; the 5%-of-wavelength ceiling keeps this early-stage.
    const double MaximumExponent=FMath::Loge(1+AmplitudeLimitMeters/Seed);
    const double Exponent=FMath::Min(AmplificationRate*ShorteningStrain*FMath::Clamp(Compression,0.,1.),MaximumExponent);
    return FMath::Min(AmplitudeLimitMeters,Seed*(FMath::Exp(Exponent)-1));
}
