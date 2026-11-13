#include "PlanetBuilder/CLPlanetBuilderExtensions.h"
namespace { TArray<FCLPlanetBuilderProvider>& Registry(){static TArray<FCLPlanetBuilderProvider> R;return R;} }
void FCLPlanetBuilderExtensions::Register(FCLPlanetBuilderProvider P){check(IsInGameThread());Unregister(P.Id);Registry().Add(MoveTemp(P));}
void FCLPlanetBuilderExtensions::Unregister(FName Id){check(IsInGameThread());Registry().RemoveAll([Id](const auto& P){return P.Id==Id;});}
TArray<FCLPlanetBuilderProvider> FCLPlanetBuilderExtensions::Providers(){check(IsInGameThread());auto R=Registry();R.Sort([](const auto& A,const auto& B){return A.Id.LexicalLess(B.Id);});return R;}
