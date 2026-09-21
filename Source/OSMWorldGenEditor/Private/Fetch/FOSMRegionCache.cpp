// Copyright InviMind. All Rights Reserved.

#include "Fetch/FOSMRegionCache.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
    const TCHAR* ManifestFileName = TEXT("region.json");
}

// ---------------------------------------------------------------------------
bool FOSMCacheManifest::SaveToFile(const FString& FilePath) const
{
    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();

    Root->SetNumberField(TEXT("version"), Version);
    Root->SetNumberField(TEXT("minLat"), MinLat);
    Root->SetNumberField(TEXT("maxLat"), MaxLat);
    Root->SetNumberField(TEXT("minLon"), MinLon);
    Root->SetNumberField(TEXT("maxLon"), MaxLon);
    Root->SetStringField(TEXT("overpassUrl"), OverpassUrl);
    Root->SetStringField(TEXT("demType"), DEMType);
    Root->SetNumberField(TEXT("demPaddingDegrees"), DEMPaddingDegrees);
    Root->SetStringField(TEXT("fetchedAtUtc"), FetchedAtUtc.ToIso8601());
    Root->SetNumberField(TEXT("osmFileSize"), static_cast<double>(OSMFileSize));
    Root->SetNumberField(TEXT("demFileSize"), static_cast<double>(DEMFileSize));
    Root->SetStringField(TEXT("lastValidationVerdict"), LastValidationVerdict);

    FString Output;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
    if (!FJsonSerializer::Serialize(Root, Writer))
    {
        return false;
    }

    // UTF-8 without BOM: the manifest sits beside region.osm, and this project has already been
    // bitten once by a cached file written as UTF-16 while declaring itself UTF-8.
    return FFileHelper::SaveStringToFile(
        Output, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

// ---------------------------------------------------------------------------
bool FOSMCacheManifest::LoadFromFile(const FString& FilePath, FOSMCacheManifest& OutManifest)
{
    OutManifest = FOSMCacheManifest();

    FString Contents;
    if (!FFileHelper::LoadFileToString(Contents, *FilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Contents);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        return false;
    }

    OutManifest.Version = Root->GetIntegerField(TEXT("version"));
    OutManifest.MinLat = Root->GetNumberField(TEXT("minLat"));
    OutManifest.MaxLat = Root->GetNumberField(TEXT("maxLat"));
    OutManifest.MinLon = Root->GetNumberField(TEXT("minLon"));
    OutManifest.MaxLon = Root->GetNumberField(TEXT("maxLon"));

    Root->TryGetStringField(TEXT("overpassUrl"), OutManifest.OverpassUrl);
    Root->TryGetStringField(TEXT("demType"), OutManifest.DEMType);
    Root->TryGetStringField(TEXT("lastValidationVerdict"), OutManifest.LastValidationVerdict);

    double Number = 0.0;
    if (Root->TryGetNumberField(TEXT("demPaddingDegrees"), Number)) OutManifest.DEMPaddingDegrees = Number;
    if (Root->TryGetNumberField(TEXT("osmFileSize"), Number))       OutManifest.OSMFileSize = static_cast<int64>(Number);
    if (Root->TryGetNumberField(TEXT("demFileSize"), Number))       OutManifest.DEMFileSize = static_cast<int64>(Number);

    FString Timestamp;
    if (Root->TryGetStringField(TEXT("fetchedAtUtc"), Timestamp))
    {
        FDateTime::ParseIso8601(*Timestamp, OutManifest.FetchedAtUtc);
    }

    return true;
}

// ---------------------------------------------------------------------------
bool FOSMCacheManifest::MatchesFetchTerms(const FString& InDEMType, double InDEMPaddingDegrees) const
{
    if (Version != CurrentVersion) return false;
    if (!DEMType.Equals(InDEMType, ESearchCase::IgnoreCase)) return false;

    // Padding is compared with a tolerance far below one DEM cell, so a trivial float
    // difference does not needlessly discard an otherwise usable cache entry.
    return FMath::Abs(DEMPaddingDegrees - InDEMPaddingDegrees) < 1e-9;
}

// ---------------------------------------------------------------------------
FString FOSMRegionCache::MakeRegionHash(const FOSMRegion& Region)
{
    // The region owns its own canonical key string, so the cache cannot drift from the
    // definition of region identity used everywhere else.
    return FMD5::HashAnsiString(*Region.ToCacheKeyString());
}

FString FOSMRegionCache::GetCacheDir(const FOSMRegion& Region)
{
    return FPaths::ConvertRelativePathToFull(FPaths::Combine(
        FPaths::ProjectSavedDir(), TEXT("OSMWorldGen"), TEXT("RegionCache"), MakeRegionHash(Region)));
}

FString FOSMRegionCache::GetOSMFilePath(const FOSMRegion& Region)
{
    return FPaths::Combine(GetCacheDir(Region), TEXT("region.osm"));
}

FString FOSMRegionCache::GetDEMFilePath(const FOSMRegion& Region)
{
    return FPaths::Combine(GetCacheDir(Region), TEXT("dem.tif"));
}

FString FOSMRegionCache::GetManifestFilePath(const FOSMRegion& Region)
{
    return FPaths::Combine(GetCacheDir(Region), ManifestFileName);
}

// ---------------------------------------------------------------------------
bool FOSMRegionCache::LoadManifest(const FOSMRegion& Region, FOSMCacheManifest& OutManifest)
{
    return FOSMCacheManifest::LoadFromFile(GetManifestFilePath(Region), OutManifest);
}

bool FOSMRegionCache::SaveManifest(const FOSMRegion& Region, const FOSMCacheManifest& Manifest)
{
    const FString Dir = GetCacheDir(Region);
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (!PlatformFile.DirectoryExists(*Dir))
    {
        PlatformFile.CreateDirectoryTree(*Dir);
    }
    return Manifest.SaveToFile(GetManifestFilePath(Region));
}

// ---------------------------------------------------------------------------
namespace
{
    /**
     * Shared reuse test. A cached file is only reusable when the file exists, a manifest
     * exists, the manifest was written under the current fetch terms, and the file is the size
     * the manifest recorded.
     */
    bool IsCachedFileReusable(
        const FString& FilePath,
        const FString& ManifestPath,
        const FString& DEMType,
        double DEMPaddingDegrees,
        bool bIsDEM)
    {
        if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*FilePath))
        {
            return false;
        }

        FOSMCacheManifest Manifest;
        if (!FOSMCacheManifest::LoadFromFile(ManifestPath, Manifest))
        {
            // No provenance record: treat as unusable rather than guessing. Re-fetching costs
            // one request; reusing a file of unknown origin costs an afternoon of debugging.
            return false;
        }

        if (!Manifest.MatchesFetchTerms(DEMType, DEMPaddingDegrees))
        {
            return false;
        }

        const int64 RecordedSize = bIsDEM ? Manifest.DEMFileSize : Manifest.OSMFileSize;
        if (RecordedSize <= 0)
        {
            return false;
        }

        return IFileManager::Get().FileSize(*FilePath) == RecordedSize;
    }
}

bool FOSMRegionCache::HasValidCachedOSM(const FOSMRegion& Region, const FString& DEMType, double DEMPaddingDegrees)
{
    return IsCachedFileReusable(
        GetOSMFilePath(Region), GetManifestFilePath(Region), DEMType, DEMPaddingDegrees, /*bIsDEM*/ false);
}

bool FOSMRegionCache::HasValidCachedDEM(const FOSMRegion& Region, const FString& DEMType, double DEMPaddingDegrees)
{
    return IsCachedFileReusable(
        GetDEMFilePath(Region), GetManifestFilePath(Region), DEMType, DEMPaddingDegrees, /*bIsDEM*/ true);
}

// ---------------------------------------------------------------------------
void FOSMRegionCache::InvalidateCache(const FOSMRegion& Region)
{
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    const FString Dir = GetCacheDir(Region);
    if (PlatformFile.DirectoryExists(*Dir))
    {
        PlatformFile.DeleteDirectoryRecursively(*Dir);
    }
}
