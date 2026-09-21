// Copyright InviMind. All Rights Reserved.

#include "Sensors/AOSMInfraredCamera.h"
#include "Sensors/UOSMThermalMPC.h"
#include "Sensors/FOSMThermalSimulation.h"
#include "Sensors/UOSMThermalStateComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/PostProcessComponent.h"
#include "Components/SphereComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/DirectionalLight.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "HAL/IConsoleManager.h"
#include "EngineUtils.h"

AOSMInfraredCamera::AOSMInfraredCamera()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;

    // 1. Root collision sphere
    CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
    CollisionSphere->InitSphereRadius(40.0f);
    CollisionSphere->SetCollisionProfileName(TEXT("Spectator"));
    CollisionSphere->SetCanEverAffectNavigation(false);
    SetRootComponent(CollisionSphere);

    // 2. Flight movement component
    MovementComponent = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("MovementComponent"));
    MovementComponent->UpdatedComponent = CollisionSphere;
    MovementComponent->MaxSpeed = 5000.0f;
    MovementComponent->Acceleration = 10000.0f;
    MovementComponent->Deceleration = 10000.0f;

    // 3. Viewport / Player camera component
    CameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("CameraComponent"));
    CameraComponent->SetupAttachment(CollisionSphere);
    CameraComponent->FieldOfView = FieldOfView;
    CameraComponent->bUsePawnControlRotation = true;
    CameraComponent->bConstrainAspectRatio = false;
    CameraComponent->PostProcessBlendWeight = 1.0f;
    CameraComponent->PostProcessSettings.bOverride_VignetteIntensity = true;
    CameraComponent->PostProcessSettings.VignetteIntensity = 0.0f;

    // 4. Offscreen scene capture component for render targets / AI streams
    SceneCaptureComponent = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("SceneCaptureComponent"));
    SceneCaptureComponent->SetupAttachment(CollisionSphere);
    SceneCaptureComponent->FOVAngle = FieldOfView;
    SceneCaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
    SceneCaptureComponent->bCaptureEveryFrame = true;
    SceneCaptureComponent->bCaptureOnMovement = true;
    SceneCaptureComponent->PostProcessBlendWeight = 1.0f;
    SceneCaptureComponent->PostProcessSettings.bOverride_VignetteIntensity = true;
    SceneCaptureComponent->PostProcessSettings.VignetteIntensity = 0.0f;
    SceneCaptureComponent->ShowFlags.SetPostProcessing(true);

    // Automatically possess player 0 in Play-In-Editor mode
    AutoPossessPlayer = EAutoReceiveInput::Player0;
}

void AOSMInfraredCamera::BeginPlay()
{
    Super::BeginPlay();
    SetupRenderTarget();
    SetupPostProcess();
}

void AOSMInfraredCamera::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    // Increment frame counter for animated grain
    FrameCounterValue++;

    if (UWorld* World = GetWorld())
    {
        // Update simulation parameters based on time progression if running
        if (FOSMThermalSimulation::GlobalParams.bIsSimulationRunning)
        {
            float EffectiveDeltaTime = DeltaSeconds * FOSMThermalSimulation::GlobalParams.TimeMultiplier;
            
            // Fast forward the time of day
            FOSMThermalSimulation::GlobalParams.CurrentTimeOfDayHours += (EffectiveDeltaTime / 3600.0f);
            if (FOSMThermalSimulation::GlobalParams.CurrentTimeOfDayHours >= 24.0f)
            {
                FOSMThermalSimulation::GlobalParams.CurrentTimeOfDayHours = FMath::Fmod(FOSMThermalSimulation::GlobalParams.CurrentTimeOfDayHours, 24.0f);
            }

            // Update global environment variables (Sun, Ambient Temp)
            FOSMThermalSimulation::UpdateEnvironmentFromTime(FOSMThermalSimulation::GlobalParams);

            // Tick local thermal state components
            FOSMThermalSimulation::TickNearSensor(World, GetActorLocation(), 1000.0f, DeltaSeconds, FOSMThermalSimulation::GlobalParams);
        }

        // Always update MPC and camera parameters so editor sliders reflect immediately
        UOSMThermalMPC::UpdateFromWorld(World);
        SetAmbientTemperatureK(FOSMThermalSimulation::GlobalParams.AmbientTemperatureK);

        // Push category temperatures + SunDirection to the MID every frame
        UpdateCategoryTemperaturesOnMID(World);

        // Push updated material parameters (including FrameCounter, gradient params, ghosting params)
        UpdateMaterialParameters();

        // Rotate the scene's directional light (OSM_Sun) to match the simulation time of day
        UpdateSunRotation(World, FOSMThermalSimulation::GlobalParams.CurrentTimeOfDayHours);
    }
}

void AOSMInfraredCamera::PostInitializeComponents()
{
    Super::PostInitializeComponents();
    SetupRenderTarget();
    SetupPostProcess();
}

void AOSMInfraredCamera::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    SetupRenderTarget();
    SetupPostProcess();
}

void AOSMInfraredCamera::SetupRenderTarget()
{
    const int32 SafeWidth = FMath::Clamp(ResolutionX, 128, 3840);
    const int32 SafeHeight = FMath::Clamp(ResolutionY, 128, 2160);

    if (!ThermalRenderTarget || ThermalRenderTarget->SizeX != SafeWidth || ThermalRenderTarget->SizeY != SafeHeight)
    {
        ThermalRenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("IR_RenderTarget"));
        if (ThermalRenderTarget)
        {
            ThermalRenderTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
            ThermalRenderTarget->ClearColor = FLinearColor::Black;
            ThermalRenderTarget->bAutoGenerateMips = false;
            ThermalRenderTarget->InitAutoFormat(SafeWidth, SafeHeight);
            ThermalRenderTarget->UpdateResourceImmediate(true);
        }
    }

    if (!PreviousFrameRT || PreviousFrameRT->SizeX != SafeWidth || PreviousFrameRT->SizeY != SafeHeight)
    {
        PreviousFrameRT = NewObject<UTextureRenderTarget2D>(this, TEXT("IR_PreviousFrameRT"));
        if (PreviousFrameRT)
        {
            PreviousFrameRT->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
            PreviousFrameRT->ClearColor = FLinearColor::Black;
            PreviousFrameRT->bAutoGenerateMips = false;
            PreviousFrameRT->InitAutoFormat(SafeWidth, SafeHeight);
            PreviousFrameRT->UpdateResourceImmediate(true);
        }
    }

    if (SceneCaptureComponent)
    {
        SceneCaptureComponent->TextureTarget = ThermalRenderTarget;
        SceneCaptureComponent->FOVAngle = FieldOfView;
    }

    if (CameraComponent)
    {
        CameraComponent->FieldOfView = FieldOfView;
    }
}

void AOSMInfraredCamera::SetupPostProcess()
{
    UMaterialInterface* BaseMat = nullptr;

    if (!ThermalPostProcessMaterial.IsNull())
    {
        BaseMat = ThermalPostProcessMaterial.LoadSynchronous();
    }

    if (!BaseMat)
    {
        BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/OSMWorldGen/Materials/M_IRPostProcessV2.M_IRPostProcessV2"));
    }

    if (!BaseMat)
    {
        BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/OSMWorldGen/Materials/M_IRPostProcess.M_IRPostProcess"));
    }

    if (BaseMat)
    {
        ThermalMID = UMaterialInstanceDynamic::Create(BaseMat, this);

        UpdateMaterialParameters();
        UpdateCategoryTemperaturesOnMID(GetWorld());

        // Apply to CameraComponent (for direct viewing / piloting in viewport)
        if (CameraComponent)
        {
            CameraComponent->bConstrainAspectRatio = false;
            CameraComponent->PostProcessSettings.bOverride_VignetteIntensity = true;
            CameraComponent->PostProcessSettings.VignetteIntensity = 0.0f;
            // Soft optical bloom to simulate FLIR Germanium lens diffusion / thermal blur
            CameraComponent->PostProcessSettings.bOverride_BloomIntensity = true;
            CameraComponent->PostProcessSettings.BloomIntensity = 0.45f;
            CameraComponent->PostProcessSettings.bOverride_BloomThreshold = true;
            CameraComponent->PostProcessSettings.BloomThreshold = 0.5f;

            CameraComponent->PostProcessSettings.WeightedBlendables.Array.Empty();
            FWeightedBlendable Blendable;
            Blendable.Object = ThermalMID;
            Blendable.Weight = 1.0f;
            CameraComponent->PostProcessSettings.WeightedBlendables.Array.Add(Blendable);
            CameraComponent->PostProcessBlendWeight = 1.0f;
        }

        // Apply to SceneCaptureComponent (for render target output)
        if (SceneCaptureComponent)
        {
            SceneCaptureComponent->PostProcessSettings.bOverride_BloomIntensity = true;
            SceneCaptureComponent->PostProcessSettings.BloomIntensity = 0.45f;
            SceneCaptureComponent->PostProcessSettings.bOverride_BloomThreshold = true;
            SceneCaptureComponent->PostProcessSettings.BloomThreshold = 0.5f;

            SceneCaptureComponent->PostProcessSettings.WeightedBlendables.Array.Empty();
            FWeightedBlendable Blendable;
            Blendable.Object = ThermalMID;
            Blendable.Weight = 1.0f;
            SceneCaptureComponent->PostProcessSettings.WeightedBlendables.Array.Add(Blendable);
            SceneCaptureComponent->PostProcessBlendWeight = 1.0f;
        }


    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("AOSMInfraredCamera: Could not load thermal post-process material. IR vision disabled."));
    }
}

namespace
{
    // Calibrated UB City afternoon baseline temperatures (in Kelvin)
    // Building wall categories are tightly clustered within minute decimal values (~307.8K to 309.0K)
    // to prevent sudden unnatural color jumps between adjacent buildings.
    static const float DefaultCategoryTempsK[16] = {
        308.00f,  //  0: Unknown       (34.85°C — ambient baseline)
        308.50f,  //  1: Concrete      (35.35°C — concrete facade wall)
        308.20f,  //  2: Brick         (35.05°C — brick facade wall)
        308.10f,  //  3: Stone         (34.95°C — stone facade wall)
        309.00f,  //  4: Metal         (35.85°C — metal facade, minute decimal diff)
        307.80f,  //  5: Glass         (34.65°C — architectural glass wall)
        308.00f,  //  6: Wood          (34.85°C — wood accents)
        308.50f,  //  7: Plastic       (35.35°C — panels)
        308.00f,  //  8: Plaster/Stucco (34.85°C — painted plaster wall)
        319.50f,  //  9: Asphalt       (46.35°C — hot sun-baked road)
        305.00f,  // 10: Soil/Earth    (31.85°C — shaded earth)
        301.50f,  // 11: Grass         (28.35°C — cool park lawn)
        298.00f,  // 12: Vegetation    (24.85°C — coolest dense trees)
        296.00f,  // 13: Water         (22.85°C — cold water body)
        308.40f,  // 14: CementBlock   (35.25°C — masonry block wall)
        318.00f,  // 15: RoofTile      (44.85°C — warm sunlit rooftop)
    };
}

void AOSMInfraredCamera::UpdateMaterialParameters()
{
    if (ThermalMID)
    {
        ThermalMID->SetScalarParameterValue(TEXT("PaletteMode"), static_cast<float>(ThermalPalette));
        ThermalMID->SetScalarParameterValue(TEXT("AmbientTempK"), AmbientTemperatureK);
        ThermalMID->SetScalarParameterValue(TEXT("HeatSensitivity"), HeatSensitivity);

        // Vertical gradient parameters
        ThermalMID->SetScalarParameterValue(TEXT("GroundLevelZ"), GroundLevelZ);
        ThermalMID->SetScalarParameterValue(TEXT("MaxHeightCm"), BuildingMaxHeightCm);

        // FLIR artifact parameters
        float EffectiveGrain = bEnableFLIRGhosting ? SensorGrainIntensity : 0.0f;
        float EffectiveScanline = bEnableFLIRGhosting ? ScanlineOpacity : 0.0f;
        float EffectiveVignette = bEnableFLIRGhosting ? VignetteStrength : 0.0f;

        ThermalMID->SetScalarParameterValue(TEXT("GrainIntensity"), EffectiveGrain);
        ThermalMID->SetScalarParameterValue(TEXT("ScanlineOpacity"), EffectiveScanline);
        ThermalMID->SetScalarParameterValue(TEXT("VignetteStrength"), EffectiveVignette);
        ThermalMID->SetScalarParameterValue(TEXT("FrameCounter"), static_cast<float>(FrameCounterValue));
    }
}

void AOSMInfraredCamera::UpdateCategoryTemperaturesOnMID(UWorld* World)
{
    if (!ThermalMID || !World) return;

    if (bUseStaticBakedThermal)
    {
        // Static mode: push calibrated temperatures directly, skip scene iteration
        for (int32 i = 0; i < 16; ++i)
        {
            ThermalMID->SetScalarParameterValue(
                FName(*FString::Printf(TEXT("CatTemp%d"), i)),
                DefaultCategoryTempsK[i]);
        }
    }
    else
    {
        // Dynamic mode: average from ThermalStateComponents
        float TempSum[16] = {0};
        int32 TempCount[16] = {0};

        for (TActorIterator<AActor> It(World); It; ++It)
        {
            UOSMThermalStateComponent* ThermalComp = It->FindComponentByClass<UOSMThermalStateComponent>();
            if (!ThermalComp) continue;

            for (const FOSMThermalZone& Zone : ThermalComp->Zones)
            {
                const int32 CatIdx = FMath::Clamp(static_cast<int32>(Zone.Category), 0, 15);
                TempSum[CatIdx] += Zone.CurrentTemperatureK;
                TempCount[CatIdx]++;
            }
        }

        // Push category temperatures to the material instance dynamic
        for (int32 i = 0; i < 16; ++i)
        {
            const float AvgTemp = TempCount[i] > 0 ? (TempSum[i] / TempCount[i]) : DefaultCategoryTempsK[i];
            ThermalMID->SetScalarParameterValue(FName(*FString::Printf(TEXT("CatTemp%d"), i)), AvgTemp);
        }
    }

    // Push SunDirection
    const FVector& SunDir = FOSMThermalSimulation::GlobalParams.SunDirection;
    ThermalMID->SetVectorParameterValue(FName(TEXT("SunDirection")), FLinearColor(SunDir.X, SunDir.Y, SunDir.Z, 0.0f));
}

void AOSMInfraredCamera::SetThermalPalette(EOSMThermalPalette NewPalette)
{
    ThermalPalette = NewPalette;
    UpdateMaterialParameters();
}

void AOSMInfraredCamera::SetAmbientTemperatureK(float NewAmbientK)
{
    AmbientTemperatureK = NewAmbientK;
    UpdateMaterialParameters();
}

void AOSMInfraredCamera::SetHeatSensitivity(float NewSensitivity)
{
    HeatSensitivity = NewSensitivity;
    UpdateMaterialParameters();
}

void AOSMInfraredCamera::CaptureThermalFrame()
{
    if (SceneCaptureComponent)
    {
        SceneCaptureComponent->CaptureScene();
    }
}

// ---------------------------------------------------------------------------
// Drone Flight Controls
// ---------------------------------------------------------------------------

void AOSMInfraredCamera::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    if (!PlayerInputComponent) return;

    PlayerInputComponent->BindAxis(TEXT("MoveForward"), this, &AOSMInfraredCamera::MoveForward);
    PlayerInputComponent->BindAxis(TEXT("MoveRight"), this, &AOSMInfraredCamera::MoveRight);
    PlayerInputComponent->BindAxis(TEXT("MoveUp"), this, &AOSMInfraredCamera::MoveUp);
    PlayerInputComponent->BindAxis(TEXT("Turn"), this, &AOSMInfraredCamera::Turn);
    PlayerInputComponent->BindAxis(TEXT("LookUp"), this, &AOSMInfraredCamera::LookUp);

    // Number keys for switching palettes in PIE mode: 1 = Ironbow, 2 = WhiteHot, 3 = BlackHot
    PlayerInputComponent->BindKey(EKeys::One, IE_Pressed, this, &AOSMInfraredCamera::SetIronbow);
    PlayerInputComponent->BindKey(EKeys::Two, IE_Pressed, this, &AOSMInfraredCamera::SetWhiteHot);
    PlayerInputComponent->BindKey(EKeys::Three, IE_Pressed, this, &AOSMInfraredCamera::SetBlackHot);
}

void AOSMInfraredCamera::MoveForward(float Val)
{
    if (Val != 0.0f)
    {
        AddMovementInput(GetActorForwardVector(), Val);
    }
}

void AOSMInfraredCamera::MoveRight(float Val)
{
    if (Val != 0.0f)
    {
        AddMovementInput(GetActorRightVector(), Val);
    }
}

void AOSMInfraredCamera::MoveUp(float Val)
{
    if (Val != 0.0f)
    {
        AddMovementInput(FVector::UpVector, Val);
    }
}

void AOSMInfraredCamera::Turn(float Val)
{
    AddControllerYawInput(Val);
}

void AOSMInfraredCamera::LookUp(float Val)
{
    AddControllerPitchInput(Val);
}

// ---------------------------------------------------------------------------
// Sun Rotation for Dynamic Time-of-Day
// ---------------------------------------------------------------------------

void AOSMInfraredCamera::UpdateSunRotation(UWorld* World, float TimeOfDayHours)
{
    if (!World) return;

    // Find the OSM_Sun directional light by label
    ADirectionalLight* SunLight = nullptr;
    for (TActorIterator<ADirectionalLight> It(World); It; ++It)
    {
#if WITH_EDITOR
        if (It->GetActorLabel() == TEXT("OSM_Sun"))
        {
            SunLight = *It;
            break;
        }
#else
        if (It->GetName().Contains(TEXT("OSM_Sun")))
        {
            SunLight = *It;
            break;
        }
#endif
    }

    if (!SunLight) return;

    float Hour = FMath::Fmod(TimeOfDayHours, 24.0f);
    float HourAngleRad = ((Hour - 6.0f) / 24.0f) * 2.0f * PI;
    float SunElevationSin = FMath::Sin(HourAngleRad);

    float SunPitch;
    if (SunElevationSin >= 0.0f)
    {
        SunPitch = -SunElevationSin * 45.0f;
    }
    else
    {
        SunPitch = -SunElevationSin * 80.0f;
    }

    FRotator CurrentRotation = SunLight->GetActorRotation();
    FRotator NewRotation(SunPitch, CurrentRotation.Yaw, 0.0f);
    SunLight->SetActorRotation(NewRotation);

    if (UDirectionalLightComponent* LightComp = Cast<UDirectionalLightComponent>(SunLight->GetLightComponent()))
    {
        if (SunElevationSin > 0.0f)
        {
            LightComp->SetIntensity(FMath::Lerp(0.5f, 3.5f, SunElevationSin));
        }
        else
        {
            LightComp->SetIntensity(0.05f);
        }
    }
}
