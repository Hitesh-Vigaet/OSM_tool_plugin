// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UWorld;

/**
 * Ensures a level has basic sky and lighting so it is never black by default
 * after an OSM import, without requiring manual actor placement.
 *
 * Detection is component-based (UDirectionalLightComponent / USkyLightComponent /
 * USkyAtmosphereComponent found on ANY actor in the world), not class-based, so a
 * pre-existing third-party sun/sky setup (e.g. Cesium's CesiumSunSky) is recognized
 * and left alone instead of being duplicated.
 */
class OSMWORLDGENGENERATORS_API FOSMSceneSetup
{
public:
    /**
     * Spawns whichever of {directional light, sky light, sky atmosphere, post process
     * volume} are missing from the world. Safe to call repeatedly — a second call after
     * the first is a no-op.
     */
    static void EnsureBasicSceneSetup(UWorld* World);
};
