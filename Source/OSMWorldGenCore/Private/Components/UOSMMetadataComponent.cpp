// Copyright InviMind. All Rights Reserved.

#include "Components/UOSMMetadataComponent.h"

UOSMMetadataComponent::UOSMMetadataComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    bWantsInitializeComponent = false;
}

bool UOSMMetadataComponent::HasTag(const FString& Key) const
{
    return Tags.Contains(Key);
}

FString UOSMMetadataComponent::GetTag(const FString& Key, const FString& DefaultValue) const
{
    const FString* Val = Tags.Find(Key);
    return Val ? *Val : DefaultValue;
}
