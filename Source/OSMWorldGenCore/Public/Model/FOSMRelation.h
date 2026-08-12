// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Model/FOSMTagDictionary.h"
#include "FOSMRelation.generated.h"

/**
 * The type of an OSM relation member (node, way, or nested relation).
 */
UENUM(BlueprintType)
enum class EOSMRelationMemberType : uint8
{
    Node     UMETA(DisplayName = "Node"),
    Way      UMETA(DisplayName = "Way"),
    Relation UMETA(DisplayName = "Relation"),
};

/**
 * Resolved type of an OSM relation based on its tags.
 */
UENUM(BlueprintType)
enum class EOSMRelationType : uint8
{
    Unknown          UMETA(DisplayName = "Unknown"),
    Multipolygon     UMETA(DisplayName = "Multipolygon"),
    Route            UMETA(DisplayName = "Route"),
    Boundary         UMETA(DisplayName = "Boundary"),
    Restriction      UMETA(DisplayName = "Restriction"),
    Building         UMETA(DisplayName = "Building"),
    AssociatedStreet UMETA(DisplayName = "Associated Street"),
    Other            UMETA(DisplayName = "Other"),
};

/**
 * A single member of an OSM relation.
 */
USTRUCT(BlueprintType)
struct OSMWORLDGENCORE_API FOSMRelationMember
{
    GENERATED_BODY()

    /** Type of the member element */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM")
    EOSMRelationMemberType Type = EOSMRelationMemberType::Node;

    /** OSM ID of the referenced element */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM")
    int64 Ref = 0;

    /** Role of this member in the relation (e.g., "outer", "inner", "stop", "platform") */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM")
    FString Role;
};

/**
 * Represents an OSM relation — a group of nodes, ways, and/or other relations
 * with assigned roles and shared tags.
 *
 * The most important relation type is multipolygon, which is used for areas with
 * holes (e.g., a building with a courtyard, a lake with an island).
 */
USTRUCT(BlueprintType)
struct OSMWORLDGENCORE_API FOSMRelation
{
    GENERATED_BODY()

    /** Unique OSM relation ID */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM")
    int64 Id = 0;

    /** Ordered list of members */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM")
    TArray<FOSMRelationMember> Members;

    /** OSM tags for this relation */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM")
    TMap<FString, FString> Tags;

    /** Resolved relation type (computed from tags during classification) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OSM")
    EOSMRelationType ResolvedType = EOSMRelationType::Unknown;

    /** Default constructor */
    FOSMRelation() = default;

    /** Check if this is a multipolygon relation */
    bool IsMultipolygon() const
    {
        return ResolvedType == EOSMRelationType::Multipolygon;
    }

    /** Get all member IDs with a specific role */
    TArray<int64> GetMemberRefsByRole(const FString& RoleFilter) const
    {
        TArray<int64> Result;
        for (const FOSMRelationMember& Member : Members)
        {
            if (Member.Role == RoleFilter)
            {
                Result.Add(Member.Ref);
            }
        }
        return Result;
    }

    /** Get all way member IDs with a specific role */
    TArray<int64> GetWayMemberRefsByRole(const FString& RoleFilter) const
    {
        TArray<int64> Result;
        for (const FOSMRelationMember& Member : Members)
        {
            if (Member.Type == EOSMRelationMemberType::Way && Member.Role == RoleFilter)
            {
                Result.Add(Member.Ref);
            }
        }
        return Result;
    }

    /** Get tag value with default */
    FString GetTag(const FString& Key, const FString& DefaultValue = FString()) const
    {
        const FString* Value = Tags.Find(Key);
        return Value ? *Value : DefaultValue;
    }

    /**
     * Resolve the relation type from its tags.
     * Should be called after tags are populated.
     */
    void ResolveType()
    {
        const FString TypeTag = GetTag(TEXT("type"));

        if (TypeTag == TEXT("multipolygon"))
        {
            ResolvedType = EOSMRelationType::Multipolygon;
        }
        else if (TypeTag == TEXT("route"))
        {
            ResolvedType = EOSMRelationType::Route;
        }
        else if (TypeTag == TEXT("boundary"))
        {
            ResolvedType = EOSMRelationType::Boundary;
        }
        else if (TypeTag == TEXT("restriction"))
        {
            ResolvedType = EOSMRelationType::Restriction;
        }
        else if (TypeTag == TEXT("building"))
        {
            ResolvedType = EOSMRelationType::Building;
        }
        else if (TypeTag == TEXT("associatedStreet"))
        {
            ResolvedType = EOSMRelationType::AssociatedStreet;
        }
        else if (!TypeTag.IsEmpty())
        {
            ResolvedType = EOSMRelationType::Other;
        }
        // else remains Unknown
    }
};
