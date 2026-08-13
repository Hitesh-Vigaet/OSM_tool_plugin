// Copyright InviMind. All Rights Reserved.

#include "Graph/FOSMGenerationConfig.h"

// ---------------------------------------------------------------------------
float FOSMAssetRule::GetTotalWeight() const
{
    float Total = 0.0f;
    for (const FOSMAssetChoice& Choice : Choices)
    {
        Total += FMath::Max(0.0f, Choice.Weight);
    }
    return Total;
}

float FOSMAssetRule::GetNormalisedRatio(int32 ChoiceIndex) const
{
    if (!Choices.IsValidIndex(ChoiceIndex))
    {
        return 0.0f;
    }

    const float Total = GetTotalWeight();
    if (Total <= 0.0f)
    {
        // Every weight is zero: report an even split rather than 0% everywhere, which would
        // read as "nothing will be chosen" when the real meaning is "no preference expressed".
        return Choices.Num() > 0 ? 1.0f / static_cast<float>(Choices.Num()) : 0.0f;
    }

    return FMath::Max(0.0f, Choices[ChoiceIndex].Weight) / Total;
}

// ---------------------------------------------------------------------------
const FOSMAssetRule* FOSMGenerationConfig::FindRule(EOSMNodeType NodeType, const FString& SubType) const
{
    // Exact subtype first, then the type-wide rule. Falling back the other way round would make
    // a general rule silently shadow the specific one the user just configured.
    const FOSMAssetRule* Fallback = nullptr;

    for (const FOSMAssetRule& Rule : Rules)
    {
        if (Rule.NodeType != NodeType) continue;

        if (Rule.SubType == SubType && !SubType.IsEmpty())
        {
            return &Rule;
        }
        if (Rule.SubType.IsEmpty())
        {
            Fallback = &Rule;
        }
    }

    return Fallback;
}

FOSMAssetRule& FOSMGenerationConfig::FindOrAddRule(EOSMNodeType NodeType, const FString& SubType)
{
    for (FOSMAssetRule& Rule : Rules)
    {
        if (Rule.NodeType == NodeType && Rule.SubType == SubType)
        {
            return Rule;
        }
    }

    FOSMAssetRule NewRule;
    NewRule.NodeType = NodeType;
    NewRule.SubType = SubType;

    // Roads are the case the per-corridor rule exists for; area features have no corridor.
    NewRule.bSelectPerCorridor = (NodeType == EOSMNodeType::RoadSegment);

    return Rules[Rules.Add(MoveTemp(NewRule))];
}
