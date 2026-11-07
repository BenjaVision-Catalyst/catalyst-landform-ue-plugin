#if WITH_DEV_AUTOMATION_TESTS
#include "VoxelQuery.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Interfaces/IPluginManager.h"
#include "CLDeliveryPrototype.h"
#include "Spline/VoxelSplineStamp.h"
#include "Spline/VoxelSplineMetadata.h"
#include "VoxelAABBTree.h"
#include "UObject/StrongObjectPtr.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLCandidateBoundsAudit,"Catalyst.Landform.Diagnostics.CandidateBounds",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLCandidateBoundsAudit::RunTest(const FString&)
{
 FString J,E;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/OrganicKarst.json")));TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>());if(!A->ImportRecipeJson(J,E)){AddError(E);return false;}
 FCLPlanetStamp S;S.bUseRecipe=true;S.Recipe=A->Recipe;S.Radius=S.Recipe.RadiusMeters*100;auto N=StaticCastSharedPtr<FCLPlanetStampRuntime>(FVoxelStampRuntime::Create(nullptr,FVoxelStampRef::New(S),nullptr));if(!N){AddError(TEXT("No runtime"));return false;}
 FVoxelAABBTree::FElementArray Elements;int32 Segments=0;double MaxExpansion=0;FBox MainBounds(ForceInit);
 for(const auto& P:N->RecipePlan->Caves->Passages)
 {
  FSplineCurves Original,Short;double Width=0;const auto Origin=P.Points[0];
  for(int32 I=0;I<P.Points.Num();++I){Width=FMath::Max(Width,FMath::Max(P.Sections[I].X,P.Sections[I].Y*(1+P.Incision)));FVector T=I==0?P.Points[1]-P.Points[0]:I==P.Points.Num()-1?P.Points[I]-P.Points[I-1]:(P.Points[I+1]-P.Points[I-1])*.35;if(I>0&&I<P.Points.Num()-1)T=T.GetClampedToMaxSize(.5*FMath::Min(FVector::Distance(P.Points[I],P.Points[I-1]),FVector::Distance(P.Points[I+1],P.Points[I])));Original.Position.Points.Emplace(float(I),P.Points[I]-Origin,T,T,P.Form==ECLCaveForm::JointCleft?CIM_Linear:CIM_CurveUser);}
  for(int32 I=0;I<P.Points.Num()-1;++I){const double Extent=FVector::Distance(P.Points[I],P.Points[I+1])+Original.Position.Points[I].LeaveTangent.Length()+Original.Position.Points[I+1].ArriveTangent.Length();const int Steps=P.Form==ECLCaveForm::JointCleft?1:FMath::Max(1,FMath::CeilToInt(Extent/4.));for(int K=0;K<=Steps;++K){const double Key=I+double(K)/Steps;const FVector T=Original.Position.EvalDerivative(Key)/Steps;if(I>0&&K==0){Short.Position.Points.Last().LeaveTangent=T;continue;}const float At=Short.Position.Points.Num();const auto Section=FMath::Lerp(P.Sections[I],P.Sections[I+1],double(K)/Steps);Short.Position.Points.Emplace(At,Original.Position.Eval(Key),T,T,CIM_CurveUser);Short.Rotation.Points.Emplace(At,FQuat::Identity,FQuat::Identity,FQuat::Identity,CIM_Linear);Short.Scale.Points.Emplace(At,FVector(Section.X,Section.Y,1),FVector::ZeroVector,FVector::ZeroVector,CIM_Linear);}}
  const double Expansion=Width*(P.Form==ECLCaveForm::LegacyOval?1.5:2.25)+1;MaxExpansion=FMath::Max(MaxExpansion,Expansion);
  for(const auto& V:FVoxelSplineSegment::Create(Short,FVoxelSplineMetadataRuntime())){const FBox B=FBox(FVector(V.Bounds.Min)+Origin,FVector(V.Bounds.Max)+Origin).ExpandBy(Expansion);Elements.Add(FVoxelBox(B),Elements.Num());MainBounds+=B;++Segments;}
 }
 for(const auto& C:N->RecipePlan->Caves->Chambers){const FBox B=FBox(-C.HalfExtents,C.HalfExtents).TransformBy(FTransform(C.Rotation,C.Center).ToMatrixWithScale()).ExpandBy(C.HalfExtents.GetMin()*.6+1);Elements.Add(FVoxelBox(B),Elements.Num());MainBounds+=B;}
 const auto Index=FVoxelAABBTree::Create(MoveTemp(Elements));const auto Actual=N->RecipePlan->Caves->Cutters->Bounds();TestTrue(TEXT("Reconstructed native feature bounds match compiled whole-cave bounds"),MainBounds.Min.Equals(Actual.Min,1.e-7)&&MainBounds.Max.Equals(Actual.Max,1.e-7));
 FFileHelper::LoadFileToString(J,TEXT("/private/tmp/landform-chunk-work-audit/result.json"));TSharedPtr<FJsonObject> Input;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(J),Input);TArray<TSharedPtr<FJsonValue>> Rows;
 for(const auto& Site:Input->GetArrayField(TEXT("sites")))
 {
  const auto O=Site->AsObject();int64 Pairs=0,Points=0,RockPairs=0,RockPoints=0;int32 Max=0,RockMax=0;
  for(const auto& Row:O->GetArrayField(TEXT("chunkRecords"))){const auto C=Row->AsObject();FVector Key;Key.InitFromString(C->GetStringField(TEXT("key")));const bool Rock=C->GetNumberField(TEXT("maxFieldCm"))<0&&C->GetNumberField(TEXT("finePointsSelected"))==0;const auto Start=Key*640-FVector(70);for(int Z=0;Z<11;++Z)for(int Y=0;Y<11;++Y)for(int X=0;X<11;++X){const auto P=N->SamplingFrame.NativeToBodyMeters(Start+FVector(X,Y,Z)*80);int32 Count=0;Index->Intersects(FVoxelFastBox(FVoxelBox(P,P)),[&](int32){++Count;return false;});Pairs+=Count;++Points;Max=FMath::Max(Max,Count);if(Rock){RockPairs+=Count;++RockPoints;RockMax=FMath::Max(RockMax,Count);}}}
  auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("site"),O->GetStringField(TEXT("site")));R->SetNumberField(TEXT("points"),double(Points));R->SetNumberField(TEXT("candidateFeaturePointPairs"),double(Pairs));R->SetNumberField(TEXT("averageCandidatesPerPoint"),double(Pairs)/Points);R->SetNumberField(TEXT("maxCandidatesPerPoint"),Max);R->SetNumberField(TEXT("skippedRockPoints"),double(RockPoints));R->SetNumberField(TEXT("skippedRockFeaturePointPairs"),double(RockPairs));R->SetNumberField(TEXT("averageCandidatesOnSkippedRock"),double(RockPairs)/RockPoints);R->SetNumberField(TEXT("maxCandidatesOnSkippedRock"),RockMax);Rows.Add(MakeShared<FJsonValueObject>(R));AddInfo(FString::Printf(TEXT("CANDIDATE_AUDIT %s features=%d avg=%.2f max=%d rockAvg=%.2f rockMax=%d"),*O->GetStringField(TEXT("site")),Segments+N->RecipePlan->Caves->Chambers.Num(),double(Pairs)/Points,Max,double(RockPairs)/RockPoints,RockMax));
 }
 auto Root=MakeShared<FJsonObject>();Root->SetNumberField(TEXT("passages"),N->RecipePlan->Caves->Passages.Num());Root->SetNumberField(TEXT("cubicIntervalFeatures"),Segments);Root->SetNumberField(TEXT("chambers"),N->RecipePlan->Caves->Chambers.Num());Root->SetNumberField(TEXT("maxIntervalBoundsExpansionMeters"),MaxExpansion);Root->SetBoolField(TEXT("wholeBoundsMatch"),!HasAnyErrors());Root->SetArrayField(TEXT("sites"),Rows);FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&J));FFileHelper::SaveStringToFile(J,TEXT("/private/tmp/landform-chunk-work-audit/candidate-result.json"));return !HasAnyErrors();
}
#endif
