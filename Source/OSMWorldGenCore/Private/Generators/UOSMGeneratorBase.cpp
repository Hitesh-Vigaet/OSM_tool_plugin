// Copyright InviMind. All Rights Reserved.

#include "Generators/UOSMGeneratorBase.h"

bool UOSMGeneratorBase::Generate(
    const FOSMGenerationContext& Context,
    const TArray<const FOSMFeature*>& Features,
    TArray<AActor*>& OutActors)
{
    // Default implementation in abstract base
    return true;
}

TArray<EOSMFeatureType> UOSMGeneratorBase::GetAcceptedFeatureTypes() const
{
    return {};
}

FText UOSMGeneratorBase::GetDisplayName() const
{
    return NSLOCTEXT("OSM", "GeneratorBase", "Base Generator");
}
