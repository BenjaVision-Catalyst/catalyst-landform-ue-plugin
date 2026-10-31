#include "PCG/CLSphereStampPoints.h"
#include "PCGContext.h"
#include "PCGElement.h"
#include "PCGPin.h"
#include "Data/PCGPointData.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Helpers/PCGHelpers.h"

class FCLSphereStampPointsElement : public IPCGElement
{
    virtual bool ExecuteInternal(FPCGContext* Context) const override
    {
        const auto* S = Context->GetInputSettings<UCLSphereStampPointsSettings>();
        if (!S || S->Count < 0 || S->Count > 256 || S->Weights.IsEmpty()) return true;
        double Total = 0; for (double W : S->Weights) { if (!FMath::IsFinite(W) || W <= 0) return true; Total += W; }
        auto* Data = NewObject<UPCGPointData>();
        auto* Metadata = Data->MutableMetadata();
        auto* Choice = Metadata->CreateAttribute<int32>(TEXT("CLStampIndex"), 0, false, false);
        auto& Points = Data->GetMutablePoints(); Points.Reserve(S->Count);
        for (int32 Index = 0; Index < S->Count; ++Index)
        {
            const int32 PointSeed = PCGHelpers::ComputeSeed(Context->GetSeed(), Index);
            FRandomStream Random(PointSeed);
            const double Z = Random.FRandRange(-1.f, 1.f), Angle = Random.FRand() * 2 * UE_PI;
            const FVector Up(FMath::Sqrt(1-Z*Z)*FMath::Cos(Angle), FMath::Sqrt(1-Z*Z)*FMath::Sin(Angle), Z);
            const FVector Reference = FMath::Abs(Z) > .95 ? FVector::XAxisVector : FVector::ZAxisVector;
            const FVector Tangent = FVector::CrossProduct(Reference, Up).GetSafeNormal();
            FPCGPoint& P = Points.Emplace_GetRef(); P.Seed = PointSeed;
            P.Transform = FTransform(FRotationMatrix::MakeFromZX(Up,Tangent).ToQuat(), Up*S->RadiusMeters*100);
            P.BoundsMin=FVector(-50,-50,-50); P.BoundsMax=FVector(50,50,50);
            double Pick=Random.FRand()*Total; int32 Selected=S->Weights.Num()-1;
            for(int32 I=0;I<S->Weights.Num();++I) { Pick-=S->Weights[I]; if(Pick<=0){Selected=I;break;} }
            P.MetadataEntry=Metadata->AddEntry(); Choice->SetValue(P.MetadataEntry,Selected);
        }
        auto& Out=Context->OutputData.TaggedData.Emplace_GetRef(); Out.Pin=PCGPinConstants::DefaultOutputLabel; Out.Data=Data;
        return true;
    }
};
FPCGElementPtr UCLSphereStampPointsSettings::CreateElement() const { return MakeShared<FCLSphereStampPointsElement>(); }
