#include "Stamps/CLStampLibrary.h"
#include "CLModelJson.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonSerializer.h"
#include "Heightmap/VoxelHeightmap.h"
#include "Heightmap/VoxelHeightmap_Height.h"
#include "Misc/SecureHash.h"

bool FCLStampDistributionSettings::Validate(FString& Error) const
{
    Error.Reset();
    auto FiniteRange=[](double V,double A,double B){return FMath::IsFinite(V)&&V>=A&&V<=B;};
    if(CollectionId.IsNone() || CollectionRevision<1 || !FiniteRange(DensityPerSquareKilometer,0,10000) ||
       uint8(SizeMode)>uint8(ECLStampSizeMode::PlanetRadius) || uint8(Blend)>uint8(ECLStampBlend::Override) ||
       !FiniteRange(MinimumReliefRatio,.001,1) || !FiniteRange(MaximumReliefRatio,MinimumReliefRatio,1) ||
       !FiniteRange(BlendSmoothnessRadiusFraction,0,.05) || !FiniteRange(EdgeFalloffFraction,.01,.5))
    {Error=TEXT("Invalid stamp collection, density, relief or blend settings");return false;}
    const double Low=SizeMode==ECLStampSizeMode::Absolute?MinimumSizeMeters:MinimumSizeRadiusFraction;
    const double High=SizeMode==ECLStampSizeMode::Absolute?MaximumSizeMeters:MaximumSizeRadiusFraction;
    if(!FiniteRange(Low,SizeMode==ECLStampSizeMode::Absolute?.1:.000001,1.e7) || !FiniteRange(High,Low,1.e7))
    {Error=TEXT("Stamp minimum/maximum size must be finite, positive and ordered");return false;}
    return true;
}
bool FCLStampLibrary::Validate(FString& Error) const
{
    Error.Reset(); TSet<FString> Keys;
    if(SchemaVersion!=1 || Collections.Num()>64){Error=TEXT("Stamp library requires schema 1 and at most 64 collections");return false;}
    for(const auto& C:Collections)
    {
        const FString Key=C.Id.ToString()+TEXT(":")+LexToString(C.Revision);
        if(C.Id.IsNone()||C.Revision<1||C.Name.IsEmpty()||Keys.Contains(Key)||C.Entries.IsEmpty()||C.Entries.Num()>64)
        {Error=TEXT("Invalid or duplicate stamp collection ID/revision, name or entries");return false;}
        Keys.Add(Key);TSet<FName> Ids;
        for(const auto& E:C.Entries)
        {
            if(E.Id.IsNone()||Ids.Contains(E.Id)||E.Heightmap.IsNull()||E.Name.IsEmpty()||E.Provenance.IsEmpty()||
               !FMath::IsFinite(E.Weight)||E.Weight<=0||E.Weight>1.e6||!FMath::IsFinite(E.ReferenceReliefMeters)||E.ReferenceReliefMeters<=0||
               E.Blend==ECLStampBlend::Collection||uint8(E.Blend)>uint8(ECLStampBlend::Override))
            {Error=TEXT("Invalid stamp source in collection ")+C.Id.ToString();return false;}
            Ids.Add(E.Id);
        }
    }
    return true;
}
UCLStampLibraryAsset* UCLStampLibraryAsset::LoadConfigured()
{return IsInGameThread()?GetDefault<UCLLandformSettings>()->StampLibrary.LoadSynchronous():nullptr;}
FString UCLStampLibraryAsset::SourceDigest(UVoxelHeightmap* H)
{
    if(!IsInGameThread()||!H||!H->Height)return {};
    const auto D=H->Height->GetData();if(!D)return {};
    FMD5 Hash;Hash.Update(reinterpret_cast<const uint8*>(D->RawData.GetData()),D->RawData.Num());
    const FString Description=FString::Printf(TEXT("%d/%d/%d/%.17g/%.17g/%.17g/%.17g/%.17g/%d"),
        D->SizeX,D->SizeY,D->bIsUINT16,H->ScaleXY,H->Height->ScaleZ,H->Height->OffsetZ,D->InternalScaleZ,D->InternalOffsetZ,H->Height->bUseBicubic);
    FTCHARToUTF8 Text(*Description);Hash.Update(reinterpret_cast<const uint8*>(Text.Get()),Text.Length());
    uint8 Digest[16];Hash.Final(Digest);return BytesToHex(Digest,16);
}
FString UCLStampLibraryAsset::CollectionDigest(const FCLStampCollection& C)
{
    auto Entries=C.Entries;Entries.Sort([](const auto& A,const auto& B){return A.Id.LexicalLess(B.Id);});
    FString Canonical=C.Id.ToString().ToLower()+TEXT(":")+LexToString(C.Revision);
    for(const auto& E:Entries)Canonical+=FString::Printf(TEXT("|%s:%.17g:%s:%.17g:%d:%s"),*E.Id.ToString().ToLower(),E.Weight,
        *E.Heightmap.ToSoftObjectPath().ToString(),E.ReferenceReliefMeters,int32(E.Blend),*E.SourceDigest);
    return FMD5::HashAnsiString(*Canonical);
}
bool UCLStampLibraryAsset::BakeSourceDigests(FString& Error)
{
    if(!Library.Validate(Error))return false;
    auto Candidate=Library;
    for(auto& C:Candidate.Collections)
    {
        const bool bNewRevision=C.Revision>C.BakedRevision;
        for(auto& E:C.Entries)
        {
            const FString Digest=SourceDigest(E.Heightmap.LoadSynchronous());
            if(Digest.IsEmpty()){Error=TEXT("Cannot load native heightmap source: ")+E.Id.ToString();return false;}
            if(!bNewRevision&&!E.SourceDigest.IsEmpty()&&E.SourceDigest!=Digest)
            {Error=TEXT("Source changed. Publish a new collection revision and versioned source asset: ")+E.Id.ToString();return false;}
            E.SourceDigest=Digest;
        }
        const FString Digest=CollectionDigest(C);
        if(!bNewRevision&&!C.DefinitionDigest.IsEmpty()&&C.DefinitionDigest!=Digest)
        {Error=TEXT("Collection weights or sources changed; publish a new revision: ")+C.Id.ToString();return false;}
        C.BakedRevision=C.Revision;C.DefinitionDigest=Digest;
    }
    Modify();Library=MoveTemp(Candidate);MarkPackageDirty();return true;
}
bool UCLStampLibraryAsset::ImportLibraryJson(const FString& Json,FString& Error)
{
    Error.Reset();if(Json.Len()>256*1024){Error=TEXT("Stamp library JSON exceeds 256 KiB");return false;}
    TSharedPtr<FJsonObject> Object;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Object)||!Object)
    {Error=TEXT("Invalid stamp library JSON");return false;}
    if(!CLModelJson::CheckFields(*Object,*FCLStampLibrary::StaticStruct(),Error,TEXT("StampLibrary")))return false;
    FCLStampLibrary Candidate;FText Reason;
    if(!FJsonObjectConverter::JsonObjectToUStruct(Object.ToSharedRef(),&Candidate,0,0,false,&Reason)||!Candidate.Validate(Error))
    {if(Error.IsEmpty())Error=Reason.ToString();return false;}
    Modify();Library=MoveTemp(Candidate);MarkPackageDirty();return true;
}
FString UCLStampLibraryAsset::GetSummaryText()const
{return FString::Printf(TEXT("%d versioned stamp collections"),Library.Collections.Num());}
