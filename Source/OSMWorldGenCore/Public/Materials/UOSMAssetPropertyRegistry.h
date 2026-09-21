// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Materials/FOSMAssetMaterialDescriptor.h"
#include "UOSMAssetPropertyRegistry.generated.h"

/**
 * Registry of all known 3D asset types and their material descriptors.
 * Pre-populated with standard urban assets (water tank, solar panel, AC unit, etc.)
 */
UCLASS(BlueprintType)
class OSMWORLDGENCORE_API UOSMAssetPropertyRegistry : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assets")
    TArray<FOSMAssetMaterialDescriptor> Descriptors;

    /** Find a descriptor by asset name. */
    const FOSMAssetMaterialDescriptor* FindByName(const FString& AssetName) const;

    /** Create a default registry with standard urban asset definitions. */
    static UOSMAssetPropertyRegistry* CreateDefaultRegistry(UObject* Outer);
};
