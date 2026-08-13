// Copyright InviMind. All Rights Reserved.

#include "Fetch/FOSMOpenTopographyClient.h"
#include "Settings/UOSMWorldGenSettings.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

void FOSMOpenTopographyClient::FetchAsync(
    const FOSMRegion& Region,
    const FString& OutputFilePath,
    FOnFetchComplete OnComplete)
{
    if (!Region.IsValid())
    {
        OnComplete.ExecuteIfBound(false, TEXT("OpenTopography fetch: no valid region was supplied."));
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

    // Ask for a slightly larger box than the region so the raster still fully contains it after
    // OpenTopography snaps the request to its source grid. See FetchPaddingDegrees for why the
    // margin is fixed rather than proportional.
    //
    // The padding deliberately stays local to this URL: the caller's Region still defines the
    // region identity (and the cache key). Nothing downstream is affected, because the GeoTIFF
    // is self-describing — FOSMDEMSampler resolves lat/lon through the file's own GeoTransform,
    // so a padded raster maps the same coordinate to the same ground point, just at a different
    // pixel index.
    const FOSMRegion PaddedRegion = Region.Expanded(FetchPaddingDegrees);

    const double PaddedSouth = PaddedRegion.GetMinLat();
    const double PaddedNorth = PaddedRegion.GetMaxLat();
    const double PaddedWest  = PaddedRegion.GetMinLon();
    const double PaddedEast  = PaddedRegion.GetMaxLon();

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
