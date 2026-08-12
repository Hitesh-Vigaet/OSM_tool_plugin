// Copyright InviMind. All Rights Reserved.

#include "Scene/FOSMSceneSetup.h"
#include "Engine/World.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/PostProcessVolume.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "EngineUtils.h"

void FOSMSceneSetup::EnsureBasicSceneSetup(UWorld* World)
{
    if (!World)
    {
        return;
    }

    bool bHasDirectionalLight = false;
    bool bHasSkyLight = false;
    bool bHasSkyAtmosphere = false;
    bool bHasPostProcessVolume = false;

    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        if (!bHasDirectionalLight && Actor->FindComponentByClass<UDirectionalLightComponent>())
        {
            bHasDirectionalLight = true;
        }
        if (!bHasSkyLight && Actor->FindComponentByClass<USkyLightComponent>())
        {
            bHasSkyLight = true;
        }
        if (!bHasSkyAtmosphere && Actor->FindComponentByClass<USkyAtmosphereComponent>())
        {
            bHasSkyAtmosphere = true;
        }
        if (!bHasPostProcessVolume && Actor->IsA<APostProcessVolume>())
        {
            bHasPostProcessVolume = true;
        }

        if (bHasDirectionalLight && bHasSkyLight && bHasSkyAtmosphere && bHasPostProcessVolume)
        {
            break;
        }
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.ObjectFlags |= RF_Transactional;

    if (!bHasDirectionalLight)
    {
        if (ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(FVector::ZeroVector, FRotator(-45.0f, -45.0f, 0.0f), SpawnParams))
        {
#if WITH_EDITOR
            Sun->SetActorLabel(TEXT("OSMWorldGen_Sun"));
#endif
            Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);
        }
    }

    if (!bHasSkyLight)
    {
        if (ASkyLight* SkyLightActor = World->SpawnActor<ASkyLight>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams))
        {
#if WITH_EDITOR
            SkyLightActor->SetActorLabel(TEXT("OSMWorldGen_SkyLight"));
#endif
            if (USkyLightComponent* Comp = SkyLightActor->GetLightComponent())
            {
                Comp->SetMobility(EComponentMobility::Movable);
                Comp->RecaptureSky();
            }
        }
    }

    if (!bHasSkyAtmosphere)
    {
        if (ASkyAtmosphere* Atmosphere = World->SpawnActor<ASkyAtmosphere>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams))
        {
#if WITH_EDITOR
            Atmosphere->SetActorLabel(TEXT("OSMWorldGen_SkyAtmosphere"));
#endif
        }
    }

    if (!bHasPostProcessVolume)
    {
        if (APostProcessVolume* PPVolume = World->SpawnActor<APostProcessVolume>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams))
        {
#if WITH_EDITOR
            PPVolume->SetActorLabel(TEXT("OSMWorldGen_PostProcess"));
#endif
            PPVolume->bUnbound = true;
        }
    }
}
