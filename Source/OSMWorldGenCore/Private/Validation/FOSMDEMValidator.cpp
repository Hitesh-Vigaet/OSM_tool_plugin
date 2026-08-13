// Copyright InviMind. All Rights Reserved.

#include "Validation/FOSMDEMValidator.h"
#include "Elevation/FOSMGeoTIFFReader.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
    /** Human name for a TIFF compression code, so rejections say what the file actually is. */
    FString DescribeCompression(uint16 Code)
    {
        switch (Code)
        {
        case 1:     return TEXT("1 (none)");
        case 2:     return TEXT("2 (CCITT modified Huffman)");
        case 3:     return TEXT("3 (CCITT Group 3 fax)");
        case 4:     return TEXT("4 (CCITT Group 4 fax)");
        case 5:     return TEXT("5 (LZW)");
        case 6:     return TEXT("6 (old-style JPEG)");
        case 7:     return TEXT("7 (JPEG)");
        case 8:     return TEXT("8 (Deflate, Adobe)");
        case 32773: return TEXT("32773 (PackBits)");
        case 32946: return TEXT("32946 (Deflate)");
        case 34887: return TEXT("34887 (LERC)");
        case 50000: return TEXT("50000 (Zstd)");
        case 50001: return TEXT("50001 (WebP)");
        default:    return FString::Printf(TEXT("%u (unrecognised)"), Code);
        }
    }

    FString DescribeSampleFormat(uint16 Code)
    {
        switch (Code)
        {
        case 1:  return TEXT("1 (unsigned integer)");
        case 2:  return TEXT("2 (signed integer)");
        case 3:  return TEXT("3 (IEEE float)");
        case 4:  return TEXT("4 (undefined)");
        default: return FString::Printf(TEXT("%u (unrecognised)"), Code);
        }
    }

    bool IsSupportedCompression(uint16 Code)
    {
        return Code == 1 || Code == 5 || Code == 8 || Code == 32946;
    }
}

// ---------------------------------------------------------------------------
FOSMValidationResult FOSMDEMValidator::ValidateFile(const FString& FilePath)
{
    FOSMValidationResult Result;

    if (FilePath.IsEmpty())
    {
        Result.AddFatal(TEXT("tif.path.empty"), TEXT("No elevation file path was provided."));
        return Result;
    }

    if (!IFileManager::Get().FileExists(*FilePath))
    {
        Result.AddFatal(TEXT("tif.file.missing"),
            FString::Printf(TEXT("File does not exist: '%s'."), *FilePath));
        return Result;
    }

    const int64 FileSize = IFileManager::Get().FileSize(*FilePath);
    if (FileSize <= 8)
    {
        Result.AddFatal(TEXT("tif.file.empty"),
            FString::Printf(TEXT("File is %lld bytes — too small to be a TIFF: '%s'."), FileSize, *FilePath));
        return Result;
    }

    TArray<uint8> Data;
    if (!FFileHelper::LoadFileToArray(Data, *FilePath))
    {
        Result.AddFatal(TEXT("tif.file.unreadable"),
            FString::Printf(TEXT("File could not be read: '%s'."), *FilePath));
        return Result;
    }

    // Header-only parse, shared with the loader — no pixels are decoded here.
    FOSMGeoTIFFHeader Header;
    FString HeaderError;
    if (!FOSMGeoTIFFReader::ReadHeader(Data, FilePath, Header, HeaderError))
    {
        Result.AddFatal(TEXT("tif.header.invalid"),
            FString::Printf(TEXT("TIFF header could not be parsed: %s."), *HeaderError));
        return Result;
    }

    // ---- Format ----
    if (Header.BitsPerSample != 16 && Header.BitsPerSample != 32)
    {
        Result.AddFatal(TEXT("tif.bitdepth.unsupported"),
            FString::Printf(
                TEXT("Unsupported bit depth: %u bits per sample. Elevation rasters must be 16-bit ")
                TEXT("integer or 32-bit float."),
                Header.BitsPerSample));
    }

    if (Header.SampleFormat != 1 && Header.SampleFormat != 2 && Header.SampleFormat != 3)
    {
        Result.AddFatal(TEXT("tif.sampleformat.unsupported"),
            FString::Printf(TEXT("Unsupported sample format: %s."), *DescribeSampleFormat(Header.SampleFormat)));
    }

    if (Header.SampleFormat == 3 && Header.BitsPerSample != 32)
    {
        Result.AddFatal(TEXT("tif.sampleformat.mismatch"),
            FString::Printf(
                TEXT("File declares IEEE float samples but %u bits per sample; float rasters must be 32-bit."),
                Header.BitsPerSample));
    }

    if (Header.SamplesPerPixel > 1)
    {
        Result.AddWarning(TEXT("tif.bands.multiple"),
            FString::Printf(
                TEXT("File has %u samples per pixel; only band 0 will be read as elevation."),
                Header.SamplesPerPixel));
    }

    if (!IsSupportedCompression(Header.Compression))
    {
        Result.AddFatal(TEXT("tif.compression.unsupported"),
            FString::Printf(
                TEXT("Unsupported compression: %s. This reader handles none, LZW, and Deflate. ")
                TEXT("Re-export the DEM with one of those, or request GTiff from OpenTopography."),
                *DescribeCompression(Header.Compression)));
    }

    // ---- Layout ----
    if (Header.IsTiled())
    {
        const int32 TileCount = Header.GetTileCount();
        if (TileCount > 1)
        {
            // The reader decodes the first tile only. A multi-tile file would silently yield
            // elevation for one corner of the region and garbage everywhere else — far worse
            // than refusing it.
            Result.AddFatal(TEXT("tif.layout.multitile"),
                FString::Printf(
                    TEXT("Raster is split into %d tiles (%ux%u each over a %dx%d image). Only ")
                    TEXT("single-tile and striped layouts are supported."),
                    TileCount, Header.TileWidth, Header.TileLength, Header.Width, Header.Height));
        }
    }
    else if (Header.StripOffset == 0)
    {
        Result.AddFatal(TEXT("tif.layout.nooffsets"),
            TEXT("File has neither strip offsets nor tile offsets — there is no way to locate its raster data."));
    }

    // ---- Georeferencing ----
    if (!Header.HasGeoreferencing())
    {
        // Without both tags the reader falls back to a guessed 1-arcsec origin, which places
        // the terrain in the wrong part of the world. Rejecting is the only safe outcome.
        const TCHAR* Missing =
            (!Header.bHasModelPixelScale && !Header.bHasModelTiepoint) ? TEXT("ModelPixelScale and ModelTiepoint")
            : (!Header.bHasModelPixelScale ? TEXT("ModelPixelScale") : TEXT("ModelTiepoint"));

        Result.AddFatal(TEXT("tif.geotags.missing"),
            FString::Printf(
                TEXT("File is missing the %s tag(s), so it carries no georeferencing. An elevation ")
                TEXT("raster without geo-tags cannot be aligned to the OSM data."),
                Missing));
    }
    else
    {
        const double ScaleX = FMath::Abs(Header.PixelScaleX);
        const double ScaleY = FMath::Abs(Header.PixelScaleY);

        if (ScaleX < MinPlausiblePixelScaleDeg || ScaleX > MaxPlausiblePixelScaleDeg
            || ScaleY < MinPlausiblePixelScaleDeg || ScaleY > MaxPlausiblePixelScaleDeg)
        {
            // A pixel scale in the hundreds or thousands means the file is in projected metres
            // (UTM and friends), not degrees. Reprojection is out of scope, so this is fatal
            // rather than a warning — the alternative is terrain placed a continent away.
            Result.AddFatal(TEXT("tif.pixelscale.implausible"),
                FString::Printf(
                    TEXT("Pixel scale (%.8g, %.8g) is outside the plausible range %g - %g degrees. ")
                    TEXT("The file is most likely in a projected CRS (metres) rather than WGS84 ")
                    TEXT("degrees; only EPSG:4326 is supported."),
                    ScaleX, ScaleY, MinPlausiblePixelScaleDeg, MaxPlausiblePixelScaleDeg));
        }

        const double OriginLon = Header.TiepointLon - Header.TiepointX * Header.PixelScaleX;
        const double OriginLat = Header.TiepointLat + Header.TiepointY * Header.PixelScaleY;

        if (OriginLat < -90.0 || OriginLat > 90.0 || OriginLon < -180.0 || OriginLon > 180.0)
        {
            Result.AddFatal(TEXT("tif.origin.outofrange"),
                FString::Printf(
                    TEXT("Raster origin (%.6f, %.6f) is not a valid WGS84 coordinate, so the file is ")
                    TEXT("not in EPSG:4326."),
                    OriginLat, OriginLon));
        }
    }

    if (!Result.HasFatal())
    {
        Result.AddInfo(TEXT("tif.header.ok"),
            FString::Printf(TEXT("Header OK: %dx%d, %u-bit, compression %s, %s layout."),
                Header.Width, Header.Height, Header.BitsPerSample,
                *DescribeCompression(Header.Compression),
                Header.IsTiled() ? TEXT("single-tile") : TEXT("striped")));
    }

    return Result;
}

// ---------------------------------------------------------------------------
FOSMValidationResult FOSMDEMValidator::ValidateLoaded(
    const FString& FilePath,
    FOSMGeoTIFFTile& OutTile,
    TArray<float>& OutHeightData,
    FStats& OutStats)
{
    OutStats = FStats();

    FOSMValidationResult Result = ValidateFile(FilePath);
    if (Result.HasFatal())
    {
        // A file rejected at header level must never be handed to the decoder — that is the
        // path that previously turned a malformed raster into an editor crash.
        return Result;
    }

    if (!FOSMGeoTIFFReader::Load(FilePath, OutTile, OutHeightData))
    {
        Result.AddFatal(TEXT("tif.load.failed"),
            FString::Printf(
                TEXT("File passed header checks but its raster data could not be decoded: '%s'. ")
                TEXT("The pixel data is most likely truncated or corrupt."),
                *FilePath));
        return Result;
    }

    const int64 ExpectedPixels = static_cast<int64>(OutTile.Width) * static_cast<int64>(OutTile.Height);
    if (OutTile.Width <= 0 || OutTile.Height <= 0 || OutHeightData.Num() != ExpectedPixels)
    {
        Result.AddFatal(TEXT("tif.raster.incomplete"),
            FString::Printf(
                TEXT("Decoded %d samples for a %dx%d raster (expected %lld) — the file is truncated."),
                OutHeightData.Num(), OutTile.Width, OutTile.Height, ExpectedPixels));
        return Result;
    }

    OutStats.Width = OutTile.Width;
    OutStats.Height = OutTile.Height;
    OutStats.ResolutionArcSeconds = OutTile.GetResolutionArcSeconds();

    FString BoundsError;
    if (!FOSMRegion::ObservedBounds(
            OutTile.GetMinLat(), OutTile.GetMinLon(), OutTile.GetMaxLat(), OutTile.GetMaxLon(),
            OutStats.DataBounds, BoundsError))
    {
        Result.AddFatal(TEXT("tif.bounds.degenerate"),
            FString::Printf(TEXT("Raster does not describe a valid geographic extent: %s"), *BoundsError));
        return Result;
    }

    // Ground sample distance at the raster's centre latitude, which is what a user can judge
    // "is this detailed enough" against.
    OutStats.ResolutionMeters =
        (OutStats.ResolutionArcSeconds / 3600.0)
        * OSMRegionLimits::KmPerDegreeLat * 1000.0
        * FMath::Cos(FMath::DegreesToRadians(OutStats.DataBounds.GetCenterLat()));

    // ---- Elevation statistics, ignoring NoData ----
    int64 NoDataCount = 0;
    float MinElev = TNumericLimits<float>::Max();
    float MaxElev = TNumericLimits<float>::Lowest();
    int64 ValidCount = 0;

    for (float Sample : OutHeightData)
    {
        const bool bIsNoData = OutTile.bHasNoData
            && FMath::Abs(static_cast<double>(Sample) - OutTile.NoDataValue) < 0.5;

        if (bIsNoData || FMath::IsNaN(Sample) || !FMath::IsFinite(Sample))
        {
            ++NoDataCount;
            continue;
        }

        MinElev = FMath::Min(MinElev, Sample);
        MaxElev = FMath::Max(MaxElev, Sample);
        ++ValidCount;
    }

    OutStats.NoDataFraction = ExpectedPixels > 0
        ? static_cast<double>(NoDataCount) / static_cast<double>(ExpectedPixels)
        : 1.0;

    if (ValidCount == 0)
    {
        Result.AddFatal(TEXT("tif.elevation.allnodata"),
            TEXT("Every pixel is NoData — the raster contains no elevation at all."));
        return Result;
    }

    OutStats.MinElevation = MinElev;
    OutStats.MaxElevation = MaxElev;

    if (MinElev < MinPlausibleElevationM || MaxElev > MaxPlausibleElevationM)
    {
        Result.AddWarning(TEXT("tif.elevation.implausible"),
            FString::Printf(
                TEXT("Elevation range %.1f to %.1f m falls outside the plausible %.0f to %.0f m. ")
                TEXT("The units may not be metres, or NoData may not be declared correctly."),
                MinElev, MaxElev, MinPlausibleElevationM, MaxPlausibleElevationM));
    }

    if (OutStats.NoDataFraction > MaxNoDataFraction)
    {
        Result.AddWarning(TEXT("tif.nodata.excessive"),
            FString::Printf(
                TEXT("%.0f%% of pixels are NoData. Terrain over those areas will be interpolated ")
                TEXT("from the few valid neighbours."),
                OutStats.NoDataFraction * 100.0));
    }

    Result.AddInfo(TEXT("tif.loaded.ok"),
        FString::Printf(TEXT("%dx%d, %.1f m/px, elevation %.1f to %.1f m over %s."),
            OutStats.Width, OutStats.Height, OutStats.ResolutionMeters,
            OutStats.MinElevation, OutStats.MaxElevation, *OutStats.DataBounds.ToString()));

    return Result;
}
