// Copyright InviMind. All Rights Reserved.

#include "Fetch/FOSMOpenTopographyClient.h"
#include "Settings/UOSMWorldGenSettings.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

void FOSMOpenTopographyClient::FetchAsync(
    const FOSMRegionCache::FBoundingBox& Bbox,
    const FString& OutputFilePath,
    FOnFetchComplete OnComplete)
{
    if (!Bbox.IsValid())
    {
        OnComplete.ExecuteIfBound(false, TEXT("OpenTopography fetch: invalid bounding box."));
        return;
    }

    const UOSMWorldGenSettings* Settings = GetDefault<UOSMWorldGenSettings>();
    if (Settings->OpenTopographyApiKey.IsEmpty())
    {
        OnComplete.ExecuteIfBound(false,
            TEXT("No OpenTopography API key configured (Project Settings > Plugins > OSM World Generator). ")
            TEXT("DEM auto-fetch skipped — falling back to manual .tif upload or flat terrain."));
        return;
    }

    // OpenTopography does not crop to arbitrary coordinates — it returns whole source-grid
    // cells, so the raster it hands back snaps to the DEM grid and can land a fraction of a
    // cell *inside* the requested box, leaving the region's south/east edges uncovered.
    //
    // Ask for a slightly larger box so the result always fully contains the region after
    // snapping. This is a fixed margin rather than a percentage because the shortfall is
    // caused by grid quantisation, so it is at most one cell regardless of region size — a
    // percentage would over-fetch badly on large regions to solve a ~90m problem.
    // 0.002 deg (~220m) exceeds one cell for any global DEM (SRTMGL1 ~30m, SRTMGL3 ~90m).
    //
    // The padding deliberately stays local to this URL: the caller's Bbox still defines the
    // region identity (and the cache filename). Nothing downstream is affected, because the
    // GeoTIFF is self-describing — FOSMDEMSampler resolves lat/lon through the file's own
    // GeoTransform, so a padded raster maps the same coordinate to the same ground point,
    // just at a different pixel index.
    constexpr double DEMFetchPaddingDegrees = 0.002;

    const double PaddedSouth = FMath::Max(Bbox.MinLat - DEMFetchPaddingDegrees, -90.0);
    const double PaddedNorth = FMath::Min(Bbox.MaxLat + DEMFetchPaddingDegrees,  90.0);
    const double PaddedWest  = FMath::Max(Bbox.MinLon - DEMFetchPaddingDegrees, -180.0);
    const double PaddedEast  = FMath::Min(Bbox.MaxLon + DEMFetchPaddingDegrees,  180.0);

    const FString URL = FString::Printf(
        TEXT("https://portal.opentopography.org/API/globaldem?demtype=%s&south=%.7f&north=%.7f&west=%.7f&east=%.7f&outputFormat=GTiff&API_Key=%s"),
        *Settings->OpenTopographyDemType,
        PaddedSouth, PaddedNorth, PaddedWest, PaddedEast,
        *Settings->OpenTopographyApiKey);

    TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(URL);
    Request->SetVerb(TEXT("GET"));
    Request->SetTimeout(Settings->RequestTimeoutSeconds);

    Request->OnProcessRequestComplete().BindLambda(
        [OutputFilePath, OnComplete](FHttpRequestPtr /*Req*/, FHttpResponsePtr Response, bool bConnectedSuccessfully)
        {
            if (!bConnectedSuccessfully || !Response.IsValid())
            {
                OnComplete.ExecuteIfBound(false, TEXT("OpenTopography request failed: no response (offline, DNS failure, or timeout)."));
                return;
            }

            const int32 Code = Response->GetResponseCode();
            if (Code != 200)
            {
                // OpenTopography returns a JSON/text error body (bad key, area too large, etc.) on failure.
                OnComplete.ExecuteIfBound(false, FString::Printf(
                    TEXT("OpenTopography request failed: HTTP %d. %s"),
                    Code, *Response->GetContentAsString().Left(500)));
                return;
            }

            const FString OutputDir = FPaths::GetPath(OutputFilePath);
            IFileManager::Get().MakeDirectory(*OutputDir, /*Tree=*/true);

            if (!FFileHelper::SaveArrayToFile(Response->GetContent(), *OutputFilePath))
            {
                OnComplete.ExecuteIfBound(false, FString::Printf(TEXT("Failed to write fetched DEM to '%s'."), *OutputFilePath));
                return;
            }

            OnComplete.ExecuteIfBound(true, FString());
        });

    Request->ProcessRequest();
}
