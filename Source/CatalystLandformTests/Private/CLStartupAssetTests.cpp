#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "AssetRegistry/AssetRegistryModule.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLStartupAssetTagsTest,"Catalyst.Landform.Inspection.PackagedVoxelGuids",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLStartupAssetTagsTest::RunTest(const FString&)
{
 auto& Registry=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();Registry.WaitForCompletion();
 const TCHAR* Packages[]={TEXT("Measured/ST_MarsRock"),TEXT("Measured/ST_MarsScree"),TEXT("Measured/ST_MarsSoil"),TEXT("Rock/MD_RockProperties"),TEXT("Surfaces/MD_PreviewMappingHigh"),TEXT("Surfaces/MD_PreviewMappingLow"),TEXT("Surfaces/ST_Alluvium"),TEXT("Surfaces/ST_MixedRock"),TEXT("Surfaces/ST_Rock"),TEXT("Surfaces/ST_Scree"),TEXT("Surfaces/ST_Soil"),TEXT("Surfaces/ST_StrongRock"),TEXT("Surfaces/ST_VolcanicAsh"),TEXT("Surfaces/ST_WeakRock")};
 TSet<FGuid> Seen;
 for(const auto* Suffix:Packages)
 {
  const FString Package=TEXT("/CatalystLandform/")+FString(Suffix);const FString Name=FPaths::GetCleanFilename(Package);
  const auto Asset=Registry.GetAssetByObjectPath(FSoftObjectPath(Package+TEXT(".")+Name));
  if(!TestTrue(Package+TEXT(" exists"),Asset.IsValid()))continue;
  FString Value;const FName Tag=Name.StartsWith(TEXT("MD_"))?TEXT("VoxelMetadataGuid"):TEXT("VoxelSurfaceTypeGuid");
  if(!TestTrue(Package+TEXT(" has saved GUID tag"),Asset.GetTagValue(Tag,Value)))continue;
  FGuid Guid;TestTrue(Package+TEXT(" has valid GUID"),FGuid::Parse(Value,Guid)&&Guid.IsValid());TestFalse(Package+TEXT(" has distinct GUID"),Seen.Contains(Guid));Seen.Add(Guid);
 }
 return true;
}
