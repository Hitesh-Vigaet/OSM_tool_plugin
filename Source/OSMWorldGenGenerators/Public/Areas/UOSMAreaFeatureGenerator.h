// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Generators/FOSMGenerationContext.h"
#include "Model/EOSMFeatureType.h"
#include "UOSMAreaFeatureGenerator.generated.h"

class UMaterialInterface;
class UProceduralMeshComponent;
class FOSMDEMSampler;

/**
 * Renders every feature category that isn't a building or a road: water bodies, streams,
 * vegetation, parks, land use, railways, barriers, power lines.
 *
 * These were already parsed and classified — EOSMFeatureType has cases for all of them — but
 * no generator consumed them, so they were silently dropped and the scene only ever showed
 * buildings and roads.
 *
 * Grouping: one actor per category ("OSM_Water", "OSM_Vegetation", …), each holding a single
 * ProceduralMeshComponent with one mesh section per feature. A category is the unit people
 * actually want to toggle, colour, or hand to a downstream system; 150 individual actors is
 * only noise in the outliner. Buildings keep their own actor-per-feature layout for now
 * because they carry per-building metadata that a merged mesh would flatten away.
 */
UCLASS(BlueprintType)
class OSMWORLDGENGENERATORS_API UOSMAreaFeatureGenerator : public UObject
{
    GENERATED_BODY()

public:
    /** Vertical offset per category so coincident flat surfaces don't z-fight (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Areas")
    float BaseZOffsetCm = 4.0f;

    /** Width used for linear features that have no OSM width tag (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Areas")
    float DefaultLinearWidthCm = 250.0f;

    /** Generate all non-building, non-road features present in the feature table. */
    bool Generate(
        const FOSMGenerationContext& Context,
        TArray<AActor*>& OutActors);

    /** Flat display colour for a category — deliberately map-like, not photoreal. */
    static FLinearColor GetCategoryColor(EOSMFeatureType Type, const FString& SubType);

private:
    /** Builds one grouped actor holding every feature of a single category. */
    AActor* BuildCategoryActor(
        const FOSMGenerationContext& Context,
        EOSMFeatureType Type,
        const TArray<const FOSMFeature*>& Features,
        const FOSMDEMSampler& GroundSampler,
        float ZOffsetCm);
};
