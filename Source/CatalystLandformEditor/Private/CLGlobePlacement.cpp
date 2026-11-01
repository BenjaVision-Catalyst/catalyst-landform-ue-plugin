#include "CLGlobePlacement.h"

FVector FCLGlobePlacementEditing::Direction(const FVector2D& A)
{
    const double Angle=A.Size(),K=Angle>1.e-12?FMath::Sin(Angle)/Angle:1;
    return FVector(A.X*K,A.Y*K,FMath::Cos(Angle));
}
FVector2D FCLGlobePlacementEditing::Chart(const FQuat& Frame,const FVector& Direction)
{
    const FVector P=Frame.UnrotateVector(Direction.GetSafeNormal());const double R=FMath::Sqrt(P.X*P.X+P.Y*P.Y);
    const double K=R>1.e-12?FMath::Atan2(R,P.Z)/R:1;return FVector2D(P.X*K,P.Y*K);
}
void FCLGlobePlacementEditing::Coordinates(const FQuat& Frame,FVector2D& Location,double& Heading)
{
    const FVector N=Frame.GetAxisZ().GetSafeNormal();Location=FVector2D(FMath::RadiansToDegrees(FMath::Atan2(N.X,N.Z)),FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(N.Y,-1.,1.))));
    const auto Basis=FCLCompiledPlanetFeature::Frame(Location,0);
    Heading=FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(Frame.GetAxisX(),Basis.GetAxisY()),FVector::DotProduct(Frame.GetAxisX(),Basis.GetAxisX())));
}
double FCLGlobePlacementTarget::Weight(const FVector& D) const
{
    if(!bEnabled||Frame.UnrotateVector(D).Z<=0)return 0;
    const FVector2D P=FCLGlobePlacementEditing::Chart(Frame,D)/(HalfExtentDegrees*(UE_DOUBLE_PI/180));
    if(bRectangle)return P.GetAbs().GetMax()<=1?1:0;
    const double T=FMath::Clamp((1-P.Size())/Transition,0.,1.);return T*T*T*(T*(T*6-15)+10);
}
TArray<FCLGlobePlacementTarget> FCLGlobePlacementEditing::Resolve(const FCLPlanetRecipe& Recipe)
{
    TArray<FCLGlobePlacementTarget> Out;
    if(Recipe.Coordinates!=ECLTerrainCoordinates::SphericalSurface)return Out;
    for(const auto& F:Recipe.BaseStructure)
    {
        FCLGlobePlacementTarget T;T.Key={F.Id,true};T.Name=F.Name;T.Frame=FCLCompiledPlanetFeature::Frame(F.LocationDegrees,F.HeadingDegrees);
        T.HalfExtentDegrees=F.HalfExtentDegrees;T.bRectangle=F.Profile==ECLPlanetFeatureProfile::Heightmap;T.bEnabled=F.bEnabled;Out.Add(T);
    }
    for(const auto& R:Recipe.TerrainRegions)if(R.bPlacedLandform)
    {
        FCLCompiledLandformPlacement P;FString E;if(!FCLCompiledLandformPlacement::Resolve(R.Placement,Recipe.BaseStructure,P,E))continue;
        FCLGlobePlacementTarget T;T.Key={R.Id,false};T.Name=R.Name;T.Frame=P.Orientation;T.HalfExtentDegrees=P.HalfExtentRadians*(180/UE_DOUBLE_PI);T.Transition=P.TransitionFraction;T.bEnabled=R.bEnabled;T.Priority=R.Priority;Out.Add(T);
    }
    Out.Sort([](const auto& A,const auto& B){if(A.Key.bStructure!=B.Key.bStructure)return A.Key.bStructure;return A.Priority!=B.Priority?A.Priority<B.Priority:A.Key.Id.ToString()<B.Key.Id.ToString();});return Out;
}
bool FCLGlobePlacementEditing::Apply(FCLPlanetRecipe& R,const FCLGlobePlacementTarget& T,bool bResize,FString& Error)
{
    if(T.Frame.ContainsNaN()||!T.Frame.IsNormalized()||T.HalfExtentDegrees.ContainsNaN()||T.HalfExtentDegrees.GetMin()<.1||T.HalfExtentDegrees.GetMax()>=85){Error=TEXT("Invalid globe placement frame or extent");return false;}
    auto Candidate=R;
    if(T.Key.bStructure)
    {
        auto* F=Candidate.BaseStructure.FindByPredicate([&](const auto& V){return V.Id==T.Key.Id;});if(!F){Error=TEXT("Structure feature no longer exists");return false;}
        Coordinates(T.Frame,F->LocationDegrees,F->HeadingDegrees);if(bResize)F->HalfExtentDegrees=T.HalfExtentDegrees;
    }
    else
    {
        auto* Region=Candidate.TerrainRegions.FindByPredicate([&](const auto& V){return V.Id==T.Key.Id;});if(!Region||!Region->bPlacedLandform){Error=TEXT("Placed Landform no longer exists");return false;}
        FQuat Local=T.Frame;const auto Id=Region->Placement.StructureFeatureId;
        if(Id.IsValid())
        {
            const auto* Parent=Candidate.BaseStructure.FindByPredicate([&](const auto& V){return V.Id==Id;});if(!Parent){Error=TEXT("Landform structure binding is missing");return false;}
            Local=FCLCompiledPlanetFeature::Frame(Parent->LocationDegrees,Parent->HeadingDegrees).Inverse()*T.Frame;
        }
        Coordinates(Local,Region->Placement.LocationDegrees,Region->Placement.HeadingDegrees);
        if(bResize){Region->Placement.bFollowStructureExtent=false;Region->Placement.CoverageScale=1;Region->Placement.HalfExtentDegrees=T.HalfExtentDegrees;}
    }
    R=MoveTemp(Candidate);Error.Reset();return true;
}
FCLGlobePlacementTarget FCLGlobePlacementEditing::Drag(const FCLGlobePlacementTarget& S,ECLGlobePlacementHandle Handle,const FVector& A,const FVector& B)
{
    auto T=S;
    if(Handle==ECLGlobePlacementHandle::Centre)T.Frame=(FQuat::FindBetweenNormals(A.GetSafeNormal(),B.GetSafeNormal())*S.Frame).GetNormalized();
    else if(Handle==ECLGlobePlacementHandle::Rotation)
    {
        const auto X=Chart(S.Frame,A),Y=Chart(S.Frame,B);const double Delta=FMath::FindDeltaAngleRadians(FMath::Atan2(X.Y,X.X),FMath::Atan2(Y.Y,Y.X));
        T.Frame=(S.Frame*FQuat(FVector::ZAxisVector,Delta)).GetNormalized();
    }
    else
    {
        const auto X=Chart(S.Frame,A),Y=Chart(S.Frame,B);
        auto Ratio=[](double From,double To){return FMath::Abs(From)>1.e-6?FMath::Max(.001,To/From):1.;};
        FVector2D Scale(1);
        if(Handle==ECLGlobePlacementHandle::Width)Scale.X=Ratio(X.X,Y.X);
        else if(Handle==ECLGlobePlacementHandle::Length)Scale.Y=Ratio(X.Y,Y.Y);
        else if(Handle==ECLGlobePlacementHandle::Uniform)Scale=FVector2D(Ratio(X.Size(),Y.Size()));
        T.HalfExtentDegrees=S.HalfExtentDegrees*Scale;T.HalfExtentDegrees.X=FMath::Clamp(T.HalfExtentDegrees.X,.1,84.9);T.HalfExtentDegrees.Y=FMath::Clamp(T.HalfExtentDegrees.Y,.1,84.9);
    }
    return T;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "UObject/StrongObjectPtr.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLGlobePlacementMathTest,"Catalyst.Landform.PlanetBuilder.PlacementFramesAndUndo",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLGlobePlacementMathTest::RunTest(const FString&)
{
    FString Error;auto R=FCLPlanetCompiler::BasePlanet();FCLPlanetFeature Parent;Parent.Id=FGuid::NewGuid();Parent.LocationDegrees=FVector2D(171,63);Parent.HeadingDegrees=37;Parent.HalfExtentDegrees=FVector2D(55,30);R.BaseStructure.Add(Parent);
    FCLTerrainRegion Region;Region.Id=FGuid::NewGuid();Region.bPlacedLandform=true;Region.bSphericalMask=true;Region.TerrainTypeId=FGuid::NewGuid();Region.TerrainTypeName=TEXT("Reusable source");Region.Placement.StructureFeatureId=Parent.Id;Region.Placement.LocationDegrees=FVector2D(17,-8);Region.Placement.HeadingDegrees=-41;Region.Placement.CoverageScale=.7;R.TerrainRegions.Add(Region);
    const auto Targets=FCLGlobePlacementEditing::Resolve(R);const auto Original=Targets.Last();
    for(const FVector2D Location:{FVector2D(179,12),FVector2D(-179,-25),FVector2D(35,89.99),FVector2D(-17,-89.99)})
    {
        const FVector N=FCLCompiledPlanetFeature::Frame(Location,0).GetAxisZ();auto Moved=FCLGlobePlacementEditing::Drag(Original,ECLGlobePlacementHandle::Centre,Original.Frame.GetAxisZ(),N);
        auto Candidate=R;TestTrue(TEXT("World-space move resolves into bound local coordinates"),FCLGlobePlacementEditing::Apply(Candidate,Moved,false,Error));const auto Back=FCLGlobePlacementEditing::Resolve(Candidate).Last();
        TestTrue(TEXT("Resolved centre follows cursor across date line and poles"),Back.Frame.GetAxisZ().Equals(N,1.e-9));TestTrue(TEXT("Resolved orientation roundtrips"),Back.Frame.Equals(Moved.Frame,1.e-9));
        TestEqual(TEXT("Structure binding is retained"),Candidate.TerrainRegions[0].Placement.StructureFeatureId,Parent.Id);TestEqual(TEXT("Reusable source identity is retained"),Candidate.TerrainRegions[0].TerrainTypeId,Region.TerrainTypeId);
        TestTrue(TEXT("Moved planet compiles"),FCLPlanetCompiler::Compile(Candidate,Error).IsValid());
    }
    const FVector A=Original.Frame.RotateVector(FCLGlobePlacementEditing::Direction(FVector2D(.1,0))),B=Original.Frame.RotateVector(FCLGlobePlacementEditing::Direction(FVector2D(0,.1)));
    auto Rotated=FCLGlobePlacementEditing::Drag(Original,ECLGlobePlacementHandle::Rotation,A,B);TestTrue(TEXT("Ring rotates exactly ninety degrees about local radial up"),Rotated.Frame.GetAxisX().Equals(Original.Frame.GetAxisY(),1.e-9));
    auto Sized=Original;Sized.HalfExtentDegrees*=FVector2D(.8,.6);auto Candidate=R;TestTrue(TEXT("Independent footprint resize applies"),FCLGlobePlacementEditing::Apply(Candidate,Sized,true,Error));
    TestTrue(TEXT("Resize retains frame binding and acquires explicit extents"),Candidate.TerrainRegions[0].Placement.StructureFeatureId==Parent.Id&&!Candidate.TerrainRegions[0].Placement.bFollowStructureExtent&&FCLGlobePlacementEditing::Resolve(Candidate).Last().HalfExtentDegrees.Equals(Sized.HalfExtentDegrees,1.e-9));
    TestTrue(TEXT("Source generation stack remains unchanged"),FCLNoiseLayer::StaticStruct()&&Candidate.TerrainRegions[0].NoiseLayers.Num()==Region.NoiseLayers.Num());
    TStrongObjectPtr<UCLPlanetRecipeAsset> Asset(NewObject<UCLPlanetRecipeAsset>(GetTransientPackage(),NAME_None,RF_Transactional));Asset->Recipe=R;
    {const FScopedTransaction Tx(FText::FromString(TEXT("Globe placement proof")));Asset->Modify();Asset->Recipe=Candidate;}
    GEditor->UndoTransaction();TestTrue(TEXT("One gesture is undoable as one transaction"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Asset->Recipe,&R,0));
    GEditor->RedoTransaction();TestTrue(TEXT("Redo restores the whole placement"),FCLPlanetRecipe::StaticStruct()->CompareScriptStruct(&Asset->Recipe,&Candidate,0));
    return true;
}
#endif
