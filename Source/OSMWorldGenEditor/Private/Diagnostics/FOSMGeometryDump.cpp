// Copyright InviMind. All Rights Reserved.

#include "Diagnostics/FOSMGeometryDump.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FOSMGeometryDump::FOSMGeometryDump(const FString& InPath)
    : Path(InPath)
{
    if (IsEnabled())
    {
        // A city's worth of geometry is a few megabytes of text. Reserving it up front turns
        // thousands of reallocations into one.
        Buffer.Reserve(4 * 1024 * 1024);
    }
}

FString FOSMGeometryDump::DefaultPath(const FString& Stem)
{
    return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("OSMWorldGen"), TEXT("Diagnostics"),
                           Stem + TEXT(".jsonl"));
}

void FOSMGeometryDump::WriteHeader(double MinLat, double MaxLat, double MinLon, double MaxLon,
                                   const FString& Notes)
{
    if (!IsEnabled())
    {
        return;
    }

    Buffer += FString::Printf(
        TEXT("{\"kind\":\"header\",\"minLat\":%.7f,\"maxLat\":%.7f,\"minLon\":%.7f,\"maxLon\":%.7f,\"notes\":\"%s\"}\n"),
        MinLat, MaxLat, MinLon, MaxLon, *Notes.ReplaceCharWithEscapedChar());
}

void FOSMGeometryDump::Add(const TCHAR* Kind, const FString& Label, const FVector& WorldLocation,
                           const TArray<FVector>& Vertices, const TArray<int32>& Triangles)
{
    if (!IsEnabled())
    {
        return;
    }

    Buffer += FString::Printf(TEXT("{\"kind\":\"%s\",\"label\":\"%s\",\"loc\":[%.1f,%.1f,%.1f],\"v\":["),
                              Kind, *Label, WorldLocation.X, WorldLocation.Y, WorldLocation.Z);

    // Flat arrays rather than nested triples: on a full city this is several hundred thousand
    // vertices, and the brackets alone would add megabytes.
    for (int32 Index = 0; Index < Vertices.Num(); ++Index)
    {
        const FVector& V = Vertices[Index];
        Buffer += FString::Printf(TEXT("%s%.1f,%.1f,%.1f"), Index ? TEXT(",") : TEXT(""), V.X, V.Y, V.Z);
    }

    Buffer += TEXT("],\"t\":[");

    for (int32 Index = 0; Index < Triangles.Num(); ++Index)
    {
        Buffer += FString::Printf(TEXT("%s%d"), Index ? TEXT(",") : TEXT(""), Triangles[Index]);
    }

    Buffer += TEXT("]}\n");
    ++Count;
}

bool FOSMGeometryDump::Write(FString& OutError)
{
    if (!IsEnabled())
    {
        return true;
    }

    if (!FFileHelper::SaveStringToFile(Buffer, *Path))
    {
        OutError = FString::Printf(TEXT("Could not write geometry dump to '%s'."), *Path);
        return false;
    }

    return true;
}
