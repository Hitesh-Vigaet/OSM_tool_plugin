// Copyright InviMind. All Rights Reserved.

#include "Elevation/FOSMGeoTIFFReader.h"
#include "OSMWorldGenCore.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// ---------------------------------------------------------------------------
// TIFF constants (subset needed for elevation rasters)
// ---------------------------------------------------------------------------
namespace TIFFConst
{
    // Byte orders
    static constexpr uint16 TIFF_LITTLEENDIAN = 0x4949; // 'II'
    static constexpr uint16 TIFF_BIGENDIAN    = 0x4D4D; // 'MM'

    // IFD tag IDs
    static constexpr uint16 TAG_ImageWidth         = 256;
    static constexpr uint16 TAG_ImageLength        = 257;
    static constexpr uint16 TAG_BitsPerSample      = 258;
    static constexpr uint16 TAG_Compression        = 259;
    static constexpr uint16 TAG_SampleFormat       = 339;
    static constexpr uint16 TAG_StripOffsets       = 273;
    static constexpr uint16 TAG_SamplesPerPixel    = 277;
    static constexpr uint16 TAG_RowsPerStrip       = 278;
    static constexpr uint16 TAG_StripByteCounts    = 279;
    static constexpr uint16 TAG_ModelPixelScaleTag = 33550;  // GeoTIFF
    static constexpr uint16 TAG_ModelTiepointTag   = 33922;  // GeoTIFF
    static constexpr uint16 TAG_GDAL_NODATA        = 42113;  // GDAL NoData string
    static constexpr uint16 TAG_Predictor          = 317;
    static constexpr uint16 TAG_TileWidth          = 322;
    static constexpr uint16 TAG_TileLength         = 323;
    static constexpr uint16 TAG_TileOffsets        = 324;
    static constexpr uint16 TAG_TileByteCounts     = 325;

    static constexpr uint16 COMPRESSION_NONE       = 1;
    static constexpr uint16 COMPRESSION_LZW        = 5;
    static constexpr uint16 COMPRESSION_DEFLATE_ADOBE = 8;
    static constexpr uint16 COMPRESSION_DEFLATE    = 32946;

    // Sample formats
    static constexpr uint16 SAMPLEFORMAT_UINT  = 1;
    static constexpr uint16 SAMPLEFORMAT_INT   = 2;
    static constexpr uint16 SAMPLEFORMAT_FLOAT = 3;

}

/**
 * TIFF-flavour LZW decompression (spec section 13).
 *
 * Differs from plain LZW in two ways that are easy to get wrong: codes are packed MSB-first,
 * and the code width increases one code EARLY ("early change") — at 511/1023/2047 rather than
 * 512/1024/2048. Verified against a real OpenTopography SRTM tile before being written here.
 */
static bool DecodeLZW(const uint8* In, int32 InSize, TArray<uint8>& Out, int32 ExpectedSize)
{
    Out.Reset(ExpectedSize);

    TArray<TArray<uint8>> Dict;
    Dict.Reserve(4096);

    auto ResetDict = [&Dict]()
    {
        Dict.Reset();
        for (int32 i = 0; i < 256; ++i)
        {
            TArray<uint8> Entry;
            Entry.Add(static_cast<uint8>(i));
            Dict.Add(MoveTemp(Entry));
        }
        // 256 = ClearCode, 257 = EndOfInformation; both are placeholders in the table.
        Dict.AddDefaulted(2);
    };

    ResetDict();

    int32 CodeWidth = 9;
    int32 PrevCode = -1;
    const int64 TotalBits = static_cast<int64>(InSize) * 8;
    int64 BitPos = 0;

    while (BitPos + CodeWidth <= TotalBits)
    {
        const int64 BytePos = BitPos / 8;
        const int32 Shift = static_cast<int32>(BitPos % 8);

        uint32 Window = 0;
        for (int32 i = 0; i < 3; ++i)
        {
            const int64 Idx = BytePos + i;
            Window = (Window << 8) | ((Idx < InSize) ? In[Idx] : 0);
        }

        const int32 Code = static_cast<int32>((Window >> (24 - Shift - CodeWidth)) & ((1u << CodeWidth) - 1));
        BitPos += CodeWidth;

        if (Code == 256) // ClearCode
        {
            ResetDict();
            CodeWidth = 9;
            PrevCode = -1;
            continue;
        }
        if (Code == 257) // EndOfInformation
        {
            break;
        }

        TArray<uint8> Entry;
        if (Code < Dict.Num() && (Code < 256 || Dict[Code].Num() > 0))
        {
            Entry = Dict[Code];
        }
        else if (PrevCode >= 0 && PrevCode < Dict.Num())
        {
            Entry = Dict[PrevCode];
            if (Entry.Num() == 0) return false;
            // Copy the byte out before appending: TArray::Add takes a reference, and passing
            // an element of the same array it's about to (possibly) reallocate trips an
            // aliasing assert in non-shipping builds.
            const uint8 FirstByte = Entry[0];
            Entry.Add(FirstByte);
        }
        else
        {
            return false;
        }

        Out.Append(Entry);

        if (PrevCode >= 0 && PrevCode < Dict.Num())
        {
            TArray<uint8> NewEntry = Dict[PrevCode];
            if (NewEntry.Num() > 0 && Entry.Num() > 0)
            {
                const uint8 FirstByte = Entry[0];
                NewEntry.Add(FirstByte);
                Dict.Add(MoveTemp(NewEntry));
            }
        }

        PrevCode = Code;

        // Early change: widen one code before the table is actually full.
        if (Dict.Num() + 1 >= (1 << CodeWidth) && CodeWidth < 12)
        {
            ++CodeWidth;
        }
    }

    return Out.Num() > 0;
}

// ---------------------------------------------------------------------------
// Helper: read a value from a byte buffer, respecting byte order
// ---------------------------------------------------------------------------
template<typename T>
static T ReadValue(const uint8* Ptr, bool bBigEndian)
{
    T Val;
    FMemory::Memcpy(&Val, Ptr, sizeof(T));
    if (bBigEndian)
    {
        // Swap bytes
        uint8* B = reinterpret_cast<uint8*>(&Val);
        for (int32 i = 0, j = (int32)sizeof(T) - 1; i < j; ++i, --j)
        {
            uint8 Tmp = B[i]; B[i] = B[j]; B[j] = Tmp;
        }
    }
    return Val;
}

// ---------------------------------------------------------------------------
// SRTM HGT Filename parsing (e.g., N51W001.hgt → lat=51, lon=-1)
// ---------------------------------------------------------------------------
bool FOSMGeoTIFFReader::ParseHGTFilename(const FString& Filename, int32& OutLat, int32& OutLon)
{
    // Pattern: [N|S][DD][E|W][DDD].hgt  or similar
    FString Base = FPaths::GetBaseFilename(Filename).ToUpper();
    if (Base.Len() < 6) return false;

    char LatHemi = Base[0];
    FString LatStr = Base.Mid(1, 2);
    char LonHemi = Base[3];
    FString LonStr = Base.Mid(4, 3);

    OutLat = FCString::Atoi(*LatStr);
    OutLon = FCString::Atoi(*LonStr);

    if (LatHemi == 'S') OutLat = -OutLat;
    if (LonHemi == 'W') OutLon = -OutLon;

    return true;
}

// ---------------------------------------------------------------------------
// SRTM HGT Loader
// ---------------------------------------------------------------------------
bool FOSMGeoTIFFReader::LoadHGT(const FString& FilePath, FOSMGeoTIFFTile& OutTile, TArray<float>& OutHeightData)
{
    TArray<uint8> RawBytes;
    if (!FFileHelper::LoadFileToArray(RawBytes, *FilePath))
    {
        UE_LOG(LogOSMWorldGen, Error, TEXT("HGT Load: failed to read file '%s'"), *FilePath);
        return false;
    }

    // SRTM HGT files are big-endian signed 16-bit integers
    // 1-arcsec:  3601×3601 = 25,934,402 bytes
    // 3-arcsec:  1201×1201 =  2,884,802 bytes
    const int64 ByteCount = RawBytes.Num();
    int32 Dim = 0;
    if (ByteCount == 3601LL * 3601LL * 2) { Dim = 3601; }
    else if (ByteCount == 1201LL * 1201LL * 2) { Dim = 1201; }
    else
    {
        UE_LOG(LogOSMWorldGen, Error, TEXT("HGT Load: unexpected file size %lld in '%s'"), ByteCount, *FilePath);
        return false;
    }

    int32 OriginLat, OriginLon;
    if (!ParseHGTFilename(FilePath, OriginLat, OriginLon))
    {
        UE_LOG(LogOSMWorldGen, Error, TEXT("HGT Load: could not parse lat/lon from filename '%s'"), *FilePath);
        return false;
    }

    const double ArcSec = (Dim == 3601) ? (1.0 / 3600.0) : (3.0 / 3600.0);

    OutTile.Width   = Dim;
    OutTile.Height  = Dim;
    OutTile.EPSG    = 4326;
    OutTile.FilePath = FilePath;
    OutTile.bHasNoData = true;
    OutTile.NoDataValue = -32768.0;

    // GeoTransform: top-left = (OriginLon, OriginLat + 1.0)
    OutTile.GeoTransform[0] = static_cast<double>(OriginLon);         // top-left lon
    OutTile.GeoTransform[1] = ArcSec;                                   // pixel width (lon)
    OutTile.GeoTransform[2] = 0.0;
    OutTile.GeoTransform[3] = static_cast<double>(OriginLat) + 1.0;   // top-left lat
    OutTile.GeoTransform[4] = 0.0;
    OutTile.GeoTransform[5] = -ArcSec;                                  // pixel height (lat, negative)

    const int32 TotalPixels = Dim * Dim;
    OutHeightData.SetNumUninitialized(TotalPixels);

    float MinH =  FLT_MAX;
    float MaxH = -FLT_MAX;

    for (int32 i = 0; i < TotalPixels; ++i)
    {
        const int16 Raw = static_cast<int16>((RawBytes[i * 2] << 8) | RawBytes[i * 2 + 1]);
        const float H = static_cast<float>(Raw);
        OutHeightData[i] = H;

        if (Raw != -32768)
        {
            MinH = FMath::Min(MinH, H);
            MaxH = FMath::Max(MaxH, H);
        }
    }

    OutTile.MinElevation = (MinH ==  FLT_MAX) ? 0.0f : MinH;
    OutTile.MaxElevation = (MaxH == -FLT_MAX) ? 0.0f : MaxH;

    UE_LOG(LogOSMWorldGen, Log, TEXT("HGT Load: %d×%d, Elev [%.1f, %.1f]m, Tile origin lat=%d lon=%d"),
        Dim, Dim, OutTile.MinElevation, OutTile.MaxElevation, OriginLat, OriginLon);

    return true;
}

// ---------------------------------------------------------------------------
// Minimal TIFF IFD Parser (no compression, single band, 16-bit int or 32-bit float)
// ---------------------------------------------------------------------------
bool FOSMGeoTIFFReader::LoadTIFF(const FString& FilePath, FOSMGeoTIFFTile& OutTile, TArray<float>& OutHeightData)
{
    TArray<uint8> Data;
    if (!FFileHelper::LoadFileToArray(Data, *FilePath))
    {
        UE_LOG(LogOSMWorldGen, Error, TEXT("TIFF Load: failed to read '%s'"), *FilePath);
        return false;
    }

    if (Data.Num() < 8)
    {
        UE_LOG(LogOSMWorldGen, Error, TEXT("TIFF Load: file too small '%s'"), *FilePath);
        return false;
    }

    const uint8* Buf = Data.GetData();

    // Byte order
    const uint16 ByteOrder = *reinterpret_cast<const uint16*>(Buf);
    const bool bBE = (ByteOrder == TIFFConst::TIFF_BIGENDIAN);
    if (ByteOrder != TIFFConst::TIFF_LITTLEENDIAN && ByteOrder != TIFFConst::TIFF_BIGENDIAN)
    {
        UE_LOG(LogOSMWorldGen, Error, TEXT("TIFF Load: unrecognised byte-order 0x%04X in '%s'"), ByteOrder, *FilePath);
        return false;
    }

    // Magic
    const uint16 Magic = ReadValue<uint16>(Buf + 2, bBE);
    if (Magic != 42)
    {
        UE_LOG(LogOSMWorldGen, Error, TEXT("TIFF Load: not a valid TIFF (magic=%d) '%s'"), Magic, *FilePath);
        return false;
    }

    // IFD offset
    uint32 IFDOffset = ReadValue<uint32>(Buf + 4, bBE);

    // ---- Parse IFD tags ----
    struct TIFFTagEntry { uint16 Tag; uint16 Type; uint32 Count; uint32 ValueOffset; };
    // TIFF types: 1=BYTE, 2=ASCII, 3=SHORT, 4=LONG, 5=RATIONAL(2×LONG), 12=DOUBLE

    int32  Width = 0, Height = 0;
    uint16 BitsPerSample = 16, SampleFormat = TIFFConst::SAMPLEFORMAT_INT;
    uint32 StripOffset = 0, RowsPerStrip = 0, StripByteCount = 0;
    uint32 SamplesPerPixel = 1;
    uint16 Compression = TIFFConst::COMPRESSION_NONE;
    uint16 Predictor = 1;
    uint32 TileWidth = 0, TileLength = 0, TileOffset = 0, TileByteCount = 0;
    double PixelScaleX = 0, PixelScaleY = 0;
    double TiepointX = 0, TiepointY = 0, TiepointLon = 0, TiepointLat = 0;
    bool   bHasModelPixelScale = false, bHasModelTiepoint = false;
    FString NoDataStr;

    if (IFDOffset + 2 > (uint32)Data.Num()) return false;
    const uint16 NumEntries = ReadValue<uint16>(Buf + IFDOffset, bBE);
    IFDOffset += 2;

    for (uint16 e = 0; e < NumEntries; ++e)
    {
        if (IFDOffset + 12 > (uint32)Data.Num()) break;

        const uint16 Tag   = ReadValue<uint16>(Buf + IFDOffset + 0, bBE);
        const uint16 Type  = ReadValue<uint16>(Buf + IFDOffset + 2, bBE);
        const uint32 Count = ReadValue<uint32>(Buf + IFDOffset + 4, bBE);
        const uint32 VOrO  = ReadValue<uint32>(Buf + IFDOffset + 8, bBE); // Value or offset
        IFDOffset += 12;

        auto GetShort = [&]() -> uint16
        {
            if (Type == 3 && Count >= 1) return bBE ? (uint16)(VOrO >> 16) : (uint16)(VOrO & 0xFFFF);
            return 0;
        };
        auto GetLong = [&]() -> uint32 { return VOrO; };
        auto GetDoubleAt = [&](uint32 ByteOff) -> double {
            if (ByteOff + 8 > (uint32)Data.Num()) return 0.0;
            return ReadValue<double>(Buf + ByteOff, bBE);
        };

        switch (Tag)
        {
        case TIFFConst::TAG_ImageWidth:        Width          = (Type == 3) ? GetShort() : GetLong(); break;
        case TIFFConst::TAG_ImageLength:       Height         = (Type == 3) ? GetShort() : GetLong(); break;
        case TIFFConst::TAG_BitsPerSample:     BitsPerSample  = GetShort(); break;
        case TIFFConst::TAG_SamplesPerPixel:   SamplesPerPixel = GetShort(); break;
        case TIFFConst::TAG_SampleFormat:      SampleFormat   = GetShort(); break;
        case TIFFConst::TAG_StripOffsets:      StripOffset    = GetLong();  break;
        case TIFFConst::TAG_RowsPerStrip:      RowsPerStrip   = GetLong();  break;
        case TIFFConst::TAG_StripByteCounts:   StripByteCount = GetLong();  break;
        case TIFFConst::TAG_Compression:       Compression    = GetShort(); break;
        case TIFFConst::TAG_Predictor:         Predictor      = GetShort(); break;
        case TIFFConst::TAG_TileWidth:         TileWidth      = (Type == 3) ? GetShort() : GetLong(); break;
        case TIFFConst::TAG_TileLength:        TileLength     = (Type == 3) ? GetShort() : GetLong(); break;
        case TIFFConst::TAG_TileOffsets:       TileOffset     = GetLong();  break;
        case TIFFConst::TAG_TileByteCounts:    TileByteCount  = GetLong();  break;
        case TIFFConst::TAG_ModelPixelScaleTag:
            if (Count >= 3)
            {
                PixelScaleX = GetDoubleAt(VOrO + 0);
                PixelScaleY = GetDoubleAt(VOrO + 8);
                bHasModelPixelScale = true;
            }
            break;
        case TIFFConst::TAG_ModelTiepointTag:
            if (Count >= 6)
            {
                TiepointX   = GetDoubleAt(VOrO + 0);
                TiepointY   = GetDoubleAt(VOrO + 8);
                // skip Z
                TiepointLon = GetDoubleAt(VOrO + 24);
                TiepointLat = GetDoubleAt(VOrO + 32);
                bHasModelTiepoint = true;
            }
            break;
        case TIFFConst::TAG_GDAL_NODATA:
            {
                // ASCII string
                uint32 StrLen = FMath::Min(Count, 64u);
                FString Tmp;
                for (uint32 ci = 0; ci < StrLen; ++ci)
                {
                    char Ch = (char)((VOrO > 4 || Count > 4) ? Buf[VOrO + ci] : ((VOrO >> (8 * ci)) & 0xFF));
                    if (Ch == 0) break;
                    Tmp.AppendChar(Ch);
                }
                NoDataStr = Tmp;
            }
            break;
        default: break;
        }
    }

    if (Width <= 0 || Height <= 0)
    {
        UE_LOG(LogOSMWorldGen, Error, TEXT("TIFF Load: invalid dimensions %dx%d in '%s'"), Width, Height, *FilePath);
        return false;
    }
    if (BitsPerSample != 16 && BitsPerSample != 32)
    {
        UE_LOG(LogOSMWorldGen, Error, TEXT("TIFF Load: unsupported bits-per-sample=%d in '%s'"), BitsPerSample, *FilePath);
        return false;
    }

    // Build GeoTransform from ModelPixelScale + ModelTiepoint
    OutTile.Width   = Width;
    OutTile.Height  = Height;
    OutTile.EPSG    = 4326;
    OutTile.FilePath = FilePath;

    if (bHasModelPixelScale && bHasModelTiepoint)
    {
        // top-left corner
        OutTile.GeoTransform[0] = TiepointLon - TiepointX * PixelScaleX;
        OutTile.GeoTransform[1] = PixelScaleX;
        OutTile.GeoTransform[2] = 0.0;
        OutTile.GeoTransform[3] = TiepointLat + TiepointY * PixelScaleY;
        OutTile.GeoTransform[4] = 0.0;
        OutTile.GeoTransform[5] = -PixelScaleY;
    }
    else
    {
        UE_LOG(LogOSMWorldGen, Warning, TEXT("TIFF Load: no GeoTIFF geo-tags found, assuming 1-arcsec default origin in '%s'"), *FilePath);
        OutTile.GeoTransform[1] = 1.0 / 3600.0;
        OutTile.GeoTransform[5] = -1.0 / 3600.0;
    }

    if (!NoDataStr.IsEmpty())
    {
        OutTile.NoDataValue = FCString::Atod(*NoDataStr);
        OutTile.bHasNoData  = true;
    }

    // ---- Read raster data ----
    const int32 TotalPixels = Width * Height;
    const int32 BytesPerSample = BitsPerSample / 8;
    OutHeightData.SetNumUninitialized(TotalPixels);

    // OpenTopography (and GDAL generally) return TILED, LZW-COMPRESSED GeoTIFFs, not the
    // uncompressed single-strip layout this reader originally assumed — which is why a
    // perfectly valid DEM failed with "invalid strip offset": it has no StripOffsets tag at
    // all, only TileOffsets. Handle both layouts, and decompress when needed.
    const bool bTiled = (TileWidth > 0 && TileLength > 0 && TileOffset > 0);

    if (!bTiled && StripOffset == 0)
    {
        UE_LOG(LogOSMWorldGen, Error, TEXT("TIFF Load: no strip or tile offsets in '%s'"), *FilePath);
        return false;
    }

    if (Compression != TIFFConst::COMPRESSION_NONE
        && Compression != TIFFConst::COMPRESSION_LZW
        && Compression != TIFFConst::COMPRESSION_DEFLATE
        && Compression != TIFFConst::COMPRESSION_DEFLATE_ADOBE)
    {
        UE_LOG(LogOSMWorldGen, Error,
            TEXT("TIFF Load: unsupported compression %d in '%s' (supported: none, LZW, Deflate)"),
            Compression, *FilePath);
        return false;
    }

    // Pull one compressed or raw chunk out of the file and hand back decoded bytes.
    auto ReadChunk = [&](uint32 Offset, uint32 ByteCount, int32 ExpectedBytes, TArray<uint8>& OutBytes) -> bool
    {
        if (Offset == 0 || Offset + ByteCount > (uint32)Data.Num())
        {
            return false;
        }

        if (Compression == TIFFConst::COMPRESSION_NONE)
        {
            OutBytes.SetNumUninitialized(FMath::Min<int32>(ExpectedBytes, ByteCount));
            FMemory::Memcpy(OutBytes.GetData(), Buf + Offset, OutBytes.Num());
            return true;
        }

        if (Compression == TIFFConst::COMPRESSION_LZW)
        {
            return DecodeLZW(Buf + Offset, ByteCount, OutBytes, ExpectedBytes);
        }

        // Deflate — zlib is already available to the engine.
        OutBytes.SetNumUninitialized(ExpectedBytes);
        if (!FCompression::UncompressMemory(NAME_Zlib, OutBytes.GetData(), ExpectedBytes, Buf + Offset, ByteCount))
        {
            return false;
        }
        return true;
    };

    // Horizontal differencing predictor: each sample is stored as a delta from its left
    // neighbour, so it has to be undone row by row before the values mean anything.
    auto ApplyPredictor = [&](TArray<uint8>& Bytes, int32 RowWidthSamples, int32 NumRows)
    {
        if (Predictor != 2 || BitsPerSample != 16) return;
        for (int32 Row = 0; Row < NumRows; ++Row)
        {
            uint8* RowPtr = Bytes.GetData() + (int64)Row * RowWidthSamples * 2;
            for (int32 Col = 1; Col < RowWidthSamples; ++Col)
            {
                const int64 ByteIdx = (int64)Col * 2;
                if (ByteIdx + 1 >= (int64)Bytes.Num()) break;
                const uint16 Prev = ReadValue<uint16>(RowPtr + ByteIdx - 2, bBE);
                const uint16 Cur  = ReadValue<uint16>(RowPtr + ByteIdx, bBE);
                const uint16 Sum  = static_cast<uint16>(Prev + Cur);
                FMemory::Memcpy(RowPtr + ByteIdx, &Sum, 2);
            }
        }
    };

    auto SampleAt = [&](const uint8* Ptr) -> float
    {
        if (BitsPerSample == 16)
        {
            const uint16 Raw = ReadValue<uint16>(Ptr, bBE);
            return (SampleFormat == TIFFConst::SAMPLEFORMAT_INT)
                ? static_cast<float>(static_cast<int16>(Raw))
                : static_cast<float>(Raw);
        }
        return ReadValue<float>(Ptr, bBE);
    };

    float MinH =  FLT_MAX;
    float MaxH = -FLT_MAX;

    if (bTiled)
    {
        // Tiles are padded out to full TileWidth x TileLength even when the image is smaller,
        // so the source row stride is the tile's width, not the image's.
        const int32 TilesAcross = FMath::DivideAndRoundUp<int32>(Width, TileWidth);
        const int32 TilesDown   = FMath::DivideAndRoundUp<int32>(Height, TileLength);
        const int32 TileSamples = TileWidth * TileLength;
        const int32 TileBytes   = TileSamples * BytesPerSample;

        if (TilesAcross * TilesDown != 1)
        {
            // Only the single-tile case can be located from the scalar tag value read above;
            // multi-tile needs the full offset array, which this reader doesn't parse yet.
            UE_LOG(LogOSMWorldGen, Error,
                TEXT("TIFF Load: '%s' uses %dx%d tiles; only single-tile images are supported. ")
                TEXT("Request a smaller region, or supply an uncompressed GeoTIFF."),
                *FilePath, TilesAcross, TilesDown);
            return false;
        }

        TArray<uint8> Tile;
        if (!ReadChunk(TileOffset, TileByteCount, TileBytes, Tile) || Tile.Num() < TileBytes)
        {
            UE_LOG(LogOSMWorldGen, Error,
                TEXT("TIFF Load: failed to decode tile data in '%s' (compression=%d, got %d of %d bytes)"),
                *FilePath, Compression, Tile.Num(), TileBytes);
            return false;
        }

        ApplyPredictor(Tile, TileWidth, TileLength);

        for (int32 Y = 0; Y < Height; ++Y)
        {
            for (int32 X = 0; X < Width; ++X)
            {
                const int64 SrcByte = ((int64)Y * TileWidth + X) * BytesPerSample;
                const float Val = SampleAt(Tile.GetData() + SrcByte);
                OutHeightData[Y * Width + X] = Val;

                const bool bIsNoData = OutTile.bHasNoData && FMath::IsNearlyEqual((double)Val, OutTile.NoDataValue, 0.5);
                if (!bIsNoData)
                {
                    MinH = FMath::Min(MinH, Val);
                    MaxH = FMath::Max(MaxH, Val);
                }
            }
        }
    }
    else
    {
        const int32 ExpectedBytes = TotalPixels * BytesPerSample;
        TArray<uint8> Raster;

        if (Compression == TIFFConst::COMPRESSION_NONE)
        {
            if (StripOffset + (uint32)ExpectedBytes > (uint32)Data.Num())
            {
                UE_LOG(LogOSMWorldGen, Error, TEXT("TIFF Load: strip data out of range in '%s'"), *FilePath);
                return false;
            }
            Raster.SetNumUninitialized(ExpectedBytes);
            FMemory::Memcpy(Raster.GetData(), Buf + StripOffset, ExpectedBytes);
        }
        else if (!ReadChunk(StripOffset, StripByteCount, ExpectedBytes, Raster) || Raster.Num() < ExpectedBytes)
        {
            UE_LOG(LogOSMWorldGen, Error,
                TEXT("TIFF Load: failed to decode strip data in '%s' (compression=%d)"), *FilePath, Compression);
            return false;
        }

        ApplyPredictor(Raster, Width, Height);

        for (int32 i = 0; i < TotalPixels; ++i)
        {
            const float Val = SampleAt(Raster.GetData() + (int64)i * BytesPerSample);
            OutHeightData[i] = Val;

            const bool bIsNoData = OutTile.bHasNoData && FMath::IsNearlyEqual((double)Val, OutTile.NoDataValue, 0.5);
            if (!bIsNoData)
            {
                MinH = FMath::Min(MinH, Val);
                MaxH = FMath::Max(MaxH, Val);
            }
        }
    }

    OutTile.MinElevation = (MinH ==  FLT_MAX) ? 0.0f : MinH;
    OutTile.MaxElevation = (MaxH == -FLT_MAX) ? 0.0f : MaxH;

    UE_LOG(LogOSMWorldGen, Log, TEXT("TIFF Load: %d×%d, %d-bit, Elev [%.1f, %.1f]m"),
        Width, Height, BitsPerSample, OutTile.MinElevation, OutTile.MaxElevation);

    return true;
}

// ---------------------------------------------------------------------------
// Main entry point — dispatch by extension
// ---------------------------------------------------------------------------
bool FOSMGeoTIFFReader::Load(const FString& FilePath, FOSMGeoTIFFTile& OutTile, TArray<float>& OutHeightData)
{
    const FString Ext = FPaths::GetExtension(FilePath).ToLower();
    if (Ext == TEXT("hgt"))
    {
        return LoadHGT(FilePath, OutTile, OutHeightData);
    }
    else if (Ext == TEXT("tif") || Ext == TEXT("tiff"))
    {
        return LoadTIFF(FilePath, OutTile, OutHeightData);
    }

    UE_LOG(LogOSMWorldGen, Error, TEXT("GeoTIFF Load: unsupported extension '.%s' in '%s'"), *Ext, *FilePath);
    return false;
}
