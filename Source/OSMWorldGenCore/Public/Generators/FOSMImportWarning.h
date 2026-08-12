// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FOSMImportWarning.generated.h"

/**
 * Severity level of an import warning or error.
 */
UENUM(BlueprintType)
enum class EOSMImportWarningLevel : uint8
{
    Info,
    Warning,
    Error
};

/**
 * Single import warning or diagnostic emitted during pipeline execution.
 */
USTRUCT(BlueprintType)
struct OSMWORLDGENCORE_API FOSMImportWarning
{
    GENERATED_BODY()

    /** Warning severity level */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Diagnostics")
    EOSMImportWarningLevel Level = EOSMImportWarningLevel::Warning;

    /** Associated OSM ID (0 if global) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Diagnostics")
    int64 OSMId = 0;

    /** Diagnostic message */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM|Diagnostics")
    FText Message;

    FOSMImportWarning() = default;

    FOSMImportWarning(EOSMImportWarningLevel InLevel, int64 InOSMId, const FText& InMessage)
        : Level(InLevel), OSMId(InOSMId), Message(InMessage)
    {
    }
};
