// Copyright InviMind. All Rights Reserved.

#include "Terrain/FOSMGeoidCorrection.h"

double FOSMGeoidCorrection::GetGeoidSeparation(double Latitude, double Longitude)
{
    // Normalise longitude to [-180, 180)
    while (Longitude >= 180.0)  Longitude -= 360.0;
    while (Longitude < -180.0)  Longitude += 360.0;

    // Grid row: 90° → -80°, step -10°.  Row 0 = 90°, Row 18 = -80°
    double NormLat = FMath::Clamp(Latitude, GridMinLat, GridMaxLat);
    double RowF = (GridMaxLat - NormLat) / GridStepDeg;  // 0 at north, increases southward
    double ColF = (Longitude - GridMinLon) / GridStepDeg; // 0 at -180°

    int32 R0 = FMath::Clamp((int32)FMath::FloorToDouble(RowF), 0, 17);
    int32 R1 = FMath::Min(R0 + 1, 18);
    int32 C0 = ((int32)FMath::FloorToDouble(ColF)) % 36;
    if (C0 < 0) C0 += 36;
    int32 C1 = (C0 + 1) % 36;

    double tR = RowF - FMath::FloorToDouble(RowF);
    double tC = ColF - FMath::FloorToDouble(ColF);

    // Bilinear interpolation
    double N = FMath::Lerp(
        FMath::Lerp((double)GeoidGrid[R0][C0], (double)GeoidGrid[R0][C1], tC),
        FMath::Lerp((double)GeoidGrid[R1][C0], (double)GeoidGrid[R1][C1], tC),
        tR);

    return N;
}
