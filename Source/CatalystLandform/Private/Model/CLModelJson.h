#pragma once
#include "Dom/JsonObject.h"
#include "UObject/UnrealType.h"
namespace CLModelJson
{
inline bool CheckFields(const FJsonObject& Object, const UStruct& Struct, FString& Error, const FString& Path)
{
    for (const auto& Pair : Object.Values)
    {
        const FProperty* Found = nullptr;
        for (TFieldIterator<FProperty> It(&Struct); It; ++It)
            if (It->GetName().Equals(Pair.Key, ESearchCase::IgnoreCase)) { Found = *It; break; }
        const FString Location = Path + TEXT("/") + Pair.Key;
        if (!Found || Found->HasAnyPropertyFlags(CPF_Deprecated)) { Error = TEXT("Unknown recipe field: ") + Location; return false; }
        auto CheckStruct = [&](const FProperty* Property, const TSharedPtr<FJsonValue>& Value) -> bool
        {
            const auto* S = CastField<FStructProperty>(Property);
            return !S || Value->Type != EJson::Object || CheckFields(*Value->AsObject(), *S->Struct, Error, Location);
        };
        if (const auto* A = CastField<FArrayProperty>(Found); A && Pair.Value->Type == EJson::Array)
        {
            for (const auto& Value : Pair.Value->AsArray()) if (!CheckStruct(A->Inner, Value)) return false;
        }
        else if (!CheckStruct(Found, Pair.Value)) return false;
    }
    return true;
}
}
