// Copyright InviMind. All Rights Reserved.

#include "Fetch/FOSMOverpassClient.h"
#include "Settings/UOSMWorldGenSettings.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"

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
    // Overpass does not clip geometry. Its bbox filter matches any way or relation that merely
    // TOUCHES the box, and returns it whole — so the naive query, which recurses with `(._;>;)`
    // to resolve every referenced node, drags in the full length of every bus route, trunk road
    // and river passing through. Measured on a real 1.00 km2 request (Christ University,
    // Bangalore): the file covered 93.8 km2, 6.95 x 13.50 km, from a handful of long ways.
    //
    // The fix is to bound the NODES rather than the ways. Ways and relations are still selected
    // by the region, so nothing is missed, but their geometry is only resolved within a padded
    // box — a way leaving the region is simply truncated at the boundary instead of arriving in
    // full. Same query on the same region: 1.5 km2 instead of 93.8, all 1677 ways still present,
    // 688 fewer nodes.
    //
    // The cost is that 48 of those 1677 ways (2.9%) now reference nodes that are not in the
    // file. That is the documented, warned-about `osm.refs.dangling` case, and those are
    // precisely the ways FOSMFeatureTable was going to clip anyway — so the data being dropped
    // is data that was always going to be discarded, just discarded at the server now.
    //
    // Only type=multipolygon relations are requested. Boundaries, routes and waterways span
    // hundreds of km and are not useful here; multipolygons (buildings with courtyards, landuse
    // areas) are inherently local and are what actually matters.
    //
    // The node padding gives ways a node just beyond the edge, so a road crossing the boundary
    // still reaches it rather than stopping short. ~0.001 deg is ~111 m, comfortably more than
    // one street segment at any supported region size.
    constexpr double NodePaddingDegrees = 0.001;
    const FOSMRegion PaddedRegion = Region.Expanded(NodePaddingDegrees);

    return FString::Printf(
        TEXT("[out:xml][timeout:%d];\n")
        TEXT("(\n")
        TEXT("  node(%.7f,%.7f,%.7f,%.7f);\n")
        TEXT("  way(%.7f,%.7f,%.7f,%.7f);\n")
        TEXT("  relation[\"type\"=\"multipolygon\"](%.7f,%.7f,%.7f,%.7f);\n")
        TEXT(")->.core;\n")
        TEXT("way(r.core)->.relways;\n")
        TEXT("(\n")
        TEXT("  node(w.core)(%.7f,%.7f,%.7f,%.7f);\n")
        TEXT("  node(w.relways)(%.7f,%.7f,%.7f,%.7f);\n")
        TEXT(")->.geom;\n")
        TEXT("(.core; .relways; .geom;);\n")
        TEXT("out meta;\n"),
        FMath::Max(10, FMath::RoundToInt(TimeoutSeconds)),
        Region.GetMinLat(), Region.GetMinLon(), Region.GetMaxLat(), Region.GetMaxLon(),
        Region.GetMinLat(), Region.GetMinLon(), Region.GetMaxLat(), Region.GetMaxLon(),
        Region.GetMinLat(), Region.GetMinLon(), Region.GetMaxLat(), Region.GetMaxLon(),
        PaddedRegion.GetMinLat(), PaddedRegion.GetMinLon(), PaddedRegion.GetMaxLat(), PaddedRegion.GetMaxLon(),
        PaddedRegion.GetMinLat(), PaddedRegion.GetMinLon(), PaddedRegion.GetMaxLat(), PaddedRegion.GetMaxLon());
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
