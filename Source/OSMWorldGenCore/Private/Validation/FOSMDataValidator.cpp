// Copyright InviMind. All Rights Reserved.

#include "Validation/FOSMDataValidator.h"
#include "Parsing/FOSMParser.h"
#include "Parsing/FOSMParseResult.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
    /** Bytes of the header we inspect for the structural gate. Enough for any XML prolog. */
    constexpr int64 HeaderProbeBytes = 4096;

    /** Smallest file that could conceivably be a valid OSM document. */
    constexpr int64 MinPlausibleFileBytes = 64;

    /**
     * Detects the UTF-16 case that bit us before: a file written as UTF-16 while its own XML
     * prolog declares UTF-8. Readers that trust the declaration produce garbage. We detect it
     * from the byte pattern rather than the declaration, because the bytes are the truth.
     */
    bool LooksLikeUTF16(const TArray<uint8>& Header)
    {
        if (Header.Num() >= 2)
        {
            const bool bBOM_LE = Header[0] == 0xFF && Header[1] == 0xFE;
            const bool bBOM_BE = Header[0] == 0xFE && Header[1] == 0xFF;
            if (bBOM_LE || bBOM_BE) return true;
        }

        // No BOM: ASCII text encoded as UTF-16 alternates real bytes with zeros. Sampling the
        // first stretch is sufficient and avoids misreading legitimately non-ASCII UTF-8.
        const int32 SampleCount = FMath::Min(Header.Num(), 64);
        if (SampleCount < 8) return false;

        int32 ZeroCount = 0;
        for (int32 Index = 0; Index < SampleCount; ++Index)
        {
            if (Header[Index] == 0) ++ZeroCount;
        }
        return ZeroCount >= SampleCount / 4;
    }
}

// ---------------------------------------------------------------------------
FOSMValidationResult FOSMDataValidator::ValidateFile(const FString& FilePath)
{
    FOSMValidationResult Result;

    if (FilePath.IsEmpty())
    {
        Result.AddFatal(TEXT("osm.path.empty"), TEXT("No .osm file path was provided."));
        return Result;
    }

    if (!IFileManager::Get().FileExists(*FilePath))
    {
        Result.AddFatal(TEXT("osm.file.missing"),
            FString::Printf(TEXT("File does not exist: '%s'."), *FilePath));
        return Result;
    }

    const int64 FileSize = IFileManager::Get().FileSize(*FilePath);

    if (FileSize <= 0)
    {
        Result.AddFatal(TEXT("osm.file.empty"),
            FString::Printf(TEXT("File is empty: '%s'."), *FilePath));
        return Result;
    }

    if (FileSize < MinPlausibleFileBytes)
    {
        Result.AddFatal(TEXT("osm.file.truncated"),
            FString::Printf(TEXT("File is only %lld bytes, too small to be a valid OSM document: '%s'."),
                FileSize, *FilePath));
        return Result;
    }

    // Read just the header — a fatal structural problem is visible in the first few hundred
    // bytes, and this gate must stay cheap enough to run before every parse.
    TArray<uint8> Header;
    if (!FFileHelper::LoadFileToArray(Header, *FilePath)
        || Header.Num() == 0)
    {
        Result.AddFatal(TEXT("osm.file.unreadable"),
            FString::Printf(TEXT("File could not be read: '%s'."), *FilePath));
        return Result;
    }

    const int32 ProbeLength = static_cast<int32>(FMath::Min<int64>(Header.Num(), HeaderProbeBytes));

    if (LooksLikeUTF16(Header))
    {
        Result.AddFatal(TEXT("osm.encoding.utf16"),
            FString::Printf(
                TEXT("File appears to be UTF-16 encoded: '%s'. OSM XML must be UTF-8 — a UTF-16 file ")
                TEXT("whose prolog declares UTF-8 parses into garbage rather than failing outright."),
                *FilePath));
        return Result;
    }

    // Convert the probe to a string for the structural checks. Truncating mid-multibyte is
    // harmless here: we only look for ASCII markers.
    FString HeaderText;
    {
        TArray<uint8> Probe(Header.GetData(), ProbeLength);
        Probe.Add(0);
        HeaderText = FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Probe.GetData())));
    }

    if (!HeaderText.Contains(TEXT("<osm")))
    {
        Result.AddFatal(TEXT("osm.root.missing"),
            FString::Printf(
                TEXT("No <osm> root element found in the first %d bytes of '%s' — this does not look ")
                TEXT("like an OSM XML document."),
                ProbeLength, *FilePath));
        return Result;
    }

    // A complete document ends with its closing root tag. Checking the tail is the cheapest
    // reliable truncation test: an interrupted download passes every header check.
    {
        const int64 TailBytes = FMath::Min<int64>(Header.Num(), 512);
        TArray<uint8> Tail(Header.GetData() + (Header.Num() - TailBytes), static_cast<int32>(TailBytes));
        Tail.Add(0);
        const FString TailText = FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Tail.GetData())));

        if (!TailText.Contains(TEXT("</osm>")))
        {
            Result.AddFatal(TEXT("osm.file.truncated"),
                FString::Printf(
                    TEXT("File does not end with </osm>: '%s'. The download was most likely interrupted."),
                    *FilePath));
            return Result;
        }
    }

    Result.AddInfo(TEXT("osm.file.ok"),
        FString::Printf(TEXT("Structure OK: %.2f MB."), static_cast<double>(FileSize) / (1024.0 * 1024.0)));

    return Result;
}

// ---------------------------------------------------------------------------
FOSMValidationResult FOSMDataValidator::ValidateParsed(
    const FOSMParseResult& Parsed,
    const FOSMRegion& RequestedRegion,
    FStats& OutStats)
{
    FOSMValidationResult Result;
    OutStats = FStats();

    OutStats.NodeCount = Parsed.Nodes.Num();
    OutStats.WayCount = Parsed.Ways.Num();
    OutStats.RelationCount = Parsed.Relations.Num();

    if (!RequestedRegion.IsValid())
    {
        Result.AddFatal(TEXT("osm.region.invalid"),
            TEXT("The requested region is invalid, so the data cannot be checked against it."));
        return Result;
    }

    if (OutStats.NodeCount == 0)
    {
        Result.AddFatal(TEXT("osm.nodes.none"),
            TEXT("File contains no <node> elements — there is no geometry to import."));
        return Result;
    }

    if (OutStats.WayCount == 0 && OutStats.RelationCount == 0)
    {
        Result.AddFatal(TEXT("osm.ways.none"),
            FString::Printf(
                TEXT("File contains %d nodes but no ways or relations — nothing that forms roads, ")
                TEXT("buildings, or areas."),
                OutStats.NodeCount));
        return Result;
    }

    // ---- Coordinate sanity, and the observed extent ----
    double ObservedMinLat = 90.0, ObservedMaxLat = -90.0;
    double ObservedMinLon = 180.0, ObservedMaxLon = -180.0;
    int32 InvalidCoordCount = 0;
    int64 FirstInvalidId = 0;

    for (const TPair<int64, FOSMNode>& Pair : Parsed.Nodes)
    {
        const FOSMNode& Node = Pair.Value;

        const bool bFinite = !FMath::IsNaN(Node.Latitude) && FMath::IsFinite(Node.Latitude)
                          && !FMath::IsNaN(Node.Longitude) && FMath::IsFinite(Node.Longitude);
        const bool bInRange = Node.Latitude >= -90.0 && Node.Latitude <= 90.0
                           && Node.Longitude >= -180.0 && Node.Longitude <= 180.0;

        if (!bFinite || !bInRange)
        {
            if (InvalidCoordCount == 0) FirstInvalidId = Node.Id;
            ++InvalidCoordCount;
            continue;
        }

        ObservedMinLat = FMath::Min(ObservedMinLat, Node.Latitude);
        ObservedMaxLat = FMath::Max(ObservedMaxLat, Node.Latitude);
        ObservedMinLon = FMath::Min(ObservedMinLon, Node.Longitude);
        ObservedMaxLon = FMath::Max(ObservedMaxLon, Node.Longitude);
    }

    if (InvalidCoordCount > 0)
    {
        Result.AddFatal(TEXT("osm.coords.invalid"),
            FString::Printf(
                TEXT("%d node(s) have coordinates outside WGS84 range or non-finite. ")
                TEXT("A single bad coordinate corrupts every derived bound."),
                InvalidCoordCount),
            FirstInvalidId);
        return Result;
    }

    // Observed extent is an observation about data, never an import region — hence the
    // deliberately separate factory that skips the size limits.
    FString BoundsError;
    if (!FOSMRegion::ObservedBounds(
            ObservedMinLat, ObservedMinLon, ObservedMaxLat, ObservedMaxLon,
            OutStats.DataBounds, BoundsError))
    {
        Result.AddFatal(TEXT("osm.bounds.degenerate"),
            FString::Printf(TEXT("Could not derive a bounding box from the file's geometry: %s"), *BoundsError));
        return Result;
    }

    // ---- Agreement with the requested region ----
    if (!RequestedRegion.Intersects(OutStats.DataBounds))
    {
        Result.AddFatal(TEXT("osm.bounds.disjoint"),
            FString::Printf(
                TEXT("The file's geometry (%s) does not overlap the requested region (%s) at all — ")
                TEXT("this is data for a different place."),
                *OutStats.DataBounds.ToString(), *RequestedRegion.ToString()));
        return Result;
    }

    const double RequestedArea = RequestedRegion.GetAreaSqKm();
    const double DataArea = OutStats.DataBounds.GetAreaSqKm();

    if (RequestedArea > 0.0 && DataArea > RequestedArea * SuspiciousAreaMultiple)
    {
        Result.AddWarning(TEXT("osm.bounds.oversized"),
            FString::Printf(
                TEXT("Data extends well beyond the requested region: %.2f km2 (%.1f x %.1f km) vs ")
                TEXT("%.2f km2 requested. Expected when a long way crosses the box; geometry will be ")
                TEXT("clipped to the region."),
                DataArea, OutStats.DataBounds.GetWidthKm(), OutStats.DataBounds.GetHeightKm(),
                RequestedArea));
    }

    // ---- Referential integrity ----
    for (const TPair<int64, FOSMWay>& Pair : Parsed.Ways)
    {
        const FOSMWay& Way = Pair.Value;
        int32 MissingHere = 0;

        for (int64 NodeRef : Way.NodeRefs)
        {
            if (!Parsed.Nodes.Contains(NodeRef)) ++MissingHere;
        }

        if (MissingHere > 0)
        {
            ++OutStats.WaysWithMissingNodes;
            OutStats.MissingNodeRefs += MissingHere;
        }
    }

    if (OutStats.WaysWithMissingNodes > 0)
    {
        // Warning, not fatal: Overpass legitimately returns ways whose nodes fall outside the
        // query box. Such ways still carry usable partial geometry.
        Result.AddWarning(TEXT("osm.refs.dangling"),
            FString::Printf(
                TEXT("%d of %d way(s) reference %d node(s) not present in the file. Usually ways ")
                TEXT("crossing the region boundary; their geometry will be incomplete."),
                OutStats.WaysWithMissingNodes, OutStats.WayCount, OutStats.MissingNodeRefs));
    }

    Result.AddInfo(TEXT("osm.parsed.ok"),
        FString::Printf(TEXT("%d nodes, %d ways, %d relations covering %s."),
            OutStats.NodeCount, OutStats.WayCount, OutStats.RelationCount,
            *OutStats.DataBounds.ToString()));

    return Result;
}

// ---------------------------------------------------------------------------
FOSMValidationResult FOSMDataValidator::ValidateAgainstRegion(
    const FString& FilePath,
    const FOSMRegion& RequestedRegion,
    FOSMParseResult& OutParsed,
    FStats& OutStats)
{
    OutStats = FStats();

    FOSMValidationResult Result = ValidateFile(FilePath);
    if (Result.HasFatal())
    {
        // Deliberately do not parse: a structurally broken file must never reach the parser.
        return Result;
    }

    OutParsed.Reset();
    if (!FOSMParser::Parse(FilePath, OutParsed))
    {
        Result.AddFatal(TEXT("osm.parse.failed"),
            FString::Printf(TEXT("Parser rejected '%s'. The file passed structural checks but its ")
                            TEXT("contents could not be read."), *FilePath));
        return Result;
    }

    OutParsed.ResolveWayCoordinates();

    FOSMValidationResult Semantic = ValidateParsed(OutParsed, RequestedRegion, OutStats);
    Result.Append(Semantic);

    return Result;
}
