// Copyright InviMind. All Rights Reserved.

#include "Fetch/FOSMNominatimClient.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "PlatformHttp.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

void FOSMNominatimClient::SearchAsync(const FString& Query, FOnGeocodeComplete OnComplete)
{
    if (Query.TrimStartAndEnd().IsEmpty())
    {
        OnComplete.ExecuteIfBound(false, TEXT("Enter a place name to search for."), FGeocodeResult());
        return;
    }

    const FString Url = FString::Printf(
        TEXT("https://nominatim.openstreetmap.org/search?q=%s&format=json&limit=1"),
        *FPlatformHttp::UrlEncode(Query));

    TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Url);
    Request->SetVerb(TEXT("GET"));
    // Nominatim's usage policy requires an identifying User-Agent on every request —
    // requests without one are liable to be blocked. See operations.osmfoundation.org/policies/nominatim.
    Request->SetHeader(TEXT("User-Agent"), TEXT("OSMWorldGenUnrealPlugin/1.0 (Unreal Engine editor plugin)"));
    Request->SetTimeout(20.0f);

    Request->OnProcessRequestComplete().BindLambda(
        [OnComplete](FHttpRequestPtr /*Req*/, FHttpResponsePtr Response, bool bConnectedSuccessfully)
        {
            if (!bConnectedSuccessfully || !Response.IsValid())
            {
                OnComplete.ExecuteIfBound(false, TEXT("Place search failed: no response (offline, DNS failure, or timeout)."), FGeocodeResult());
                return;
            }

            const int32 Code = Response->GetResponseCode();
            if (Code != 200)
            {
                OnComplete.ExecuteIfBound(false, FString::Printf(TEXT("Place search failed: HTTP %d."), Code), FGeocodeResult());
                return;
            }

            TArray<TSharedPtr<FJsonValue>> JsonArray;
            const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
            if (!FJsonSerializer::Deserialize(Reader, JsonArray) || JsonArray.Num() == 0)
            {
                OnComplete.ExecuteIfBound(false, TEXT("No results found for that place name. Try adding a city/country, or use the manual bbox entry below."), FGeocodeResult());
                return;
            }

            const TSharedPtr<FJsonObject> Obj = JsonArray[0]->AsObject();
            if (!Obj.IsValid())
            {
                OnComplete.ExecuteIfBound(false, TEXT("Unexpected response from the place search service."), FGeocodeResult());
                return;
            }

            const TArray<TSharedPtr<FJsonValue>>* BboxArray = nullptr;
            if (!Obj->TryGetArrayField(TEXT("boundingbox"), BboxArray) || BboxArray->Num() != 4)
            {
                OnComplete.ExecuteIfBound(false, TEXT("That result has no bounding box."), FGeocodeResult());
                return;
            }

            // Nominatim's boundingbox order is [south_lat, north_lat, west_lon, east_lon],
            // each encoded as a JSON string.
            FGeocodeResult Result;
            Result.MinLat = FCString::Atod(*(*BboxArray)[0]->AsString());
            Result.MaxLat = FCString::Atod(*(*BboxArray)[1]->AsString());
            Result.MinLon = FCString::Atod(*(*BboxArray)[2]->AsString());
            Result.MaxLon = FCString::Atod(*(*BboxArray)[3]->AsString());
            Obj->TryGetStringField(TEXT("display_name"), Result.DisplayName);

            OnComplete.ExecuteIfBound(true, FString(), Result);
        });

    Request->ProcessRequest();
}
