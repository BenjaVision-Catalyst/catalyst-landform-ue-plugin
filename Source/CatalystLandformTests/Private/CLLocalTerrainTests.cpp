#if WITH_DEV_AUTOMATION_TESTS
#include "Model/CLTerrainType.h"
#include "CLSectionGeometry.h"
#include "CLTerrainEditing.h"
#include "CLRecipeAssetTools.h"
#include "CLPlanetStamp.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Interfaces/IPluginManager.h"
#include "Async/Async.h"
#include "Surface/VoxelSurfaceTypeTable.h"
#include "VoxelLayers.h"
#include "VoxelQuery.h"

namespace
{
UCLTerrainTypeAsset* LocalMountains(FString& Error)
{
    FString Json;
    FFileHelper::LoadFileToString(Json,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/TerrainTypes/MountainRidgesLocal.json")));
    auto* T=NewObject<UCLTerrainTypeAsset>();return T->ImportTerrainTypeJson(Json,Error)?T:nullptr;
}
FCLPlanetRecipe LocalRecipe(UCLTerrainTypeAsset* T,FString& Error)
{
    auto R=FCLPlanetCompiler::RollingPlains();R.RadiusMeters=1000;R.RadialGuides=FCLRadialGuidePresets::Defaults()[2];
    T->TerrainType.ApplyTo(R,Error);return R;
}
TArray<double> LocalSamples(const FCLCompiledPlanetRecipe& P,const TArray<FVector>& XY)
{TArray<double> H;P.SampleLocalHeightsMeters(XY,H);return H;}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLLocalWindowTest,"Catalyst.Landform.LocalTerrain.WindowAndHalo",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLLocalWindowTest::RunTest(const FString&)
{
    FString Error;auto* T=LocalMountains(Error);if(!TestNotNull(TEXT("Local preset imports"),T)){AddError(Error);return false;}
    const auto R=LocalRecipe(T,Error);const auto Whole=FCLPlanetCompiler::Compile(R,Error);
    if(!TestTrue(TEXT("Bounded assembly field compiles"),Whole.IsValid())){AddError(Error);return false;}
    TArray<FCLResolvedStampPlacement> All;Whole->GetStampPlacements(All);
    TSet<FGuid> Ids;for(const auto& P:All)Ids.Add(P.Id);
    TestEqual(TEXT("Tiled placement IDs are unique"),Ids.Num(),All.Num());
    int32 OutsideCenters=0;double MaxError=0;int32 Samples=0;
    for(double Width:{100.,500.,1000.})
    {
        const FBox2D Region(FVector2D(-Width*.5),FVector2D(Width*.5));
        const auto P=FCLPlanetCompiler::Compile(R,Error,nullptr,&Region);
        if(!TestTrue(TEXT("View field compiles"),P.IsValid())){AddError(Error);return false;}
        TArray<FCLResolvedStampPlacement> Placements;P->GetStampPlacements(Placements);
        TestTrue(TEXT("Local view avoids the whole planet's placement budget"),Placements.Num()<=All.Num());
        for(const auto& B:Placements)
        {
            const auto* A=All.FindByPredicate([&](const auto& V){return V.Id==B.Id;});
            if(!TestNotNull(TEXT("Regional placements keep assembly identities"),A))continue;
            TestTrue(TEXT("View size never changes native PCG selection, transform or relief"),A->SourceId==B.SourceId&&A->PositionMeters.Equals(B.PositionMeters,1.e-10)&&A->TangentX.Equals(B.TangentX,1.e-10)&&A->WidthMeters==B.WidthMeters&&A->ReliefMeters==B.ReliefMeters);
            TestTrue(TEXT("Preset uses guide-resolved relief, independent of footprint width"),B.ReliefMeters>=64.999&&B.ReliefMeters<=120.001);
        }
        TArray<FVector> XY;for(int32 Y=0;Y<=12;++Y)for(int32 X=0;X<=12;++X)XY.Add(FVector(Width*(X/12.-.5),Width*(Y/12.-.5),0));
        const auto A=LocalSamples(*Whole,XY),B=LocalSamples(*P,XY);
        for(int32 I=0;I<A.Num();++I){MaxError=FMath::Max(MaxError,FMath::Abs(A[I]-B[I]));++Samples;}
    }
    // A small view centred on a real stamp toe needs the centre in a neighbouring cell.
    const auto& Stamp=All[All.Num()/2];
    const FVector2D Center=Stamp.PositionMeters+FVector2D(Stamp.TangentX)*Stamp.WidthMeters*.2;
    const FBox2D Toe(Center-FVector2D(5),Center+FVector2D(5));
    const auto ToePlan=FCLPlanetCompiler::Compile(R,Error,nullptr,&Toe);
    if(!TestTrue(TEXT("Offset ten-metre region compiles"),ToePlan.IsValid())){AddError(Error);return false;}
    TArray<FCLResolvedStampPlacement> Halo;ToePlan->GetStampPlacements(Halo);
    for(const auto& P:Halo)if(!Toe.IsInside(P.PositionMeters))++OutsideCenters;
    TestTrue(TEXT("Contributing halo retains stamps centred outside the requested region"),OutsideCenters>0&&Halo.ContainsByPredicate([&](const auto& P){return P.Id==Stamp.Id;}));
    TArray<FVector> Probe={FVector(Center.X,Center.Y,0)};
    TestTrue(TEXT("Halo evaluation equals full assembly evaluation"),FMath::Abs(LocalSamples(*ToePlan,Probe)[0]-LocalSamples(*Whole,Probe)[0])<1.e-7);
    TestTrue(TEXT("All overlapping views evaluate exactly the same field"),MaxError<1.e-7);
    auto Huge=R;Huge.NoiseLayers[1].Distribution.DensityPerSquareKilometer=10000;
    TestFalse(TEXT("Budget overflow fails without dropping placements"),FCLPlanetCompiler::Compile(Huge,Error).IsValid());
    AddInfo(FString::Printf(TEXT("Local field: %d assembly stamps, %d regional samples, %.12g m window error; %d external centres in halo"),All.Num(),Samples,MaxError,OutsideCenters));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLLocalGuideTest,"Catalyst.Landform.LocalTerrain.GuidesAndDimensions",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLLocalGuideTest::RunTest(const FString&)
{
    FString Error;auto* T=LocalMountains(Error);if(!T){AddError(Error);return false;}
    auto R=LocalRecipe(T,Error);const auto A=FCLPlanetCompiler::Compile(R,Error);
    if(!A){AddError(Error);return false;}
    TArray<FCLResolvedStampPlacement> Original;A->GetStampPlacements(Original);
    const TArray<FVector> XY={FVector(0,0,0),FVector(120,-70,0),FVector(-220,180,0)};
    const auto Initial=LocalSamples(*A,XY);
    // Same guide fractions in a larger planet: horizontal metrics stay fixed; relief scales.
    R.RadiusMeters*=2;const auto B=FCLPlanetCompiler::Compile(R,Error);
    if(!B){AddError(Error);return false;}
    TArray<FCLResolvedStampPlacement> Resized;B->GetStampPlacements(Resized);
    TestEqual(TEXT("Planet radius does not affect local point count"),Original.Num(),Resized.Num());
    for(int32 I=0;I<Original.Num();++I)
    {
        TestTrue(TEXT("Metric size, position and identity survive assembly resize"),Original[I].Id==Resized[I].Id&&Original[I].PositionMeters==Resized[I].PositionMeters&&Original[I].WidthMeters==Resized[I].WidthMeters);
        TestTrue(TEXT("Guide-relative stamp relief doubles with guide interval"),FMath::Abs(Resized[I].ReliefMeters-Original[I].ReliefMeters*2)<.001);
    }
    const auto Scaled=LocalSamples(*B,XY);
    for(int32 I=0;I<Initial.Num();++I)TestTrue(TEXT("Noise, stamps and detail share the guide scale"),FMath::Abs(Scaled[I]-Initial[I]*2)<.0001);
    for(const auto& RadialGuides:FCLRadialGuidePresets::Defaults())
    {
        R.RadialGuides=RadialGuides;const auto P=FCLPlanetCompiler::Compile(R,Error);
        if(!TestTrue(TEXT("Reusable terrain resolves every built-in Radial Guides"),P.IsValid())){AddError(Error);return false;}
        for(double H:LocalSamples(*P,XY))TestTrue(TEXT("Different guides produce valid bounded terrain"),FMath::IsFinite(H)&&H<=0&&H>-R.RadiusMeters);
    }
    R=LocalRecipe(T,Error);R.RadialGuides.Guides.RemoveAll([](const auto& S){return S.Name==TEXT("SurfaceMax");});
    TestFalse(TEXT("Missing guide is a compiler diagnostic"),FCLPlanetCompiler::Compile(R,Error).IsValid());
    auto Bad=T->TerrainType;Bad.SchemaVersion=4;TestFalse(TEXT("Old schemas cannot silently acquire local semantics"),Bad.Validate(Error));
    R=LocalRecipe(T,Error);R.SchemaVersion=5;TestFalse(TEXT("Recipe version declares local semantics"),FCLPlanetCompiler::Compile(R,Error).IsValid());
    R=LocalRecipe(T,Error);R.NoiseLayers[1].Distribution.MaximumRelief.Fraction=-.1;
    TestFalse(TEXT("Resolved relief must be positive and ordered"),FCLPlanetCompiler::Compile(R,Error).IsValid());
    auto* Exported=NewObject<UCLTerrainTypeAsset>();Exported->TerrainType=T->TerrainType;FString Json;
    TestTrue(TEXT("Server terrain definition exports"),Exported->ExportJsonString(Json,Error));
    auto* Imported=NewObject<UCLTerrainTypeAsset>();TestTrue(TEXT("Server definition round trips"),Imported->ImportTerrainTypeJson(Json,Error));
    TestFalse(TEXT("Source contains no assembly patch, radius or seed"),Json.Contains(TEXT("terrainPatch"))||Json.Contains(TEXT("radiusMeters"))||Json.Contains(TEXT("1729")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLLocalSphereTest,"Catalyst.Landform.LocalTerrain.DesignerNativeSphereParity",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLLocalSphereTest::RunTest(const FString&)
{
    FString Error;auto* T=LocalMountains(Error);if(!T){AddError(Error);return false;}
    auto R=LocalRecipe(T,Error);const auto Plan=FCLPlanetCompiler::Compile(R,Error);
    if(!Plan){AddError(Error);return false;}
    FCLPlanetStamp Stamp;Stamp.bUseRecipe=true;Stamp.Radius=R.RadiusMeters*100;Stamp.Recipe=R;
    const auto Ref=FVoxelStampRef::New(Stamp);const auto Runtime=StaticCastSharedPtr<FCLPlanetStampRuntime>(FVoxelStampRuntime::Create(nullptr,Ref,nullptr));
    if(!TestTrue(TEXT("Native spherical volume initializes"),Runtime.IsValid()))return false;
    FVoxelDependencyCollector Dependencies(STATIC_FNAME("CL.LocalTerrainParity"));const auto Surfaces=FVoxelSurfaceTypeTable::Get();
    const FVoxelQuery Query(0,FVoxelLayers::Empty(),*Surfaces,Dependencies);
    double MaxError=0,DisplayError=0;int32 Count=0;
    for(double Width:{100.,500.,1000.})
    {
        const FBox2D Region(FVector2D(-Width*.5),FVector2D(Width*.5));
        const auto Local=FCLPlanetCompiler::Compile(R,Error,nullptr,&Region);if(!Local){AddError(Error);return false;}
        const auto G=FCLSectionGeometry::Build(*Local,0,Width);
        // Native sparse volume evaluation, fed the actual Designer vertices.
        FVoxelDoubleVectorBuffer Buffer;Buffer.Allocate(G.SurfaceVertexCount);
        for(int32 I=0;I<G.SurfaceVertexCount;++I)
        {
            Buffer.Set(I,G.SourceSurfacePoints[I]);
            DisplayError=FMath::Max(DisplayError,FMath::Abs(G.Vertices[I].Z/(100*FCLSectionGeometry::DisplaySize)-G.SourceSurfacePoints[I].Size()/(100*R.RadiusMeters)));
        }
        const auto Distances=Runtime->SampleDistances(Query,Buffer);
        for(int32 I=0;I<G.SurfaceVertexCount;++I)MaxError=FMath::Max(MaxError,FMath::Abs(double(Distances[I])));
        Count+=G.SurfaceVertexCount;
    }
    TestTrue(TEXT("Designer agrees with native spherical zero set within 0.1mm"),MaxError<.01);
    TestTrue(TEXT("Flat display preserves the evaluated radial heights"),DisplayError<1.e-9);
    for(FVector Anchor:{FVector::XAxisVector,FVector(1,2,-3).GetSafeNormal()})
    {
        auto Rotated=R;Rotated.TerrainPatch.Anchor=Anchor;Rotated.TerrainPatch.TangentX=FVector::YAxisVector;
        const auto P=FCLPlanetCompiler::Compile(Rotated,Error);if(!P){AddError(Error);return false;}
        const TArray<FVector> XY={FVector(0,0,0),FVector(-180,130,0),FVector(210,-120,0)};
        const auto H=LocalSamples(*Plan,XY);
        for(int32 I=0;I<XY.Num();++I)
            TestTrue(TEXT("Rotating assembly preserves metric terrain values"),FMath::Abs(P->SampleHeightMeters(P->TerrainDirection(FVector2D(XY[I])))-H[I])<.0001);
    }
    // The finite chart must close smoothly into the neutral base, including the antipode.
    for(FVector2D P:{FVector2D(750,0),FVector2D(900,400)})TestEqual(TEXT("Outside patch is the neutral sphere"),Plan->SampleHeightMeters(Plan->TerrainDirection(P)),Plan->BaseHeightMeters);
    TestEqual(TEXT("No antipodal chart repetition"),Plan->SampleHeightMeters(-R.TerrainPatch.Anchor),Plan->BaseHeightMeters);
    const auto Work=Async(EAsyncExecution::ThreadPool,[Plan]{TArray<double> H;const FVector P(10,20,0);Plan->SampleLocalHeightsMeters(MakeArrayView(&P,1),H);return H[0];});
    TestTrue(TEXT("Frozen field samples on worker"),FMath::IsFinite(Work.Get()));
    AddInfo(FString::Printf(TEXT("Local Designer/native sphere: %d vertices, %.9f cm residual, %.12g normalized display error"),Count,MaxError,DisplayError));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLLocalEditorTest,"Catalyst.Landform.LocalTerrain.LayerControls",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLLocalEditorTest::RunTest(const FString&)
{
    auto* Factory=NewObject<UCLTerrainTypeFactory>();
    auto* Created=Cast<UCLTerrainTypeAsset>(Factory->FactoryCreateNew(UCLTerrainTypeAsset::StaticClass(),GetTransientPackage(),NAME_None,RF_Transient,nullptr,nullptr));
    if(!TestNotNull(TEXT("New terrain factory creates a source"),Created))return false;
    TestEqual(TEXT("New sources use metric local coordinates"),Created->TerrainType.Coordinates,ECLTerrainCoordinates::LocalPatch);
    TestEqual(TEXT("New sources use native VPP noise"),Created->TerrainType.AlgorithmVersion,2);
    FString Error;FCLTerrainEditing NewEditing;NewEditing.Open(Created);
    TestTrue(TEXT("New source evaluates without spherical distribution"),NewEditing.CompilePreview(Error).IsValid());
    auto* T=LocalMountains(Error);if(!T){AddError(Error);return false;}
    FCLTerrainEditing Editing;Editing.Open(T);
    const auto Full=Editing.CompilePreview(Error,500);if(!Full){AddError(Error);return false;}
    const auto Small=Editing.CompilePreview(Error,100);if(!Small){AddError(Error);return false;}
    TestTrue(TEXT("Designer compiles the requested metric window"),Small->EvaluationRegion.GetSize().Equals(FVector2D(100),1.e-9));
    const TArray<FVector> XY={FVector(0,0,0),FVector(30,20,0)};
    const auto A=LocalSamples(*Full,XY),B=LocalSamples(*Small,XY);
    for(int32 I=0;I<A.Num();++I)TestEqual(TEXT("Designer area control preserves common heights"),A[I],B[I]);
    for(int32 I=0;I<3;++I)
    {
        Editing.SetSolo(I);const auto Solo=Editing.CompilePreview(Error);
        if(!TestTrue(TEXT("Each local layer solos without upstream interference"),Solo.IsValid())){AddError(Error);return false;}
        TestEqual(TEXT("Noise and stamp Solo each evaluate one operation"),Solo->Layers.Num(),1);
        TestEqual(TEXT("Solo preserves the selected operation"),Solo->Layers[0].Operation,T->TerrainType.NoiseLayers[I].Operation);
    }
    Editing.SetSolo(INDEX_NONE);
    for(int32 I=0;I<3;++I)TestTrue(TEXT("Layer can be disabled"),Editing.Enable(I,false));
    const auto Empty=Editing.CompilePreview(Error);if(!Empty){AddError(Error);return false;}
    for(double H:LocalSamples(*Empty,XY))TestEqual(TEXT("All muted leaves the flat guide midpoint"),H,-450.);
    TestTrue(TEXT("Re-enable stamps alone"),Editing.Enable(1,true));
    const auto Stamps=Editing.CompilePreview(Error);if(!Stamps){AddError(Error);return false;}
    TestTrue(TEXT("Stamps-only retains a neutral surface with usable headroom"),FMath::IsNearlyEqual(Stamps->BaseHeightMeters,-450.,1.e-9)&&Stamps->GetDistributedStampCount()>0);
    return true;
}
#endif
