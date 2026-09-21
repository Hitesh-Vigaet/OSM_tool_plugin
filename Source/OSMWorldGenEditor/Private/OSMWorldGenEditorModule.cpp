// Copyright InviMind. All Rights Reserved.

#include "OSMWorldGenEditor.h"
#include "Modules/ModuleManager.h"
#include "ToolMenus.h"
#include "Wizard/SOSMImportWizard.h"
#include "ControlCenter/SOSMControlCenter.h"
#include "Materials/FOSMThermalMaterialBuilderV2.h"

#define LOCTEXT_NAMESPACE "FOSMWorldGenEditorModule"

void FOSMWorldGenEditorModule::StartupModule()
{
    // Registered at startup rather than on first use: the wizard invokes the tab immediately
    // after an import, and a spawner that does not exist yet silently does nothing.
    SOSMControlCenter::RegisterTabSpawner();

    UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FOSMWorldGenEditorModule::RegisterMenuExtensions));
}

void FOSMWorldGenEditorModule::ShutdownModule()
{
    UToolMenus::UnRegisterStartupCallback(this);
    UToolMenus::UnregisterOwner(this);
}

void FOSMWorldGenEditorModule::RegisterMenuExtensions()
{
    FToolMenuOwnerScoped OwnerScoped(this);

    UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("MainFrame.MainMenu.Tools");
    if (Menu)
    {
        FToolMenuSection& Section = Menu->FindOrAddSection("OSMWorldGenSection");
        Section.AddMenuEntry(
            "OSMImportWizard",
            LOCTEXT("OSMImportWizardLabel", "OSM World Generator"),
            LOCTEXT("OSMImportWizardTooltip", "Open the 5-step georeferenced OpenStreetMap 3D world import wizard."),
            FSlateIcon(),
            FUIAction(FExecuteAction::CreateStatic(&SOSMImportWizard::OpenWizardWindow))
        );

        Section.AddMenuEntry(
            "OSMControlCenter",
            LOCTEXT("OSMControlCenterLabel", "OSM Control Center"),
            LOCTEXT("OSMControlCenterTooltip",
                "Inspect and configure the imported city graph — nodes, relationships, groups and asset ratios."),
            FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([]()
            {
                FGlobalTabmanager::Get()->TryInvokeTab(SOSMControlCenter::TabId);
            }))
        );

        Section.AddMenuEntry(
            "RebuildThermalMaterial",
            LOCTEXT("RebuildThermalMaterialLabel", "Rebuild Thermal Post-Process Material"),
            LOCTEXT("RebuildThermalMaterialTooltip", "Reconstruct and compile M_IRPostProcessV2 with the latest shader code."),
            FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([]()
            {
                FOSMThermalMaterialBuilderV2::GetOrCreateThermalPostProcessMaterial(/*bForceRebuild=*/true);
            }))
        );
    }
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FOSMWorldGenEditorModule, OSMWorldGenEditor)
