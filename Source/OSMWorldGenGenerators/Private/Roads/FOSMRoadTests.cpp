// Copyright InviMind. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Roads/FOSMRoadMeshBuilder.h"
#include "Roads/FOSMIntersectionBuilder.h"
#include "Roads/UOSMRoadTypeDataAsset.h"
#include "Components/SplineComponent.h"
#include "ProceduralMeshComponent.h"
#include "Engine/World.h"
#include "Editor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOSMRoadMeshTest, "OSMWorldGen.Roads.MeshGeneration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOSMRoadMeshTest::RunTest(const FString& Parameters)
{
    // Skip if no world
    UWorld* World = nullptr;
#if WITH_EDITOR
    if (GEditor)
    {
        World = GEditor->GetEditorWorldContext().World();
    }
#endif
    if (!World)
    {
        return true; // Cannot test without world context for Actor spawning
    }

    // Spawn a dummy actor to hold the components
    AActor* DummyActor = World->SpawnActor<AActor>();
    
    USplineComponent* Spline = NewObject<USplineComponent>(DummyActor);
    Spline->RegisterComponent();
    Spline->ClearSplinePoints();
    Spline->AddSplinePoint(FVector(0, 0, 0), ESplineCoordinateSpace::Local, false);
    Spline->AddSplinePoint(FVector(1000, 0, 0), ESplineCoordinateSpace::Local, false);
    Spline->UpdateSpline();

    UProceduralMeshComponent* ProcMesh = NewObject<UProceduralMeshComponent>(DummyActor);
    ProcMesh->RegisterComponent();

    UOSMRoadTypeDataAsset* DummyAsset = NewObject<UOSMRoadTypeDataAsset>();
    DummyAsset->DefaultWidthMeters = 8.0f; // 800 cm

    // Test mesh generation
    FOSMRoadMeshBuilder::BuildRoadMesh(Spline, ProcMesh, DummyAsset, 0.0f, 0);

    // Verify geometry was created
    TestTrue("ProcMesh has section 0", ProcMesh->GetNumSections() > 0);
    
    // Cleanup
    DummyActor->Destroy();

    return true;
}
