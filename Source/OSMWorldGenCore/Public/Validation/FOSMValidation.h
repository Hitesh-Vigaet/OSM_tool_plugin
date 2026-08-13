// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * How badly a check failed (plan_v3_pipeline.md Phase 1.3).
 *
 * The distinction is load-bearing: Fatal means the file must not reach Phase 2 under any
 * circumstance, Warning means the data is usable but the user needs to know something about
 * it. There is deliberately no "maybe" level — a check that cannot decide is a check that
 * needs rewriting, not a third severity.
 */
enum class EOSMIssueSeverity : uint8
{
    Info,
    Warning,
    Fatal
};

/**
 * A single validation finding.
 *
 * Every issue carries a stable machine-readable Code as well as human text, so automation
 * tests can assert on the *specific reason* a file was rejected rather than merely that it
 * was rejected — which is the acceptance criterion "every rejection names the actual cause".
 * Matching on message text would make the tests break on every wording change; matching on
 * Code makes them break only when behaviour changes.
 */
struct OSMWORLDGENCORE_API FOSMValidationIssue
{
    EOSMIssueSeverity Severity = EOSMIssueSeverity::Info;

    /** Stable identifier, e.g. "osm.bounds.disjoint", "tif.compression.unsupported". */
    FString Code;

    /** Human-readable explanation, including the offending values. */
    FString Message;

    /** Optional OSM element id this issue concerns (0 when not element-specific). */
    int64 ElementId = 0;

    FOSMValidationIssue() = default;

    FOSMValidationIssue(EOSMIssueSeverity InSeverity, FString InCode, FString InMessage, int64 InElementId = 0)
        : Severity(InSeverity), Code(MoveTemp(InCode)), Message(MoveTemp(InMessage)), ElementId(InElementId)
    {
    }

    FString ToString() const;
};

/**
 * The verdict of one gate: a list of issues, plus the derived accept/reject decision.
 *
 * Note there is no separate bSucceeded flag. Acceptance is *computed* from the issues, so it
 * is impossible for a result to claim success while carrying a fatal issue — a class of bug
 * that two independent booleans would eventually produce.
 */
struct OSMWORLDGENCORE_API FOSMValidationResult
{
    TArray<FOSMValidationIssue> Issues;

    void Add(EOSMIssueSeverity Severity, const FString& Code, const FString& Message, int64 ElementId = 0)
    {
        Issues.Emplace(Severity, Code, Message, ElementId);
    }

    void AddFatal(const FString& Code, const FString& Message, int64 ElementId = 0)
    {
        Add(EOSMIssueSeverity::Fatal, Code, Message, ElementId);
    }

    void AddWarning(const FString& Code, const FString& Message, int64 ElementId = 0)
    {
        Add(EOSMIssueSeverity::Warning, Code, Message, ElementId);
    }

    void AddInfo(const FString& Code, const FString& Message, int64 ElementId = 0)
    {
        Add(EOSMIssueSeverity::Info, Code, Message, ElementId);
    }

    /** Merge another gate's findings into this one. */
    void Append(const FOSMValidationResult& Other)
    {
        Issues.Append(Other.Issues);
    }

    bool HasFatal() const { return CountOf(EOSMIssueSeverity::Fatal) > 0; }
    bool HasWarnings() const { return CountOf(EOSMIssueSeverity::Warning) > 0; }

    /** The decision. A file is accepted exactly when nothing fatal was found. */
    bool IsAccepted() const { return !HasFatal(); }

    int32 CountOf(EOSMIssueSeverity Severity) const;

    /** True if an issue with this exact code is present — the hook automation tests assert on. */
    bool HasCode(const FString& Code) const;

    /** First issue with the given severity, or nullptr. Used to headline a rejection. */
    const FOSMValidationIssue* FindFirst(EOSMIssueSeverity Severity) const;

    /** Multi-line listing of every issue, one per line. */
    FString ToString() const;
};
