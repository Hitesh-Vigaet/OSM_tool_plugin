// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FOSMDEMSampler;

/**
 * Lat/lon to local metres, about a fixed origin.
 *
 * Equirectangular, which is accurate to a few centimetres across a city-sized region and needs no
 * projection library. Defined once and shared, because the whole point of the terrain surface is
 * that every part of the pipeline agrees on where a coordinate lands — two copies of this maths
 * that drift apart would reintroduce exactly the problem it exists to solve.
 *
 * X is north (latitude), Y is east (longitude), both in METRES.
 */
struct OSMWORLDGENCORE_API FOSMLocalProjection
{
    static constexpr double MetersPerDegreeLat = 111320.0;

    double OriginLat = 0.0;
    double OriginLon = 0.0;
    double LonScale = MetersPerDegreeLat;

    FOSMLocalProjection() = default;
    FOSMLocalProjection(double InOriginLat, double InOriginLon);

    FVector2D ToMeters(const FVector2D& LatLon) const;
    FVector2D ToLatLon(const FVector2D& Meters) const;
};

/**
 * The ground. One height grid, and the single authority on how high the ground is anywhere.
 *
 * This exists because the pipeline previously had TWO definitions of ground level. The terrain was
 * a grid sampled every 8 m and linearly interpolated between nodes; roads and areas each sampled
 * the raw DEM at their own vertices, continuously. On a slope those two answers differ by tens of
 * centimetres while the layer offsets separating them are only a few — so the terrain won in
 * places and pushed up through the roads, and elsewhere the roads floated. Measured on a real
 * region: 7.8% of road vertices ended up below the ground they were supposed to sit on.
 *
 * So nothing samples the DEM directly any more. Everything asks this class, and HeightCmAt
 * evaluates the EXACT surface the mesh renders — same grid, same diagonal, same interpolation.
 * A road vertex placed at this height sits precisely on the terrain plane, and a 4 cm lift is
 * then reliably 4 cm of clearance rather than a coin toss.
 *
 * Heights are in centimetres relative to the DEM's lowest point, so the world origin sits at the
 * bottom of the terrain rather than an arbitrary distance below sea level.
 */
class OSMWORLDGENCORE_API FOSMTerrainSurface
{
public:
    /**
     * Sample the DEM into a grid covering the given bounds.
     *
     * @param Sampler       DEM to sample. Null builds a flat surface at zero, which is a valid
     *                      ground for a region with no elevation data rather than an error.
     * @param BoundsMeters  Extent to cover, in local metres. Must include every piece of geometry
     *                      that will be grounded: anything outside gets the nearest edge height,
     *                      which is a plausible answer but not a correct one.
     * @param QuadMeters    Target grid spacing.
     */
    bool Build(const FOSMDEMSampler* Sampler, const FOSMLocalProjection& InProjection,
               const FBox2D& BoundsMeters, double QuadMeters);

    bool IsValid() const { return Heights.Num() > 0; }

    /**
     * Ground height in cm at a position in local metres.
     *
     * Evaluates the rendered surface itself, not the DEM — see the class comment. Outside the
     * grid the nearest edge value is returned, so geometry beyond the terrain sits at a sensible
     * height rather than dropping to zero.
     */
    double HeightCmAt(double XMeters, double YMeters) const;

    double HeightCmAt(const FVector2D& Meters) const { return HeightCmAt(Meters.X, Meters.Y); }

    /**
     * Lowest and highest ground anywhere under a polygon, in cm. Ring is in local metres.
     *
     * Exact, not sampled. The surface is planar within each triangle, so its extremes over a
     * region can only occur at a grid node inside it or somewhere on its boundary; and along a
     * boundary edge the height is linear between the points where that edge crosses from one
     * triangle to the next. Evaluating the ring vertices, every triangle crossing along the ring,
     * and every interior grid node therefore cannot miss an extreme.
     *
     * Reading only the ring's CORNERS is what put buildings into hillsides: a footprint wider than
     * one grid quad can straddle a ridge no corner touches, so the foundation was dug to the lowest
     * corner and the roof measured from the highest corner while the ground in between — which is
     * what actually pokes through the wall — was never consulted. Measured on the corpus terrain,
     * 43 of 60 test footprints have extremes their corners do not reach.
     *
     * An earlier attempt at this sampled the ring vertices and interior nodes but NOT the crossings
     * along each edge, and was wrong for exactly the footprints it was written to fix.
     */
    void MinMaxHeightCmOverPolygon(TArrayView<const FVector2D> RingMeters,
                                   double& OutMinCm, double& OutMaxCm) const;

    /**
     * Insert a vertex wherever a polyline crosses from one terrain triangle into the next.
     *
     * This is what makes draped geometry follow the ground EXACTLY rather than approximately. A
     * road is a flat ribbon between the points where its height was sampled; the terrain between
     * those points is not flat. Sampling the endpoints perfectly does not help when the endpoints
     * are far apart — measured on a real region, road segments run up to 377 m and area edges up
     * to 777 m across an 8 m terrain grid, and the ground rises as much as 7.8 m through the
     * middle of a surface whose every sampled vertex sits correctly.
     *
     * Splitting at triangle boundaries rather than at some fixed interval is what makes it exact:
     * within one triangle the terrain IS planar, so a segment that stays inside one triangle is
     * coplanar with the ground under it and cannot cut through. A fixed interval would only ever
     * reduce the error.
     *
     * @param LineMeters  Polyline in local metres.
     * @param bClosed     Treat as a ring, splitting the closing edge too.
     * @param OutLine     Densified polyline. Original vertices are always kept, in order.
     */
    void SplitAtTriangleBoundaries(TArrayView<const FVector2D> LineMeters, bool bClosed,
                                   TArray<FVector2D>& OutLine) const;

    /**
     * The terrain's own grid nodes lying inside a polygon, in local metres.
     *
     * Draping an area's OUTLINE is not enough on its own. The triangulator joins far-apart boundary
     * vertices straight across the interior, so a large polygon over a hill still bulges away from
     * the ground in the middle even with a perfect edge — measured at up to 7.9 m. Feeding these
     * nodes back in as interior points makes the surface bend with the terrain instead.
     */
    void GetGridNodesInsidePolygon(TArrayView<const FVector2D> RingMeters,
                                   TArray<FVector2D>& OutNodes) const;

    /** The renderable mesh. Wound for a -Z geometric normal, so it is visible from above. */
    void BuildMesh(TArray<FVector>& OutVertices, TArray<int32>& OutTriangles,
                   TArray<FVector>& OutNormals, TArray<FVector2D>& OutUVs) const;

    const FOSMLocalProjection& GetProjection() const { return Projection; }
    FBox2D GetBoundsMeters() const { return FBox2D(FVector2D(MinX, MinY), FVector2D(MaxX, MaxY)); }
    int32 GetDivisionsX() const { return DivX; }
    int32 GetDivisionsY() const { return DivY; }

    /** Lowest DEM elevation in metres, which local height zero corresponds to. */
    double GetBaseElevationMeters() const { return BaseElevation; }

private:
    /** Grid node height in cm, clamped to the grid. */
    double NodeHeightCm(int32 IndexX, int32 IndexY) const;

    FOSMLocalProjection Projection;

    double MinX = 0.0, MinY = 0.0, MaxX = 0.0, MaxY = 0.0;
    double StepX = 1.0, StepY = 1.0;
    int32 DivX = 0, DivY = 0;
    double BaseElevation = 0.0;

    /** Row-major over X then Y, (DivX+1) * (DivY+1) entries, in centimetres. */
    TArray<double> Heights;
};
