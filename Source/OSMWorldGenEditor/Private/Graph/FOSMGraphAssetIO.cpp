// Copyright InviMind. All Rights Reserved.

#include "Graph/FOSMGraphAssetIO.h"
#include "Graph/UOSMCityGraph.h"
#include "Region/FOSMRegion.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

const TCHAR* FOSMGraphAssetIO::GetGraphPackageRoot()
{
    return TEXT("/Game/OSMWorldGen/Graphs");
}

// ---------------------------------------------------------------------------
FString FOSMGraphAssetIO::MakeAssetName(const FOSMRegion& Region)
{
    if (!Region.IsValid())
    {
        return TEXT("CityGraph_Invalid");
    }

    // Derived from the region's own centre, so re-importing the same region updates one asset
    // instead of leaving a trail of near-identical ones. '.' and '-' are not legal in package
    // names, hence the substitutions.
    FString Name = FString::Printf(TEXT("CityGraph_%.4f_%.4f"),
        Region.GetCenterLat(), Region.GetCenterLon());

    Name.ReplaceInline(TEXT("."), TEXT("p"));
    Name.ReplaceInline(TEXT("-"), TEXT("m"));
    return Name;
}

FString FOSMGraphAssetIO::MakePackagePath(const FOSMRegion& Region)
{
    return FString::Printf(TEXT("%s/%s"), GetGraphPackageRoot(), *MakeAssetName(Region));
}

// ---------------------------------------------------------------------------
UOSMCityGraph* FOSMGraphAssetIO::SaveGraph(UOSMCityGraph* SourceGraph, const FOSMRegion& Region, FString& OutError)
{
    if (!SourceGraph)
    {
        OutError = TEXT("No graph to save.");
        return nullptr;
    }

    if (!Region.IsValid())
    {
        OutError = TEXT("Cannot save a graph without a valid region — the asset name derives from it.");
        return nullptr;
    }

    const FString PackagePath = MakePackagePath(Region);
    const FString AssetName = MakeAssetName(Region);

    UPackage* Package = CreatePackage(*PackagePath);
    if (!Package)
    {
        OutError = FString::Printf(TEXT("Could not create package '%s'."), *PackagePath);
        return nullptr;
    }

    Package->FullyLoad();

    // Replace any existing asset in place rather than letting a suffix accumulate: re-importing
    // a region should refresh its graph, not leave CityGraph_2 beside a stale original for the
    // user to open the wrong one later.
    if (UOSMCityGraph* Existing = FindObject<UOSMCityGraph>(Package, *AssetName))
    {
        Existing->ClearFlags(RF_Public | RF_Standalone);
        Existing->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
    }

    UOSMCityGraph* Saved = DuplicateObject<UOSMCityGraph>(SourceGraph, Package, FName(*AssetName));
    if (!Saved)
    {
        OutError = TEXT("Failed to duplicate the graph into its package.");
        return nullptr;
    }

    Saved->SetFlags(RF_Public | RF_Standalone);
    FAssetRegistryModule::AssetCreated(Saved);
    Package->MarkPackageDirty();

    const FString FileName = FPackageName::LongPackageNameToFilename(
        PackagePath, FPackageName::GetAssetPackageExtension());

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags = SAVE_NoError;

    if (!UPackage::SavePackage(Package, Saved, *FileName, SaveArgs))
    {
        OutError = FString::Printf(TEXT("Failed to write '%s' to disk."), *FileName);
        return nullptr;
    }

    OutError.Empty();
    return Saved;
}

// ---------------------------------------------------------------------------
UOSMCityGraph* FOSMGraphAssetIO::LoadGraph(const FOSMRegion& Region)
{
    if (!Region.IsValid())
    {
        return nullptr;
    }

    const FString ObjectPath = FString::Printf(TEXT("%s.%s"),
        *MakePackagePath(Region), *MakeAssetName(Region));

    return LoadObject<UOSMCityGraph>(nullptr, *ObjectPath);
}
