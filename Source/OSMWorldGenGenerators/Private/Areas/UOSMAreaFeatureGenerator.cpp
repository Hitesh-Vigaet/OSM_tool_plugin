// Copyright InviMind. All Rights Reserved.

#include "Areas/UOSMAreaFeatureGenerator.h"
#include "Terrain/FOSMDEMSampler.h"
#include "CRS/FOSMCRSTransformer.h"
#include "Model/FOSMFeature.h"
#include "Model/FOSMFeatureTable.h"
#include "OSMWorldGenGenerators.h"

#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "EngineUtils.h"
#include "CompGeom/PolygonTriangulation.h"
#include "Algo/Reverse.h"

namespace
{
    /** Categories rendered as filled areas (closed rings). */
    bool IsAreaCategory(EOSMFeatureType Type)
    {
        return Type == EOSMFeatureType::WaterArea
            || Type == EOSMFeatureType::NaturalArea
            || Type == EOSMFeatureType::Landuse
            || Type == EOSMFeatureType::Leisure;
    }

    /** Categories rendered as ribbons along a polyline. */
    bool IsLinearCategory(EOSMFeatureType Type)
    {
        return Type == EOSMFeatureType::Waterway
            || Type == EOSMFeatureType::Railway
            || Type == EOSMFeatureType::Barrier
            || Type == EOSMFeatureType::Power;
    }

    FString CategoryActorLabel(EOSMFeatureType Type)
    {
        switch (Type)
        {
        case EOSMFeatureType::WaterArea:   return TEXT("OSM_Water");
        case EOSMFeatureType::Waterway:    return TEXT("OSM_Waterways");
        case EOSMFeatureType::NaturalArea: return TEXT("OSM_Vegetation");
        case EOSMFeatureType::Landuse:     return TEXT("OSM_Landuse");
        case EOSMFeatureType::Leisure:     return TEXT("OSM_Leisure");
        case EOSMFeatureType::Railway:     return TEXT("OSM_Railways");
        case EOSMFeatureType::Barrier:     return TEXT("OSM_Barriers");
        case EOSMFeatureType::Power:       return TEXT("OSM_Power");
        default:                           return TEXT("OSM_Other");
        }
    }

    /**
     * Per-category draw order. Land use is the backdrop, water sits above it, small leisure
     * areas above that, linear features on top — otherwise coincident flat surfaces z-fight.
     */
    float CategoryZOrder(EOSMFeatureType Type)
    {
        switch (Type)
        {
        case EOSMFeatureType::Landuse:     return 0.0f;
        case EOSMFeatureType::NaturalArea: return 1.0f;
        case EOSMFeatureType::WaterArea:   return 2.0f;
        case EOSMFeatureType::Leisure:     return 3.0f;
        case EOSMFeatureType::Waterway:    return 4.0f;
        case EOSMFeatureType::Railway:     return 5.0f;
        case EOSMFeatureType::Barrier:     return 6.0f;
        case EOSMFeatureType::Power:       return 7.0f;
        default:                           return 8.0f;
        }
    }

    /** Remove consecutive duplicate points and any repeated closing vertex. */
    void CleanRing(const TArray<FVector2D>& In, TArray<FVector2D>& Out)
    {
        Out.Reset(In.Num());
        for (const FVector2D& P : In)
        {
            if (Out.Num() == 0 || FVector2D::Distance(Out.Last(), P) >= 1.0)
            {
                Out.Add(P);
            }
        }
        while (Out.Num() >= 2 && FVector2D::Distance(Out[0], Out.Last()) < 1.0)
        {
            Out.Pop();
        }
    }

    double SignedArea2D(const TArray<FVector2D>& Ring)
    {
        double Area = 0.0;
        for (int32 i = 0, n = Ring.Num(); i < n; ++i)
        {
            const FVector2D& A = Ring[i];
            const FVector2D& B = Ring[(i + 1) % n];
            Area += (A.X * B.Y) - (B.X * A.Y);
        }
        return Area * 0.5;
    }
}

// ---------------------------------------------------------------------------
FLinearColor UOSMAreaFeatureGenerator::GetCategoryColor(EOSMFeatureType Type, const FString& SubType)
{
    // Flat, map-like colours. The point at this stage is legibility — being able to see at a
    // glance that water, vegetation and land use actually loaded — not realism.
    switch (Type)
    {
    case EOSMFeatureType::WaterArea:
    case EOSMFeatureType::Waterway:
        return FLinearColor(0.10f, 0.35f, 0.75f);

    case EOSMFeatureType::NaturalArea:
        if (SubType.Equals(TEXT("wood"), ESearchCase::IgnoreCase)) return FLinearColor(0.06f, 0.32f, 0.10f);
        if (SubType.Equals(TEXT("scrub"), ESearchCase::IgnoreCase)) return FLinearColor(0.30f, 0.48f, 0.18f);
        if (SubType.Equals(TEXT("sand"), ESearchCase::IgnoreCase) ||
            SubType.Equals(TEXT("beach"), ESearchCase::IgnoreCase)) return FLinearColor(0.80f, 0.72f, 0.45f);
        if (SubType.Equals(TEXT("wetland"), ESearchCase::IgnoreCase)) return FLinearColor(0.25f, 0.45f, 0.42f);
        return FLinearColor(0.20f, 0.45f, 0.16f);

    case EOSMFeatureType::Leisure:
        if (SubType.Equals(TEXT("pitch"), ESearchCase::IgnoreCase)) return FLinearColor(0.15f, 0.55f, 0.25f);
        if (SubType.Equals(TEXT("swimming_pool"), ESearchCase::IgnoreCase)) return FLinearColor(0.15f, 0.55f, 0.80f);
        return FLinearColor(0.25f, 0.60f, 0.25f);

    case EOSMFeatureType::Landuse:
        if (SubType.Equals(TEXT("residential"), ESearchCase::IgnoreCase)) return FLinearColor(0.45f, 0.42f, 0.38f);
        if (SubType.Equals(TEXT("industrial"), ESearchCase::IgnoreCase)) return FLinearColor(0.45f, 0.35f, 0.45f);
        if (SubType.Equals(TEXT("farmland"), ESearchCase::IgnoreCase) ||
            SubType.Equals(TEXT("orchard"), ESearchCase::IgnoreCase)) return FLinearColor(0.55f, 0.55f, 0.20f);
        if (SubType.Equals(TEXT("cemetery"), ESearchCase::IgnoreCase)) return FLinearColor(0.30f, 0.40f, 0.28f);
        if (SubType.Equals(TEXT("construction"), ESearchCase::IgnoreCase)) return FLinearColor(0.60f, 0.50f, 0.30f);
        return FLinearColor(0.40f, 0.40f, 0.35f);

    case EOSMFeatureType::Railway:  return FLinearColor(0.25f, 0.25f, 0.28f);
    case EOSMFeatureType::Barrier:  return FLinearColor(0.35f, 0.28f, 0.22f);
    case EOSMFeatureType::Power:    return FLinearColor(0.70f, 0.65f, 0.20f);
    default:                        return FLinearColor(0.5f, 0.5f, 0.5f);
    }
}

// ---------------------------------------------------------------------------
bool UOSMAreaFeatureGenerator::Generate(
    const FOSMGenerationContext& Context,
    TArray<AActor*>& OutActors)
{
    if (!Context.CRSTransformer || !Context.CRSTransformer->IsInitialized())
    {
        UE_LOG(LogOSMWorldGenGenerators, Error, TEXT("AreaGenerator: CRS Transformer is not initialized!"));
        return false;
    }
    if (!Context.TargetWorld || !Context.FeatureTable)
    {
        UE_LOG(LogOSMWorldGenGenerators, Error, TEXT("AreaGenerator: missing world or feature table!"));
        return false;
    }

    // Same shared ground surface the terrain, road and building generators use.
    FOSMDEMSampler GroundSampler;
    if (!Context.DEMFilePath.IsEmpty())
    {
        GroundSampler.Load(Context.DEMFilePath);
    }

    static const EOSMFeatureType Categories[] = {
        EOSMFeatureType::Landuse,
        EOSMFeatureType::NaturalArea,
        EOSMFeatureType::WaterArea,
        EOSMFeatureType::Leisure,
        EOSMFeatureType::Waterway,
        EOSMFeatureType::Railway,
        EOSMFeatureType::Barrier,
        EOSMFeatureType::Power,
    };

    for (EOSMFeatureType Type : Categories)
    {
        if (Context.bCancelRequested && *Context.bCancelRequested) return false;

        TArray<const FOSMFeature*> Features = Context.FeatureTable->GetFeaturesByType(Type);
        if (Features.Num() == 0)
        {
            continue;
        }

        const float ZOffset = BaseZOffsetCm * CategoryZOrder(Type);
        if (AActor* Actor = BuildCategoryActor(Context, Type, Features, GroundSampler, ZOffset))
        {
            OutActors.Add(Actor);
            UE_LOG(LogOSMWorldGenGenerators, Log, TEXT("AreaGenerator: %s — %d features"),
                *CategoryActorLabel(Type), Features.Num());
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
AActor* UOSMAreaFeatureGenerator::BuildCategoryActor(
    const FOSMGenerationContext& Context,
    EOSMFeatureType Type,
    const TArray<const FOSMFeature*>& Features,
    const FOSMDEMSampler& GroundSampler,
    float ZOffsetCm)
{
    UWorld* World = Context.TargetWorld.Get();
    if (!World) return nullptr;

    const FString Label = CategoryActorLabel(Type);

    // Regenerating should replace the previous pass, not stack a second copy on top of it.
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (It->GetActorLabel() == Label)
        {
            World->DestroyActor(*It);
        }
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
    AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParams);
    if (!Actor) return nullptr;

    Actor->SetActorLabel(Label);

    USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
    Actor->SetRootComponent(Root);
    Root->RegisterComponent();

    UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(Actor, TEXT("AreaMesh"));
    Mesh->SetupAttachment(Root);
    Actor->AddInstanceComponent(Mesh);
    Mesh->RegisterComponent();

    const bool bArea = IsAreaCategory(Type);
    const bool bLinear = IsLinearCategory(Type);

    int32 SectionIndex = 0;

    for (const FOSMFeature* Feature : Features)
    {
        if (!Feature) continue;

        TArray<FVector> Vertices;
        TArray<int32> Triangles;
        TArray<FVector> Normals;
        TArray<FVector2D> UVs;
        TArray<FColor> VertexColors;
        TArray<FProcMeshTangent> Tangents;

        auto GroundZAt = [&](double Lat, double Lon) -> double
        {
            const double ElevM = GroundSampler.IsLoaded()
                ? GroundSampler.SampleElevationSafe(Lat, Lon, 0.0)
                : 0.0;
            return ElevM * 100.0 + ZOffsetCm;
        };

        if (bArea && Feature->HasPolygon())
        {
            const TArray<FVector>& RingLatLon = Feature->GetOuterRing();

            TArray<FVector2D> Ring2D;
            Ring2D.Reserve(RingLatLon.Num());
            double SumZ = 0.0;
            for (const FVector& LatLon : RingLatLon)
            {
                const FVector P = Context.CRSTransformer->TransformToUnreal(LatLon.X, LatLon.Y, 0.0);
                Ring2D.Add(FVector2D(P.X, P.Y));
                SumZ += GroundZAt(LatLon.X, LatLon.Y);
            }

            TArray<FVector2D> Clean;
            CleanRing(Ring2D, Clean);
            if (Clean.Num() < 3) continue;

            // Areas are laid flat at the ring's mean ground height. Draping each vertex on the
            // DEM independently would tilt and tear these polygons on sloped ground, and at
            // SRTM's ~90 m sample spacing that detail isn't real anyway.
            const double FlatZ = SumZ / FMath::Max(1, RingLatLon.Num());

            // Normalise to CCW so the triangulator's output winding is predictable.
            if (SignedArea2D(Clean) < 0.0)
            {
                Algo::Reverse(Clean);
            }
            if (FMath::Abs(SignedArea2D(Clean)) < 100.0) continue; // < 0.01 m^2

            TArray<UE::Geometry::FIndex3i> Tris;
            PolygonTriangulation::TriangulateSimplePolygon<double>(Clean, Tris, false);
            if (Tris.Num() == 0) continue;

            Vertices.Reserve(Clean.Num());
            for (const FVector2D& P : Clean)
            {
                Vertices.Add(FVector(P.X, P.Y, FlatZ));
                Normals.Add(FVector::UpVector);
                UVs.Add(P / 10000.0f);
                Tangents.Add(FProcMeshTangent(1.0f, 0.0f, 0.0f));
            }

            for (const UE::Geometry::FIndex3i& T : Tris)
            {
                // Unreal front faces are wound clockwise viewed from the front, so an
                // up-facing triangle needs a negative 2D signed cross product.
                const FVector2D& A = Clean[T.A];
                const FVector2D& B = Clean[T.B];
                const FVector2D& C = Clean[T.C];
                const double Cross = (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);

                if (Cross < 0.0)
                {
                    Triangles.Add(T.A); Triangles.Add(T.B); Triangles.Add(T.C);
                }
                else
                {
                    Triangles.Add(T.A); Triangles.Add(T.C); Triangles.Add(T.B);
                }
            }
        }
        else if (bLinear && Feature->HasPolyline())
        {
            const TArray<FVector>& Line = Feature->Polyline;
            const float HalfW = DefaultLinearWidthCm * 0.5f;

            TArray<FVector> Pts;
            Pts.Reserve(Line.Num());
            for (const FVector& LatLon : Line)
            {
                FVector P = Context.CRSTransformer->TransformToUnreal(LatLon.X, LatLon.Y, 0.0);
                P.Z = GroundZAt(LatLon.X, LatLon.Y);
                Pts.Add(P);
            }
            if (Pts.Num() < 2) continue;

            for (int32 i = 0; i < Pts.Num(); ++i)
            {
                const FVector Prev = Pts[FMath::Max(0, i - 1)];
                const FVector Next = Pts[FMath::Min(Pts.Num() - 1, i + 1)];
                FVector Dir = (Next - Prev);
                Dir.Z = 0.0;
                if (!Dir.Normalize())
                {
                    Dir = FVector::ForwardVector;
                }
                const FVector Side = FVector::CrossProduct(FVector::UpVector, Dir).GetSafeNormal();

                Vertices.Add(Pts[i] - Side * HalfW);
                Vertices.Add(Pts[i] + Side * HalfW);
                Normals.Add(FVector::UpVector);
                Normals.Add(FVector::UpVector);
                UVs.Add(FVector2D(0.0f, i));
                UVs.Add(FVector2D(1.0f, i));
                Tangents.Add(FProcMeshTangent(Dir.X, Dir.Y, 0.0f));
                Tangents.Add(FProcMeshTangent(Dir.X, Dir.Y, 0.0f));
            }

            for (int32 i = 0; i + 1 < Pts.Num(); ++i)
            {
                const int32 B = i * 2;
                // Clockwise-from-above winding, matching the road mesh builder.
                Triangles.Add(B + 0); Triangles.Add(B + 1); Triangles.Add(B + 3);
                Triangles.Add(B + 0); Triangles.Add(B + 3); Triangles.Add(B + 2);
            }
        }

        if (Vertices.Num() == 0 || Triangles.Num() == 0) continue;

        const FLinearColor Color = GetCategoryColor(Type, Feature->SubType);
        VertexColors.Init(Color.ToFColor(false), Vertices.Num());

        Mesh->CreateMeshSection(SectionIndex, Vertices, Triangles, Normals, UVs, VertexColors, Tangents, /*bCreateCollision=*/false);
        ++SectionIndex;
    }

    if (SectionIndex == 0)
    {
        World->DestroyActor(Actor);
        return nullptr;
    }

    // One flat colour per category. WorldGridMaterial ignores the colour parameters, so this
    // is best-effort: if the project supplies no material the shapes still render (untinted)
    // rather than not appearing at all.
    if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial")))
    {
        if (UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, Actor))
        {
            const FLinearColor Color = GetCategoryColor(Type, Features.Num() > 0 ? Features[0]->SubType : FString());
            static const FName ColorParams[] = { TEXT("BaseColor"), TEXT("Color"), TEXT("TintColor"), TEXT("Albedo") };
            for (const FName& Param : ColorParams)
            {
                MID->SetVectorParameterValue(Param, Color);
            }
            Mesh->SetMaterial(0, MID);
        }
    }

    return Actor;
}
