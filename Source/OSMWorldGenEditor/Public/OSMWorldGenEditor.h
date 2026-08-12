// Copyright InviMind. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * OSMWorldGenEditor module — editor-only module containing:
 *   - Import wizard (Slate widget)
 *   - Progress reporting UI
 *   - Asset-matching rule editor
 *   - Plugin settings panel
 */
class FOSMWorldGenEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    void RegisterMenuExtensions();
};
