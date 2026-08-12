// Copyright InviMind. All Rights Reserved.

#include "Buildings/FOSMBuildingHeightResolver.h"
#include "Model/FOSMFeature.h"

// ---------------------------------------------------------------------------
FOSMBuildingHeightResolver::FResolutionResult FOSMBuildingHeightResolver::Resolve(
    const FOSMFeature& Feature,
    float DefaultFloorHeight,
    float GlobalDefault)
{
    FResolutionResult Result;

    // Priority 1: Explicit "height" tag
    if (const FString* HeightTag = Feature.Tags.Find(TEXT("height")))
    {
        float Parsed = 0.0f;
        if (TryParseHeightTag(*HeightTag, Parsed) && Parsed > 0.0f)
        {
            Result.HeightMeters = Parsed;
            Result.Source = FResolutionResult::ESource::ExplicitTag;
            return Result;
        }
    }

    // Priority 2: "building:height" tag
    if (const FString* BHeightTag = Feature.Tags.Find(TEXT("building:height")))
    {
        float Parsed = 0.0f;
        if (TryParseHeightTag(*BHeightTag, Parsed) && Parsed > 0.0f)
        {
            Result.HeightMeters = Parsed;
            Result.Source = FResolutionResult::ESource::ExplicitTag;
            return Result;
        }
    }

    // Priority 3: "building:levels" or "levels" tag
    for (const FString& LevelsKey : {TEXT("building:levels"), TEXT("levels")})
    {
        if (const FString* LevelsTag = Feature.Tags.Find(LevelsKey))
        {
            const int32 Levels = FCString::Atoi(**LevelsTag);
            if (Levels > 0)
            {
                Result.HeightMeters = static_cast<float>(Levels) * DefaultFloorHeight;
                Result.Source = FResolutionResult::ESource::LevelsTag;
                return Result;
            }
        }
    }

    // Priority 4: Subtype default
    const float SubtypeH = GetSubtypeDefaultHeight(Feature.SubType);
    if (SubtypeH > 0.0f)
    {
        Result.HeightMeters = SubtypeH;
        Result.Source = FResolutionResult::ESource::SubtypeDefault;
        return Result;
    }

    // Priority 5: Global fallback
    Result.HeightMeters = GlobalDefault;
    Result.Source = FResolutionResult::ESource::GlobalDefault;
    return Result;
}

// ---------------------------------------------------------------------------
bool FOSMBuildingHeightResolver::TryParseHeightTag(const FString& TagValue, float& OutMeters)
{
    // Strip whitespace
    FString Clean = TagValue.TrimStartAndEnd();

    if (Clean.IsEmpty()) return false;

    // Detect unit suffix
    bool bIsFeet = false;
    if (Clean.EndsWith(TEXT(" ft")) || Clean.EndsWith(TEXT("ft")))
    {
        bIsFeet = true;
        Clean = Clean.Replace(TEXT("ft"), TEXT("")).TrimStartAndEnd();
    }
    else if (Clean.EndsWith(TEXT(" m")) || Clean.EndsWith(TEXT("m")))
    {
        Clean = Clean.Replace(TEXT("m"), TEXT("")).TrimStartAndEnd();
    }

    // Handle compound feet-and-inches: e.g., "5'11"" → parse as feet only for simplicity
    if (Clean.Contains(TEXT("'")))
    {
        TArray<FString> Parts;
        Clean.ParseIntoArray(Parts, TEXT("'"));
        if (Parts.Num() >= 1)
        {
            OutMeters = FCString::Atof(*Parts[0]) * 0.3048f;
            if (Parts.Num() > 1)
            {
                FString InchStr = Parts[1].Replace(TEXT("\""), TEXT("")).TrimStartAndEnd();
                OutMeters += FCString::Atof(*InchStr) * 0.0254f;
            }
            return OutMeters > 0.0f;
        }
    }

    const float Raw = FCString::Atof(*Clean);
    if (Raw <= 0.0f && !FChar::IsDigit(Clean[0])) return false;

    OutMeters = bIsFeet ? (Raw * 0.3048f) : Raw;
    return true;
}

// ---------------------------------------------------------------------------
float FOSMBuildingHeightResolver::GetSubtypeDefaultHeight(const FString& BuildingSubtype)
{
    // A concise lookup for common building types
    static const TMap<FString, float> Defaults
    {
        // Residential
        { TEXT("house"),           7.0f  },
        { TEXT("detached"),        7.0f  },
        { TEXT("semidetached_house"), 7.0f },
        { TEXT("bungalow"),        3.5f  },
        { TEXT("cabin"),           4.0f  },
        { TEXT("terrace"),         9.0f  },
        { TEXT("apartments"),     15.0f  },
        { TEXT("residential"),    10.0f  },
        // Commercial
        { TEXT("commercial"),     12.0f  },
        { TEXT("retail"),          6.0f  },
        { TEXT("supermarket"),     8.0f  },
        { TEXT("shop"),            5.0f  },
        { TEXT("office"),         18.0f  },
        { TEXT("hotel"),          25.0f  },
        // Industrial
        { TEXT("industrial"),     10.0f  },
        { TEXT("warehouse"),       9.0f  },
        { TEXT("factory"),        12.0f  },
        { TEXT("shed"),            3.5f  },
        { TEXT("garage"),          3.0f  },
        { TEXT("garages"),         3.0f  },
        // Civic
        { TEXT("school"),          8.0f  },
        { TEXT("church"),         15.0f  },
        { TEXT("cathedral"),      35.0f  },
        { TEXT("mosque"),         20.0f  },
        { TEXT("hospital"),       20.0f  },
        { TEXT("public"),         10.0f  },
        { TEXT("civic"),          12.0f  },
        // Infrastructure
        { TEXT("barn"),            8.0f  },
        { TEXT("greenhouse"),      3.5f  },
        { TEXT("roof"),            3.0f  },
        { TEXT("carport"),         3.0f  },
        { TEXT("parking"),         9.0f  },
    };

    if (const float* Found = Defaults.Find(BuildingSubtype))
    {
        return *Found;
    }
    return 0.0f; // Not found
}
