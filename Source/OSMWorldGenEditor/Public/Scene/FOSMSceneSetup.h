// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FOSMRegion;

/**
 * Creates the minimum viewing environment for looking at an imported region: sun, sky, fog,
 * and a camera pointed at the data.
 *
 * This is the first thing in the pipeline that spawns actors, so the boundary is drawn
 * deliberately: it creates ENVIRONMENT only — never buildings, roads, terrain, or anything
 * derived from the city graph. The "no generated geometry before Phase 5" rule is about the
 * city, not about being able to see it. It is also user-triggered rather than automatic,
 * because silently adding actors to someone's level is not a thing a tool should do on its own.
 *
 * Every operation is idempotent: running it twice reuses the actors it made rather than
 * stacking a second sun on top of the first.
 */
class OSMWORLDGENEDITOR_API FOSMSceneSetup
{
public:
    /** What SetUpScene did, for reporting back to the user. */
    struct FResult
    {
        int32 ActorsCreated = 0;
        int32 ActorsReused = 0;
        TArray<FString> Notes;

        bool bSucceeded = false;
        FString Error;

        FString ToString() const;
    };

    /**
     * Ensure the level has a sun, sky, sky light and height fog, then frame the region.
     *
     * @param Region  Used to size the camera pull-back so the whole region is in view.
     */
    static FResult SetUpScene(const FOSMRegion& Region);

    /**
     * Move the editor viewport camera to look at the region, without touching any actors.
     *
     * Separate from SetUpScene because "I cannot see anything" is usually a camera problem, and
     * the fix for it should not require modifying the level.
     */
    static bool FrameRegion(const FOSMRegion& Region);

    /** Tag applied to actors this class creates, so they can be found and reused. */
    static const FName GetOwnedActorTag();
};
