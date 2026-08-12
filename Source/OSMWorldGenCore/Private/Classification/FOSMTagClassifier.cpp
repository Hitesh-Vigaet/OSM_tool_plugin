// Copyright InviMind. All Rights Reserved.

#include "Classification/FOSMTagClassifier.h"
#include "Classification/FOSMClassificationRules.h"
#include "OSMWorldGenCore.h"

EOSMFeatureType FOSMTagClassifier::Classify(const TMap<FString, FString>& Tags, FString& OutSubType)
{
    OutSubType.Empty();
    const auto& Rules = FOSMClassificationRules::GetDefaultRules();

    for (const auto& Rule : Rules)
    {
        if (const FString* ValuePtr = Tags.Find(Rule.Key))
        {
            if (Rule.ValuePattern.IsEmpty() || *ValuePtr == Rule.ValuePattern)
            {
                OutSubType = *ValuePtr;
                return Rule.Type;
            }
        }
    }

    return EOSMFeatureType::Unknown;
}

FOSMComputedProperties FOSMTagClassifier::ResolveProperties(
    EOSMFeatureType Type,
    const FString& SubType,
    const TMap<FString, FString>& Tags)
{
    FOSMComputedProperties Props;

    if (Type == EOSMFeatureType::Building)
    {
        // 1. Check height tag
        if (const FString* HeightStr = Tags.Find(TEXT("height")))
        {
            Props.HeightMeters = FCString::Atof(**HeightStr);
            Props.bHeightFromTags = (Props.HeightMeters > 0.0f);
        }
        else if (const FString* BldgHeightStr = Tags.Find(TEXT("building:height")))
        {
            Props.HeightMeters = FCString::Atof(**BldgHeightStr);
            Props.bHeightFromTags = (Props.HeightMeters > 0.0f);
        }

        // 2. Check building:levels tag if height was not specified
        if (const FString* LevelsStr = Tags.Find(TEXT("building:levels")))
        {
            Props.Levels = FCString::Atoi(**LevelsStr);
            if (!Props.bHeightFromTags && Props.Levels > 0)
            {
                Props.HeightMeters = Props.Levels * 3.0f; // Default 3.0m per floor
                Props.bHeightFromTags = true;
            }
        }

        // 3. Fallback height defaults based on building subtype
        if (!Props.bHeightFromTags)
        {
            if (SubType == TEXT("house") || SubType == TEXT("detached")) Props.HeightMeters = 8.0f;
            else if (SubType == TEXT("apartments")) Props.HeightMeters = 18.0f;
            else if (SubType == TEXT("office")) Props.HeightMeters = 30.0f;
            else if (SubType == TEXT("commercial")) Props.HeightMeters = 15.0f;
            else if (SubType == TEXT("garage") || SubType == TEXT("shed")) Props.HeightMeters = 3.5f;
            else Props.HeightMeters = 9.0f; // Global default 9m
        }
    }
    else if (Type == EOSMFeatureType::Highway)
    {
        // 1. Width tag
        if (const FString* WidthStr = Tags.Find(TEXT("width")))
        {
            Props.WidthMeters = FCString::Atof(**WidthStr);
        }

        // 2. Lanes tag
        if (const FString* LanesStr = Tags.Find(TEXT("lanes")))
        {
            Props.LaneCount = FCString::Atoi(**LanesStr);
        }

        // 3. One-way tag
        if (const FString* OneWayStr = Tags.Find(TEXT("oneway")))
        {
            Props.bIsOneWay = (*OneWayStr == TEXT("yes") || *OneWayStr == TEXT("true") || *OneWayStr == TEXT("1"));
        }

        // 4. Fallback road width based on highway classification
        if (Props.WidthMeters <= 0.0f)
        {
            if (SubType == TEXT("motorway")) Props.WidthMeters = 14.0f;
            else if (SubType == TEXT("trunk")) Props.WidthMeters = 12.0f;
            else if (SubType == TEXT("primary")) Props.WidthMeters = 10.0f;
            else if (SubType == TEXT("secondary")) Props.WidthMeters = 8.0f;
            else if (SubType == TEXT("tertiary")) Props.WidthMeters = 7.0f;
            else if (SubType == TEXT("residential")) Props.WidthMeters = 6.0f;
            else if (SubType == TEXT("service")) Props.WidthMeters = 4.0f;
            else if (SubType == TEXT("footway") || SubType == TEXT("path")) Props.WidthMeters = 2.0f;
            else Props.WidthMeters = 6.0f;
        }
    }

    if (const FString* NamePtr = Tags.Find(TEXT("name")))
    {
        Props.Name = *NamePtr;
    }

    return Props;
}

bool FOSMTagClassifier::ClassifyAll(
    const FOSMParseResult& ParseResult,
    FOSMFeatureTable& OutTable,
    TFunction<void(float Percent, const FText& Status)> OnProgress,
    FThreadSafeBool* bCancel)
{
    OutTable.Reset();
    const int32 TotalWays = ParseResult.Ways.Num();
    int32 ProcessedCount = 0;

    if (OnProgress)
    {
        OnProgress(0.0f, NSLOCTEXT("OSM", "StartingClassification", "Classifying features..."));
    }

    for (const auto& Pair : ParseResult.Ways)
    {
        if (bCancel && *bCancel)
        {
            return false;
        }

        const FOSMWay& Way = Pair.Value;
        if (!Way.AreCoordinatesResolved())
        {
            continue;
        }

        FString SubType;
        EOSMFeatureType FeatureType = Classify(Way.Tags, SubType);
        if (FeatureType == EOSMFeatureType::Unknown)
        {
            continue; // Skip unclassified features
        }

        FOSMFeature Feature;
        Feature.OSMId = Way.Id;
        Feature.Type = FeatureType;
        Feature.SubType = SubType;
        Feature.Tags = Way.Tags;
        Feature.bIsArea = Way.bIsClosed;
        Feature.bFromRelation = false;

        // Populate geometry as FVector (lat, lon, 0)
        if (Feature.bIsArea)
        {
            TArray<FVector> Ring;
            Ring.Reserve(Way.ResolvedCoords.Num());
            for (const FVector2D& Coord : Way.ResolvedCoords)
            {
                Ring.Add(FVector(Coord.X, Coord.Y, 0.0));
            }
            Feature.Polygons.Add(MoveTemp(Ring));
        }
        else
        {
            Feature.Polyline.Reserve(Way.ResolvedCoords.Num());
            for (const FVector2D& Coord : Way.ResolvedCoords)
            {
                Feature.Polyline.Add(FVector(Coord.X, Coord.Y, 0.0));
            }
        }

        Feature.Computed = ResolveProperties(FeatureType, SubType, Way.Tags);
        Feature.Computed.CentroidLatLon = Way.ComputeCentroid();

        OutTable.AddFeature(MoveTemp(Feature));

        ProcessedCount++;
        if (OnProgress && ProcessedCount % 2000 == 0)
        {
            float Progress = static_cast<float>(ProcessedCount) / static_cast<float>(FMath::Max(1, TotalWays));
            OnProgress(Progress, FText::Format(NSLOCTEXT("OSM", "ClassifyingWays", "Classified {0} of {1} ways..."), ProcessedCount, TotalWays));
        }
    }

    if (OnProgress)
    {
        OnProgress(1.0f, NSLOCTEXT("OSM", "ClassificationComplete", "Feature classification complete!"));
    }

    UE_LOG(LogOSMWorldGen, Log, TEXT("Classification complete: %s"), *OutTable.GetSummary());
    return true;
}
