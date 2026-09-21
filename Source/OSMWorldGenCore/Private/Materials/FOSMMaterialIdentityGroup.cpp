// Copyright InviMind. All Rights Reserved.

#include "Materials/FOSMMaterialIdentityGroup.h"
#include "Materials/MaterialInterface.h"

UMaterialInterface* FOSMMaterialIdentityGroup::SelectVisualVariant(int32 Seed) const
{
    if (VisualVariants.Num() == 0) return nullptr;

    // Deterministic: same seed always picks the same variant
    const int32 Index = ((Seed % VisualVariants.Num()) + VisualVariants.Num()) % VisualVariants.Num();

    if (VisualVariants[Index].IsNull()) return nullptr;

    return VisualVariants[Index].LoadSynchronous();
}
