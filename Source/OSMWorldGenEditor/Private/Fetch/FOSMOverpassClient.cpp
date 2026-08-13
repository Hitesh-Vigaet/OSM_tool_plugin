// Copyright InviMind. All Rights Reserved.

#include "Fetch/FOSMOverpassClient.h"
#include "Settings/UOSMWorldGenSettings.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFilemanager.h"

namespace
{
    // Independent public mirrors, each a separate cluster rather than another frontend of the
    // same one, tried in order. The free public instances are genuinely flaky — a 504 or a
    // 30 s connection timeout on one is routine and says nothing about the others — and a
    // failed fetch means the user gets an empty scene, so it's worth exhausting the list
    // before reporting failure rather than making them click Fetch again.
    static const TCHAR* GOverpassMirrors[] = {
        TEXT("https://overpass-api.de/api/interpreter"),
        TEXT("https://overpass.kumi.systems/api/interpreter"),
        TEXT("https://overpass.private.coffee/api/interpreter"),
        TEXT("https://overpass.osm.jp/api/interpreter"),
    };
    static constexpr int32 GNumOverpassMirrors = UE_ARRAY_COUNT(GOverpassMirrors);

    /** Set on each successful fetch; surfaced via GetLastSuccessfulEndpoint() for the manifest. */
    FString GLastSuccessfulEndpoint;

    void DoFetch(const FString& Query, const FString& Url, float TimeoutSeconds, const FString& OutputFilePath,
        FOSMOverpassClient::FOnFetchComplete OnComplete, int32 NextMirrorIndex);

    void TryNextMirror(const FString& Query, float TimeoutSeconds, const FString& OutputFilePath,
        FOSMOverpassClient::FOnFetchComplete OnComplete, int32 MirrorIndex, const FString& LastError)
    {
        if (MirrorIndex >= GNumOverpassMirrors)
        {
            OnComplete.ExecuteIfBound(false, FString::Printf(
                TEXT("All %d Overpass servers failed. Last error: %s"), GNumOverpassMirrors, *LastError));
            return;
        }

        UE_LOG(LogTemp, Log, TEXT("Overpass: trying server %d/%d (%s)"),
            MirrorIndex + 1, GNumOverpassMirrors, GOverpassMirrors[MirrorIndex]);

        DoFetch(Query, GOverpassMirrors[MirrorIndex], TimeoutSeconds, OutputFilePath, OnComplete, MirrorIndex + 1);
    }

    void DoFetch(const FString& Query, const FString& Url, float TimeoutSeconds, const FString& OutputFilePath,
        FOSMOverpassClient::FOnFetchComplete OnComplete, int32 NextMirrorIndex)
    {
        TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
        Request->SetURL(Url);
        Request->SetVerb(TEXT("POST"));
        Request->SetHeader(TEXT("Content-Type"), TEXT("text/plain; charset=utf-8"));
        Request->SetContentAsString(Query);
        Request->SetTimeout(TimeoutSeconds);

        Request->OnProcessRequestComplete().BindLambda(
            [Query, Url, TimeoutSeconds, OutputFilePath, OnComplete, NextMirrorIndex](FHttpRequestPtr /*Req*/, FHttpResponsePtr Response, bool bConnectedSuccessfully)
            {
                const bool bFailed = !bConnectedSuccessfully || !Response.IsValid() || Response->GetResponseCode() != 200;
                if (bFailed)
                {
                    FString ShortReason;
                    if (!bConnectedSuccessfully || !Response.IsValid())
                    {
                        ShortReason = TEXT("no response (offline, DNS failure, or timeout)");
                    }
                    else
                    {
                        const int32 Code = Response->GetResponseCode();
                        ShortReason = (Code == 504 || Code == 503)
                            ? FString::Printf(TEXT("server overloaded (HTTP %d)"), Code)
                            : FString::Printf(TEXT("HTTP %d"), Code);
                    }

                    UE_LOG(LogTemp, Warning, TEXT("Overpass: server failed (%s), falling through to next mirror"), *ShortReason);
                    TryNextMirror(Query, TimeoutSeconds, OutputFilePath, OnComplete, NextMirrorIndex, ShortReason);
                    return;
                }

                const FString OutputDir = FPaths::GetPath(OutputFilePath);
                IFileManager::Get().MakeDirectory(*OutputDir, /*Tree=*/true);

                // Force real UTF-8: FFileHelper's default AutoDetect encoding writes FString's
                // native UTF-16 representation with a BOM, mislabeling a file whose XML prolog
                // says encoding="UTF-8" (Overpass always sends UTF-8) and breaking any external
                // tool — a plain XML parser, a text editor — that trusts that declaration.
                if (!FFileHelper::SaveStringToFile(Response->GetContentAsString(), *OutputFilePath,
                    FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
                {
                    OnComplete.ExecuteIfBound(false, FString::Printf(TEXT("Failed to write fetched OSM data to '%s'."), *OutputFilePath));
                    return;
                }

                GLastSuccessfulEndpoint = Url;
                OnComplete.ExecuteIfBound(true, FString());
            });

        Request->ProcessRequest();
    }
}

FString FOSMOverpassClient::BuildQuery(const FOSMRegion& Region, float TimeoutSeconds)
{
    // Fetch every node/way/relation in the bbox, then recurse (`>`) to pull in nodes
    // referenced by ways/relations that cross the boundary — this produces a file
    // structurally equivalent to a manual bbox export, so FOSMXMLParser needs no changes.
    //
    // Only type=multipolygon relations are requested. Overpass's bbox filter on relation()
    // requires only that the relation's own bounding box OVERLAP the query — it does not
    // clip the relation — so any relation reaching into the box arrives complete. Boundaries
    // (state/district), routes (highways, bus lines) and waterways routinely span hundreds
    // of km, and one of them is enough to blow the computed extent out and mis-size the
    // terrain. Multipolygons are what actually matter here (buildings with courtyards,
    // landuse areas) and are inherently local. Downstream clipping in FOSMFeatureTable is
    // the real backstop; this just avoids downloading the junk in the first place.
    return FString::Printf(
        TEXT("[out:xml][timeout:%d];\n")
        TEXT("(\n")
        TEXT("  node(%.7f,%.7f,%.7f,%.7f);\n")
        TEXT("  way(%.7f,%.7f,%.7f,%.7f);\n")
        TEXT("  relation[\"type\"=\"multipolygon\"](%.7f,%.7f,%.7f,%.7f);\n")
        TEXT(");\n")
        TEXT("(._;>;);\n")
        TEXT("out meta;\n"),
        FMath::Max(10, FMath::RoundToInt(TimeoutSeconds)),
        Region.GetMinLat(), Region.GetMinLon(), Region.GetMaxLat(), Region.GetMaxLon(),
        Region.GetMinLat(), Region.GetMinLon(), Region.GetMaxLat(), Region.GetMaxLon(),
        Region.GetMinLat(), Region.GetMinLon(), Region.GetMaxLat(), Region.GetMaxLon());
}

void FOSMOverpassClient::FetchAsync(
    const FOSMRegion& Region,
    const FString& OutputFilePath,
    FOnFetchComplete OnComplete)
{
    if (!Region.IsValid())
    {
        OnComplete.ExecuteIfBound(false, TEXT("Overpass fetch: no valid region was supplied."));
        return;
    }

    const UOSMWorldGenSettings* Settings = GetDefault<UOSMWorldGenSettings>();

    // Public Overpass instances are slow under load; 30 s was not enough for a dense urban
    // bbox and produced connection timeouts rather than a real answer.
    const float TimeoutSeconds = FMath::Max(90.0f, Settings->RequestTimeoutSeconds);
    const FString Query = BuildQuery(Region, TimeoutSeconds);

    // Start at the configured URL, then work through the mirror list on failure.
    DoFetch(Query, Settings->OverpassApiUrl, TimeoutSeconds, OutputFilePath, OnComplete, /*NextMirrorIndex=*/0);
}

const FString& FOSMOverpassClient::GetLastSuccessfulEndpoint()
{
    return GLastSuccessfulEndpoint;
}
