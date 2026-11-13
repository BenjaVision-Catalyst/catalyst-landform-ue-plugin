#include "Model/CLMeasuredTerrain.h"
#include "Stamps/CLStampLibrary.h"
#include "Heightmap/VoxelHeightmap.h"
#include "Heightmap/VoxelHeightmap_Height.h"
#include "Heightmap/VoxelHeightmap_HeightData.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8
#include "VoxelNode_SampleHeightmapImpl.ispc.generated.h"
#endif
#include "Heightmap/VoxelHeightmapFunctionLibrary.h"
#include "Misc/FileHelper.h"
#include "Misc/SecureHash.h"
#include "UObject/StrongObjectPtr.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include <limits>
#include "Engine/Texture2D.h"
#include "Surface/CLSurfaceLibrary.h"
#include "MegaMaterial/VoxelMegaMaterial.h"

namespace
{
constexpr int32 Halo = 2;
bool HashText(const FString &S, FString &Out)
{
    FTCHARToUTF8 Utf8(*S);
    uint8 H[20];
    FSHA1::HashBuffer(Utf8.Get(), Utf8.Length(), H);
    Out = BytesToHex(H, 20).ToLower();
    return true;
}
bool NativeRef(UVoxelHeightmap *H, FVoxelHeightmapRef &Out, FString &Error)
{
    if (!H || !H->Height)
    {
        Error = TEXT("Required measured terrain resource is not resident/cooked");
        return false;
    }
    Out.HeightData = H->Height->GetData();
    Out.ScaleXY = H->ScaleXY;
    Out.ScaleZ = H->Height->ScaleZ;
    Out.OffsetZ = H->Height->OffsetZ;
    if (!Out.HeightData || Out.ScaleXY != 1 || Out.ScaleZ != 1 || Out.OffsetZ != 0 || Out.HeightData->bIsUINT16 ||
        Out.HeightData->InternalScaleZ != 1 || Out.HeightData->InternalOffsetZ != 0 || H->Height->bUseBicubic)
    {
        Error = TEXT("Measured profile requires native float metre offsets with identity scaling");
        return false;
    }
    return true;
}
} // namespace
FString UCLMeasuredTerrainLibrary::DefinitionDigest(const FCLMeasuredTerrainEntry &E)
{
    // Reflection/FName display casing differs between editor and cooked builds.
    // This explicit versioned semantic encoding is independent of both reflected
    // property spelling and client artwork/asset locations.
    auto Raster = [](const FCLMeasuredRaster &R) {
        auto J = MakeShared<FJsonObject>();
        J->SetNumberField(TEXT("width"), R.Width);
        J->SetNumberField(TEXT("height"), R.Height);
        J->SetNumberField(TEXT("westCenterDegrees"), R.WestCenterDegrees);
        J->SetNumberField(TEXT("northCenterDegrees"), R.NorthCenterDegrees);
        J->SetNumberField(TEXT("longitudeStepDegrees"), R.LongitudeStepDegrees);
        J->SetNumberField(TEXT("latitudeStepDegrees"), R.LatitudeStepDegrees);
        J->SetNumberField(TEXT("radiusOffsetMeters"), R.RadiusOffsetMeters);
        J->SetNumberField(TEXT("featherMeters"), R.FeatherMeters);
        J->SetBoolField(TEXT("global"), R.bGlobal);
        J->SetNumberField(TEXT("northPoleOffsetMeters"), R.NorthPoleOffsetMeters);
        J->SetNumberField(TEXT("southPoleOffsetMeters"), R.SouthPoleOffsetMeters);
        J->SetStringField(TEXT("payloadDigest"), R.PayloadDigest.ToLower());
        J->SetStringField(TEXT("coverageDigest"), R.CoverageDigest.ToLower());
        return J;
    };
    auto J = MakeShared<FJsonObject>();
    J->SetNumberField(TEXT("definitionEncoding"), 1);
    J->SetStringField(TEXT("id"), E.Id.ToString().ToLower());
    J->SetNumberField(TEXT("revision"), E.Revision);
    J->SetStringField(TEXT("bodyId"), E.BodyId);
    J->SetStringField(TEXT("frameId"), E.FrameId);
    J->SetStringField(TEXT("datum"), E.Datum);
    J->SetStringField(TEXT("provenance"), E.Provenance);
    J->SetNumberField(TEXT("minimumRadiusMeters"), E.MinimumRadiusMeters);
    J->SetNumberField(TEXT("maximumRadiusMeters"), E.MaximumRadiusMeters);
    J->SetObjectField(TEXT("global"), Raster(E.Global));
    TArray<TSharedPtr<FJsonValue>> Regions;
    for (const auto &R : E.Regions) Regions.Add(MakeShared<FJsonValueObject>(Raster(R)));
    J->SetArrayField(TEXT("regions"), Regions);
    FString Json, Hash;
    FJsonSerializer::Serialize(J, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json));
    HashText(Json, Hash);
    return Hash;
}

bool UCLMeasuredTerrainLibrary::BakeDigests(FString &Error)
{
    Error.Reset();
    if (!IsInGameThread())
    {
        Error = TEXT("Measured resource baking requires the game thread");
        return false;
    }
    auto Candidate = Entries;
    for (auto &E : Candidate)
    {
        auto Bake = [&](FCLMeasuredRaster &R) {
            R.PayloadDigest = UCLStampLibraryAsset::SourceDigest(R.Heightmap);
            R.CoverageDigest = R.Coverage ? UCLStampLibraryAsset::SourceDigest(R.Coverage) : FString();
            return !R.PayloadDigest.IsEmpty();
        };
        if (!Bake(E.Global))
        {
            Error = TEXT("Missing global heightmap");
            return false;
        }
        for (auto &R : E.Regions)
            if (!Bake(R))
            {
                Error = TEXT("Missing regional heightmap");
                return false;
            }
        E.DefinitionDigest = DefinitionDigest(E);
    }
    Entries = MoveTemp(Candidate);
    MarkPackageDirty();
    return true;
}
bool UCLMeasuredTerrainTools::ImportNativeHeights(UVoxelHeightmap *Target, UTexture2D *SourceTexture, const FString &Path, int32 W, int32 H,
                                                  FString &Error)
{
#if WITH_EDITOR
    if (!IsInGameThread() || !Target || !SourceTexture || !Target->Height || W < 4 || H < 4 || int64(W) * H > 100000000)
    {
        Error = TEXT("Invalid bounded native heightmap import");
        return false;
    }
    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *Path) || Bytes.Num() != int64(W) * H * 4)
    {
        Error = TEXT("Float32 raster byte count does not match dimensions");
        return false;
    }
    TVoxelArray<float> Values;
    Values.SetNumUninitialized(int64(W) * H);
    for (int64 I = 0; I < Values.Num(); ++I)
    {
        uint32 Bits;
        FMemory::Memcpy(&Bits, Bytes.GetData() + I * 4, 4);
#if !PLATFORM_LITTLE_ENDIAN
        Bits = BYTESWAP_ORDER32(Bits);
#endif
        FMemory::Memcpy(&Values[I], &Bits, 4);
        if (!FMath::IsFinite(Values[I]))
        {
            Error = TEXT("Native raster requires finite values and a separate explicit coverage mask");
            return false;
        }
    }
    // SetHeights is transient in editor packages. Native Texture.Source persists
    // the exact float raster; VPP reconstructs editor data and serializes cooked data.
    SourceTexture->Modify();
    SourceTexture->SRGB = false;
    SourceTexture->CompressionSettings = TC_SingleFloat;
    SourceTexture->MipGenSettings = TMGS_NoMipmaps;
    SourceTexture->Source.Init(W, H, 1, 1, TSF_R32F, reinterpret_cast<const uint8 *>(Values.GetData()));
    SourceTexture->MarkPackageDirty();
    Target->Modify();
    Target->Height->Modify();
    Target->ScaleXY = 1;
    Target->Height->ScaleZ = 1;
    Target->Height->OffsetZ = 0;
    Target->Height->bUseBicubic = false;
    Target->Height->bCompressTo16Bits = false;
    Target->Height->bEnableMinHeight = false;
    Target->Height->Texture = SourceTexture;
    Target->Height->TextureChannel = EVoxelTextureChannel::R;
    Target->Height->OnReimport();
    if (!Target->Height->GetData())
    {
        Error = TEXT("Native VPP could not reconstruct the persistent float texture source");
        return false;
    }
    Target->MarkPackageDirty();
    Target->OnChanged_EditorOnly.Broadcast();
    return true;
#else
    Error = TEXT("Raster import is authoring-only; cooked workers use native serialized data");
    return false;
#endif
}
struct FCLCompiledMeasuredTerrain::FNative
{
    struct FRaster
    {
        FCLMeasuredRaster Definition;
        FVoxelHeightmapRef Height, Coverage;
    };
    TStrongObjectPtr<UCLMeasuredTerrainLibrary> Owner;
    TArray<FRaster> Rasters;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 8
    const UVoxelHeightmapFunctionLibrary* Sampler=nullptr;
#endif
    FVoxelFloatBuffer SampleHeightmap(const FVoxelHeightmapRef& Heightmap,const FVoxelVector2DBuffer& Position) const
    {
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8
        // NativeRef preflight admits identity-scaled float32 rasters only. Keep VPP's
        // own bilinear/edge behavior instead of introducing a second interpolation.
        FVoxelVector2DBuffer Local=Position;Local.ExpandConstants();FVoxelFloatBuffer Result;Result.Allocate(Local.Num());
        ispc::VoxelNode_SampleHeightmap_float(Local.X.GetData(),Local.Y.GetData(),Result.GetData(),Local.Num(),false,1,0,1,
            Heightmap.HeightData->SizeX,Heightmap.HeightData->SizeY,Heightmap.HeightData->RawData.View<float>().GetData());
        return Result;
#else
        return Sampler->SampleHeightmap(Heightmap,Position,false);
#endif
    }
    UCLSurfaceLibraryAsset *SurfaceLibrary = nullptr;
    UVoxelMegaMaterial *Material = nullptr;
};
TSharedPtr<const FCLCompiledMeasuredTerrain> FCLCompiledMeasuredTerrain::Compile(const FCLMeasuredTerrainReference &Ref,
                                                                                 double Radius, FString &Error,
                                                                                 UCLMeasuredTerrainLibrary *Library)
{
    Error.Reset();
    if (!IsInGameThread() || !Ref.IsActive() || Ref.Revision < 1 || Ref.Digest.IsEmpty() || !FMath::IsFinite(Radius))
    {
        Error = TEXT("Measured resource requires a pinned identity/revision/digest and game-thread activation");
        return nullptr;
    }
    if (!Library)
        Library = GetDefault<UCLMeasuredTerrainSettings>()->Library.LoadSynchronous();
    if (!Library)
    {
        Error = TEXT("Required measured terrain library is not resident/cooked");
        return nullptr;
    }
    TSet<FString> Identities;
    for (const auto &Entry : Library->Entries)
    {
        const FString Key = Entry.Id.ToString().ToLower() + TEXT(":") + FString::FromInt(Entry.Revision);
        if (Identities.Contains(Key))
        {
            Error = TEXT("Ambiguous duplicate measured resource identity/revision");
            return nullptr;
        }
        Identities.Add(Key);
    }
    const auto *E =
        Library->Entries.FindByPredicate([&](const auto &V) { return V.Id == Ref.Id && V.Revision == Ref.Revision; });
    if (!E) { Error = TEXT("Missing measured resource identity/revision"); return nullptr; }
    if (E->DefinitionDigest != Ref.Digest) { Error = TEXT("Measured recipe pin differs from resident resource digest"); return nullptr; }
    const FString ActualDigest = UCLMeasuredTerrainLibrary::DefinitionDigest(*E);
    if (E->DefinitionDigest != ActualDigest)
    {
        Error = FString::Printf(TEXT("Measured semantic digest differs: stored %s, calculated %s"), *E->DefinitionDigest, *ActualDigest);
        return nullptr;
    }
    if (E->BodyId.IsEmpty() ||
        E->FrameId.IsEmpty() || E->Datum.IsEmpty() || E->Regions.Num() > 4 ||
        !FMath::IsFinite(E->MinimumRadiusMeters) || !FMath::IsFinite(E->MaximumRadiusMeters) ||
        E->MinimumRadiusMeters < 1000 || E->MinimumRadiusMeters > E->MaximumRadiusMeters ||
        E->MaximumRadiusMeters >= Radius || bool(E->SurfaceLibrary) != bool(E->Material))
    {
        Error = TEXT("Missing, edited or unsupported measured terrain definition/outer sky boundary");
        return nullptr;
    }
    auto Out = MakeShared<FCLCompiledMeasuredTerrain>();
    auto Native = MakeShared<FNative>();
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 8
    Native->Sampler=GetDefault<UVoxelHeightmapFunctionLibrary>();
#endif
    Native->Owner.Reset(Library);
    Native->SurfaceLibrary = E->SurfaceLibrary;
    Native->Material = E->Material;
    TArray<FCLMeasuredRaster> All = {E->Global};
    All.Append(E->Regions);
    Error = TEXT("Invalid measured raster metadata, dimensions or native data binding");
    for (int32 I = 0; I < All.Num(); ++I)
    {
        const auto &R = All[I];
        FNative::FRaster N;
        N.Definition = R;
        if (R.Width < 4 || R.Height < 4 || R.Width > 20000 || R.Height > 20000 ||
            !FMath::IsFinite(R.LongitudeStepDegrees) || !FMath::IsFinite(R.LatitudeStepDegrees) ||
            R.LongitudeStepDegrees <= 0 || R.LatitudeStepDegrees <= 0 || !FMath::IsFinite(R.WestCenterDegrees) ||
            !FMath::IsFinite(R.NorthCenterDegrees) || !FMath::IsFinite(R.RadiusOffsetMeters) ||
            R.RadiusOffsetMeters < 1000 || !FMath::IsFinite(R.FeatherMeters) || R.FeatherMeters < 1 ||
            R.bGlobal != (I == 0) || !NativeRef(R.Heightmap, N.Height, Error))
            return nullptr;
        if (N.Height.HeightData->SizeX != R.Width + 2 * Halo || N.Height.HeightData->SizeY != R.Height + 2 * Halo ||
            UCLStampLibraryAsset::SourceDigest(R.Heightmap) != R.PayloadDigest)
        {
            Error = TEXT("Measured raster dimensions/payload digest mismatch");
            return nullptr;
        }
        if (!FMath::IsFinite(N.Height.HeightData->DataMin) || !FMath::IsFinite(N.Height.HeightData->DataMax) ||
            N.Height.HeightData->DataMin + R.RadiusOffsetMeters < E->MinimumRadiusMeters - .01 ||
            N.Height.HeightData->DataMax + R.RadiusOffsetMeters > E->MaximumRadiusMeters + .01 ||
            R.NorthCenterDegrees > 90 || R.NorthCenterDegrees - (R.Height - 1) * R.LatitudeStepDegrees < -90)
        {
            Error = TEXT("Measured raster violates declared physical bounds or latitude coverage");
            return nullptr;
        }
        if (I == 0 && (!FMath::IsNearlyEqual(R.Width * R.LongitudeStepDegrees, 360., 1e-8) ||
                       !FMath::IsNearlyEqual(R.Height * R.LatitudeStepDegrees, 180., 1e-8) ||
                       !FMath::IsNearlyEqual(R.WestCenterDegrees, R.LongitudeStepDegrees * .5, 1e-8) || R.Coverage ||
                       !FMath::IsNearlyEqual(R.NorthCenterDegrees, 90. - R.LatitudeStepDegrees * .5, 1e-8) ||
                       !FMath::IsFinite(R.NorthPoleOffsetMeters) || !FMath::IsFinite(R.SouthPoleOffsetMeters)))
        {
            Error = TEXT("Global measured profile requires a complete periodic planetocentric raster");
            return nullptr;
        }
        if (I > 0 && (R.WestCenterDegrees < 0 || R.WestCenterDegrees + (R.Width - 1) * R.LongitudeStepDegrees >= 360))
        {
            Error = TEXT("Regional measured profile cannot cross the longitude seam; split its source coverage explicitly");
            return nullptr;
        }
        if (I > 0 &&
            (!NativeRef(R.Coverage, N.Coverage, Error) || N.Coverage.HeightData->SizeX != N.Height.HeightData->SizeX ||
             N.Coverage.HeightData->SizeY != N.Height.HeightData->SizeY ||
             UCLStampLibraryAsset::SourceDigest(R.Coverage) != R.CoverageDigest))
            return nullptr;
        if (I > 0 && (N.Coverage.HeightData->DataMin < 0 || N.Coverage.HeightData->DataMax > 1))
        {
            Error = TEXT("Native measured coverage must be in [0,1]");
            return nullptr;
        }
        Out->ResidentBytes += N.Height.HeightData->GetAllocatedSize();
        if (N.Coverage.HeightData)
            Out->ResidentBytes += N.Coverage.HeightData->GetAllocatedSize();
        // Workers retain native immutable buffers, not live authoring references.
        N.Definition.Heightmap = nullptr;
        N.Definition.Coverage = nullptr;
        Native->Rasters.Add(MoveTemp(N));
    }
    Error.Reset();
    Out->Native = Native;
    Out->OuterRadiusMeters = Radius;
    Out->MinimumHeightMeters = E->MinimumRadiusMeters - Radius;
    Out->MaximumHeightMeters = E->MaximumRadiusMeters - Radius;
    return Out;
}
UCLSurfaceLibraryAsset *FCLCompiledMeasuredTerrain::GetSurfaceLibrary() const
{
    check(IsInGameThread());
    return Native->SurfaceLibrary;
}
UVoxelMegaMaterial *FCLCompiledMeasuredTerrain::GetMaterial() const
{
    check(IsInGameThread());
    return Native->Material;
}
FVector FCLCompiledMeasuredTerrain::NativeDirection(double Lat, double Lon)
{
    const double P = FMath::DegreesToRadians(Lat), L = FMath::DegreesToRadians(Lon);
    // Explicit reflection: canonical east-positive Y maps to native -Y. No quaternion hides it.
    return FVector(FMath::Cos(P) * FMath::Cos(L), -FMath::Cos(P) * FMath::Sin(L), FMath::Sin(P));
}
FVector2D FCLCompiledMeasuredTerrain::SourceCoordinates(const FVector &P)
{
    const FVector U = P.GetSafeNormal();
    double Lon = FMath::RadiansToDegrees(FMath::Atan2(-U.Y, U.X));
    if (Lon < 0)
        Lon += 360;
    return FVector2D(FMath::RadiansToDegrees(FMath::Atan2(U.Z, FVector2D(U.X, U.Y).Size())), Lon);
}
void FCLCompiledMeasuredTerrain::SampleHeights(TConstArrayView<FVector> Directions, TArray<double> &Heights) const
{
    Heights.Init(std::numeric_limits<double>::quiet_NaN(), Directions.Num());
    // The centre has no geographic direction, but must classify as solid in
    // native radial generation. Any bounded measured radius gives that sign.
    for (int32 I = 0; I < Directions.Num(); ++I)
        if (!Directions[I].ContainsNaN() && Directions[I].IsNearlyZero()) Heights[I] = MinimumHeightMeters;
    TArray<FVector2D> Coordinates;
    Coordinates.Reserve(Directions.Num());
    for (const auto &D : Directions)
        Coordinates.Add(SourceCoordinates(D));
    for (const auto &N : Native->Rasters)
    {
        const auto &R = N.Definition;
        FVoxelVector2DBuffer Positions;
        TArray<int32> Indices;
        TArray<double> Weights;
        TArray<FVector2f> Pixels;
        Pixels.Reserve(Directions.Num());
        for (int32 I = 0; I < Directions.Num(); ++I)
        {
            if (Directions[I].IsNearlyZero() || Directions[I].ContainsNaN())
                continue;
            const auto C = Coordinates[I];
            double X = (C.Y - R.WestCenterDegrees) / R.LongitudeStepDegrees,
                   Y = (R.NorthCenterDegrees - C.X) / R.LatitudeStepDegrees, W = 1;
            if (R.bGlobal)
            {
                X = FMath::Fmod(X + R.Width, R.Width);
                Y = FMath::Clamp(Y, 0., double(R.Height - 1));
            }
            else
            {
                if (X < 0 || Y < 0 || X > R.Width - 1 || Y > R.Height - 1)
                    continue;
                const double Margin = FMath::Min(FMath::Min(X, R.Width - 1 - X) * R.LongitudeStepDegrees *
                                                     FMath::Cos(FMath::DegreesToRadians(C.X)),
                                                 FMath::Min(Y, R.Height - 1 - Y) * R.LatitudeStepDegrees) *
                                      UE_PI / 180 * R.RadiusOffsetMeters;
                W = FMath::Clamp(Margin / R.FeatherMeters, 0., 1.);
                W = W * W * (3 - 2 * W);
            }
            Pixels.Add(FVector2f(X + Halo - (R.Width + 2 * Halo) * .5, Y + Halo - (R.Height + 2 * Halo) * .5));
            Indices.Add(I);
            Weights.Add(W);
        }
        if (Indices.IsEmpty())
            continue;
        Positions.Allocate(Indices.Num());
        for (int32 K = 0; K < Indices.Num(); ++K)
            Positions.Set(K, Pixels[K]);
        const auto Values = Native->SampleHeightmap(N.Height, Positions);
        FVoxelFloatBuffer Mask;
        if (!R.bGlobal)
            Mask = Native->SampleHeightmap(N.Coverage, Positions);
        for (int32 K = 0; K < Indices.Num(); ++K)
        {
            const int32 I = Indices[K];
            if (!R.bGlobal && (!FMath::IsFinite(Mask[K]) || Mask[K] < .999999f))
                continue;
            double V = Values[K];
            if (!FMath::IsFinite(V))
                continue;
            if (R.bGlobal)
            {
                const double Lat = Coordinates[I].X;
                if (Lat > R.NorthCenterDegrees)
                    V = FMath::Lerp(R.NorthPoleOffsetMeters, V,
                                    FMath::Clamp((90 - Lat) / (90 - R.NorthCenterDegrees), 0., 1.));
                const double South = R.NorthCenterDegrees - (R.Height - 1) * R.LatitudeStepDegrees;
                if (Lat < South)
                    V = FMath::Lerp(R.SouthPoleOffsetMeters, V, FMath::Clamp((Lat + 90) / (South + 90), 0., 1.));
            }
            V += R.RadiusOffsetMeters - OuterRadiusMeters;
            Heights[I] = R.bGlobal ? V : FMath::Lerp(Heights[I], V, Weights[K]);
        }
    }
}
