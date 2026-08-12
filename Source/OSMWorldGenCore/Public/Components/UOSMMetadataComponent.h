// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Model/EOSMFeatureType.h"
#include "Model/FOSMTagDictionary.h"
#include "UOSMMetadataComponent.generated.h"

/**
 * UOSMMetadataComponent attaches raw OpenStreetMap tags and computed metadata
 * to generated actors (buildings, roads, water, etc.).
 *
 * Makes every generated actor queryable by OSM ID, feature type, sub-type, or tags.
 */
UCLASS(ClassGroup=(OSMWorldGen), meta=(BlueprintSpawnableComponent))
class OSMWORLDGENCORE_API UOSMMetadataComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UOSMMetadataComponent();

    /** Original OpenStreetMap element ID */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM")
    int64 OSMId = 0;

    /** Classified feature type */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM")
    EOSMFeatureType FeatureType = EOSMFeatureType::Unknown;

    /** Sub-type string (e.g. "residential", "motorway", "river") */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM")
    FString SubType;

    /** Complete original OSM tag dictionary */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM")
    TMap<FString, FString> Tags;

    /** Footprint centroid in WGS84 (Lat, Lon) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Spatial")
    FVector2D CentroidLatLon = FVector2D::ZeroVector;

    /** Footprint area in square meters (for polygons) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Spatial")
    float FootprintAreaSqm = 0.0f;

    /** Whether this actor has been replaced with an authored asset */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Replacement")
    bool bIsReplacedAsset = false;

    /** The rule name that matched this actor for replacement */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Replacement")
    FString MatchedReplacementRule;

    /** Check if a specific tag key exists */
    UFUNCTION(BlueprintCallable, Category = "OSM")
    bool HasTag(const FString& Key) const;

    /** Get tag value with fallback */
    UFUNCTION(BlueprintCallable, Category = "OSM")
    FString GetTag(const FString& Key, const FString& DefaultValue = "") const;
};
