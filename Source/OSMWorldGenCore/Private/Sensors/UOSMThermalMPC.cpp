// Copyright InviMind. All Rights Reserved.

#include "Sensors/UOSMThermalMPC.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Sensors/UOSMThermalStateComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Sensors/FOSMThermalSimulation.h"

const FString& UOSMThermalMPC::GetDefaultMPCPath()
{
    static const FString Path = TEXT("/OSMWorldGen/Materials/MPC_ThermalState.MPC_ThermalState");
    return Path;
}

FName UOSMThermalMPC::GetParameterNameForCategory(EOSMSurfaceCategory Category)
{
    switch (Category)
    {
    case EOSMSurfaceCategory::Concrete:    return FName(TEXT("Temp_Concrete"));
    case EOSMSurfaceCategory::Brick:       return FName(TEXT("Temp_Brick"));
    case EOSMSurfaceCategory::Stone:       return FName(TEXT("Temp_Stone"));
    case EOSMSurfaceCategory::Metal:       return FName(TEXT("Temp_Metal"));
    case EOSMSurfaceCategory::Glass:       return FName(TEXT("Temp_Glass"));
    case EOSMSurfaceCategory::Wood:        return FName(TEXT("Temp_Wood"));
    case EOSMSurfaceCategory::Plastic:     return FName(TEXT("Temp_Plastic"));
    case EOSMSurfaceCategory::Plaster:     return FName(TEXT("Temp_Plaster"));
    case EOSMSurfaceCategory::Asphalt:     return FName(TEXT("Temp_Asphalt"));
    case EOSMSurfaceCategory::Soil:        return FName(TEXT("Temp_Soil"));
    case EOSMSurfaceCategory::Grass:       return FName(TEXT("Temp_Grass"));
    case EOSMSurfaceCategory::Vegetation:  return FName(TEXT("Temp_Vegetation"));
    case EOSMSurfaceCategory::Water:       return FName(TEXT("Temp_Water"));
    case EOSMSurfaceCategory::CementBlock: return FName(TEXT("Temp_CementBlock"));
    case EOSMSurfaceCategory::RoofTile:    return FName(TEXT("Temp_RoofTile"));
    case EOSMSurfaceCategory::Unknown:
    default:                               return FName(TEXT("Temp_Unknown"));
    }
}

UMaterialParameterCollection* UOSMThermalMPC::GetOrCreateMPC(UObject* Outer)
{
    UMaterialParameterCollection* Existing = LoadObject<UMaterialParameterCollection>(nullptr, *GetDefaultMPCPath());
    if (Existing)
    {
        return Existing;
    }

    UObject* UseOuter = Outer ? Outer : GetTransientPackage();
    UMaterialParameterCollection* MPC = NewObject<UMaterialParameterCollection>(UseOuter, TEXT("MPC_ThermalState"));
    if (!MPC)
    {
        return nullptr;
    }

    // Populate the 16 category scalar parameters
    for (uint8 CatIdx = 0; CatIdx < static_cast<uint8>(EOSMSurfaceCategory::MAX); ++CatIdx)
    {
        const EOSMSurfaceCategory Cat = static_cast<EOSMSurfaceCategory>(CatIdx);
        FCollectionScalarParameter Param;
        Param.Id = FGuid::NewGuid();
        Param.ParameterName = GetParameterNameForCategory(Cat);
        Param.DefaultValue = 293.15f;
        MPC->ScalarParameters.Add(Param);
    }

    // Add SunDirection vector parameter
    FCollectionVectorParameter SunDirParam;
    SunDirParam.Id = FGuid::NewGuid();
    SunDirParam.ParameterName = FName(TEXT("SunDirection"));
    SunDirParam.DefaultValue = FLinearColor(0.0f, 0.0f, -1.0f, 0.0f);
    MPC->VectorParameters.Add(SunDirParam);

    return MPC;
}

void UOSMThermalMPC::SetCategoryTemperature(
    UWorld* World,
    UMaterialParameterCollection* MPC,
    EOSMSurfaceCategory Category,
    float TemperatureK)
{
    if (!World) return;

    UMaterialParameterCollection* TargetMPC = MPC ? MPC : GetOrCreateMPC(World);
    if (!TargetMPC) return;

    UMaterialParameterCollectionInstance* Inst = World->GetParameterCollectionInstance(TargetMPC);
    if (Inst)
    {
        Inst->SetScalarParameterValue(GetParameterNameForCategory(Category), TemperatureK);
    }
}

void UOSMThermalMPC::UpdateFromWorld(UWorld* World, UMaterialParameterCollection* MPC)
{
    if (!World) return;

    UMaterialParameterCollection* TargetMPC = MPC ? MPC : GetOrCreateMPC(World);
    if (!TargetMPC) return;

    UMaterialParameterCollectionInstance* Inst = World->GetParameterCollectionInstance(TargetMPC);
    if (!Inst) return;

    // Track total temperature and count per category to compute average
    float TempSum[16] = {0};
    int32 TempCount[16] = {0};

    for (TActorIterator<AActor> It(World); It; ++It)
    {
        UOSMThermalStateComponent* ThermalComp = It->FindComponentByClass<UOSMThermalStateComponent>();
        if (!ThermalComp) continue;

        for (const FOSMThermalZone& Zone : ThermalComp->Zones)
        {
            const int32 CatIdx = FMath::Clamp(static_cast<int32>(Zone.Category), 0, 15);
            TempSum[CatIdx] += Zone.CurrentTemperatureK;
            TempCount[CatIdx]++;
        }
    }

    // Push computed category averages to MPC
    for (int32 i = 0; i < 16; ++i)
    {
        const EOSMSurfaceCategory Cat = static_cast<EOSMSurfaceCategory>(i);
        const float AvgTemp = TempCount[i] > 0 ? (TempSum[i] / TempCount[i]) : 293.15f;
        Inst->SetScalarParameterValue(GetParameterNameForCategory(Cat), AvgTemp);
    }
    
    // Set Sun Direction from Simulation Global Params
    const FVector& SunDir = FOSMThermalSimulation::GlobalParams.SunDirection;
    Inst->SetVectorParameterValue(FName(TEXT("SunDirection")), FLinearColor(SunDir.X, SunDir.Y, SunDir.Z, 0.0f));
}
