// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "controls/settings/SettingsPages.h"

#include "controls/SettingsDialog.h"
#include "controls/settings/CatalogsDataSettingsPage.h"
#include "controls/settings/ExportSettingsPage.h"
#include "controls/settings/ExternalToolsSettingsPage.h"
#include "controls/settings/GeneralSettingsPage.h"
#include "controls/settings/InterfaceSettingsPage.h"
#include "controls/settings/LoggingSettingsPage.h"
#include "controls/settings/PlateSolvingSettingsPage.h"
#include "controls/settings/ProcessingSettingsPage.h"
#include "controls/settings/ProjectsSettingsPage.h"
#include "controls/settings/RawImageSettingsPage.h"

namespace an::ui
{

void addSettingsPages(SettingsDialog& dialog, GlobalSettingsPersistence* settings, const SettingsPagesOptions& options)
{
    auto* const interfacePage = new InterfaceSettingsPage(settings, &dialog);
    interfacePage->setAvailableShells(options.shells);
    if (options.keepMenuBarShown)
    {
        interfacePage->setMenuBarKeeper(options.keepMenuBarShown);
    }

    if (options.order == SettingsPageOrder::Observatory)
    {
        dialog.addPage(new GeneralSettingsPage(settings, &dialog));
        dialog.addPage(new ProjectsSettingsPage(settings, &dialog));
        dialog.addPage(interfacePage);
        dialog.addPage(new ProcessingSettingsPage(settings, &dialog));
        dialog.addPage(new RawImageSettingsPage(settings, &dialog));
        dialog.addPage(new PlateSolvingSettingsPage(settings, &dialog));
        dialog.addPage(new CatalogsDataSettingsPage(settings, &dialog));
        dialog.addPage(new ExportSettingsPage(settings, &dialog));
        dialog.addPage(new ExternalToolsSettingsPage(settings, &dialog));
        dialog.addPage(new LoggingSettingsPage(settings, &dialog));
        return;
    }

    dialog.addPage(new GeneralSettingsPage(settings, &dialog));
    dialog.addPage(new RawImageSettingsPage(settings, &dialog));
    dialog.addPage(new ExternalToolsSettingsPage(settings, &dialog));
    dialog.addPage(interfacePage);
    dialog.addPage(new ExportSettingsPage(settings, &dialog));
    dialog.addPage(new ProcessingSettingsPage(settings, &dialog));
    dialog.addPage(new LoggingSettingsPage(settings, &dialog));
    dialog.addPage(new CatalogsDataSettingsPage(settings, &dialog));
    dialog.addPage(new PlateSolvingSettingsPage(settings, &dialog));
}

} // namespace an::ui
