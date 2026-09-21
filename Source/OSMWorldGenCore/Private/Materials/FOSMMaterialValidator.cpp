// Copyright InviMind. All Rights Reserved.

#include "Materials/FOSMMaterialValidator.h"
#include "Materials/UOSMPhysicalMaterial.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"

bool FOSMMaterialValidator::ValidateComponent(
    UProceduralMeshComponent* MeshComp,
    TArray<FString>& OutErrors,
    int32& OutSectionsChecked,
    int32& OutMissingPhysMat,
    int32& OutMissingVisualMat)
{
    if (!MeshComp) return false;

    const int32 NumSections = MeshComp->GetNumSections();
    OutSectionsChecked += NumSections;

    AActor* Owner = MeshComp->GetOwner();
    const FString OwnerLabel = Owner ? Owner->GetName() : TEXT("UnknownActor");

    bool bAllValid = true;

    for (int32 SectionIdx = 0; SectionIdx < NumSections; ++SectionIdx)
    {
        UMaterialInterface* Mat = MeshComp->GetMaterial(SectionIdx);
        if (!Mat)
        {
            OutMissingVisualMat++;
            bAllValid = false;
            OutErrors.Add(FString::Printf(TEXT("%s (Section %d): Missing visual material"), *OwnerLabel, SectionIdx));
            continue;
        }

        UPhysicalMaterial* PhysMat = Mat->GetPhysicalMaterial();
        UOSMPhysicalMaterial* OSMPhysMat = Cast<UOSMPhysicalMaterial>(PhysMat);

        if (!OSMPhysMat)
        {
            OutMissingPhysMat++;
            bAllValid = false;
            OutErrors.Add(FString::Printf(TEXT("%s (Section %d): Missing or invalid UOSMPhysicalMaterial on material %s"),
                *OwnerLabel, SectionIdx, *Mat->GetName()));
        }
    }

    return bAllValid;
}

FOSMMaterialValidator::FValidationResult FOSMMaterialValidator::ValidateWorld(UWorld* World)
{
    FValidationResult Result;
    if (!World)
    {
        Result.Errors.Add(TEXT("Cannot validate: World is null"));
        return Result;
    }

    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        if (!Actor || !Actor->Tags.Contains(FName(TEXT("OSMWorldGen.Generated"))))
        {
            continue;
        }

        TArray<UProceduralMeshComponent*> Comps;
        Actor->GetComponents<UProceduralMeshComponent>(Comps);

        for (UProceduralMeshComponent* Comp : Comps)
        {
            if (!Comp) continue;

            Result.TotalComponents++;
            int32 Sections = 0;
            int32 MissingPhys = 0;
            int32 MissingVis = 0;

            ValidateComponent(Comp, Result.Errors, Sections, MissingPhys, MissingVis);

            Result.TotalSections += Sections;
            Result.MissingPhysMat += MissingPhys;
            Result.MissingVisualMat += MissingVis;
        }
    }

    return Result;
}

FString FOSMMaterialValidator::FValidationResult::ToString() const
{
    return FString::Printf(TEXT("Validation: %d components, %d sections, %d missing PhysMat, %d missing VisualMat, %d errors"),
        TotalComponents, TotalSections, MissingPhysMat, MissingVisualMat, Errors.Num());
}
