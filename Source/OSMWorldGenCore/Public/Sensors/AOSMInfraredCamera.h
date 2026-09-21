// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Materials/EOSMSurfaceCategory.h"
#include "AOSMInfraredCamera.generated.h"

class UCameraComponent;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class USphereComponent;
class UFloatingPawnMovement;
class UPostProcessComponent;

/**
 * Thermal color palettes for IR simulation.
 */
UENUM(BlueprintType)
enum class EOSMThermalPalette : uint8
{
    /** Ironbow: Vibrant military false-color spectrum (Deep Purple -> Magenta -> Orange -> Yellow -> White) */
    Ironbow  UMETA(DisplayName = "Ironbow (Vibrant False-Color)"),

    /** White-Hot: High dynamic contrast FLIR (Deep Black -> Charcoal -> Glowing White) */
    WhiteHot UMETA(DisplayName = "White Hot"),

    /** Black-Hot: Inverted FLIR (Hot objects dark, cold objects bright) */
    BlackHot UMETA(DisplayName = "Black Hot")
};

/**
 * Controllable Infrared / Thermal Camera Drone Pawn.
 *
 * Capabilities:
 * 1. Full-Screen Viewport Piloting: Right-click in Outliner -> "Pilot" or click "Spawn IR Camera" in Control Center.
 * 2. Play-In-Editor (PIE) Drone Flight: Press Play to automatically possess this pawn and fly with WASD + Space/Ctrl + Mouse.
 * 3. Real-time Palette Switching: 1 (Ironbow), 2 (White-Hot), 3 (Black-Hot) or Control Center toolbar buttons.
 * 4. Offscreen Stream: Outputs to a 1280x720 RenderTarget for drone HUDs and AI vision models.
 */
UCLASS(BlueprintType, Blueprintable)
class OSMWORLDGENCORE_API AOSMInfraredCamera : public APawn
{
    GENERATED_BODY()

public:
    AOSMInfraredCamera();

    // -----------------------------------------------------------------------
    // Components
    // -----------------------------------------------------------------------

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<USphereComponent> CollisionSphere;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UFloatingPawnMovement> MovementComponent;

    /** Viewport / Player camera component for direct piloting & possession */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UCameraComponent> CameraComponent;

    /** Offscreen capture component writing to RenderTarget */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<USceneCaptureComponent2D> SceneCaptureComponent;

    // -----------------------------------------------------------------------
    // Camera & Thermal Settings
    // -----------------------------------------------------------------------

    /** Output render target width in pixels (e.g. 1280 or 640) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IR Camera|Resolution", meta = (ClampMin = "128", ClampMax = "3840"))
    int32 ResolutionX = 1280;

    /** Output render target height in pixels (e.g. 720 or 480) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IR Camera|Resolution", meta = (ClampMin = "128", ClampMax = "2160"))
    int32 ResolutionY = 720;

    /** Camera field of view in degrees */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IR Camera|Optics", meta = (ClampMin = "10.0", ClampMax = "150.0"))
    float FieldOfView = 75.0f;

    /** Active thermal display color palette (Ironbow, White-Hot, Black-Hot) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IR Camera|Thermal")
    EOSMThermalPalette ThermalPalette = EOSMThermalPalette::Ironbow;

    /** Ambient temperature in Kelvin (affects reflected IR on low-emissivity metals) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IR Camera|Thermal", meta = (ClampMin = "200.0", ClampMax = "350.0"))
    float AmbientTemperatureK = 293.15f;

    /** Heat Sensitivity threshold. Higher means lower temperatures show up brighter. Lower means only very hot objects glow. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IR Camera|Thermal", meta = (ClampMin = "0.1", ClampMax = "10.0"))
    float HeatSensitivity = 1.0f;

    /** When true, skip per-frame thermal state averaging and use static calibrated values.
     *  Use this for the demo — temperatures are pre-tuned for UB City afternoon. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IR Camera|Thermal")
    bool bUseStaticBakedThermal = true;

    /** Z-level of ground plane in cm (used for vertical wall gradient in shader) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IR Camera|Thermal", meta = (ClampMin = "-100000.0", ClampMax = "100000.0"))
    float GroundLevelZ = 0.0f;

    /** Maximum building height in cm (used for normalizing vertical gradients) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IR Camera|Thermal", meta = (ClampMin = "100.0", ClampMax = "50000.0"))
    float BuildingMaxHeightCm = 5000.0f;

    // ---- FLIR Sensor Artifacts (Ghosting) ----

    /** Master toggle for all sensor artifact effects */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IR Camera|FLIR Artifacts")
    bool bEnableFLIRGhosting = true;

    /** How much of the previous frame bleeds into the current (0.0 = none, 0.15 = subtle trail) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IR Camera|FLIR Artifacts", meta = (ClampMin = "0.0", ClampMax = "0.5"))
    float TemporalBlendFactor = 0.15f;

    /** Sensor readout grain noise intensity (0.0 = clean, crisp image) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IR Camera|FLIR Artifacts", meta = (ClampMin = "0.0", ClampMax = "0.3"))
    float SensorGrainIntensity = 0.0f;

    /** Horizontal scanline overlay opacity (0.0 = clean modern digital stream) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IR Camera|FLIR Artifacts", meta = (ClampMin = "0.0", ClampMax = "0.2"))
    float ScanlineOpacity = 0.0f;

    /** Corner vignette darkening strength */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IR Camera|FLIR Artifacts", meta = (ClampMin = "0.0", ClampMax = "2.0"))
    float VignetteStrength = 0.4f;

    /** Post-process material asset used for thermal rendering */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IR Camera|Material")
    TSoftObjectPtr<UMaterialInterface> ThermalPostProcessMaterial;

    /** Render target where thermal frames are written */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IR Camera|Output")
    TObjectPtr<UTextureRenderTarget2D> ThermalRenderTarget;

    // -----------------------------------------------------------------------
    // Runtime API
    // -----------------------------------------------------------------------

    /** Switch the thermal palette at runtime */
    UFUNCTION(BlueprintCallable, Category = "OSM|IR Camera")
    void SetThermalPalette(EOSMThermalPalette NewPalette);

    /** Update ambient temperature at runtime */
    UFUNCTION(BlueprintCallable, Category = "OSM|IR Camera")
    void SetAmbientTemperatureK(float NewAmbientK);

    /** Trigger a manual capture if bCaptureEveryFrame is false */
    UFUNCTION(BlueprintCallable, Category = "OSM|IR Camera")
    void CaptureThermalFrame();

    /** Update heat sensitivity at runtime */
    UFUNCTION(BlueprintCallable, Category = "OSM|IR Camera")
    void SetHeatSensitivity(float NewSensitivity);

    // -----------------------------------------------------------------------
    // Pawn Movement & Input
    // -----------------------------------------------------------------------

    virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

    void MoveForward(float Val);
    void MoveRight(float Val);
    void MoveUp(float Val);
    void Turn(float Val);
    void LookUp(float Val);

    void SetIronbow()  { SetThermalPalette(EOSMThermalPalette::Ironbow); }
    void SetWhiteHot() { SetThermalPalette(EOSMThermalPalette::WhiteHot); }
    void SetBlackHot() { SetThermalPalette(EOSMThermalPalette::BlackHot); }

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void PostInitializeComponents() override;
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual bool ShouldTickIfViewportsOnly() const override { return true; }

public:
    void SetupRenderTarget();
    void SetupPostProcess();
    void UpdateMaterialParameters();

    /** Push all 16 category temperatures + SunDirection to the MID every frame */
    void UpdateCategoryTemperaturesOnMID(UWorld* World);

    /** Rotate the scene's directional light (OSM_Sun) to match the given time of day */
    void UpdateSunRotation(UWorld* World, float TimeOfDayHours);

private:
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> ThermalMID;

    /** Previous frame render target for temporal ghosting blend */
    UPROPERTY(Transient)
    TObjectPtr<UTextureRenderTarget2D> PreviousFrameRT;

    /** Frame counter for animated grain noise */
    int32 FrameCounterValue = 0;
};
