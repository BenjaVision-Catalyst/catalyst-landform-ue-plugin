#include "CLInspectionMaterials.h"
#include "CLPlanet.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/InstancedSkinnedMeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialExpressionBreakMaterialAttributes.h"
#include "Materials/MaterialExpressionSetMaterialAttributes.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionPreviousFrameSwitch.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "MaterialEditingLibrary.h"
#include "MegaMaterial/VoxelMegaMaterial.h"
#include "MegaMaterial/VoxelMegaMaterialGeneratedData.h"
#include "UObject/UnrealType.h"
#include "ShaderCompiler.h"
#include "MaterialShared.h"

namespace
{
bool Scenery(UMeshComponent* C)
{return C && (C->IsA<UInstancedStaticMeshComponent>()||C->IsA<UInstancedSkinnedMeshComponent>());}
template<class T> T* Node(UMaterial& M)
{return CastChecked<T>(UMaterialEditingLibrary::CreateMaterialExpression(&M,T::StaticClass(),0,0));}
UMaterialExpressionCustom* Custom(UMaterial& M,const FString& Code,TArray<TPair<FName,FExpressionInput>> Inputs)
{
    auto* C=Node<UMaterialExpressionCustom>(M); C->Code=Code; C->OutputType=CMOT_Float3;
    C->IncludeFilePaths.Add(TEXT("/Plugin/CatalystLandform/Public/CLWorldAnchoredPresentation.ush"));
    C->Inputs.Reset();
    for(auto& Pair:Inputs){FCustomInput I; I.InputName=Pair.Key; I.Input=Pair.Value; C->Inputs.Add(I);}
    return C;
}
FExpressionInput Input(UMaterialExpression* E,int32 Output=0)
{FExpressionInput I; I.Connect(Output,E); return I;}
}
UMaterialInterface* FCLInspectionMaterials::ProjectMaterial(UMaterialInterface* Source,FString& Error)
{
    if(!Source) return nullptr;
    if(auto* Existing=Materials.Find(Source)) return Existing->Get();
    if(auto* Instance=Cast<UMaterialInstanceConstant>(Source))
    {
        auto* Parent=ProjectMaterial(Instance->Parent,Error); if(!Parent) return nullptr;
        auto* Copy=DuplicateObject<UMaterialInstanceConstant>(Instance,GetTransientPackage());
        Materials.Emplace(Source,TStrongObjectPtr<UMaterialInterface>(Copy));
        Copy->SetParentEditorOnly(Parent);
        // A gentle breeze changes displacement amplitude, never authored timing.
        float WindIntensity=0;
        if(Instance->GetScalarParameterValue(FMaterialParameterInfo(TEXT("wind_intensity")),WindIntensity))
            Copy->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("wind_intensity")),WindIntensity*.08f);
        if(Instance->NaniteOverrideMaterial.bEnableOverride)
        {
            auto* Override=Instance->NaniteOverrideMaterial.GetOverrideMaterial();
            Copy->NaniteOverrideMaterial.OverrideMaterialEditor=Override && Override!=Source?ProjectMaterial(Override,Error):nullptr;
            if(Override && Override!=Source && !Copy->NaniteOverrideMaterial.OverrideMaterialEditor)return nullptr;
        }
        Copy->BasePropertyOverrides.bOverride_MaxWorldPositionOffsetDisplacement=true;
        Copy->BasePropertyOverrides.MaxWorldPositionOffsetDisplacement=0;
        Copy->PostEditChange();
        return Copy;
    }
    auto* Original=Cast<UMaterial>(Source);
    if(!Original){Error=TEXT("Walking presentation needs a material or constant material instance: ")+Source->GetPathName(); return nullptr;}
    auto* M=DuplicateObject<UMaterial>(Original,GetTransientPackage());
    Materials.Emplace(Source,TStrongObjectPtr<UMaterialInterface>(M));
    auto* Override=Original->GetNaniteOverride();
    M->NaniteOverrideMaterial.OverrideMaterialEditor=Override && Override!=Source?ProjectMaterial(Override,Error):nullptr;
    if(Override && Override!=Source && !M->NaniteOverrideMaterial.OverrideMaterialEditor)return nullptr;
    M->MaxWorldPositionOffsetDisplacement=0;
    auto* Data=M->GetEditorOnlyData();
    FExpressionInput Normal,Wpo;
    UMaterialExpressionBreakMaterialAttributes* Break=nullptr;
    if(M->bUseMaterialAttributes)
    {
        Break=Node<UMaterialExpressionBreakMaterialAttributes>(*M);
        Break->MaterialAttributes=Data->MaterialAttributes;
        auto* Set=Node<UMaterialExpressionSetMaterialAttributes>(*M);
        Set->Inputs[0]=Data->MaterialAttributes;
        Data->MaterialAttributes.Connect(0,Set);
        Normal=Input(Break); Wpo=Input(Break);
    }
    else {Normal=Data->Normal; Wpo=Data->WorldPositionOffset;}
    if(!Normal.Expression){auto* N=Node<UMaterialExpressionConstant3Vector>(*M);N->Constant=FLinearColor(0,0,1);Normal=Input(N);}
    if(!Wpo.Expression){auto* N=Node<UMaterialExpressionConstant3Vector>(*M);N->Constant=FLinearColor::Black;Wpo=Input(N);}
    auto* Zero=Node<UMaterialExpressionConstant>(*M); Zero->R=0;
    auto* One=Node<UMaterialExpressionConstant>(*M); One->R=1;
    auto* Previous=Node<UMaterialExpressionPreviousFrameSwitch>(*M);
    Previous->CurrentFrame.Connect(0,Zero);Previous->PreviousFrame.Connect(0,One);
    auto* Offset=Custom(*M,TEXT("return Authored+CLWorldAnchoredOffset(GetPrimitiveData(Parameters),WSToDF(GetWorldPosition(Parameters)),Previous>.5);"),{{TEXT("Authored"),Wpo},{TEXT("Previous"),Input(Previous)}});
    const FString NormalCode=M->bTangentSpaceNormal?
        TEXT("float3 W=normalize(mul(N,Parameters.TangentToWorld)); W=CLWorldAnchoredSurfaceNormal(GetPrimitiveData(Parameters),WSToDF(Parameters.LWCData.WorldPosition_NoOffsets),W); return mul(W,transpose(Parameters.TangentToWorld));"):
        TEXT("return CLWorldAnchoredSurfaceNormal(GetPrimitiveData(Parameters),WSToDF(Parameters.LWCData.WorldPosition_NoOffsets),N);");
    auto* ProjectedNormal=Custom(*M,NormalCode,{{TEXT("N"),Normal}});
    if(Break)
    {
        if(!UMaterialEditingLibrary::ConnectMaterialExpressions(Break,TEXT("Normal"),ProjectedNormal,TEXT("N")) ||
           !UMaterialEditingLibrary::ConnectMaterialExpressions(Break,TEXT("WorldPositionOffset"),Offset,TEXT("Authored")))
        {Error=TEXT("Cannot preserve the source material's normal/WPO outputs");return nullptr;}
    }
    if(M->bUseMaterialAttributes)
    {
        auto* Set=CastChecked<UMaterialExpressionSetMaterialAttributes>(Data->MaterialAttributes.Expression);
        Set->ConnectInputAttribute(MP_Normal,ProjectedNormal); Set->ConnectInputAttribute(MP_WorldPositionOffset,Offset);
    }
    else {Data->Normal.Connect(0,ProjectedNormal);Data->WorldPositionOffset.Connect(0,Offset);}
    const auto Errors=UMaterialEditingLibrary::RecompileMaterial(M);
    if(!Errors.IsEmpty()){Error=Source->GetPathName()+TEXT(": ")+FString::Join(Errors,TEXT("; "));return nullptr;}
    return M;
}
bool FCLInspectionMaterials::Prepare(UWorld& World,UVoxelMegaMaterial& Source,FString& Error)
{
    auto* Projection=LoadObject<UMaterialFunction>(nullptr,TEXT("/CatalystLandform/Surfaces/MF_PlanetPresentation.MF_PlanetPresentation"));
    if(!Projection){Error=TEXT("Missing Landform presentation material function");return false;}
    auto* Function=NewObject<UMaterialFunction>(GetTransientPackage(),NAME_None,RF_Transient);
    KeepAlive.Emplace(Function);
    auto Add=[&](UClass* Class){return UMaterialEditingLibrary::CreateMaterialExpressionInFunction(Function,Class,0,0);};
    auto* In=CastChecked<UMaterialExpressionFunctionInput>(Add(UMaterialExpressionFunctionInput::StaticClass()));
    In->InputName=TEXT("Canonical");In->InputType=FunctionInput_MaterialAttributes;
    UMaterialExpression* Attributes=In;
    if(Source.AttributePostProcess && Source.AttributePostProcess!=Projection)
    {
        auto* Call=CastChecked<UMaterialExpressionMaterialFunctionCall>(Add(UMaterialExpressionMaterialFunctionCall::StaticClass()));
        Call->SetMaterialFunction(Source.AttributePostProcess);
        if(Call->FunctionInputs.Num()!=1||Call->FunctionOutputs.Num()!=1){Error=TEXT("Terrain appearance requires one material-attributes input/output");return false;}
        Call->FunctionInputs[0].Input.Connect(0,Attributes);Attributes=Call;
    }
    auto* Call=CastChecked<UMaterialExpressionMaterialFunctionCall>(Add(UMaterialExpressionMaterialFunctionCall::StaticClass()));
    Call->SetMaterialFunction(Projection);
    if(Call->FunctionInputs.Num()!=1||Call->FunctionOutputs.Num()!=1){Error=TEXT("Invalid Landform projection interface");return false;}
    Call->FunctionInputs[0].Input.Connect(0,Attributes);
    auto* Out=CastChecked<UMaterialExpressionFunctionOutput>(Add(UMaterialExpressionFunctionOutput::StaticClass()));
    Out->OutputName=TEXT("Presented");Out->A.Connect(0,Call);
    UMaterialEditingLibrary::UpdateMaterialFunction(Function);
    Mega.Reset(UVoxelMegaMaterial::CreateTransient());
    for(TFieldIterator<FProperty> It(UVoxelMegaMaterial::StaticClass());It;++It)
        if(It->HasAnyPropertyFlags(CPF_Edit)) It->CopyCompleteValue_InContainer(Mega.Get(),&Source);
    Mega->AttributePostProcess=Function;
    Mega->GetGeneratedData_EditorOnly().RebuildNow();
    for(TActorIterator<AActor> It(&World);It;++It)
    {
        TInlineComponentArray<UMeshComponent*> Components;It->GetComponents(Components);
        for(auto* C:Components) if(Scenery(C)) for(auto* M:C->GetMaterials())
            if(M && !ProjectMaterial(M,Error)) return false;
    }
    if(GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
    TArray<UMaterialInterface*> Validate;
    for(const auto& Pair:Materials) Validate.Add(Pair.Value.Get());
    for(const auto& Pair:Mega->GetGeneratedData_EditorOnly().TargetToMaterial) Validate.Add(Pair.Value.Get());
    for(auto* Material:Validate) if(Material)
    {
        const auto* Resource=Material->GetMaterialResource(GMaxRHIShaderPlatform);
        if(!Resource || !Resource->GetCompileErrors().IsEmpty())
        {Error=TEXT("Walking material did not compile: ")+Material->GetPathName();return false;}
    }
    Error.Reset();return true;
}
bool FCLInspectionMaterials::Apply(UWorld& World,FString& Error)
{
    for(TActorIterator<AActor> It(&World);It;++It)
    {
        TInlineComponentArray<UMeshComponent*> Components;It->GetComponents(Components);
        for(auto* C:Components) if(Scenery(C)) for(int32 Slot=0;Slot<C->GetNumMaterials();++Slot)
        {
            auto* Source=C->GetMaterial(Slot); if(!Source) continue;
            auto* Replacement=Materials.Find(Source);
            if(!Replacement){Error=TEXT("Scenery material changed after walking preparation");return false;}
            C->SetMaterial(Slot,Replacement->Get());
        }
    }
    Error.Reset();return true;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Editor.h"
#include "Surface/CLSurfaceLibrary.h"
#include "Materials/MaterialExpressionMakeMaterialAttributes.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "MaterialShared.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCLInspectionMaterialCompositionTest,
    "Catalyst.Landform.Inspection.MaterialComposition",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCLInspectionMaterialCompositionTest::RunTest(const FString&)
{
    auto* World=GEditor?GEditor->GetEditorWorldContext().World():nullptr;
    auto* Source=GetDefault<UCLSurfaceSettings>()->PlanetMaterial.LoadSynchronous();
    if(!TestNotNull(TEXT("Existing editor world"),World)||!TestNotNull(TEXT("Native surface catalogue"),Source))return false;
    auto* Actor=World->SpawnActor<AActor>();
    auto* Component=NewObject<UInstancedStaticMeshComponent>(Actor);Actor->AddInstanceComponent(Component);
    Component->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    Component->RegisterComponent();
    auto* Plain=NewObject<UMaterial>(GetTransientPackage());TStrongObjectPtr<UMaterial> KeepPlain(Plain);
    auto* Color=Node<UMaterialExpressionConstant3Vector>(*Plain);Color->Constant=FLinearColor(.2,.4,.1);
    Plain->GetEditorOnlyData()->BaseColor.Connect(0,Color);
    auto* Attributes=NewObject<UMaterial>(GetTransientPackage());TStrongObjectPtr<UMaterial> KeepAttributes(Attributes);Attributes->bUseMaterialAttributes=true;
    auto* Make=Node<UMaterialExpressionMakeMaterialAttributes>(*Attributes);Make->BaseColor.Connect(0,Node<UMaterialExpressionConstant3Vector>(*Attributes));
    Attributes->GetEditorOnlyData()->MaterialAttributes.Connect(0,Make);
    auto* WindAmplitude=Node<UMaterialExpressionScalarParameter>(*Plain);WindAmplitude->ParameterName=TEXT("wind_intensity");WindAmplitude->DefaultValue=1.25f;
    auto* WindSpeed=Node<UMaterialExpressionScalarParameter>(*Plain);WindSpeed->ParameterName=TEXT("wind_speed");WindSpeed->DefaultValue=.35f;
    Plain->GetEditorOnlyData()->Roughness.Connect(0,WindAmplitude);Plain->GetEditorOnlyData()->Metallic.Connect(0,WindSpeed);
    Plain->UpdateCachedExpressionData();
    auto* WindInstance=NewObject<UMaterialInstanceConstant>(GetTransientPackage());TStrongObjectPtr<UMaterialInstanceConstant> KeepWind(WindInstance);WindInstance->SetParentEditorOnly(Plain);WindInstance->PostEditChange();
    auto* WindComponent=NewObject<UInstancedStaticMeshComponent>(Actor);Actor->AddInstanceComponent(WindComponent);WindComponent->SetStaticMesh(Component->GetStaticMesh());WindComponent->RegisterComponent();WindComponent->SetMaterial(0,WindInstance);
    Component->SetMaterial(0,Plain);
    auto* AttributeComponent=NewObject<UInstancedStaticMeshComponent>(Actor);Actor->AddInstanceComponent(AttributeComponent);
    AttributeComponent->SetStaticMesh(Component->GetStaticMesh());AttributeComponent->RegisterComponent();AttributeComponent->SetMaterial(0,Attributes);
    auto* OriginalProcess=Source->AttributePostProcess.Get();const auto OriginalSurfaces=Source->SurfaceTypes;
    FString Error;FCLInspectionMaterials Prepared;
    const bool Ready=Prepared.Prepare(*World,*Source,Error);
    TestTrue(TEXT("Native material composition compiles"),Ready);if(!Ready)AddError(Error);
    if(Ready)
    {
        TestTrue(TEXT("MegaMaterial is an independent transient copy"),Prepared.GetMegaMaterial()!=Source && Prepared.GetMegaMaterial()->bEditorOnly);
        TestTrue(TEXT("Geological catalogue is retained"),Prepared.GetMegaMaterial()->SurfaceTypes==OriginalSurfaces);
        TestTrue(TEXT("Original MegaMaterial postprocess is untouched"),Source->AttributePostProcess==OriginalProcess);
        TestTrue(TEXT("Projected scenery copies apply"),Prepared.Apply(*World,Error));
        if(auto* WindCopy=Cast<UMaterialInstanceConstant>(WindComponent->GetMaterial(0)))
        {
            float Value=0;WindCopy->GetScalarParameterValue(FMaterialParameterInfo(TEXT("wind_intensity")),Value);TestEqual(TEXT("Grass sway amplitude is gentle"),Value,.1f);
            WindCopy->GetScalarParameterValue(FMaterialParameterInfo(TEXT("wind_speed")),Value);TestEqual(TEXT("Authored grass timing is retained"),Value,.35f);
            WindInstance->GetScalarParameterValue(FMaterialParameterInfo(TEXT("wind_intensity")),Value);TestEqual(TEXT("Authored grass amplitude is untouched"),Value,1.25f);
        }else AddError(TEXT("No walking grass material instance"));
        for(int32 Slot=0;Slot<2;++Slot)
        {
            auto* Copy=Cast<UMaterial>((Slot?AttributeComponent:Component)->GetMaterial(0));TestNotNull(TEXT("Projected material"),Copy);
            if(Copy)
            {
                TestTrue(TEXT("Source material is untouched"),Copy!=(Slot?Attributes:Plain));
                if(auto* Resource=Copy->GetMaterialResource(GMaxRHIShaderPlatform))
                    for(const auto& CompileError:Resource->GetCompileErrors())AddError(CompileError);
                else AddError(TEXT("No compiled material resource"));
            }
        }
    }
    Actor->Destroy();return true;
}
#endif
