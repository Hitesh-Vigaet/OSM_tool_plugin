// Copyright InviMind. All Rights Reserved.

#include "Materials/UOSMAssetPropertyRegistry.h"

const FOSMAssetMaterialDescriptor* UOSMAssetPropertyRegistry::FindByName(const FString& AssetName) const
{
    for (const FOSMAssetMaterialDescriptor& Desc : Descriptors)
    {
        if (Desc.AssetName.Equals(AssetName, ESearchCase::IgnoreCase))
        {
            return &Desc;
        }
    }
    return nullptr;
}

UOSMAssetPropertyRegistry* UOSMAssetPropertyRegistry::CreateDefaultRegistry(UObject* Outer)
{
    UOSMAssetPropertyRegistry* Registry = NewObject<UOSMAssetPropertyRegistry>(Outer);
    if (!Registry) return nullptr;

    // 1. Water Tank (Plastic)
    {
        FOSMAssetMaterialDescriptor TankPlastic;
        TankPlastic.AssetName = TEXT("WaterTank_Plastic");
        TankPlastic.SlotCategories.Add(0, EOSMSurfaceCategory::Plastic);
        Registry->Descriptors.Add(TankPlastic);
    }

    // 2. Water Tank (Metal)
    {
        FOSMAssetMaterialDescriptor TankMetal;
        TankMetal.AssetName = TEXT("WaterTank_Metal");
        TankMetal.SlotCategories.Add(0, EOSMSurfaceCategory::Metal);
        Registry->Descriptors.Add(TankMetal);
    }

    // 3. Solar Panel
    {
        FOSMAssetMaterialDescriptor Solar;
        Solar.AssetName = TEXT("SolarPanel");
        Solar.SlotCategories.Add(0, EOSMSurfaceCategory::Glass); // Face
        Solar.SlotCategories.Add(1, EOSMSurfaceCategory::Metal); // Frame
        Solar.SlotCategories.Add(2, EOSMSurfaceCategory::Metal); // Back
        Registry->Descriptors.Add(Solar);
    }

    // 4. AC Unit (Active thermal source)
    {
        FOSMAssetMaterialDescriptor AC;
        AC.AssetName = TEXT("AC_Unit");
        AC.SlotCategories.Add(0, EOSMSurfaceCategory::Metal); // Housing
        AC.SlotCategories.Add(1, EOSMSurfaceCategory::Metal); // Grille
        AC.bIsActiveThermalSource = true;
        AC.ActiveHeatOutputWatts = 4000.0f;
        AC.ActiveSourceTemperatureK = 333.15f; // ~60°C hot exhaust
        Registry->Descriptors.Add(AC);
    }

    // 5. Window (Recessed)
    {
        FOSMAssetMaterialDescriptor Window;
        Window.AssetName = TEXT("Window_Recessed");
        Window.SlotCategories.Add(0, EOSMSurfaceCategory::Metal); // Frame
        Window.SlotCategories.Add(1, EOSMSurfaceCategory::Glass); // Pane
        Registry->Descriptors.Add(Window);
    }

    // 6. Antenna / Dish
    {
        FOSMAssetMaterialDescriptor Antenna;
        Antenna.AssetName = TEXT("Antenna");
        Antenna.SlotCategories.Add(0, EOSMSurfaceCategory::Metal);
        Registry->Descriptors.Add(Antenna);
    }

    // 7. Street Lamp
    {
        FOSMAssetMaterialDescriptor Lamp;
        Lamp.AssetName = TEXT("StreetLamp");
        Lamp.SlotCategories.Add(0, EOSMSurfaceCategory::Metal); // Pole
        Lamp.SlotCategories.Add(1, EOSMSurfaceCategory::Glass); // Fixture
        Lamp.bIsActiveThermalSource = true;
        Lamp.ActiveHeatOutputWatts = 150.0f;
        Lamp.ActiveSourceTemperatureK = 373.15f; // ~100°C lamp surface
        Registry->Descriptors.Add(Lamp);
    }

    return Registry;
}
