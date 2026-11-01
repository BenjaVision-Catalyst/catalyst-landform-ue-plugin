#pragma once
#include "CoreMinimal.h"
#include "Model/CLPlanetRecipe.h"
#include "UObject/StrongObjectPtr.h"
class FScopedTransaction;

/** Shared authoring operations; names remain valid references after a rename. */
struct CATALYSTLANDFORMEDITOR_API FCLRadialGuideEditing
{
    static bool CanRename(const FCLRadialGuideSet& RadialGuides, int32 Index, const FString& Name, FString& Error);
    static bool Rename(FCLPlanetRecipe& Recipe, int32 Index, const FString& Name, FString& Error);
    static int32 Add(FCLRadialGuideSet& RadialGuides, double NormalizedHeight);
    static bool Remove(FCLRadialGuideSet& RadialGuides, int32 Index);
};

/** One transactional authoring gesture; scene/Details/VPP notification occurs on commit only. */
class CATALYSTLANDFORMEDITOR_API FCLRadialGuideSession : public TSharedFromThis<FCLRadialGuideSession>
{
public:
    FCLRadialGuideSession();
    ~FCLRadialGuideSession();
    UCLPlanetRecipeAsset* Asset = nullptr;
    TStrongObjectPtr<UCLRadialGuideLibraryAsset> Library;
    int32 Selected = INDEX_NONE;
    bool bReadOnly = false; // terrain-type preview uses an independent context snapshot
    TUniquePtr<FScopedTransaction> Gesture;
    FCLPlanetRecipe BeforeRecipe;
    FCLRadialGuideLibrary BeforeLibrary;
    UCLRadialGuideLibraryAsset* BeforeGuideLibrary=nullptr;
    FGuid BeforeGuideSetId;
    TFunction<void()> OnPreviewChanged;
    bool bPendingChange = false;
    void Open(UCLPlanetRecipeAsset* InAsset);
    bool OpenPreview(UCLPlanetRecipeAsset* TransientPreview, const FCLRadialGuideSet* InitialRadialGuides = nullptr);
    void UsePreviewRadialGuides(const FCLRadialGuideSet& RadialGuides);
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

/** Normalized widget coordinates shared by globe and cutaway annotations. */
struct FCLGuideIndicator
{
    FString Text;
    FLinearColor Color;
    FVector2D Position;
    int32 Index=INDEX_NONE;
};
