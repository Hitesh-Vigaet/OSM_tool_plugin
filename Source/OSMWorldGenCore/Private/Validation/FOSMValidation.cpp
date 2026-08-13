// Copyright InviMind. All Rights Reserved.

#include "Validation/FOSMValidation.h"

namespace
{
    const TCHAR* SeverityLabel(EOSMIssueSeverity Severity)
    {
        switch (Severity)
        {
        case EOSMIssueSeverity::Fatal:   return TEXT("FATAL");
        case EOSMIssueSeverity::Warning: return TEXT("WARN");
        default:                         return TEXT("INFO");
        }
    }
}

// ---------------------------------------------------------------------------
FString FOSMValidationIssue::ToString() const
{
    FString Result = FString::Printf(TEXT("[%s] %s: %s"), SeverityLabel(Severity), *Code, *Message);
    if (ElementId != 0)
    {
        Result += FString::Printf(TEXT(" (OSM id %lld)"), ElementId);
    }
    return Result;
}

// ---------------------------------------------------------------------------
int32 FOSMValidationResult::CountOf(EOSMIssueSeverity Severity) const
{
    int32 Count = 0;
    for (const FOSMValidationIssue& Issue : Issues)
    {
        if (Issue.Severity == Severity) ++Count;
    }
    return Count;
}

bool FOSMValidationResult::HasCode(const FString& Code) const
{
    return Issues.ContainsByPredicate(
        [&Code](const FOSMValidationIssue& Issue) { return Issue.Code == Code; });
}

const FOSMValidationIssue* FOSMValidationResult::FindFirst(EOSMIssueSeverity Severity) const
{
    return Issues.FindByPredicate(
        [Severity](const FOSMValidationIssue& Issue) { return Issue.Severity == Severity; });
}

FString FOSMValidationResult::ToString() const
{
    if (Issues.Num() == 0)
    {
        return TEXT("No issues.");
    }

    TArray<FString> Lines;
    Lines.Reserve(Issues.Num());
    for (const FOSMValidationIssue& Issue : Issues)
    {
        Lines.Add(Issue.ToString());
    }
    return FString::Join(Lines, TEXT("\n"));
}
