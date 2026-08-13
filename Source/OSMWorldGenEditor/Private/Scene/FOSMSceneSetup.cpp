// Copyright InviMind. All Rights Reserved.

#include "Scene/FOSMSceneSetup.h"
#include "Region/FOSMRegion.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "LevelEditorViewport.h"

const FName FOSMSceneSetup::GetOwnedActorTag()
{
    return FName(TEXT("OSMWorldGen.Environment"));
}

namespace
{
    UWorld* GetEditorWorld()
    {
        return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    }

    /**
     * Find an existing actor of the given class, preferring one this plugin made.
     *
     * Reuses ANY instance rather than only tagged ones: a level that already has a sun does not
     * need a second one, and quietly adding a competing directional light is exactly the kind of
     * "helpful" behaviour that makes a scene inexplicable later.
     */
    template <typename ActorType>
    ActorType* FindExistingActor(UWorld* World)
    {
        TActorIterator<ActorType> It(World);
        return It ? *It : nullptr;
    }

    template <typename ActorType>
    ActorType* FindOrSpawn(UWorld* World, const FTransform& Transform, bool& bOutCreated)
    {
        if (ActorType* Existing = FindExistingActor<ActorType>(World))
        {
            bOutCreated = false;
            return Existing;
        }

        FActorSpawnParameters Params;
        Params.ObjectFlags = RF_Transactional;   // so the user can undo the whole setup

        ActorType* Spawned = World->SpawnActor<ActorType>(ActorType::StaticClass(), Transform, Params);
        if (Spawned)
        {
            Spawned->Tags.AddUnique(FOSMSceneSetup::GetOwnedActorTag());
        }

        bOutCreated = (Spawned != nullptr);
        return Spawned;
    }
}

// ---------------------------------------------------------------------------
FString FOSMSceneSetup::FResult::ToString() const
{
    if (!bSucceeded)
    {
        return FString::Printf(TEXT("Scene setup failed: %s"), *Error);
    }

    FString Text = FString::Printf(TEXT("Scene ready — %d actor(s) created, %d reused."),
        ActorsCreated, ActorsReused);

    for (const FString& Note : Notes)
    {
        Text += FString::Printf(TEXT("\n  %s"), *Note);
    }
    return Text;
}

// ---------------------------------------------------------------------------
FOSMSceneSetup::FResult FOSMSceneSetup::SetUpScene(const FOSMRegion& Region)
{
    FResult Result;

    UWorld* World = GetEditorWorld();
    if (!World)
    {
        Result.Error = TEXT("No editor world is open. Open or create a level first.");
        return Result;
    }

    auto Account = [&Result](const TCHAR* Name, bool bCreated)
    {
        if (bCreated) ++Result.ActorsCreated;
        else          ++Result.ActorsReused;

        Result.Notes.Add(FString::Printf(TEXT("%s %s"), Name, bCreated ? TEXT("created") : TEXT("already present, reused")));
    };

    // ---- Sun ----
    {
        bool bCreated = false;

        // Pitched down 45 degrees and swung off-axis, so building footprints and road edges cast
        // readable shadows instead of being lit flat-on from directly above.
        const FTransform SunTransform(FRotator(-45.0f, 130.0f, 0.0f), FVector(0.0f, 0.0f, 20000.0f));

        if (ADirectionalLight* Sun = FindOrSpawn<ADirectionalLight>(World, SunTransform, bCreated))
        {
            if (bCreated)
            {
                Sun->SetActorLabel(TEXT("OSM_Sun"));
                if (UDirectionalLightComponent* Component = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
                {
                    Component->SetIntensity(3.5f);
                    Component->SetMobility(EComponentMobility::Movable);
                    Component->bAtmosphereSunLight = true;
                }
            }
            Account(TEXT("Directional light (sun):"), bCreated);
        }
    }

    // ---- Sky light ----
    {
        bool bCreated = false;
        const FTransform Transform(FRotator::ZeroRotator, FVector(0.0f, 0.0f, 20000.0f));

        if (ASkyLight* Sky = FindOrSpawn<ASkyLight>(World, Transform, bCreated))
        {
            if (bCreated)
            {
                Sky->SetActorLabel(TEXT("OSM_SkyLight"));
                if (USkyLightComponent* Component = Sky->GetLightComponent())
                {
                    // Movable and real-time captured: a static capture would bake the sky before
                    // the atmosphere exists and leave everything lit black.
                    Component->SetMobility(EComponentMobility::Movable);
                    Component->bRealTimeCapture = true;
                    Component->SetIntensity(1.0f);
                }
            }
            Account(TEXT("Sky light:"), bCreated);
        }
    }

    // ---- Atmosphere and fog ----
    //
    // Sky atmosphere and height fog are looked up by class path rather than linked directly,
    // which keeps this module free of a build dependency for two optional visual actors. If a
    // project has them stripped, setup still succeeds with a note instead of failing.
    struct FOptionalActor
    {
        const TCHAR* ClassPath;
        const TCHAR* Label;
        const TCHAR* Description;
    };

    static const FOptionalActor OptionalActors[] =
    {
        { TEXT("/Script/Engine.SkyAtmosphere"),       TEXT("OSM_SkyAtmosphere"), TEXT("Sky atmosphere:") },
        { TEXT("/Script/Engine.ExponentialHeightFog"), TEXT("OSM_HeightFog"),     TEXT("Height fog:") },
    };

    for (const FOptionalActor& Optional : OptionalActors)
    {
        UClass* ActorClass = LoadClass<AActor>(nullptr, Optional.ClassPath);
        if (!ActorClass)
        {
            Result.Notes.Add(FString::Printf(TEXT("%s class unavailable in this build, skipped"), Optional.Description));
            continue;
        }

        TActorIterator<AActor> ExistingIt(World, ActorClass);
        const bool bFound = static_cast<bool>(ExistingIt);

        if (bFound)
        {
            ++Result.ActorsReused;
            Result.Notes.Add(FString::Printf(TEXT("%s already present, reused"), Optional.Description));
            continue;
        }

        FActorSpawnParameters Params;
        Params.ObjectFlags = RF_Transactional;

        if (AActor* Spawned = World->SpawnActor<AActor>(ActorClass, FTransform::Identity, Params))
        {
            Spawned->SetActorLabel(Optional.Label);
            Spawned->Tags.AddUnique(GetOwnedActorTag());
            ++Result.ActorsCreated;
            Result.Notes.Add(FString::Printf(TEXT("%s created"), Optional.Description));
        }
    }

    FrameRegion(Region);

    Result.bSucceeded = true;
    return Result;
}

// ---------------------------------------------------------------------------
bool FOSMSceneSetup::FrameRegion(const FOSMRegion& Region)
{
    if (!GEditor)
    {
        return false;
    }

    // The graph draws centred on the world origin, so the camera goes there. Without this the
    // overlay is invisible for the most mundane reason possible: the camera is somewhere else,
    // and a 1 km region is only ~100,000 units across in a world that spans far more.
    const double ExtentCm = Region.IsValid()
        ? FMath::Max(Region.GetWidthKm(), Region.GetHeightKm()) * 1000.0 * 100.0
        : 100000.0;

    // Pull back far enough that the whole region fits, and look down at 45 degrees so the layout
    // reads as a map while still showing depth.
    const double Distance = ExtentCm * 0.9;
    const FVector Location(-Distance * 0.7, -Distance * 0.7, Distance * 0.7);
    const FRotator Rotation(-35.0f, 45.0f, 0.0f);

    bool bMoved = false;
    for (FLevelEditorViewportClient* ViewportClient : GEditor->GetLevelViewportClients())
    {
        if (!ViewportClient || ViewportClient->IsOrtho())
        {
            continue;
        }

        ViewportClient->SetViewLocation(Location);
        ViewportClient->SetViewRotation(Rotation);
        ViewportClient->Invalidate();
        bMoved = true;
    }

    return bMoved;
}
