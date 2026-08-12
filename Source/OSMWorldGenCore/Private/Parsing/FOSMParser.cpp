// Copyright InviMind. All Rights Reserved.

#include "Parsing/FOSMParser.h"
#include "Parsing/FOSMXMLParser.h"
#include "Parsing/FOSMPBFParser.h"
#include "OSMWorldGenCore.h"

bool FOSMParser::Parse(
    const FString& FilePath,
    FOSMParseResult& OutResult,
    TFunction<void(float Percent, const FText& Status)> OnProgress,
    FThreadSafeBool* bCancel)
{
    if (FilePath.EndsWith(TEXT(".pbf")) || FilePath.EndsWith(TEXT(".osm.pbf")))
    {
        return FOSMPBFParser::Parse(FilePath, OutResult, OnProgress, bCancel);
    }
    else
    {
        return FOSMXMLParser::Parse(FilePath, OutResult, OnProgress, bCancel);
    }
}

bool FOSMParser::IsPBFSupported()
{
#if OSM_WITH_LIBOSMIUM
    return true;
#else
    return false;
#endif
}
