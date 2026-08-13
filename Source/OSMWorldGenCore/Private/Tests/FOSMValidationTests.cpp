// Copyright InviMind. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Model/FOSMFeatureTable.h"
#include "Parsing/FOSMParseResult.h"
#include "Region/FOSMRegion.h"
#include "Validation/FOSMDEMValidator.h"
#include "Validation/FOSMDataValidator.h"
#include "Validation/FOSMImportReport.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Phase 1.5 regression corpus tests.
 *
 * Each corpus file reproduces a failure that actually happened during development. The tests
 * assert both the verdict AND the specific issue code, because "it was rejected" is not the
 * property that matters — a gate that rejects everything would pass that. What matters is that
 * a rejection names the real cause, which is the difference between a fixable error message
 * and another afternoon of guessing.
 *
 * Codes are asserted rather than message text so the tests break when behaviour changes, not
 * when wording is improved.
 *
 * Regenerate the corpus with:  python3 Tests/make_corpus.py
 */
namespace OSMTestCorpus
{
    /** The region the OSM fixtures were built around; matches REGION in make_corpus.py. */
    FOSMRegion GetTestRegion()
    {
        FOSMRegion Region;
        FString Error;
        const bool bBuilt = FOSMRegion::FromBoundingBox(12.9700, 77.6020, 12.9800, 77.6120, Region, Error);
        check(bBuilt);
        return Region;
    }

    FString GetCorpusPath(const FString& FileName)
    {
        const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("OSMWorldGen"));
        const FString BaseDir = Plugin.IsValid()
            ? Plugin->GetBaseDir()
            : FPaths::ProjectPluginsDir() / TEXT("OSM_plugin");

        return FPaths::ConvertRelativePathToFull(BaseDir / TEXT("Tests") / TEXT("Data") / FileName);
    }

    /** Skips a test gracefully when the corpus has not been generated on this machine. */
    bool CorpusAvailable(FAutomationTestBase& Test, const FString& FileName, FString& OutPath)
    {
        OutPath = GetCorpusPath(FileName);
        if (!FPaths::FileExists(OutPath))
        {
            Test.AddWarning(FString::Printf(
                TEXT("Corpus file '%s' not found. Run: python3 Tests/make_corpus.py"), *FileName));
            return false;
        }
        return true;
    }
}

// ===========================================================================
// FOSMRegion — the single source of truth
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FOSMRegionFactoryTest,
    "OSMWorldGen.Region.Factories",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMRegionFactoryTest::RunTest(const FString& Parameters)
{
    FOSMRegion Region;
    FString Error;

    // A default-constructed region must be unusable, so a region that was never explicitly
    // built cannot masquerade as the origin-centred box at (0,0).
    TestFalse(TEXT("Default-constructed region is invalid"), FOSMRegion().IsValid());

    // ---- FromCenterAndArea ----
    TestTrue(TEXT("1 km2 region builds"),
        FOSMRegion::FromCenterAndArea(12.975, 77.607, 1.0, Region, Error));
    TestTrue(TEXT("Built region is valid"), Region.IsValid());
    TestNearlyEqual(TEXT("Area round-trips"), Region.GetAreaSqKm(), 1.0, 0.01);
    TestNearlyEqual(TEXT("Region is square on the ground"),
        Region.GetWidthKm(), Region.GetHeightKm(), 0.01);
    TestNearlyEqual(TEXT("Centre latitude preserved"), Region.GetCenterLat(), 12.975, 1e-9);
    TestNearlyEqual(TEXT("Centre longitude preserved"), Region.GetCenterLon(), 77.607, 1e-9);

    // The size gate that a 500 km landscape got through.
    TestFalse(TEXT("Over-large area is refused"),
        FOSMRegion::FromCenterAndArea(12.975, 77.607, 10000.0, Region, Error));
    TestTrue(TEXT("Refusal explains itself"), Error.Contains(TEXT("outside the allowed range")));
    TestFalse(TEXT("Refused region is left invalid"), Region.IsValid());

    TestFalse(TEXT("Sub-minimum area is refused"),
        FOSMRegion::FromCenterAndArea(12.975, 77.607, 0.0001, Region, Error));

    // The flat-earth approximation this plugin uses breaks down near the poles.
    TestFalse(TEXT("Polar region is refused"),
        FOSMRegion::FromCenterAndArea(89.5, 0.0, 1.0, Region, Error));

    // ---- FromBoundingBox ----
    TestFalse(TEXT("Inverted box is refused"),
        FOSMRegion::FromBoundingBox(13.0, 77.6, 12.9, 77.7, Region, Error));
    TestFalse(TEXT("Out-of-range latitude is refused"),
        FOSMRegion::FromBoundingBox(-91.0, 77.6, 12.9, 77.7, Region, Error));
    TestFalse(TEXT("Degenerate box is refused"),
        FOSMRegion::FromBoundingBox(12.9, 77.6, 12.9, 77.6, Region, Error));

    // ---- ObservedBounds bypasses the size limits, deliberately ----
    FOSMRegion Observed;
    TestTrue(TEXT("A 40 km observed extent is allowed"),
        FOSMRegion::ObservedBounds(12.807712, 77.565313, 13.172957, 77.713060, Observed, Error));
    TestTrue(TEXT("Observed extent is valid"), Observed.IsValid());
    TestFalse(TEXT("The same extent is refused as an import region"),
        FOSMRegion::FromBoundingBox(12.807712, 77.565313, 13.172957, 77.713060, Region, Error));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FOSMRegionGeometryTest,
    "OSMWorldGen.Region.Geometry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMRegionGeometryTest::RunTest(const FString& Parameters)
{
    FString Error;
    FOSMRegion Region, Covering, Partial, Elsewhere;

    FOSMRegion::FromBoundingBox(12.970, 77.602, 12.980, 77.612, Region, Error);
    FOSMRegion::ObservedBounds(12.968, 77.600, 12.982, 77.614, Covering, Error);
    FOSMRegion::ObservedBounds(12.975, 77.602, 12.985, 77.612, Partial, Error);
    FOSMRegion::ObservedBounds(50.000, 10.000, 50.010, 10.010, Elsewhere, Error);

    TestTrue(TEXT("Containing box contains the region"), Covering.Contains(Region));
    TestNearlyEqual(TEXT("Full coverage is 1.0"), Region.CoverageBy(Covering), 1.0, 1e-9);

    // Half the region's latitude span is covered.
    TestNearlyEqual(TEXT("Half coverage is 0.5"), Region.CoverageBy(Partial), 0.5, 1e-6);

    TestFalse(TEXT("A distant box does not intersect"), Region.Intersects(Elsewhere));
    TestNearlyEqual(TEXT("A distant box gives zero coverage"), Region.CoverageBy(Elsewhere), 0.0, 1e-9);

    // Padding must widen the box without invalidating it — the DEM fetch depends on this.
    const FOSMRegion Padded = Region.Expanded(0.002);
    TestTrue(TEXT("Padded region is valid"), Padded.IsValid());
    TestTrue(TEXT("Padded region contains the original"), Padded.Contains(Region));
    TestNearlyEqual(TEXT("Padding applied to min latitude"),
        Padded.GetMinLat(), Region.GetMinLat() - 0.002, 1e-9);

    return true;
}

// ===========================================================================
// .osm gate
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FOSMDataGateTest,
    "OSMWorldGen.Validation.OSMGate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMDataGateTest::RunTest(const FString& Parameters)
{
    const FOSMRegion Region = OSMTestCorpus::GetTestRegion();
    FString Path;

    // ---- The clean file must be accepted ----
    if (OSMTestCorpus::CorpusAvailable(*this, TEXT("valid_small.osm"), Path))
    {
        FOSMParseResult Parsed;
        FOSMDataValidator::FStats Stats;
        const FOSMValidationResult Result =
            FOSMDataValidator::ValidateAgainstRegion(Path, Region, Parsed, Stats);

        TestTrue(TEXT("valid_small.osm is accepted"), Result.IsAccepted());
        TestTrue(TEXT("valid_small.osm has nodes"), Stats.NodeCount > 0);
        TestTrue(TEXT("valid_small.osm has ways"), Stats.WayCount > 0);
        TestEqual(TEXT("valid_small.osm has no dangling references"), Stats.WaysWithMissingNodes, 0);
        TestTrue(TEXT("valid_small.osm bounds are valid"), Stats.DataBounds.IsValid());
    }

    // ---- The 40 km way: accepted, but flagged ----
    if (OSMTestCorpus::CorpusAvailable(*this, TEXT("oversized_way.osm"), Path))
    {
        FOSMParseResult Parsed;
        FOSMDataValidator::FStats Stats;
        const FOSMValidationResult Result =
            FOSMDataValidator::ValidateAgainstRegion(Path, Region, Parsed, Stats);

        // Accepted because clipping handles it; warned because an unnoticed 40 km way inside a
        // 1 km region is exactly what produced a 500 km landscape.
        TestTrue(TEXT("oversized_way.osm is accepted"), Result.IsAccepted());
        TestTrue(TEXT("oversized_way.osm is flagged as oversized"),
            Result.HasCode(TEXT("osm.bounds.oversized")));
        TestTrue(TEXT("oversized_way.osm extent exceeds the region"),
            Stats.DataBounds.GetAreaSqKm() > Region.GetAreaSqKm() * 3.0);
    }

    // ---- Structural rejections, each naming its own cause ----
    struct FCase
    {
        const TCHAR* FileName;
        const TCHAR* ExpectedCode;
        const TCHAR* Description;
    };

    static const FCase RejectionCases[] =
    {
        { TEXT("utf16_mislabeled.osm"), TEXT("osm.encoding.utf16"),  TEXT("UTF-16 body with a UTF-8 declaration") },
        { TEXT("truncated.osm"),        TEXT("osm.file.truncated"),  TEXT("interrupted download") },
        { TEXT("empty.osm"),            TEXT("osm.file.empty"),      TEXT("zero-byte file") },
        { TEXT("not_osm.osm"),          TEXT("osm.root.missing"),    TEXT("XML that is not OSM") },
    };

    for (const FCase& Case : RejectionCases)
    {
        if (!OSMTestCorpus::CorpusAvailable(*this, Case.FileName, Path))
        {
            continue;
        }

        const FOSMValidationResult Result = FOSMDataValidator::ValidateFile(Path);

        TestFalse(FString::Printf(TEXT("%s is rejected (%s)"), Case.FileName, Case.Description),
            Result.IsAccepted());
        TestTrue(FString::Printf(TEXT("%s is rejected for the right reason (%s)"),
            Case.FileName, Case.ExpectedCode),
            Result.HasCode(Case.ExpectedCode));
    }

    // ---- Dangling references warn rather than reject ----
    if (OSMTestCorpus::CorpusAvailable(*this, TEXT("dangling_refs.osm"), Path))
    {
        FOSMParseResult Parsed;
        FOSMDataValidator::FStats Stats;
        const FOSMValidationResult Result =
            FOSMDataValidator::ValidateAgainstRegion(Path, Region, Parsed, Stats);

        // Overpass legitimately returns ways whose nodes fall outside the query box, so this
        // must not be fatal — the partial geometry is still usable.
        TestTrue(TEXT("dangling_refs.osm is accepted"), Result.IsAccepted());
        TestTrue(TEXT("dangling_refs.osm is flagged"), Result.HasCode(TEXT("osm.refs.dangling")));
        TestTrue(TEXT("dangling_refs.osm counts the bad ways"), Stats.WaysWithMissingNodes > 0);
    }

    // ---- Data for a different place is fatal ----
    if (OSMTestCorpus::CorpusAvailable(*this, TEXT("valid_small.osm"), Path))
    {
        FOSMRegion Elsewhere;
        FString Error;
        FOSMRegion::FromCenterAndArea(48.8584, 2.2945, 1.0, Elsewhere, Error); // Paris

        FOSMParseResult Parsed;
        FOSMDataValidator::FStats Stats;
        const FOSMValidationResult Result =
            FOSMDataValidator::ValidateAgainstRegion(Path, Elsewhere, Parsed, Stats);

        TestFalse(TEXT("Bangalore data is rejected for a Paris region"), Result.IsAccepted());
        TestTrue(TEXT("Rejection identifies disjoint bounds"), Result.HasCode(TEXT("osm.bounds.disjoint")));
    }

    return true;
}

// ===========================================================================
// .tif gate
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FOSMDEMGateTest,
    "OSMWorldGen.Validation.DEMGate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMDEMGateTest::RunTest(const FString& Parameters)
{
    FString Path;

    // ---- The clean raster must load and be accepted ----
    if (OSMTestCorpus::CorpusAvailable(*this, TEXT("valid_striped.tif"), Path))
    {
        FOSMGeoTIFFTile Tile;
        TArray<float> HeightData;
        FOSMDEMValidator::FStats Stats;
        const FOSMValidationResult Result =
            FOSMDEMValidator::ValidateLoaded(Path, Tile, HeightData, Stats);

        TestTrue(TEXT("valid_striped.tif is accepted"), Result.IsAccepted());
        TestEqual(TEXT("Raster width read correctly"), Stats.Width, 48);
        TestEqual(TEXT("Raster height read correctly"), Stats.Height, 48);
        TestEqual(TEXT("Every pixel decoded"), HeightData.Num(), 48 * 48);
        TestTrue(TEXT("Elevation range is plausible"),
            Stats.MinElevation >= 800.0f && Stats.MaxElevation <= 1000.0f);
        TestTrue(TEXT("Bounds were georeferenced"), Stats.DataBounds.IsValid());
    }

    // ---- Structural rejections, each naming its own cause ----
    struct FCase
    {
        const TCHAR* FileName;
        const TCHAR* ExpectedCode;
        const TCHAR* Description;
    };

    static const FCase RejectionCases[] =
    {
        { TEXT("no_geotags.tif"),      TEXT("tif.geotags.missing"),          TEXT("no georeferencing tags") },
        { TEXT("jpeg_compressed.tif"), TEXT("tif.compression.unsupported"),  TEXT("JPEG compression") },
        { TEXT("projected_crs.tif"),   TEXT("tif.pixelscale.implausible"),   TEXT("projected CRS in metres") },
        { TEXT("multitile.tif"),       TEXT("tif.layout.multitile"),         TEXT("multi-tile layout") },
        { TEXT("not_a_tiff.tif"),      TEXT("tif.header.invalid"),           TEXT("not a TIFF at all") },
    };

    for (const FCase& Case : RejectionCases)
    {
        if (!OSMTestCorpus::CorpusAvailable(*this, Case.FileName, Path))
        {
            continue;
        }

        const FOSMValidationResult Result = FOSMDEMValidator::ValidateFile(Path);

        TestFalse(FString::Printf(TEXT("%s is rejected (%s)"), Case.FileName, Case.Description),
            Result.IsAccepted());
        TestTrue(FString::Printf(TEXT("%s is rejected for the right reason (%s)"),
            Case.FileName, Case.ExpectedCode),
            Result.HasCode(Case.ExpectedCode));
    }

    // ---- A truncated raster must fail cleanly, not crash ----
    if (OSMTestCorpus::CorpusAvailable(*this, TEXT("truncated.tif"), Path))
    {
        // The reader logs an error on the way to refusing this file, which is correct
        // behaviour but would otherwise be counted as a test failure by the automation
        // framework. Declaring it expected asserts that the message IS produced, so the
        // diagnostic cannot silently disappear.
        AddExpectedError(TEXT("TIFF Load: strip data out of range"),
            EAutomationExpectedErrorFlags::Contains, 1);

        FOSMGeoTIFFTile Tile;
        TArray<float> HeightData;
        FOSMDEMValidator::FStats Stats;
        const FOSMValidationResult Result =
            FOSMDEMValidator::ValidateLoaded(Path, Tile, HeightData, Stats);

        // The whole point of this case: a malformed raster used to reach the decoder and take
        // the editor down with it.
        TestFalse(TEXT("truncated.tif is rejected"), Result.IsAccepted());
        TestTrue(TEXT("truncated.tif rejection is a load/raster failure"),
            Result.HasCode(TEXT("tif.load.failed")) || Result.HasCode(TEXT("tif.raster.incomplete")));
    }

    return true;
}

// ===========================================================================
// Cross-file gate
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FOSMCrossFileGateTest,
    "OSMWorldGen.Validation.CrossFile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMCrossFileGateTest::RunTest(const FString& Parameters)
{
    const FOSMRegion Region = OSMTestCorpus::GetTestRegion();
    FString Error;
    double Coverage = 0.0;

    // ---- Full coverage: accepted, no warning ----
    {
        FOSMRegion DEMBounds;
        FOSMRegion::ObservedBounds(12.968, 77.600, 12.982, 77.614, DEMBounds, Error);

        const FOSMValidationResult Result =
            FOSMImportValidator::ValidateCrossFile(Region, Region, DEMBounds, Coverage);

        TestTrue(TEXT("Fully covering DEM is accepted"), Result.IsAccepted());
        TestNearlyEqual(TEXT("Coverage is complete"), Coverage, 1.0, 1e-9);
        TestFalse(TEXT("No partial-coverage warning"),
            Result.HasCode(TEXT("cross.dem.coverage.partial")));
    }

    // ---- The exact shortfall seen in the wizard: grid snapping leaves a sliver ----
    {
        FOSMRegion DEMBounds;
        FOSMRegion::ObservedBounds(12.97005, 77.60205, 12.97995, 77.61195, DEMBounds, Error);

        const FOSMValidationResult Result =
            FOSMImportValidator::ValidateCrossFile(Region, Region, DEMBounds, Coverage);

        // Benign and expected — DEM providers return whole source-grid cells — so this warns
        // rather than blocking the import.
        TestTrue(TEXT("Near-complete coverage is accepted"), Result.IsAccepted());
        TestTrue(TEXT("Partial coverage is flagged"),
            Result.HasCode(TEXT("cross.dem.coverage.partial")));
        TestTrue(TEXT("Coverage is high but not total"), Coverage > 0.98 && Coverage < 1.0);
    }

    // ---- Too little coverage is fatal ----
    {
        FOSMRegion DEMBounds;
        FOSMRegion::ObservedBounds(12.9750, 77.6070, 12.9800, 77.6120, DEMBounds, Error);

        const FOSMValidationResult Result =
            FOSMImportValidator::ValidateCrossFile(Region, Region, DEMBounds, Coverage);

        TestFalse(TEXT("Quarter coverage is rejected"), Result.IsAccepted());
        TestTrue(TEXT("Rejection identifies insufficient coverage"),
            Result.HasCode(TEXT("cross.dem.coverage.insufficient")));
    }

    // ---- Two files for different places: the user's explicit requirement ----
    {
        FOSMRegion OSMBounds, DEMBounds;
        FOSMRegion::ObservedBounds(12.970, 77.602, 12.980, 77.612, OSMBounds, Error);
        FOSMRegion::ObservedBounds(48.850, 2.290, 48.860, 2.300, DEMBounds, Error);  // Paris

        const FOSMValidationResult Result =
            FOSMImportValidator::ValidateCrossFile(Region, OSMBounds, DEMBounds, Coverage);

        TestFalse(TEXT("Mismatched files are rejected"), Result.IsAccepted());
        TestTrue(TEXT("Rejection identifies the mismatch"),
            Result.HasCode(TEXT("cross.files.disjoint")));
    }

    return true;
}

// ===========================================================================
// End-to-end import
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FOSMImportPipelineTest,
    "OSMWorldGen.Validation.ImportPipeline",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOSMImportPipelineTest::RunTest(const FString& Parameters)
{
    const FOSMRegion Region = OSMTestCorpus::GetTestRegion();
    FString OSMPath, DEMPath;

    if (!OSMTestCorpus::CorpusAvailable(*this, TEXT("valid_small.osm"), OSMPath)
        || !OSMTestCorpus::CorpusAvailable(*this, TEXT("valid_striped.tif"), DEMPath))
    {
        return true;
    }

    // ---- The happy path ----
    {
        FOSMFeatureTable Features;
        const FOSMImportReport Report =
            FOSMImportValidator::Run(Region, OSMPath, DEMPath, Features);

        TestTrue(TEXT("Clean import is accepted"), Report.IsAccepted());
        TestTrue(TEXT("Features were classified"), Report.TotalFeatures > 0);
        TestTrue(TEXT("DEM coverage was measured"), Report.DEMCoverageFraction > 0.9);
        TestTrue(TEXT("Report carries the region"), Report.Region.IsValid());
        TestTrue(TEXT("Report renders a summary"), Report.ToDisplayString().Len() > 0);

        // The region must survive the import untouched — this is the invariant whose absence
        // turned a 1.5 km request into a 500 km landscape.
        TestNearlyEqual(TEXT("Region min latitude unchanged"),
            Report.Region.GetMinLat(), Region.GetMinLat(), 1e-12);
        TestNearlyEqual(TEXT("Region area unchanged"),
            Report.Region.GetAreaSqKm(), Region.GetAreaSqKm(), 1e-9);
    }

    // ---- A bad DEM must stop the import, not degrade it silently ----
    {
        FString BadDEMPath;
        if (OSMTestCorpus::CorpusAvailable(*this, TEXT("no_geotags.tif"), BadDEMPath))
        {
            FOSMFeatureTable Features;
            const FOSMImportReport Report =
                FOSMImportValidator::Run(Region, OSMPath, BadDEMPath, Features);

            TestFalse(TEXT("Import with an ungeoreferenced DEM is rejected"), Report.IsAccepted());
            TestTrue(TEXT("Rejection names the missing geo-tags"),
                Report.Validation.HasCode(TEXT("tif.geotags.missing")));
            TestEqual(TEXT("No features are produced from a rejected import"), Report.TotalFeatures, 0);
        }
    }

    // ---- Import without a DEM is allowed, but says so ----
    {
        FOSMFeatureTable Features;
        const FOSMImportReport Report =
            FOSMImportValidator::Run(Region, OSMPath, FString(), Features);

        TestTrue(TEXT("Import without a DEM is accepted"), Report.IsAccepted());
        TestTrue(TEXT("Missing DEM is reported"), Report.Validation.HasCode(TEXT("import.dem.absent")));
        TestFalse(TEXT("Report records that there is no DEM"), Report.bHasDEM);
    }

    // ---- An invalid region cannot start an import at all ----
    {
        FOSMFeatureTable Features;
        const FOSMImportReport Report =
            FOSMImportValidator::Run(FOSMRegion(), OSMPath, DEMPath, Features);

        TestFalse(TEXT("Import with no region is rejected"), Report.IsAccepted());
        TestTrue(TEXT("Rejection names the invalid region"),
            Report.Validation.HasCode(TEXT("import.region.invalid")));
    }

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
