// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Resolves a free-text place name (e.g. "Jakkur Lake, Bangalore") to a bounding box via
 * OpenStreetMap's Nominatim geocoding API — no browser round-trip, no copy/paste, no API
 * key. This is the low-friction alternative to manually drawing a box on openstreetmap.org
 * and transcribing coordinates; the manual bbox fields remain for cases Nominatim can't
 * resolve to a sensible area (an arbitrary empty patch of countryside, a custom crop).
 */
class OSMWORLDGENEDITOR_API FOSMNominatimClient
{
public:
    struct FGeocodeResult
    {
        double MinLat = 0.0, MaxLat = 0.0;
        double MinLon = 0.0, MaxLon = 0.0;
        FString DisplayName;
    };

    /** @param bSuccess false on network/HTTP/parse failure or no match; ErrorMessage is human-readable. */
    DECLARE_DELEGATE_ThreeParams(FOnGeocodeComplete, bool /*bSuccess*/, const FString& /*ErrorMessage*/, const FGeocodeResult& /*Result*/);

    /** Async — OnComplete fires on the game thread once the HTTP request resolves. */
    static void SearchAsync(const FString& Query, FOnGeocodeComplete OnComplete);
};
