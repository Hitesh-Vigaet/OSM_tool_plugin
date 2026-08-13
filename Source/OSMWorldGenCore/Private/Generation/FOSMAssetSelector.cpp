// Copyright InviMind. All Rights Reserved.

#include "Generation/FOSMAssetSelector.h"
#include "Graph/UOSMCityGraph.h"

// ---------------------------------------------------------------------------
int32 FOSMAssetSelector::MakeNodeSeed(int32 RuleSeed, int32 NodeId)
{
    // HashCombine rather than addition or XOR: adjacent node ids must not produce adjacent
    // seeds, or a run of neighbouring buildings walks through the choice list in order and the
    // result reads as a pattern rather than a distribution.
    return static_cast<int32>(HashCombine(GetTypeHash(RuleSeed), GetTypeHash(NodeId)));
}

// ---------------------------------------------------------------------------
int32 FOSMAssetSelector::PickWeightedChoice(const FOSMAssetRule& Rule, int32 Seed)
{
    if (Rule.Choices.Num() == 0)
    {
        return INDEX_NONE;
    }

    // Only choices with a real asset are selectable. A configured-but-empty slot is a slot the
    // user has not filled in yet, and treating it as a valid outcome would silently place
    // nothing while reporting success.
    TArray<int32> Selectable;
    double TotalWeight = 0.0;

    for (int32 Index = 0; Index < Rule.Choices.Num(); ++Index)
    {
        const FOSMAssetChoice& Choice = Rule.Choices[Index];
        if (Choice.Asset.IsNull())
        {
            continue;
        }

        Selectable.Add(Index);
        TotalWeight += FMath::Max(0.0f, Choice.Weight);
    }

    if (Selectable.Num() == 0)
    {
        return INDEX_NONE;
    }

    FRandomStream Stream(Seed);

    if (TotalWeight <= 0.0)
    {
        // Every weight is zero: that expresses "no preference", not "never pick anything".
        return Selectable[Stream.RandRange(0, Selectable.Num() - 1)];
    }

    const double Roll = Stream.FRand() * TotalWeight;
    double Accumulated = 0.0;

    for (const int32 Index : Selectable)
    {
        Accumulated += FMath::Max(0.0f, Rule.Choices[Index].Weight);
        if (Roll <= Accumulated)
        {
            return Index;
        }
    }

    // Floating-point accumulation can leave Roll a hair above the total; falling back to the
    // last selectable choice keeps the function total rather than returning "nothing" for a
    // rule that plainly has options.
    return Selectable.Last();
}

// ---------------------------------------------------------------------------
FString FOSMAssetSelector::DescribeOutcome(EOSMSelectionOutcome Outcome)
{
    switch (Outcome)
    {
    case EOSMSelectionOutcome::Selected:             return TEXT("selected");
    case EOSMSelectionOutcome::InheritedFromCorridor: return TEXT("inherited from corridor");
    case EOSMSelectionOutcome::FallbackNoAsset:      return TEXT("fallback: rule has no usable asset");
    default:                                          return TEXT("fallback: no rule configured");
    }
}

// ---------------------------------------------------------------------------
TArray<FOSMAssetSelection> FOSMAssetSelector::SelectForGraph(const UOSMCityGraph& Graph)
{
    TArray<FOSMAssetSelection> Selections;
    Selections.Reserve(Graph.Nodes.Num());

    // Corridor membership, resolved once. A road segment inside a named corridor takes the
    // corridor's choice so a street keeps one surface along its length.
    TMap<int32, FString> NodeToCorridor;
    for (const FOSMNodeGroup& Group : Graph.Groups)
    {
        if (Group.Kind != EOSMGroupKind::Corridor) continue;

        for (const int32 NodeId : Group.NodeIds)
        {
            NodeToCorridor.Add(NodeId, Group.Name);
        }
    }

    // Corridor decisions are cached so every segment of one street resolves identically. The key
    // includes the node type, since two groups could share a name across types.
    TMap<FString, int32> CorridorChoice;

    for (const FOSMGraphNode& Node : Graph.Nodes)
    {
        // Junctions and terrain are structural, not things an asset is placed on.
        if (Node.Type == EOSMNodeType::Junction || Node.Type == EOSMNodeType::TerrainTile)
        {
            continue;
        }

        FOSMAssetSelection Selection;
        Selection.NodeId = Node.Id;
        Selection.NodeType = Node.Type;
        Selection.SubType = Node.SubType;

        const FOSMAssetRule* Rule = Graph.Config.FindRule(Node.Type, Node.SubType);
        if (!Rule)
        {
            Selection.Outcome = EOSMSelectionOutcome::FallbackNoRule;
            Selections.Add(MoveTemp(Selection));
            continue;
        }

        int32 ChoiceIndex = INDEX_NONE;
        bool bInherited = false;

        const FString* CorridorName = Rule->bSelectPerCorridor ? NodeToCorridor.Find(Node.Id) : nullptr;

        if (CorridorName)
        {
            const FString Key = FString::Printf(TEXT("%d|%s"), static_cast<int32>(Node.Type), **CorridorName);

            if (const int32* Cached = CorridorChoice.Find(Key))
            {
                ChoiceIndex = *Cached;
                bInherited = true;
            }
            else
            {
                // Seeded from the corridor NAME, not from the first segment's id: seeding from a
                // segment would make the whole street's surface depend on which segment happened
                // to be visited first, which changes when the region is re-fetched.
                const int32 CorridorSeed = MakeNodeSeed(Rule->Seed, static_cast<int32>(GetTypeHash(*CorridorName)));
                ChoiceIndex = PickWeightedChoice(*Rule, CorridorSeed);
                CorridorChoice.Add(Key, ChoiceIndex);
            }

            Selection.CorridorName = *CorridorName;
        }
        else
        {
            ChoiceIndex = PickWeightedChoice(*Rule, MakeNodeSeed(Rule->Seed, Node.Id));
        }

        if (ChoiceIndex == INDEX_NONE)
        {
            Selection.Outcome = EOSMSelectionOutcome::FallbackNoAsset;
        }
        else
        {
            Selection.ChoiceIndex = ChoiceIndex;
            Selection.Asset = Rule->Choices[ChoiceIndex].Asset;
            Selection.Label = Rule->Choices[ChoiceIndex].Label;
            Selection.Outcome = bInherited
                ? EOSMSelectionOutcome::InheritedFromCorridor
                : EOSMSelectionOutcome::Selected;
        }

        Selections.Add(MoveTemp(Selection));
    }

    return Selections;
}
