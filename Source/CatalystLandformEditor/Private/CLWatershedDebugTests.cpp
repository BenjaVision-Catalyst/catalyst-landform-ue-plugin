#if WITH_DEV_AUTOMATION_TESTS
#include "CLWatershedDebug.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Model/CLWatershed.h"
#include "Misc/AutomationTest.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
FCLPlanetRecipe CLWatershedDebugRecipe(FString& Error)
{
    FString J;FFileHelper::LoadFileToString(J,*(IPluginManager::Get().FindPlugin(TEXT("CatalystLandform"))->GetBaseDir()/TEXT("Examples/Watershed.json")));
    TStrongObjectPtr<UCLPlanetRecipeAsset> A(NewObject<UCLPlanetRecipeAsset>());if(!A->ImportRecipeJson(J,Error))return {};
    auto R=A->Recipe;R.ElevationDistribution.bEnabled=false;R.NoiseLayers.Reset();R.BaseStructure.Reset();R.BaseSurfaceNormalized=.7;R.RadiusMeters=1000;
    for(auto& G:R.RadialGuides.Guides)if(G.Name==TEXT("SurfaceMin"))G.NormalizedHeight=.4;else if(G.Name==TEXT("SurfaceMax"))G.NormalizedHeight=.8;else if(G.Name==TEXT("OceanHeight"))G.NormalizedHeight=.6;
    FCLPlanetFeature Ocean;Ocean.Id=FGuid(8,0,0,1);Ocean.Name=TEXT("Connected ocean");Ocean.LocationDegrees=FVector2D(90,0);Ocean.HalfExtentDegrees=FVector2D(60,60);Ocean.Relief.Fraction=-.5;
    auto Lake=Ocean;Lake.Id=FGuid(8,0,0,2);Lake.Name=TEXT("Inland basin");Lake.LocationDegrees=FVector2D(-90,0);Lake.HalfExtentDegrees=FVector2D(16,16);Lake.Relief.Fraction=-.625;
    auto Shelf=Ocean;Shelf.Id=FGuid(8,0,0,3);Shelf.Name=TEXT("Uplifted shelf");Shelf.LocationDegrees=FVector2D(0,90);Shelf.HalfExtentDegrees=FVector2D(40,40);Shelf.Relief.Fraction=.2;
    auto Second=Lake;Second.Id=FGuid(8,0,0,4);Second.Name=TEXT("Uplifted inland basin");Second.LocationDegrees=Shelf.LocationDegrees;
    R.BaseStructure={Ocean,Lake,Shelf,Second};return R;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLWatershedDebugGeometryTest,"Catalyst.Landform.Watershed.DebugGeometry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLWatershedDebugGeometryTest::RunTest(const FString&)
{
    FString Error;auto R=CLWatershedDebugRecipe(Error);if(!Error.IsEmpty()){AddError(Error);return false;}
    const auto P=FCLPlanetCompiler::Compile(R,Error);if(!P){AddError(Error);return false;}
    const auto D=FCLWatershedDebugGeometry::Build(*P);if(!TestTrue(TEXT("Accepted drainage produces debug geometry"),D.IsValid()))return false;
    TestTrue(TEXT("Native mesh is topologically valid"),D->Water.CheckValidity({},UE::Geometry::EValidityCheckFailMode::ReturnOnly));
    TestTrue(TEXT("Ocean and lakes have filled surfaces and shore boundaries"),D->Bodies.Num()>=3&&D->Bodies[0].Triangles>0&&D->Shores.Num()>0);
    TSet<int64> Levels;int32 LakeTriangles=0;for(int32 I=1;I<D->Bodies.Num();++I)if(D->Bodies[I].Triangles){Levels.Add(FMath::RoundToInt64(D->Bodies[I].HeightMeters*1000));LakeTriangles+=D->Bodies[I].Triangles;}
    TestTrue(TEXT("Distinct inland basins retain distinct levels"),Levels.Num()>=2&&LakeTriangles>0);
    double RadiusError=0,ShoreError=0;TArray<FVector> Directions;
    for(int32 V:D->Water.VertexIndicesItr())
    {
        const FVector Pnt=D->Water.GetVertex(V);double Best=DBL_MAX;for(const auto& B:D->Bodies)Best=FMath::Min(Best,FMath::Abs(Pnt.Size()-P->RadiusMeters-B.HeightMeters));RadiusError=FMath::Max(RadiusError,Best);
        TestFalse(TEXT("Water vertices are finite"),Pnt.ContainsNaN());
    }
    TestTrue(TEXT("Water surfaces follow body radial levels"),RadiusError<1.e-8);
    for(const auto& S:D->Shores)Directions.Append({S.A.GetSafeNormal(),S.B.GetSafeNormal()});
    TArray<double> Heights;P->SampleHeightsMeters(Directions,Heights);
    for(int32 I=0;I<D->Shores.Num();++I)for(int32 K=0;K<2;++K)ShoreError=FMath::Max(ShoreError,FMath::Abs(Heights[2*I+K]-D->Bodies[D->Shores[I].Body].HeightMeters));
    TestTrue(TEXT("Shore boundaries meet final native terrain within 0.5 m in the bounded fixture"),ShoreError<.5);
    const auto& C=P->Watershed->GetCells();TSet<uint64> Edges;int32 Count=0;double DrapeError=0;
    for(const auto& Path:D->Paths)
    {
        TestEqual(TEXT("Native spline retains every draped edge sample"),Path.Curve.Points.Num(),1+4*(Path.Cells.Num()-1));
        for(int32 I=1;I<Path.Cells.Num();++I){const int32 A=Path.Cells[I-1],B=Path.Cells[I];TestEqual(TEXT("Spline follows the final receiver"),C[A].Receiver,B);const uint64 Key=uint64(A)<<32|uint32(B);TestFalse(TEXT("Routing edges are never duplicated at junctions"),Edges.Contains(Key));Edges.Add(Key);++Count;}
        Directions.Reset();for(const auto& V:Path.Curve.Points){Directions.Add(V.OutVal.GetSafeNormal());TestEqual(TEXT("Native linear interpolation cannot overshoot across ridges"),V.InterpMode,CIM_Linear);}
        P->SampleHeightsMeters(Directions,Heights);for(int32 I=0;I<Heights.Num();++I)DrapeError=FMath::Max(DrapeError,FMath::Abs(Path.Curve.Points[I].OutVal.Size()-P->RadiusMeters-Heights[I]-.05));
    }
    TestEqual(TEXT("Spline chains cover every selected drainage edge exactly once"),Count,D->RoutingEdges);TestTrue(TEXT("River paths sample the native surface"),DrapeError<1.e-7);
    FCLCompileProgress Cancel;Cancel.Cancelled=true;TestFalse(TEXT("Superseded debug work cancels"),FCLWatershedDebugGeometry::Build(*P,&Cancel).IsValid());
    auto Dry=R;Dry.Watershed.bEnabled=false;const auto Off=FCLPlanetCompiler::Compile(Dry,Error);TestTrue(TEXT("Disabled Watershed creates no debug water"),Off&&!FCLWatershedDebugGeometry::Build(*Off));
    auto Constant=R;Constant.BaseStructure.Reset();Constant.BaseSurfaceNormalized=.5;
    const auto Ocean=FCLPlanetCompiler::Compile(Constant,Error);const auto OceanDebug=Ocean?FCLWatershedDebugGeometry::Build(*Ocean):nullptr;
    TestTrue(TEXT("All-ocean debug is closed and contains no invented shores or rivers"),OceanDebug&&OceanDebug->Bodies[0].Triangles>0&&OceanDebug->Shores.IsEmpty()&&OceanDebug->Paths.IsEmpty()&&OceanDebug->Water.CheckValidity({},UE::Geometry::EValidityCheckFailMode::ReturnOnly));
    Constant.BaseSurfaceNormalized=.99;const auto Land=FCLPlanetCompiler::Compile(Constant,Error);const auto LandDebug=Land?FCLWatershedDebugGeometry::Build(*Land):nullptr;
    TestTrue(TEXT("All-land closed-terminal profiles invent no water surface"),LandDebug&&LandDebug->Water.TriangleCount()==0&&LandDebug->Shores.IsEmpty());
    AddInfo(FString::Printf(TEXT("%d water triangles; %d bodies; %d shore segments; %d paths / %d edges; shore error %.6f m; %.3f s"),D->Water.TriangleCount(),D->Bodies.Num(),D->Shores.Num(),D->Paths.Num(),Count,ShoreError,D->Seconds));return true;
}
#endif
