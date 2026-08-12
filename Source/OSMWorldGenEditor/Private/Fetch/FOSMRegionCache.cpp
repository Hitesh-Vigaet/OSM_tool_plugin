// Copyright InviMind. All Rights Reserved.

#include "Fetch/FOSMRegionCache.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"

FString FOSMRegionCache::MakeBboxHash(const FBoundingBox& Bbox)
{
    // Round to ~1cm-equivalent precision (1e-7 deg) before hashing so floating-point
    // jitter from repeated map-drag selections of "the same" region doesn't miss the cache.
    const FString Key = FString::Printf(TEXT("%.7f,%.7f,%.7f,%.7f"),
        Bbox.MinLat, Bbox.MinLon, Bbox.MaxLat, Bbox.MaxLon);
    return FMD5::HashAnsiString(*Key);
}

FString FOSMRegionCache::GetCacheDir(const FBoundingBox& Bbox)
{
    return FPaths::ConvertRelativePathToFull(FPaths::Combine(
        FPaths::ProjectSavedDir(), TEXT("OSMWorldGen"), TEXT("RegionCache"), MakeBboxHash(Bbox)));
}

FString FOSMRegionCache::GetOSMFilePath(const FBoundingBox& Bbox)
{
    return FPaths::Combine(GetCacheDir(Bbox), TEXT("region.osm"));
}

FString FOSMRegionCache::GetDEMFilePath(const FBoundingBox& Bbox)
{
    return FPaths::Combine(GetCacheDir(Bbox), TEXT("dem.tif"));
}

bool FOSMRegionCache::HasCachedOSM(const FBoundingBox& Bbox)
{
    return FPlatformFileManager::Get().GetPlatformFile().FileExists(*GetOSMFilePath(Bbox));
}

bool FOSMRegionCache::HasCachedDEM(const FBoundingBox& Bbox)
{
    return FPlatformFileManager::Get().GetPlatformFile().FileExists(*GetDEMFilePath(Bbox));
}

void FOSMRegionCache::InvalidateCache(const FBoundingBox& Bbox)
{
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    const FString Dir = GetCacheDir(Bbox);
    if (PlatformFile.DirectoryExists(*Dir))
    {
        PlatformFile.DeleteDirectoryRecursively(*Dir);
    }
}
