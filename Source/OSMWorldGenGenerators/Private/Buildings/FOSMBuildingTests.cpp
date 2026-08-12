// Copyright InviMind. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Buildings/FOSMBuildingHeightResolver.h"
#include "Model/FOSMFeature.h"

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOSMHeightResolverTest_ExplicitTag,
    "OSMWorldGen.Buildings.HeightResolver.ExplicitMeters",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOSMHeightResolverTest_ExplicitTag::RunTest(const FString& Parameters)
{
    FOSMFeature F;
    F.Tags.Add(TEXT("height"), TEXT("42.5"));

    auto R = FOSMBuildingHeightResolver::Resolve(F);
    TestEqual("Source is ExplicitTag",
        R.Source, FOSMBuildingHeightResolver::FResolutionResult::ESource::ExplicitTag);
    TestTrueExpr(FMath::IsNearlyEqual(R.HeightMeters, 42.5f, 0.1f));
    return true;
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOSMHeightResolverTest_FeetUnit,
    "OSMWorldGen.Buildings.HeightResolver.FeetConversion",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOSMHeightResolverTest_FeetUnit::RunTest(const FString& Parameters)
{
    FOSMFeature F;
    F.Tags.Add(TEXT("height"), TEXT("100 ft")); // 30.48 m

    auto R = FOSMBuildingHeightResolver::Resolve(F);
    TestTrueExpr(FMath::IsNearlyEqual(R.HeightMeters, 30.48f, 0.1f));
    return true;
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOSMHeightResolverTest_Levels,
    "OSMWorldGen.Buildings.HeightResolver.LevelsTag",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOSMHeightResolverTest_Levels::RunTest(const FString& Parameters)
{
    FOSMFeature F;
    F.Tags.Add(TEXT("building:levels"), TEXT("5"));

    // 5 levels × 3.0 m default floor height = 15.0 m
    auto R = FOSMBuildingHeightResolver::Resolve(F, 3.0f, 9.0f);
    TestEqual("Source is LevelsTag",
        R.Source, FOSMBuildingHeightResolver::FResolutionResult::ESource::LevelsTag);
    TestTrueExpr(FMath::IsNearlyEqual(R.HeightMeters, 15.0f, 0.01f));
    return true;
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOSMHeightResolverTest_SubtypeDefault,
    "OSMWorldGen.Buildings.HeightResolver.SubtypeDefault",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOSMHeightResolverTest_SubtypeDefault::RunTest(const FString& Parameters)
{
    FOSMFeature F;
    F.SubType = TEXT("house"); // Known subtype → 7.0 m

    auto R = FOSMBuildingHeightResolver::Resolve(F);
    TestEqual("Source is SubtypeDefault",
        R.Source, FOSMBuildingHeightResolver::FResolutionResult::ESource::SubtypeDefault);
    TestTrueExpr(FMath::IsNearlyEqual(R.HeightMeters, 7.0f, 0.01f));
    return true;
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOSMHeightResolverTest_GlobalFallback,
    "OSMWorldGen.Buildings.HeightResolver.GlobalFallback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOSMHeightResolverTest_GlobalFallback::RunTest(const FString& Parameters)
{
    FOSMFeature F;
    // No tags, unknown subtype → global default
    F.SubType = TEXT("unknown_type_xyz");

    auto R = FOSMBuildingHeightResolver::Resolve(F, 3.0f, 9.0f);
    TestEqual("Source is GlobalDefault",
        R.Source, FOSMBuildingHeightResolver::FResolutionResult::ESource::GlobalDefault);
    TestTrueExpr(FMath::IsNearlyEqual(R.HeightMeters, 9.0f, 0.01f));
    return true;
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOSMHeightResolverTest_FeetAndInches,
    "OSMWorldGen.Buildings.HeightResolver.FeetAndInches",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOSMHeightResolverTest_FeetAndInches::RunTest(const FString& Parameters)
{
    float OutMeters = 0.0f;
    // 5'11" = (5 × 0.3048) + (11 × 0.0254) = 1.524 + 0.2794 = 1.8034 m
    bool bParsed = FOSMBuildingHeightResolver::TryParseHeightTag(TEXT("5'11\""), OutMeters);
    TestTrue("Parsed successfully", bParsed);
    TestTrueExpr(FMath::IsNearlyEqual(OutMeters, 1.8034f, 0.01f));
    return true;
}
