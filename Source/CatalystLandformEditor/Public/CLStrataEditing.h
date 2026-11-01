#pragma once
#include "CoreMinimal.h"
#include "Model/CLPlanetRecipe.h"
#include "UObject/StrongObjectPtr.h"
class FScopedTransaction;

/** Shared authoring operations; names remain valid references after a rename. */
struct CATALYSTLANDFORMEDITOR_API FCLStrataEditing
{
    static bool CanRename(const FCLStrata& Strata, int32 Index, const FString& Name, FString& Error);
    static bool Rename(FCLPlanetRecipe& Recipe, int32 Index, const FString& Name, FString& Error);
    static int32 Add(FCLStrata& Strata, double NormalizedHeight);
    static bool Remove(FCLStrata& Strata, int32 Index);
};

/** One transactional authoring gesture; scene/Details/VPP notification occurs on commit only. */
class CATALYSTLANDFORMEDITOR_API FCLStrataSession : public TSharedFromThis<FCLStrataSession>
{
public:
    FCLStrataSession();
    ~FCLStrataSession();
    UCLPlanetRecipeAsset* Asset = nullptr;
    TStrongObjectPtr<UCLStrataLibraryAsset> Library;
    int32 Selected = INDEX_NONE;
    bool bReadOnly = false; // terrain-type preview uses an independent context snapshot
    TUniquePtr<FScopedTransaction> Gesture;
    FCLPlanetRecipe BeforeRecipe;
    FCLStrataLibrary BeforeLibrary;
    TFunction<void()> OnPreviewChanged;
    bool bPendingChange = false;
    void Open(UCLPlanetRecipeAsset* InAsset);
    bool OpenPreview(UCLPlanetRecipeAsset* TransientPreview, const FCLStrata* InitialStrata = nullptr);
    void UsePreviewStrata(const FCLStrata& Strata);
    void Begin(const FText& Text);
    void End(bool bCancel = false);
    void Changed();
    void Height(int32 Index, double Height);
    void Remove(int32 Index);
    int32 Add(double Height = .05);
    void Apply(FGuid Id);
    void NewSet();
    void RemoveSet();
};
