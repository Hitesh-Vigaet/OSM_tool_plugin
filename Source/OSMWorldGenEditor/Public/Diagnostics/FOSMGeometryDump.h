// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Writes the geometry the world builder actually emitted to a file, for offline inspection.
 *
 * The reason this exists: every defect so far — everything piled at the world origin, walls that
 * render as loose panels, a region that covers 2% of the city — was invisible to the build report,
 * which happily said "542 buildings" while the level was wreckage. Counts describe what was
 * attempted. This records what was produced, so it can be looked at.
 *
 * Deliberately dumps the FINAL vertices and triangles rather than the source data. A renderer fed
 * from the OSM file would confirm the OSM file is fine — which is already known, and is not where
 * the defects are.
 *
 * Format is JSON Lines: one self-contained object per line, header first. Chosen over a single
 * JSON document so the file streams, stays readable under `head`, and survives truncation if a
 * build crashes partway — a dump of a crashed build is exactly when this is most useful.
 *
 * Costs nothing when no path is set.
 */
class OSMWORLDGENEDITOR_API FOSMGeometryDump
{
public:
    /** An empty path disables the dump; every call then becomes a no-op. */
    explicit FOSMGeometryDump(const FString& InPath);

    bool IsEnabled() const { return !Path.IsEmpty(); }

    /** Record the region bounds and build settings. Call once, before any geometry. */
    void WriteHeader(double MinLat, double MaxLat, double MinLon, double MaxLon, const FString& Notes);

    /**
     * Record one emitted mesh.
     *
     * @param Kind            "terrain", "building", "road", "water", "vegetation", ...
     * @param WorldLocation   The actor's world transform in cm; vertices are local to it.
     * @param Vertices        Local vertices in cm, exactly as handed to the mesh component.
     * @param Triangles       Index triples, exactly as handed to the mesh component.
     */
    void Add(const TCHAR* Kind, const FString& Label, const FVector& WorldLocation,
             const TArray<FVector>& Vertices, const TArray<int32>& Triangles);

    /** Flush to disk. Returns false with OutError set on failure. */
    bool Write(FString& OutError);

    /** Number of meshes recorded. */
    int32 Num() const { return Count; }

    /** Default location for a dump, under the project's Saved directory. */
    static FString DefaultPath(const FString& Stem);

private:
    FString Path;
    FString Buffer;
    int32 Count = 0;
};
