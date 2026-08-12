// Copyright InviMind. All Rights Reserved.

#include "OSMWorldGenEditor.h"
#include "Modules/ModuleManager.h"
#include "ToolMenus.h"
#include "Wizard/SOSMImportWizard.h"

#define LOCTEXT_NAMESPACE "FOSMWorldGenEditorModule"

void FOSMWorldGenEditorModule::StartupModule()
{
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
    }
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FOSMWorldGenEditorModule, OSMWorldGenEditor)
