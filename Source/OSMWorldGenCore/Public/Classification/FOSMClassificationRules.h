// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Model/EOSMFeatureType.h"

/**
 * Single tag classification rule.
 */
struct FOSMClassificationRule
{
    FString Key;
    FString ValuePattern; // empty means match any value
    EOSMFeatureType Type;
    int32 Priority;
};

/**
 * Classification rule table and default values.
 */
class OSMWORLDGENCORE_API FOSMClassificationRules
{
public:
    /** Get default rules ordered by priority (higher priority evaluated first) */
    static const TArray<FOSMClassificationRule>& GetDefaultRules()
    {
        static TArray<FOSMClassificationRule> Rules = {
            { TEXT("building"), TEXT(""), EOSMFeatureType::Building, 100 },
            { TEXT("building:part"), TEXT(""), EOSMFeatureType::Building, 95 },
            { TEXT("highway"), TEXT(""), EOSMFeatureType::Highway, 90 },
            { TEXT("railway"), TEXT(""), EOSMFeatureType::Railway, 85 },
            { TEXT("natural"), TEXT("water"), EOSMFeatureType::WaterArea, 80 },
            { TEXT("waterway"), TEXT("riverbank"), EOSMFeatureType::WaterArea, 80 },
            { TEXT("waterway"), TEXT(""), EOSMFeatureType::Waterway, 75 },
            { TEXT("natural"), TEXT(""), EOSMFeatureType::NaturalArea, 70 },
            { TEXT("landuse"), TEXT(""), EOSMFeatureType::Landuse, 65 },
            { TEXT("leisure"), TEXT(""), EOSMFeatureType::Leisure, 60 },
            { TEXT("amenity"), TEXT(""), EOSMFeatureType::Amenity, 55 },
            { TEXT("barrier"), TEXT(""), EOSMFeatureType::Barrier, 50 },
            { TEXT("boundary"), TEXT("administrative"), EOSMFeatureType::Boundary, 45 },
            { TEXT("power"), TEXT(""), EOSMFeatureType::Power, 40 }
        };
        return Rules;
    }
};
