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

    const FString URL = FString::Printf(
        TEXT("https://portal.opentopography.org/API/globaldem?demtype=%s&south=%.7f&north=%.7f&west=%.7f&east=%.7f&outputFormat=GTiff&API_Key=%s"),
        *Settings->OpenTopographyDemType,
        Bbox.MinLat, Bbox.MaxLat, Bbox.MinLon, Bbox.MaxLon,
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
