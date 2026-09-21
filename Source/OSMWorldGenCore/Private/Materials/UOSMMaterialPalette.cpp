// Copyright InviMind. All Rights Reserved.

#include "Materials/UOSMMaterialPalette.h"
#include "Materials/UOSMPhysicalMaterial.h"
#include "Materials/MaterialInterface.h"

const FOSMMaterialIdentityGroup* UOSMMaterialPalette::FindGroup(EOSMSurfaceCategory Category) const
{
    for (const FOSMMaterialIdentityGroup& Group : Groups)
    {
        if (Group.Category == Category)
        {
            return &Group;
        }
    }
    return nullptr;
}

UOSMPhysicalMaterial* UOSMMaterialPalette::GetPhysicalMaterial(EOSMSurfaceCategory Category) const
{
    if (const FOSMMaterialIdentityGroup* Group = FindGroup(Category))
    {
        return Group->PhysicalMaterial;
    }
    return nullptr;
}

UMaterialInterface* UOSMMaterialPalette::SelectVisualMaterial(EOSMSurfaceCategory Category, int32 Seed) const
{
    if (const FOSMMaterialIdentityGroup* Group = FindGroup(Category))
    {
        return Group->SelectVisualVariant(Seed);
    }
    return nullptr;
}

void UOSMMaterialPalette::SetGroup(const FOSMMaterialIdentityGroup& InGroup)
{
    for (FOSMMaterialIdentityGroup& Existing : Groups)
    {
        if (Existing.Category == InGroup.Category)
        {
            Existing = InGroup;
            return;
        }
    }
    Groups.Add(InGroup);
}
