// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "common.h"
#include "app/AfterNightApp.h"
#include "app/ApplicationSession.h"
#include "app/AfterNightAppUiBuilder.h"
#include "app/ModernizationFeatureFlags.h"
#include "app/ExtensionManager.h"
#include "app/ExtensionEnvironmentManager.h"
#include "app/ExtensionHostManager.h"
#include "app/ExtensionLaunchPreparations.h"
#include "app/SharedBufferManager.h"
#include "app/ProcessRegistry.h"
#include "app/ProcessLauncher.h"
#include "app/ProcessMenuState.h"
#include "app/ChannelExtractor.h"
#include "app/ThemeManager.h"
#include "app/MenuActionManager.h"
#include "app/FileCompanions.h"
#include "app/FileManager.h"
#include "app/FileDialogUtils.h"
#include "app/ViewControlCoordinator.h"
#include "app/ViewCreationCoordinator.h"
#include "app/ImageOperationsCoordinator.h"
#include "app/MaskOverlayCoordinator.h"
#include "app/DialogOrchestrationCoordinator.h"
#include "app/RuntimeStorageCoordinator.h"
#include "app/UiBootstrapCoordinator.h"
#include "app/ActionStateCoordinator.h"
#include "app/WindowLayoutCoordinator.h"
#include "app/AppLifecycleCoordinator.h"
#include "app/ExtensionCommon.h"
#include "theme/AnDesignTokens.h"
#include "theme/AnProxyStyle.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlManager.h"
#include "controls/ProgressDialogControl.h"
#include "controls/MainDashboard.h"
#include "controls/LogViewerControl.h"
#include "controls/settings/ExtensionStatusPage.h"
#include "controls/settings/ProcessingLimits.h"
#include "controls/ThumbnailPersistence.h"
#include "controls/FolderTrackerPersistence.h"
#include "controls/ProcessSettingsPersistence.h"
#include "controls/GlobalSettingsPersistence.h"
#include "controls/TopLevelProcessBaseControl.h"
#include "widgets/ImageInfoStatusBarWidget.h"
#include "controls/ExportSequenceControl.h"
#include "controls/NewSequenceControl.h"
#include "controls/settings/SettingsKeys.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/KDDockWidgetsHelper.h"
#include "controls/ImageProcessControl.h"
#include "controls/ImageHistogramControl.h"
#include "controls/ImageSelectorTooltipBuilder.h"
#include "controls/DashboardUtils.h"
#include "session/IEngineSessionProvider.h"
#include "session/ImageDocument.h"

#include "app/MessageBoxes.h"

#include "core/ApplicationContext.h"
#include "core/ApplicationCore.h"
#include "core/ApplicationEvents.h"
#include "services/AppEventBus.h"
#include "services/DialogService.h"
#include "services/FavoriteProcessesModel.h"
#include "services/FileDialogService.h"
#include "services/OpenProcessRegistry.h"
#include "services/ProcessLaunchService.h"
#include "services/RecentFilesModel.h"
#include "services/RecentProcessesModel.h"
#include "views/ViewService.h"

#include <AfterNightPlatform/Text.h>
#include <AfterNightPlatform/Logging.h>

#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/IO/SequenceDescriptor.h>
#include <AfterNightEngine/IO/SequenceSelection.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/OutputIntent.h>

#include <kddockwidgets/Config.h>
#include "kddockwidgets/core/FloatingWindow.h"
#include "kddockwidgets/core/MainWindow.h"
#include "kddockwidgets/core/DropArea.h"
#include "kddockwidgets/core/DockWidget.h"

#include <QList>
#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPointer>
#include <QUuid>
#include <QUrl>
#include <QScreen>
#include <QThreadPool>
#include <QTimer>
#include <QVBoxLayout>
#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <map>

namespace
{

using an::ui::ScopedTopLevelFlagRestorer;
using an::ui::shell::hasCachedNativeParameterSchemas;
using an::ui::shell::primeMissingNativeParameterSchemas;

constexpr const char* kExtensionProcessMenuActionProperty = "anExtensionProcessMenuAction";
constexpr const char* kExtensionFamilyMenuActionProperty = "anExtensionFamilyMenuAction";
bool cleanupMissingStoredProcessIdsOnNextRefresh = false;

std::string viewControlModeName(const ViewControlMode mode)
{
    switch (mode)
    {
    case ViewControlMode::standard:
        return "standard";
    case ViewControlMode::rt_preview:
        return "rt_preview";
    case ViewControlMode::st_preview:
        return "st_preview";
    }
    return "unknown";
}

std::string imageChannelViewModeName(const ImageChannelViewMode mode)
{
    switch (mode)
    {
    case ImageChannelViewMode::RGB_K_MODE:
        return "rgb";
    case ImageChannelViewMode::RGB_R_MODE:
        return "rgb_r";
    case ImageChannelViewMode::RGB_G_MODE:
        return "rgb_g";
    case ImageChannelViewMode::RGB_B_MODE:
        return "rgb_b";
    case ImageChannelViewMode::CIE_AB_MODE:
        return "cie_ab";
    case ImageChannelViewMode::CIE_L_MODE:
        return "cie_l";
    case ImageChannelViewMode::CIE_a_MODE:
        return "cie_a";
    case ImageChannelViewMode::CIE_b_MODE:
        return "cie_b";
    case ImageChannelViewMode::CIE_c_MODE:
        return "cie_c";
    case ImageChannelViewMode::CIE_h_MODE:
        return "cie_h";
    case ImageChannelViewMode::CIE_X_MODE:
        return "cie_x";
    case ImageChannelViewMode::CIE_Y_MODE:
        return "cie_y";
    case ImageChannelViewMode::CIE_Z_MODE:
        return "cie_z";
    case ImageChannelViewMode::HSL_H_MODE:
        return "hsl_h";
    case ImageChannelViewMode::HSL_S_MODE:
        return "hsl_s";
    case ImageChannelViewMode::HSL_L_MODE:
        return "hsl_l";
    case ImageChannelViewMode::HSV_H_MODE:
        return "hsv_h";
    case ImageChannelViewMode::HSV_S_MODE:
        return "hsv_s";
    case ImageChannelViewMode::HSV_V_MODE:
        return "hsv_v";
    case ImageChannelViewMode::HSI_H_MODE:
        return "hsi_h";
    case ImageChannelViewMode::HSI_S_MODE:
        return "hsi_s";
    case ImageChannelViewMode::HSI_I_MODE:
        return "hsi_i";
    case ImageChannelViewMode::YCBCR_Y_MODE:
        return "ycbcr_y";
    case ImageChannelViewMode::YCBCR_CB_MODE:
        return "ycbcr_cb";
    case ImageChannelViewMode::YCBCR_CR_MODE:
        return "ycbcr_cr";
    }
    return "unknown";
}

std::string imageDisplayModeName(const ImageViewMode mode)
{
    switch (mode)
    {
    case ImageViewMode::LINEAR_MODE:
        return "linear";
    case ImageViewMode::AUTOSTRETCH_MODE:
        return "autostretch";
    }
    return "unknown";
}

std::string maskRenderingModeName(const MaskRenderingMode mode)
{
    switch (mode)
    {
    case MaskRenderingMode::red_mode:
        return "red";
    case MaskRenderingMode::green_mode:
        return "green";
    case MaskRenderingMode::blue_mode:
        return "blue";
    case MaskRenderingMode::yellow_mode:
        return "yellow";
    case MaskRenderingMode::magenta_mode:
        return "magenta";
    case MaskRenderingMode::cyan_mode:
        return "cyan";
    case MaskRenderingMode::replace_mode:
        return "replace";
    case MaskRenderingMode::multiply_mode:
        return "multiply";
    }
    return "unknown";
}

QString colorTokenHex(const QColor& color)
{
    return color.name(QColor::HexArgb);
}

QCborMap fontTokenToCborMap(const QFont& font)
{
    return QCborMap{
        {QStringLiteral("family"), font.family()},       {QStringLiteral("point_size"), font.pointSizeF()},
        {QStringLiteral("weight"), font.weight()},       {QStringLiteral("italic"), font.italic()},
        {QStringLiteral("underline"), font.underline()}, {QStringLiteral("fixed_pitch"), font.fixedPitch()}};
}

QCborMap currentExtensionThemeTokens()
{
    const auto& token_set = an::ui::tokens();
    return QCborMap{{QStringLiteral("is_dark"), an::ui::isDarkTheme()},
                    {QStringLiteral("contrast_profile"), static_cast<int>(an::ui::themeContrastProfile())},
                    {QStringLiteral("accent_family"), static_cast<int>(an::ui::themeAccentFamily())},
                    {QStringLiteral("surface_tint_family"), static_cast<int>(an::ui::themeSurfaceTintFamily())},
                    {QStringLiteral("colors"),
                     QCborMap{{QStringLiteral("bgPrimary"), colorTokenHex(token_set.colors.bgPrimary)},
                              {QStringLiteral("bgSurface"), colorTokenHex(token_set.colors.bgSurface)},
                              {QStringLiteral("bgCard"), colorTokenHex(token_set.colors.bgCard)},
                              {QStringLiteral("textPrimary"), colorTokenHex(token_set.colors.textPrimary)},
                              {QStringLiteral("textSecondary"), colorTokenHex(token_set.colors.textSecondary)},
                              {QStringLiteral("textTertiary"), colorTokenHex(token_set.colors.textTertiary)},
                              {QStringLiteral("textOnAccent"), colorTokenHex(token_set.colors.textOnAccent)},
                              {QStringLiteral("accentPrimary"), colorTokenHex(token_set.colors.accentPrimary)},
                              {QStringLiteral("accentHover"), colorTokenHex(token_set.colors.accentHover)},
                              {QStringLiteral("borderPrimary"), colorTokenHex(token_set.colors.borderPrimary)},
                              {QStringLiteral("borderSubtle"), colorTokenHex(token_set.colors.borderSubtle)},
                              {QStringLiteral("statusSuccess"), colorTokenHex(token_set.colors.statusSuccess)},
                              {QStringLiteral("statusWarning"), colorTokenHex(token_set.colors.statusWarning)},
                              {QStringLiteral("statusError"), colorTokenHex(token_set.colors.statusError)},
                              {QStringLiteral("statusInfo"), colorTokenHex(token_set.colors.statusInfo)}}},
                    {QStringLiteral("spacing"), QCborMap{{QStringLiteral("xs"), token_set.spacing.xs},
                                                         {QStringLiteral("sm"), token_set.spacing.sm},
                                                         {QStringLiteral("md"), token_set.spacing.md},
                                                         {QStringLiteral("lg"), token_set.spacing.lg},
                                                         {QStringLiteral("xl"), token_set.spacing.xl}}},
                    {QStringLiteral("radius"), QCborMap{{QStringLiteral("none"), token_set.radius.none},
                                                        {QStringLiteral("sm"), token_set.radius.sm},
                                                        {QStringLiteral("md"), token_set.radius.md},
                                                        {QStringLiteral("lg"), token_set.radius.lg},
                                                        {QStringLiteral("xl"), token_set.radius.xl}}},
                    {QStringLiteral("typography"),
                     QCborMap{{QStringLiteral("heading1"), fontTokenToCborMap(token_set.typography.heading1)},
                              {QStringLiteral("heading2"), fontTokenToCborMap(token_set.typography.heading2)},
                              {QStringLiteral("heading3"), fontTokenToCborMap(token_set.typography.heading3)},
                              {QStringLiteral("body"), fontTokenToCborMap(token_set.typography.body)},
                              {QStringLiteral("caption"), fontTokenToCborMap(token_set.typography.caption)},
                              {QStringLiteral("monospace"), fontTokenToCborMap(token_set.typography.monospace)}}}};
}

an::ui::extensions::ExtensionThemeSnapshot currentExtensionThemeSnapshot()
{
    an::ui::extensions::ExtensionThemeSnapshot snapshot;
    snapshot.theme_name = an::ui::isDarkTheme() ? "AfterNight Dark" : "AfterNight Light";
    snapshot.tokens = currentExtensionThemeTokens();
    return snapshot;
}

void logModernizationFeatureFlags()
{
    using an::ui::modernization::ModernizationFeatureFlags;

    LOG_DEBUG("Modernization feature flags: "
              "AN_UI_TOKENS_ENABLED={} (default={}), "
              "AN_UI_PROGRAMMATIC_PROCESSES={} (default={}), "
              "AN_DIALOG_MODERN_STYLE_ENABLED={} (default={}), "
              "AN_PERSISTENCE_THREADSAFE_ENABLED={} (default={})",
              ModernizationFeatureFlags::uiTokensEnabled(), ModernizationFeatureFlags::kUiTokensEnabledDefault,
              ModernizationFeatureFlags::programmaticProcessesEnabled(),
              ModernizationFeatureFlags::kProgrammaticProcessesEnabledDefault,
              ModernizationFeatureFlags::dialogModernStyleEnabled(),
              ModernizationFeatureFlags::kDialogModernStyleEnabledDefault,
              ModernizationFeatureFlags::persistenceThreadsafeEnabled(),
              ModernizationFeatureFlags::kPersistenceThreadsafeEnabledDefault);
}

QMenu* extensionMenuForCategory(an::ui::app::AfterNightAppUiElements& ui, const std::string_view category)
{
    if (category == "tone_brightness")
    {
        return ui.menuAdjustments;
    }
    if (category == "filters")
    {
        return ui.menuFilters;
    }
    if (category == "geometry")
    {
        return ui.menuGeometry_2;
    }
    if (category == "color")
    {
        return ui.menuColor_Calibration;
    }
    if (category == "denoising")
    {
        return ui.menuNoise_Reduction;
    }
    if (category == "sharpening_enhancement")
    {
        return ui.menuDetails;
    }
    if (category == "star_object")
    {
        return ui.menuStar_Processing;
    }
    if (category == "background_extraction")
    {
        return ui.menuBackground_Modelization;
    }
    if (category == "transforms")
    {
        return ui.menuIntensity_Transformations;
    }
    if (category == "sensor_calibration" || category == "multi_image_stacking")
    {
        return ui.menuPreprocess;
    }
    if (category == "mask_generation")
    {
        return ui.menuMask_Generation;
    }

    return nullptr;
}

[[nodiscard]] QString extensionStatusPageTitle()
{
    return QCoreApplication::translate("ExtensionStatusPage", "Extensions");
}

[[nodiscard]] QString extensionManagerPageTitle()
{
    return QCoreApplication::translate("ExtensionManagerPage", "Extension Manager");
}

[[nodiscard]] QString extensionBrowseTabTitle()
{
    return QCoreApplication::translate("ExtensionsDialog", "Browse");
}

[[nodiscard]] QString extensionInstalledTabTitle()
{
    return QCoreApplication::translate("ExtensionsDialog", "Installed");
}

[[nodiscard]] bool isExtensionDialogPageTitle(const QString& page_name)
{
    const QString requested = page_name.trimmed();
    return QString::compare(requested, extensionStatusPageTitle(), Qt::CaseInsensitive) == 0 ||
           QString::compare(requested, extensionManagerPageTitle(), Qt::CaseInsensitive) == 0 ||
           QString::compare(requested, extensionBrowseTabTitle(), Qt::CaseInsensitive) == 0 ||
           QString::compare(requested, extensionInstalledTabTitle(), Qt::CaseInsensitive) == 0;
}

void removeMenuActionsByProperty(QMenu* menu, const char* propertyName)
{
    if (!menu || !propertyName)
    {
        return;
    }

    const QList<QAction*> actions = menu->actions();
    for (QAction* action : actions)
    {
        if (!action)
        {
            continue;
        }

        if (action->property(propertyName).toBool())
        {
            QMenu* submenu = action->menu();
            menu->removeAction(action);
            if (submenu)
            {
                delete submenu;
            }
            else
            {
                delete action;
            }
            continue;
        }

        removeMenuActionsByProperty(action->menu(), propertyName);
    }
}

void clearDiscoveredExtensionProcessActions(an::ui::app::AfterNightAppUiElements& ui)
{
    removeMenuActionsByProperty(ui.menuProcess, kExtensionProcessMenuActionProperty);
}

void clearDiscoveredExtensionFamilyMenus(an::ui::app::AfterNightAppUiElements& ui)
{
    removeMenuActionsByProperty(ui.menuExtensions, kExtensionFamilyMenuActionProperty);
}

void addExternalUrlAction(QMenu* menu, const QString& text, const std::string& url)
{
    if (!menu || url.empty())
    {
        return;
    }

    auto* action = menu->addAction(text);
    const QString qurl = QString::fromUtf8(url);
    action->setStatusTip(qurl);
    action->setToolTip(qurl);
    QObject::connect(action, &QAction::triggered, menu, [qurl]() { QDesktopServices::openUrl(QUrl(qurl)); });
}

void appendDiscoveredExtensionFamilyMenus(AfterNightApp* app, an::ui::app::AfterNightAppUiElements& ui,
                                          const std::vector<an::ui::extensions::ExtensionLoadResult>& discovered)
{
    clearDiscoveredExtensionFamilyMenus(ui);
    if (!app || !ui.menuExtensions)
    {
        return;
    }

    const std::vector<an::ui::shell::ExtensionFamily> entries =
        an::ui::shell::ProcessLaunchService::extensionFamilies(discovered);
    if (entries.empty())
    {
        return;
    }

    QAction* separator = ui.menuExtensions->addSeparator();
    separator->setProperty(kExtensionFamilyMenuActionProperty, true);

    QPointer<AfterNightApp> app_guard(app);
    for (const an::ui::shell::ExtensionFamily& entry : entries)
    {
        auto* family_menu = new QMenu(entry.displayName, ui.menuExtensions);
        QAction* family_action = ui.menuExtensions->addMenu(family_menu);
        family_action->setProperty(kExtensionFamilyMenuActionProperty, true);

        auto* manage_action = family_menu->addAction(QCoreApplication::translate("AfterNightAppClass", "Manage..."));
        QObject::connect(manage_action, &QAction::triggered, app, [app_guard]() {
            if (app_guard)
            {
                app_guard->openSettingsAt(extensionStatusPageTitle());
            }
        });

        addExternalUrlAction(family_menu, QCoreApplication::translate("AfterNightAppClass", "Homepage"),
                             entry.homepageUrl);
        addExternalUrlAction(family_menu, QCoreApplication::translate("AfterNightAppClass", "Repository"),
                             entry.repositoryUrl);
        addExternalUrlAction(family_menu, QCoreApplication::translate("AfterNightAppClass", "Support"),
                             entry.supportUrl);
    }
}

void refreshDiscoveredExtensionActions(AfterNightApp* app, an::ui::MenuActionManager* menuActionManager,
                                       const std::vector<an::ui::extensions::ExtensionLoadResult>& discovered)
{
    if (!app || !menuActionManager)
    {
        return;
    }

    for (const auto& result : discovered)
    {
        if (!result.manifest || result.status == an::ui::extensions::ExtensionLoadStatus::Invalid)
        {
            continue;
        }

        for (const auto& process : result.manifest->processes)
        {
            if (auto* action = app->findChild<QAction*>(QString::fromStdString(process.action_object_name)))
            {
                action->setProperty("anRegistryDescriptorId", QString::fromStdString(process.id));
                menuActionManager->bindRegistryAction(action, process.category, process.id);
            }
        }
    }
}

void appendDiscoveredExtensionActions(AfterNightApp* app, an::ui::MenuActionManager* menuActionManager,
                                      an::ui::app::AfterNightAppUiElements& ui,
                                      const std::vector<an::ui::extensions::ExtensionLoadResult>& discovered)
{
    if (!app || !menuActionManager)
    {
        return;
    }

    clearDiscoveredExtensionProcessActions(ui);
    appendDiscoveredExtensionFamilyMenus(app, ui, discovered);

    std::set<QMenu*> menus_with_extension_entries;
    auto& registry = an::ui::process_registry::ProcessRegistry::instance();

    for (const auto& result : discovered)
    {
        if (!result.manifest || result.status == an::ui::extensions::ExtensionLoadStatus::Invalid)
        {
            continue;
        }

        for (const auto& process : result.manifest->processes)
        {
            QMenu* menu = extensionMenuForCategory(ui, process.category);
            if (!menu)
            {
                if (an::platform::isLoggingInitialized())
                {
                    LOG_WARNING("No process-menu target is defined for extension category='{}'", process.category);
                }
                continue;
            }

            if (auto* existing_action = app->findChild<QAction*>(QString::fromStdString(process.action_object_name)))
            {
                existing_action->setProperty("anRegistryDescriptorId", QString::fromStdString(process.id));
                menuActionManager->bindRegistryAction(existing_action, process.category, process.id);
                continue;
            }

            const auto* descriptor = registry.find(process.id);
            if (!descriptor)
            {
                if (an::platform::isLoggingInitialized())
                {
                    LOG_WARNING("Extension descriptor missing after registration, id={}", process.id);
                }
                continue;
            }

            if (menus_with_extension_entries.insert(menu).second)
            {
                QAction* separator = menu->addSeparator();
                separator->setProperty(kExtensionProcessMenuActionProperty, true);
            }

            auto* action = new QAction(menu);
            action->setObjectName(QString::fromStdString(process.action_object_name));
            action->setText(QString::fromUtf8(descriptor->display_name.c_str()));
            action->setProperty("anRegistryDescriptorId", QString::fromStdString(process.id));
            action->setProperty(kExtensionProcessMenuActionProperty, true);
            menu->addAction(action);
            menuActionManager->bindRegistryAction(action, descriptor->category, process.id);
        }
    }
}

void bindDiscoveredExtensionLaunches(an::ui::extensions::ExtensionHostLaunchCoordinator* coordinator,
                                     const std::filesystem::path& app_dir,
                                     const std::vector<an::ui::extensions::ExtensionLoadResult>& discovered)
{
    if (!coordinator)
    {
        return;
    }

    auto& registry = an::ui::process_registry::ProcessRegistry::instance();
    const std::filesystem::path host_executable =
        an::ui::extensions::bundled_extension_host_executable_for_app_dir(app_dir);
    const std::filesystem::path lite_host_executable =
        an::ui::extensions::bundled_extension_host_lite_executable_for_app_dir(app_dir);

    for (const auto& result : discovered)
    {
        if (!result.manifest || result.status != an::ui::extensions::ExtensionLoadStatus::Ready ||
            !result.environment || result.environment->state != an::ui::extensions::ExtensionEnvironmentState::Ready)
        {
            continue;
        }

        const auto sdk_backend = result.manifest->sdk_backend;
        if (sdk_backend == an::ui::extensions::ExtensionSdkBackend::Rpc)
        {
            continue;
        }
        const bool protocol_backend = sdk_backend == an::ui::extensions::ExtensionSdkBackend::Protocol;
        const std::filesystem::path selected_host_executable =
            protocol_backend ? lite_host_executable : host_executable;
        if (selected_host_executable.empty())
        {
            continue;
        }

        for (const auto& process : result.manifest->processes)
        {
            auto* descriptor = registry.findMutable(process.id);
            if (!descriptor)
            {
                continue;
            }

            an::ui::extensions::ExtensionHostLaunchBinding binding;
            binding.descriptor_id = process.id;
            binding.extension_id = result.manifest->id;
            binding.display_name = descriptor->display_name;
            binding.sdk_backend = protocol_backend ? "protocol" : "runtime";
            binding.launch_mode = result.manifest->launch_mode;
            binding.host_version = QCoreApplication::applicationVersion().isEmpty()
                                       ? "dev"
                                       : QCoreApplication::applicationVersion().toStdString();
            binding.host_arguments.push_back("--extension-package-root");
            binding.host_arguments.push_back(result.manifest->package_root);
            binding.host_arguments.push_back("--entry-point");
            binding.host_arguments.push_back(result.manifest->entry_point);
            binding.host_arguments.push_back("--process-class");
            binding.host_arguments.push_back(process.class_name);
            binding.host_arguments.push_back("--environment-root");
            binding.host_arguments.push_back(result.environment->environment_path.string());
            binding.host_arguments.push_back("--runtime-root");
            binding.host_arguments.push_back((app_dir / "PythonLib").string());
            const bool workflow_launch =
                result.manifest->launch_mode == an::ui::extensions::ExtensionLaunchMode::Workflow;
            if (process.capabilities.execute || workflow_launch)
            {
                binding.capabilities.push_back("execute");
            }
            if (process.capabilities.preview)
            {
                binding.capabilities.push_back("preview");
            }
            if (process.capabilities.keep_open)
            {
                binding.capabilities.push_back("keep_open");
            }
            if (workflow_launch)
            {
                binding.capabilities.push_back("workflow");
            }
            binding.process_capabilities = process.capabilities;
            binding.host_executable = selected_host_executable;
            binding.extension_host_owns_process_window = !protocol_backend;
            if (!protocol_backend && result.settings_schema &&
                result.settings_schema->state == an::ui::extensions::ExtensionSettingsSchemaState::Ready)
            {
                const auto params_schema = result.settings_schema->process_params_schemas.find(process.class_name);
                if (params_schema != result.settings_schema->process_params_schemas.end())
                {
                    binding.has_cached_native_param_schema = true;
                    binding.cached_native_param_schema = params_schema->second;
                }
            }
            if (workflow_launch)
            {
                coordinator->bindWorkflowDescriptor(descriptor, binding);
            }
            else
            {
                coordinator->bindSingleImageDescriptor(descriptor, binding);
            }
        }
    }
}

void cleanupUnusedExtensionEnvironmentsAtStartup(const std::vector<an::ui::extensions::ExtensionLoadResult>& discovered,
                                                 const an::ui::extensions::ExtensionDiscoveryOptions& discovery_options)
{
    an::ui::extensions::ExtensionEnvironmentManager environment_manager;

    // An environment a ready one of the same package's context replaced goes at once; one no installed package's
    // context names waits for the grace period.
    std::vector<std::filesystem::path> replacedPaths;
    std::uintmax_t replacedBytes = 0;
    if (environment_manager.removeReplacedEnvironments(discovered, discovery_options.context, &replacedPaths,
                                                       &replacedBytes, discovery_options.cancellation_token) &&
        !replacedPaths.empty() && an::platform::isLoggingInitialized())
    {
        LOG_INFO("Removed {} replaced extension environment(s) and reclaimed {} byte(s)", replacedPaths.size(),
                 replacedBytes);
    }
    if (discovery_options.cancellation_token.isCancellationRequested())
    {
        return;
    }

    std::vector<std::filesystem::path> removed_paths;
    std::uintmax_t reclaimed_bytes = 0;
    std::string error;
    if (!environment_manager.removeUnusedEnvironments(
            discovered, discovery_options.context, an::ui::extensions::automatic_extension_environment_cleanup_policy(),
            &removed_paths, &reclaimed_bytes, &error, discovery_options.cancellation_token))
    {
        if (!discovery_options.cancellation_token.isCancellationRequested() && an::platform::isLoggingInitialized())
        {
            LOG_WARNING("Automatic extension environment cleanup failed: {}", error);
        }
        return;
    }

    if (!removed_paths.empty() && an::platform::isLoggingInitialized())
    {
        LOG_INFO("Automatic extension environment cleanup removed {} environment(s) and reclaimed {} byte(s)",
                 removed_paths.size(), reclaimed_bytes);
    }
}

void registerDiscoveredExtensions(AfterNightApp* app,
                                  an::ui::extensions::ExtensionHostLaunchCoordinator* extensionLaunchCoordinator,
                                  an::ui::MenuActionManager* menuActionManager,
                                  an::ui::app::AfterNightAppUiElements& ui,
                                  const an::ui::extensions::ExtensionOperationCancellationToken& cancellation_token)
{
    if (!app || !menuActionManager)
    {
        return;
    }

    const auto discovery_options = an::ui::shell::defaultExtensionDiscoveryOptions(false, cancellation_token);
    const std::filesystem::path app_dir(QCoreApplication::applicationDirPath().toStdString());
    const bool automatic_environment_cleanup_enabled =
        !app->getGlobalSettingsPersistence() ||
        app->getGlobalSettingsPersistence()->select(
            QString::fromLatin1(an::ui::extensions::kAutomaticExtensionEnvironmentCleanupSetting),
            QStringLiteral("1")) != QStringLiteral("0");

    // The catalog is intentionally a small, guarded snapshot of only known-ready
    // installations. It avoids recalculating package dependency/source hashes on
    // the UI thread before extension actions and launch bindings are available.
    const auto startup_catalog = an::ui::extensions::load_ready_extension_startup_catalog(discovery_options);
    if (!startup_catalog.empty())
    {
        an::ui::extensions::ExtensionManager extensionManager;
        extensionManager.registerDiscoveredProcesses(startup_catalog,
                                                     an::ui::process_registry::ProcessRegistry::instance());
        bindDiscoveredExtensionLaunches(extensionLaunchCoordinator, app_dir, startup_catalog);
        appendDiscoveredExtensionActions(app, menuActionManager, ui, startup_catalog);
        cleanupMissingStoredProcessIdsOnNextRefresh = true;
        app->refreshStoredProcessMenus();
        app->updateActiveImageActions();
    }

    QPointer<AfterNightApp> app_guard(app);
    auto* ui_ptr = &ui;
    QThreadPool::globalInstance()->start([app_guard, extensionLaunchCoordinator, menuActionManager, ui_ptr, app_dir,
                                          discovery_options, automatic_environment_cleanup_enabled,
                                          startup_catalog_for_cleanup = startup_catalog]() mutable {
        const auto cancellation_requested = [&discovery_options]() {
            return discovery_options.cancellation_token.isCancellationRequested();
        };
        an::ui::extensions::ExtensionManager extensionManager;
        auto discovered = extensionManager.discoverExtensions(discovery_options);
        if (cancellation_requested())
        {
            return;
        }
        if (automatic_environment_cleanup_enabled)
        {
            auto cleanup_discovered = discovered;
            cleanup_discovered.insert(cleanup_discovered.end(), startup_catalog_for_cleanup.begin(),
                                      startup_catalog_for_cleanup.end());
            cleanupUnusedExtensionEnvironmentsAtStartup(cleanup_discovered, discovery_options);
        }
        if (cancellation_requested())
        {
            return;
        }
        if (!an::ui::extensions::store_ready_extension_startup_catalog(discovered, discovery_options.context) &&
            an::platform::isLoggingInitialized())
        {
            LOG_WARNING("Failed to update the extension startup activation catalog");
        }
        if (discovered.empty())
        {
            if (app_guard)
            {
                QMetaObject::invokeMethod(
                    app_guard.data(),
                    [app_guard, menuActionManager, ui_ptr, discovered = std::move(discovered)]() mutable {
                        if (!app_guard || !menuActionManager || !ui_ptr)
                        {
                            return;
                        }

                        an::ui::extensions::ExtensionManager extensionManager;
                        extensionManager.registerDiscoveredProcesses(
                            discovered, an::ui::process_registry::ProcessRegistry::instance());
                        appendDiscoveredExtensionActions(app_guard.data(), menuActionManager, *ui_ptr, discovered);
                        cleanupMissingStoredProcessIdsOnNextRefresh = true;
                        app_guard->refreshStoredProcessMenus();
                        app_guard->updateActiveImageActions();
                    },
                    Qt::QueuedConnection);
            }
            return;
        }

        // Every installed package's environment that is not ready is prepared in the background, whatever its source:
        // refreshed in place for this build, taken over from an earlier version of the extension, or provisioned -
        // from an index only while the run may reach one.
        an::ui::extensions::ExtensionEnvironmentManager environmentManager;
        std::string bootstrap_error;
        const int queued_bootstraps = environmentManager.queueEnvironmentMaintenance(
            discovered, !an::ui::extensions::extensionNetworkDisabledByPolicy(), &bootstrap_error,
            discovery_options.cancellation_token);
        if (cancellation_requested())
        {
            return;
        }
        if (!bootstrap_error.empty() && an::platform::isLoggingInitialized())
        {
            LOG_WARNING("Extension environment maintenance queueing reported: {}", bootstrap_error);
        }
        else if (queued_bootstraps > 0 && an::platform::isLoggingInitialized())
        {
            LOG_INFO("Queued {} extension environment maintenance operation(s)", queued_bootstraps);
        }

        if (app_guard)
        {
            auto discovered_for_ui = discovered;
            QMetaObject::invokeMethod(
                app_guard.data(),
                [app_guard, extensionLaunchCoordinator, menuActionManager, ui_ptr, app_dir,
                 discovered_for_ui = std::move(discovered_for_ui)]() mutable {
                    if (!app_guard || !menuActionManager || !extensionLaunchCoordinator || !ui_ptr)
                    {
                        return;
                    }

                    an::ui::extensions::ExtensionManager extensionManager;
                    extensionManager.registerDiscoveredProcesses(discovered_for_ui,
                                                                 an::ui::process_registry::ProcessRegistry::instance());
                    bindDiscoveredExtensionLaunches(extensionLaunchCoordinator, app_dir, discovered_for_ui);
                    appendDiscoveredExtensionActions(app_guard.data(), menuActionManager, *ui_ptr, discovered_for_ui);
                    cleanupMissingStoredProcessIdsOnNextRefresh = true;
                    app_guard->refreshStoredProcessMenus();
                    app_guard->updateActiveImageActions();
                },
                Qt::QueuedConnection);
        }

        bool requires_native_param_cache_migration = false;
        for (const auto& result : discovered)
        {
            if (cancellation_requested())
            {
                return;
            }
            if (result.status == an::ui::extensions::ExtensionLoadStatus::Ready && result.environment &&
                result.environment->state == an::ui::extensions::ExtensionEnvironmentState::Ready &&
                !hasCachedNativeParameterSchemas(result))
            {
                requires_native_param_cache_migration = true;
                break;
            }
        }

        if (requires_native_param_cache_migration)
        {
            const bool caches_updated = primeMissingNativeParameterSchemas(discovered, discovery_options);
            if (cancellation_requested())
            {
                return;
            }

            if (caches_updated && app_guard)
            {
                an::ui::extensions::ExtensionManager refreshed_manager;
                auto refreshed_discovered = refreshed_manager.discoverExtensions(discovery_options);
                if (cancellation_requested())
                {
                    return;
                }
                QMetaObject::invokeMethod(
                    app_guard.data(),
                    [app_guard, extensionLaunchCoordinator, app_dir,
                     refreshed_discovered = std::move(refreshed_discovered)]() mutable {
                        if (!app_guard || !extensionLaunchCoordinator)
                        {
                            return;
                        }

                        bindDiscoveredExtensionLaunches(extensionLaunchCoordinator, app_dir, refreshed_discovered);
                    },
                    Qt::QueuedConnection);
            }
        }

        if (queued_bootstraps <= 0)
        {
            return;
        }

        std::string worker_error;
        const int processed_bootstraps = environmentManager.processQueuedEnvironmentMaintenance(
            discovered, &worker_error, discovery_options.cancellation_token);
        if (cancellation_requested())
        {
            return;
        }
        if (!worker_error.empty())
        {
            if (an::platform::isLoggingInitialized())
            {
                LOG_WARNING("Extension environment maintenance processing reported: {}", worker_error);
            }
        }
        else if (processed_bootstraps > 0 && an::platform::isLoggingInitialized())
        {
            LOG_INFO("Processed {} extension environment maintenance operation(s)", processed_bootstraps);
        }

        if (!app_guard)
        {
            return;
        }

        an::ui::extensions::ExtensionManager refresh_manager;
        auto refreshed_discovered = refresh_manager.discoverExtensions(discovery_options);
        if (cancellation_requested())
        {
            return;
        }
        // An environment the maintenance made ready has no native parameter schemas cached for what it is now.
        if (primeMissingNativeParameterSchemas(refreshed_discovered, discovery_options))
        {
            if (cancellation_requested())
            {
                return;
            }
            refreshed_discovered = refresh_manager.discoverExtensions(discovery_options);
            if (cancellation_requested())
            {
                return;
            }
        }
        if (automatic_environment_cleanup_enabled)
        {
            auto cleanup_discovered = refreshed_discovered;
            cleanup_discovered.insert(cleanup_discovered.end(), startup_catalog_for_cleanup.begin(),
                                      startup_catalog_for_cleanup.end());
            cleanupUnusedExtensionEnvironmentsAtStartup(cleanup_discovered, discovery_options);
        }
        if (cancellation_requested())
        {
            return;
        }
        if (!an::ui::extensions::store_ready_extension_startup_catalog(refreshed_discovered,
                                                                       discovery_options.context) &&
            an::platform::isLoggingInitialized())
        {
            LOG_WARNING("Failed to refresh the extension startup activation catalog after bootstrap");
        }
        if (refreshed_discovered.empty())
        {
            return;
        }

        QMetaObject::invokeMethod(
            app_guard.data(),
            [app_guard, extensionLaunchCoordinator, menuActionManager, app_dir,
             refreshed_discovered = std::move(refreshed_discovered)]() mutable {
                if (!app_guard || !menuActionManager || !extensionLaunchCoordinator)
                {
                    return;
                }

                an::ui::shell::ProcessLaunchService::refreshRegisteredExtensionDescriptors(refreshed_discovered);
                bindDiscoveredExtensionLaunches(extensionLaunchCoordinator, app_dir, refreshed_discovered);
                refreshDiscoveredExtensionActions(app_guard.data(), menuActionManager, refreshed_discovered);
                cleanupMissingStoredProcessIdsOnNextRefresh = true;
                app_guard->refreshStoredProcessMenus();
                app_guard->updateActiveImageActions();
            },
            Qt::QueuedConnection);
    });
}

void refreshInstalledExtensionMenus(AfterNightApp* app,
                                    an::ui::extensions::ExtensionHostLaunchCoordinator* extensionLaunchCoordinator,
                                    an::ui::MenuActionManager* menuActionManager,
                                    an::ui::app::AfterNightAppUiElements& ui,
                                    const an::ui::extensions::ExtensionOperationCancellationToken& cancellation_token)
{
    if (!app || !extensionLaunchCoordinator || !menuActionManager)
    {
        return;
    }

    an::ui::invalidateInstalledExtensionStatusCache(true);

    const auto discovery_options = an::ui::shell::defaultExtensionDiscoveryOptions(false, cancellation_token);
    const std::filesystem::path app_dir(QCoreApplication::applicationDirPath().toStdString());
    QPointer<AfterNightApp> app_guard(app);
    auto* ui_ptr = &ui;
    QThreadPool::globalInstance()->start(
        [app_guard, extensionLaunchCoordinator, menuActionManager, ui_ptr, app_dir, discovery_options]() mutable {
            an::ui::extensions::ExtensionManager extensionManager;
            auto discovered = extensionManager.discoverExtensions(discovery_options);
            if (discovery_options.cancellation_token.isCancellationRequested())
            {
                return;
            }
            if (!an::ui::extensions::store_ready_extension_startup_catalog(discovered, discovery_options.context) &&
                an::platform::isLoggingInitialized())
            {
                LOG_WARNING("Failed to refresh the extension startup activation catalog after extension changes");
            }
            if (!app_guard)
            {
                return;
            }

            QMetaObject::invokeMethod(
                app_guard.data(),
                [app_guard, extensionLaunchCoordinator, menuActionManager, ui_ptr, app_dir,
                 discovered = std::move(discovered)]() mutable {
                    if (!app_guard || !extensionLaunchCoordinator || !menuActionManager || !ui_ptr)
                    {
                        return;
                    }

                    an::ui::extensions::ExtensionManager extensionManager;
                    extensionManager.registerDiscoveredProcesses(discovered,
                                                                 an::ui::process_registry::ProcessRegistry::instance());
                    bindDiscoveredExtensionLaunches(extensionLaunchCoordinator, app_dir, discovered);
                    appendDiscoveredExtensionActions(app_guard.data(), menuActionManager, *ui_ptr, discovered);
                    cleanupMissingStoredProcessIdsOnNextRefresh = true;
                    app_guard->refreshStoredProcessMenus();
                    app_guard->updateActiveImageActions();
                },
                Qt::QueuedConnection);
        });
}

} // anonymous namespace

namespace
{

// A document around an asset an extension produced, shown under `name`; an empty name leaves the view's name to the
// view creation coordinator.
std::optional<an::ui::ImageDocument> documentOfAsset(an::consumer::ImageAsset&& asset, const std::string& name)
{
    an::ui::DocumentNames names;
    names.displayName = QString::fromStdString(name);
    an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(std::move(asset), std::move(names));
    if (!document.hasValue())
    {
        LOG_WARNING("An extension's image could not be shown: {}", document.status().message());
        return std::nullopt;
    }
    return std::move(*document);
}

// An image of a description with every sample zero, a new asset of the session.
an::Result<an::consumer::ImageAsset> zeroedImageOf(const an::consumer::EngineSession& session,
                                                   const an::ImageDescriptor& descriptor)
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    if (!hold.hasValue())
    {
        return hold.status();
    }

    an::Result<an::consumer::AnyImage> image =
        an::Status::failure(an::ErrorCode::Unsupported, "the image's samples are of no representation it can hold");
    (void)an::dispatchSampleType(descriptor.sampleType(), [&]<typename SampleT>() {
        an::Result<an::Image<SampleT>> created = an::Image<SampleT>::create(
            hold->engine(), descriptor, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed);
        image = created.hasValue() ? an::Result<an::consumer::AnyImage>(an::consumer::AnyImage(std::move(*created)))
                                   : an::Result<an::consumer::AnyImage>(created.status());
    });
    if (!image.hasValue())
    {
        return image.status();
    }
    return an::consumer::ImageAsset::adopt(session, std::move(*image));
}

// Exports what a document holds to a new shared buffer, read under a lease taken without waiting: a document a worker
// is writing is refused rather than waited for on the GUI thread.
std::optional<an::ui::extensions::SharedBufferDescriptor>
exportDocumentSnapshot(const an::ui::ImageDocument& document, const an::ui::extensions::ImageExportRequest& request,
                       std::string* error)
{
    an::Result<an::consumer::AssetReadLease> lease = document.tryRead();
    if (!lease.hasValue())
    {
        if (error)
        {
            *error = lease.status().code() == an::ErrorCode::Busy
                         ? std::string("Requested view is being changed; ask for its snapshot again")
                         : std::string("Requested view has no exportable image: ") + lease.status().message();
        }
        return std::nullopt;
    }

    const an::ui::extensions::SharedBufferManager shared_buffer_manager;
    an::Result<an::ui::extensions::SharedImageBuffer> buffer =
        shared_buffer_manager.exportImage(lease->image(), request);
    if (!buffer.hasValue())
    {
        if (error)
        {
            *error = std::string("Requested view could not be exported: ") + buffer.status().message();
        }
        return std::nullopt;
    }
    return buffer->descriptor();
}

// Opens a sequence a workflow wrote on a worker and shows it on the GUI thread, under the file's name; a sequence that
// cannot be read or shown is said and logged there. The session outlives the pool's work: the application drains the
// pool before it shuts the session down.
void openWorkflowSequenceOutput(AfterNightApp* app, const an::consumer::EngineSession& session, const QString& path,
                                const an::SequenceFormat format)
{
    QPointer<AfterNightApp> guard(app);
    QThreadPool::globalInstance()->start([guard, &session, path, format]() {
        auto opened = std::make_shared<an::Result<an::ui::ImageDocument>>(
            an::ui::openSequenceDocument(session, path, format, {}));
        QMetaObject::invokeMethod(
            qApp,
            [guard, opened, path]() {
                if (!guard)
                {
                    return;
                }

                const std::string path_text = path.toStdString();
                if (!opened->hasValue())
                {
                    LOG_WARNING("Failed to load workflow sequence output '{}': {}", path_text,
                                opened->status().message());
                    guard->notifyErrorMessage(guard.data(), QStringLiteral("Workflow Output"),
                                              QStringLiteral("Failed to load workflow sequence output: %1\n%2")
                                                  .arg(path, QString::fromUtf8(opened->status().message())));
                    return;
                }

                an::ui::ImageDocument document = std::move(**opened);
                an::ui::DocumentNames names = document.names();
                names.displayName = QFileInfo(path).completeBaseName();
                names.sourcePath = path;
                document.setNames(std::move(names));
                ImageViewControl* const created_control = guard->createNewImageViewControlFromSequence(
                    nullptr, std::move(document), guard.data(), false,
                    fmt::format("Workflow sequence loaded from '{}'", path_text), ViewControlMode::standard, nullptr,
                    false, false);
                if (!created_control)
                {
                    if (document.valid())
                    {
                        document.close();
                    }
                    LOG_WARNING("Failed to create a view for workflow sequence output '{}'", path_text);
                    guard->notifyErrorMessage(
                        guard.data(), QStringLiteral("Workflow Output"),
                        QStringLiteral("Failed to create a view for workflow sequence output: %1").arg(path));
                    return;
                }

                created_control->fitToWindowAsync();
            },
            Qt::QueuedConnection);
    });
}

} // namespace

struct AfterNightApp::Impl
{
    explicit Impl(an::ui::shell::ApplicationCore& core) : m_Core(core) {}

    // The application without a window, which `main` made before this window and which outlives it.
    an::ui::shell::ApplicationCore& m_Core;

    // What the core's marshalled events run against while this window lives: the core's services.
    std::unique_ptr<an::ui::shell::ServiceEventTarget> m_EventTarget;

    an::ui::app::AfterNightAppUiElements ui;

    // The application's list of views, the core's view service's.
    ImageViewControlManager* m_ImageViewControlManager = nullptr;
    std::unique_ptr<an::ui::ProcessLauncher> m_ProcessLauncher;
    std::unique_ptr<an::ui::ChannelExtractor> m_ChannelExtractor;
    std::unique_ptr<an::ui::ThemeManager> m_ThemeManager;
    std::unique_ptr<an::ui::MenuActionManager> m_MenuActionManager;
    std::unique_ptr<an::ui::extensions::ExtensionHostLaunchCoordinator> m_ExtensionHostLaunchCoordinator;
    std::unique_ptr<an::ui::FileManager> m_FileManager;
    std::unique_ptr<an::ui::ViewControlCoordinator> m_ViewControlCoordinator;
    std::unique_ptr<an::ui::ImageOperationsCoordinator> m_ImageOperationsCoordinator;
    std::unique_ptr<an::ui::MaskOverlayCoordinator> m_MaskOverlayCoordinator;
    std::unique_ptr<an::ui::DialogOrchestrationCoordinator> m_DialogOrchestrationCoordinator;
    std::unique_ptr<an::ui::UiBootstrapCoordinator> m_UiBootstrapCoordinator;
    std::unique_ptr<an::ui::ActionStateCoordinator> m_ActionStateCoordinator;
    std::unique_ptr<an::ui::WindowLayoutCoordinator> m_WindowLayoutCoordinator;
    std::unique_ptr<an::ui::AppLifecycleCoordinator> m_AppLifecycleCoordinator;
    std::unique_ptr<an::ui::shell::ApplicationContext> m_AppContext;

    // The session the caller of `initialize` owns and lends; it outlives this window.
    an::ui::ApplicationSession* m_Session = nullptr;

    std::unique_ptr<QComboBox> m_ImageViewControlListComboBox;
    std::unique_ptr<ImageInfoStatusBarWidget> m_ImageInfoStatusBarWidget;
    std::unique_ptr<QTimer> m_ExtensionHostPumpTimer;
    bool m_ExtensionHostPumpWakePending = false;
    std::unique_ptr<QFileSystemWatcher> m_ExtensionInstallWatcher;
    std::unique_ptr<QTimer> m_ExtensionInstallRefreshTimer;
    an::ui::extensions::ExtensionOperationCancellationToken m_ExtensionBackgroundWorkCancellationToken;

    std::atomic_bool m_IsLayoutLocked = false;
    bool m_UnlockLayoutWhenRestoring = false;

    LogViewerControl* m_LogViewerControl = nullptr;
    // The console's destroyed handler, which reads this state: let go before the state goes, since the console is a
    // child the window's base class deletes after it.
    QMetaObject::Connection m_LogViewerClosedConnection;

    bool m_DarkMode = false;

    IconManagerSptr m_IconManager;

    QToolBar* m_DockManagerToolBar = nullptr;
    QToolButton* m_ToolbarLockLayout = nullptr;

    KDDockWidgets::QtWidgets::MainWindow* m_DockMainWindow = nullptr;

    MainDashboard* m_MainDashboard = nullptr;
    QScrollArea* m_MainDashboardScrollArea = nullptr;

    bool m_IsTerminating = false;

    int m_MaxThreadsForBatchProcess = 0;
    int m_MaxThreadsForPreviewProcess = 0;
};

// The window's state lives behind the pimpl; beside it the window holds only the tables of the three interfaces of the
// shell contract it implements.
static_assert(sizeof(AfterNightApp) <= sizeof(QMainWindow) + sizeof(std::uintptr_t) * 5,
              "AfterNightApp should stay thin: its state lives behind the pimpl.");

AfterNightApp* AfterNightApp::m_Instance = nullptr;
AfterNightApp* AfterNightApp::createInstance(an::ui::shell::ApplicationCore& core)
{
    if (!m_Instance)
    {
        m_Instance = new AfterNightApp(core);
    }

    return m_Instance;
}

AfterNightApp* AfterNightApp::getInstance()
{
    return m_Instance;
}

AfterNightApp* AfterNightApp::instanceOrNull() noexcept
{
    return m_Instance;
}

void AfterNightApp::destroyInstance()
{
    if (m_Instance)
    {
        delete m_Instance;
        m_Instance = nullptr;
    }
}

AfterNightApp::AfterNightApp(an::ui::shell::ApplicationCore& core)
    : QMainWindow(nullptr), m_impl(std::make_unique<Impl>(core))
{
    // The core's services answer through this window - where a view goes, what a dialog belongs to, what the status
    // line shows - until it goes; the work the threads hand the GUI thread runs against the services meanwhile.
    m_impl->m_ImageViewControlManager = &core.viewService().manager();
    core.setShell(this);
    an::ui::shell::ServiceEventTarget::Dependencies eventTargetDeps;
    eventTargetDeps.updateActiveImageActions = [this]() { updateActiveImageActions(); };
    eventTargetDeps.addRecentFile = [this](const std::string& fileName,
                                           const std::shared_ptr<const an::consumer::ImageAsset>& image) {
        addRecentFile(fileName, image);
    };
    eventTargetDeps.removeRecentFile = [this](const std::string& fileName) { removeRecentFile(fileName); };
    m_impl->m_EventTarget = std::make_unique<an::ui::shell::ServiceEventTarget>(core, std::move(eventTargetDeps));
    core.setEventTarget(m_impl->m_EventTarget.get());
    // Install the process-global app context before constructing UI services so
    // any control created during startup observes a live context. It is the core's and this shell's; what is still
    // this window's is handed to it.
    an::ui::shell::ApplicationContext::Dependencies contextDeps;
    contextDeps.applyTheme = [this](const QString& themeId) { applyTheme(themeId); };
    contextDeps.openRecentSingleFile = [this](const QString& filePath) { openRecentSingleFile(filePath); };
    contextDeps.openDroppedFiles = [this](const QStringList& files) { openDroppedFiles(files); };
    contextDeps.setCustomImageActionEnabled = [this](ImageViewControl* view, const GlobalViewControlActions action,
                                                     const bool enable) {
        setCustomImageActionEnabled(view, action, enable);
    };
    m_impl->m_AppContext = std::make_unique<an::ui::shell::ApplicationContext>(core, *this, std::move(contextDeps));
    an::ui::setAppContext(m_impl->m_AppContext.get());

    m_impl->ui = an::ui::app::buildAfterNightAppUi(this);
    setObjectName("afterNightMainWindow");
    setMinimumSize(an::ui::kMainWindowMinimumWidth, an::ui::kMainWindowMinimumHeight);

    m_impl->m_IconManager = core.iconManager();
    m_impl->m_ProcessLauncher = std::make_unique<an::ui::ProcessLauncher>();
    m_impl->m_ChannelExtractor = std::make_unique<an::ui::ChannelExtractor>();
    an::ui::ThemeManager::Dependencies themeManagerDeps;
    themeManagerDeps.darkMode = &m_impl->m_DarkMode;
    themeManagerDeps.reloadIcons = [this](const an::ui::ThemeIconRefreshRequest& request) {
        if (!m_impl->m_IconManager)
        {
            return;
        }

        m_impl->m_IconManager->setColors(request.normalIconColor, request.disabledIconColor);
        m_impl->m_IconManager->setSemanticColors(request.infoIconColor, request.warningIconColor,
                                                 request.errorIconColor, request.questionIconColor,
                                                 request.successIconColor);
        m_impl->m_IconManager->loadIcons(":/AfterNight/icons/SVG");
        m_impl->m_IconManager->loadIcons(":/AfterNight/icons/PNG");
    };
    themeManagerDeps.setActionIcons = [this]() { setActionIcons(); };
    themeManagerDeps.mainDashboard = &m_impl->m_MainDashboard;
    themeManagerDeps.toolbarLockLayout = &m_impl->m_ToolbarLockLayout;
    themeManagerDeps.isLayoutLocked = &m_impl->m_IsLayoutLocked;
    themeManagerDeps.imageViewControlManager = m_impl->m_ImageViewControlManager;
    themeManagerDeps.dockMainWindow = &m_impl->m_DockMainWindow;
    themeManagerDeps.forEachOpenProcess = [this](const std::function<void(TopLevelProcessBaseControl*)>& visit) {
        m_impl->m_Core.openProcesses().forEach(visit);
    };
    themeManagerDeps.mainWindow = this;
    themeManagerDeps.iconProvider = [this](const std::string& iconName) {
        return m_impl->m_IconManager ? m_impl->m_IconManager->getIcon(QString::fromUtf8(iconName)) : QIcon();
    };
    m_impl->m_ThemeManager = std::make_unique<an::ui::ThemeManager>(themeManagerDeps);

    // Create the image view channel combobox
    m_impl->m_ImageViewControlListComboBox = std::make_unique<QComboBox>();
    m_impl->m_ExtensionHostLaunchCoordinator = std::make_unique<an::ui::extensions::ExtensionHostLaunchCoordinator>();
    m_impl->m_ExtensionHostLaunchCoordinator->setManagedImageHooks(
        // An execute's result is an edit of the target view, recorded in its history before it is shown.
        [](ImageViewControl* control, an::consumer::ImageAsset&& image, const std::string& historyStepName,
           const std::string& historyStepDescription) {
            if (!control || !image.valid())
            {
                return;
            }

            auto result = std::make_shared<an::consumer::ImageAsset>(std::move(image));
            control->editImage(historyStepName, historyStepDescription,
                               [result](const an::consumer::EngineSession&, const an::consumer::AssetWriteLease&,
                                        std::stop_token) -> an::Result<an::consumer::ImageAsset> {
                                   if (!result->valid())
                                   {
                                       return an::Status::failure(an::ErrorCode::InvalidState,
                                                                  "the extension's result was already shown");
                                   }
                                   return std::move(*result);
                               });
        },
        // A preview lands in the preview view the session keeps, made beside the target the first time.
        [this](ImageViewControl* control, ImageViewControl* preview_control,
               an::consumer::ImageAsset&& image) -> ImageViewControl* {
            if (!control || !image.valid())
            {
                return preview_control;
            }

            if (preview_control)
            {
                preview_control->replaceImage(std::move(image), true);
                return preview_control;
            }

            std::optional<an::ui::ImageDocument> document = documentOfAsset(std::move(image), std::string());
            if (!document.has_value())
            {
                return nullptr;
            }
            ImageViewControl* const created_preview = createNewImageViewControlFromImage(
                nullptr, std::move(*document), nullptr, std::string(), ViewControlMode::rt_preview, control);
            if (created_preview)
            {
                created_preview->fitToWindowAsync();
            }
            return created_preview;
        },
        // The target's mask travels read-only as the selection; an execute also carries an output mask of the
        // target's shape, every sample zero - nothing masked - for the extension to write.
        [this](ImageViewControl* control,
               const bool for_preview) -> std::vector<an::ui::extensions::ManagedAuxiliaryImage> {
            std::vector<an::ui::extensions::ManagedAuxiliaryImage> auxiliary_images;
            if (!control)
            {
                return auxiliary_images;
            }

            ImageViewControl* const current_mask = control->getCurrentMask();
            if (current_mask)
            {
                if (std::shared_ptr<const an::consumer::ImageAsset> mask_image = current_mask->document().asset())
                {
                    auxiliary_images.push_back({an::ui::extensions::SharedBufferRole::Mask,
                                                an::ui::extensions::SharedBufferAccessMode::ReadOnly, "selection",
                                                std::move(mask_image)});
                }
            }

            if (for_preview)
            {
                return auxiliary_images;
            }

            std::optional<an::ImageDescriptor> template_descriptor;
            if (current_mask)
            {
                template_descriptor = current_mask->imageDescriptor();
            }
            if (!template_descriptor.has_value())
            {
                template_descriptor = control->imageDescriptor();
            }
            an::ui::IEngineSessionProvider* const provider = engineSessionProvider();
            if (!template_descriptor.has_value() || provider == nullptr)
            {
                return auxiliary_images;
            }

            an::Result<an::consumer::ImageAsset> output_mask =
                zeroedImageOf(provider->engineSession(), *template_descriptor);
            if (!output_mask.hasValue())
            {
                LOG_WARNING("The extension's output mask could not be made: {}", output_mask.status().message());
                return auxiliary_images;
            }
            auxiliary_images.push_back({an::ui::extensions::SharedBufferRole::Mask,
                                        an::ui::extensions::SharedBufferAccessMode::ReadWrite, "output_mask",
                                        std::make_shared<const an::consumer::ImageAsset>(std::move(*output_mask))});
            return auxiliary_images;
        },
        // The masks an execute wrote open as views beside the target, and the first becomes its mask.
        [this](ImageViewControl* control, std::vector<an::ui::extensions::ManagedNamedImageResult>&& output_masks) {
            if (!control || output_masks.empty())
            {
                return;
            }

            ImageViewControl* first_mask_control = nullptr;
            for (auto& output_mask : output_masks)
            {
                if (!output_mask.image.valid())
                {
                    continue;
                }

                const std::string symbolic_name =
                    output_mask.symbolic_name.empty() ? std::string("output_mask") : output_mask.symbolic_name;
                std::optional<an::ui::ImageDocument> document =
                    documentOfAsset(std::move(output_mask.image), symbolic_name);
                if (!document.has_value())
                {
                    continue;
                }
                ImageViewControl* const created_control = createNewImageViewControlFromImage(
                    nullptr, std::move(*document), nullptr,
                    fmt::format("Extension mask '{}' created from '{}'", symbolic_name, control->getUniqueName()),
                    ViewControlMode::standard, control);
                if (!first_mask_control && created_control)
                {
                    first_mask_control = created_control;
                }
            }

            if (first_mask_control)
            {
                control->setCurrentMask(first_mask_control);
                first_mask_control->fitToWindowAsync();
            }
        },
        // A preview is computed over the selection, when the target's view has one.
        [](ImageViewControl* control) -> std::optional<QRect> {
            if (!control)
            {
                return std::nullopt;
            }

            auto* const view_widget = control->getViewWidget();
            const std::optional<an::ImageDescriptor> descriptor = control->imageDescriptor();
            if (!view_widget || !descriptor.has_value())
            {
                return std::nullopt;
            }

            return an::ui::extensions::normalizeManagedPreviewSubArea(
                view_widget->isSelectionAreaEnabled(),
                QSize(static_cast<int>(descriptor->extent().width()), static_cast<int>(descriptor->extent().height())),
                view_widget->getCurrentSelectionArea());
        },
        // A result an extension generates rather than applies opens as a new view beside the target.
        [this](ImageViewControl* control, an::consumer::ImageAsset&& image, const std::string& historyStepName,
               const std::string& historyStepDescription) -> ImageViewControl* {
            if (!control || !image.valid())
            {
                return nullptr;
            }

            const std::string history_description =
                historyStepDescription.empty() ? historyStepName : historyStepDescription;
            std::optional<an::ui::ImageDocument> document = documentOfAsset(std::move(image), historyStepName);
            if (!document.has_value())
            {
                return nullptr;
            }
            ImageViewControl* const created_control = createNewImageViewControlFromImage(
                nullptr, std::move(*document), nullptr, history_description, ViewControlMode::standard, control);
            if (created_control)
            {
                created_control->fitToWindowAsync();
            }
            return created_control;
        });
    m_impl->m_ExtensionHostLaunchCoordinator->setManagedViewNameProvider(
        [](ImageViewControl* control) { return control ? control->getUniqueName() : std::string(); });
    m_impl->m_ExtensionHostLaunchCoordinator->setSessionPumpWakeCallback([this]() {
        if (m_impl->m_ExtensionHostPumpWakePending)
        {
            return;
        }

        m_impl->m_ExtensionHostPumpWakePending = true;
        QTimer::singleShot(0, this, [this]() {
            m_impl->m_ExtensionHostPumpWakePending = false;
            if (m_impl->m_ExtensionHostLaunchCoordinator)
            {
                m_impl->m_ExtensionHostLaunchCoordinator->pumpSessions();
            }
        });
    });
    m_impl->m_ExtensionHostPumpTimer = std::make_unique<QTimer>(this);
    m_impl->m_ExtensionHostPumpTimer->setInterval(100);
    QObject::connect(m_impl->m_ExtensionHostPumpTimer.get(), &QTimer::timeout, this, [this]() {
        if (m_impl->m_ExtensionHostLaunchCoordinator)
        {
            m_impl->m_ExtensionHostLaunchCoordinator->pumpSessions();
        }
    });
    m_impl->m_ExtensionHostPumpTimer->start();
    an::ui::MenuActionManager::Dependencies menuActionDeps;
    menuActionDeps.signalContext = this;
    menuActionDeps.ui = &m_impl->ui;
    menuActionDeps.imageSelector = m_impl->m_ImageViewControlListComboBox.get();
    menuActionDeps.fileManager = [this]() -> an::ui::FileManager* { return m_impl->m_FileManager.get(); };
    menuActionDeps.imageOperations = [this]() -> an::ui::ImageOperationsCoordinator* {
        return m_impl->m_ImageOperationsCoordinator.get();
    };
    menuActionDeps.maskOverlay = [this]() -> an::ui::MaskOverlayCoordinator* {
        return m_impl->m_MaskOverlayCoordinator.get();
    };
    menuActionDeps.tryLaunchRegistryProcessById = [this](const std::string& descriptorId) {
        return tryLaunchRegistryProcessById(descriptorId);
    };
    menuActionDeps.onImageSelectorChanged = [this](const QString& text) {
        on_imageViewControlListComboBox_currentTextChanged(text);
    };
    menuActionDeps.onNewSequenceRequested = [this]() { newSequence(); };
    menuActionDeps.onLogViewerRequested = [this]() { on_LogViewerClicked(m_impl->ui.actionLog_Viewer->isChecked()); };
    menuActionDeps.onOpenHelpRequested = [this]() { openHelp(); };
    menuActionDeps.onAboutRequested = [this]() { about(); };
    menuActionDeps.onSystemInformationRequested = [this]() { systemInformation(); };
    menuActionDeps.onOpenSettingsRequested = [this]() { openSettings(); };
    menuActionDeps.onOpenExtensionManagerRequested = [this]() { openSettingsAt(extensionManagerPageTitle()); };
    menuActionDeps.onLockLayoutRequested = [this]() { lockLayout(); };
    menuActionDeps.onUnlockLayoutRequested = [this]() { unlockLayout(); };
    menuActionDeps.onGroupAllImagesRequested = [this]() { groupAllImages(); };
    menuActionDeps.onSplitAllImagesRequested = [this]() { splitAllImages(); };
    menuActionDeps.iconManager = [this]() { return getIconManager(); };
    m_impl->m_MenuActionManager = std::make_unique<an::ui::MenuActionManager>(menuActionDeps);
    registerDiscoveredExtensions(this, m_impl->m_ExtensionHostLaunchCoordinator.get(),
                                 m_impl->m_MenuActionManager.get(), m_impl->ui,
                                 m_impl->m_ExtensionBackgroundWorkCancellationToken);
    m_impl->m_MenuActionManager->connectProcessActions();
    m_impl->m_ExtensionInstallRefreshTimer = std::make_unique<QTimer>(this);
    m_impl->m_ExtensionInstallRefreshTimer->setSingleShot(true);
    m_impl->m_ExtensionInstallRefreshTimer->setInterval(250);
    QObject::connect(m_impl->m_ExtensionInstallRefreshTimer.get(), &QTimer::timeout, this, [this]() {
        refreshInstalledExtensionMenus(this, m_impl->m_ExtensionHostLaunchCoordinator.get(),
                                       m_impl->m_MenuActionManager.get(), m_impl->ui,
                                       m_impl->m_ExtensionBackgroundWorkCancellationToken);
    });

    m_impl->m_ExtensionInstallWatcher = std::make_unique<QFileSystemWatcher>(this);
    const std::array<std::filesystem::path, 2> extension_watch_roots = {
        an::ui::extensions::repository_extensions_root(), an::ui::extensions::user_extensions_root()};
    for (const auto& watch_root : extension_watch_roots)
    {
        std::error_code ec;
        std::filesystem::create_directories(watch_root, ec);
        if (!ec)
        {
            m_impl->m_ExtensionInstallWatcher->addPath(QString::fromStdString(watch_root.string()));
        }
    }
    QObject::connect(m_impl->m_ExtensionInstallWatcher.get(), &QFileSystemWatcher::directoryChanged, this,
                     [this](const QString& path) {
                         if (!m_impl->m_ExtensionInstallWatcher->directories().contains(path) &&
                             QFileInfo::exists(path))
                         {
                             m_impl->m_ExtensionInstallWatcher->addPath(path);
                         }
                         m_impl->m_ExtensionInstallRefreshTimer->start();
                     });
    an::ui::FileManager::Dependencies fileManagerDeps;
    fileManagerDeps.signalContext = this;
    fileManagerDeps.ui = &m_impl->ui;
    fileManagerDeps.recentFiles = &m_impl->m_Core.recentFiles().entriesForEdit();
    fileManagerDeps.lastOpenDirectory = &m_impl->m_Core.lastOpenDirectoryForEdit();
    fileManagerDeps.recentFilesLimit = [this]() { return m_impl->m_Core.recentFiles().limit(); };
    fileManagerDeps.saveRecentFiles = [this]() { m_impl->m_Core.recentFiles().save(); };
    fileManagerDeps.reloadRecentFilesThumbnails = [this]() {
        if (m_impl->m_MainDashboard)
        {
            m_impl->m_MainDashboard->reloadRecentFilesThumbnails();
        }
    };
    fileManagerDeps.notifyRecentFilesListDeleted = [this]() {
        if (m_impl->m_MainDashboard)
        {
            m_impl->m_MainDashboard->notifyRecentFilesListDeleted();
        }
    };
    fileManagerDeps.rawOpenPolicy = [this]() { return rawOpenPolicy(); };
    fileManagerDeps.persistLastOpenDirectory = [this](const QString& path) { setLastOpenDirectoryAndPersist(path); };
    fileManagerDeps.switchToDockManager = [this]() { switchToDockManager(); };
    fileManagerDeps.thumbnailsDirPath = [this]() { return getThumbnailsDirPath(); };
    fileManagerDeps.updateImageViewControlList = [this]() { updateImageViewControlList(); };
    // A file the person opens is shown in a view of theirs.
    fileManagerDeps.viewFactory = &m_impl->m_Core.viewService().userViews();
    m_impl->m_FileManager = std::make_unique<an::ui::FileManager>(fileManagerDeps);
    an::ui::ViewControlCoordinator::Dependencies viewControlDeps;
    viewControlDeps.imageViewControlManager = m_impl->m_ImageViewControlManager;
    viewControlDeps.imageViewControlListComboBox = m_impl->m_ImageViewControlListComboBox.get();
    viewControlDeps.mainDashboard = &m_impl->m_MainDashboard;
    viewControlDeps.switchToMainDashboard = [this]() { switchToMainDashboard(); };
    viewControlDeps.getDockWidgetsByViewControlMap = [this]() { return getDockWidgetsByViewControlMap(); };
    m_impl->m_ViewControlCoordinator = std::make_unique<an::ui::ViewControlCoordinator>(viewControlDeps);
    an::ui::ImageOperationsCoordinator::Dependencies imageOperationsDeps;
    imageOperationsDeps.activeViewControl = [this]() -> ImageViewControl* {
        return m_impl->m_ImageViewControlManager ? m_impl->m_ImageViewControlManager->getActiveImageViewControl()
                                                 : nullptr;
    };
    imageOperationsDeps.listViewControls = [this]() -> std::vector<ImageViewControl*> {
        return m_impl->m_ImageViewControlManager ? m_impl->m_ImageViewControlManager->getListOfImageViewControls()
                                                 : std::vector<ImageViewControl*>();
    };
    imageOperationsDeps.messageParent = [this]() -> QWidget* { return this; };
    imageOperationsDeps.notifyErrorMessage = [this](const QString& title, const QString& text) {
        notifyErrorMessage(this, title, text);
    };
    // The image menu's actions that open no window: the processes library's table, which the registry's own image
    // operations run too.
    imageOperationsDeps.nonInteractive = an::ui::nonInteractiveImageOperations();
    // The image menu's Export opens the export window over the sequence the view shows.
    imageOperationsDeps.exportSequence = [](ImageViewControl* control) {
        if (control != nullptr)
        {
            (void)ExportSequenceControl::openFor(*control);
        }
    };
    m_impl->m_ImageOperationsCoordinator = std::make_unique<an::ui::ImageOperationsCoordinator>(imageOperationsDeps);
    an::ui::MaskOverlayCoordinator::Dependencies maskOverlayDeps;
    maskOverlayDeps.ui = &m_impl->ui;
    maskOverlayDeps.imageViewControlManager = m_impl->m_ImageViewControlManager;
    maskOverlayDeps.notifyErrorMessage = [this](const QString& title, const QString& text) {
        notifyErrorMessage(this, title, text);
    };
    m_impl->m_MaskOverlayCoordinator = std::make_unique<an::ui::MaskOverlayCoordinator>(maskOverlayDeps);
    an::ui::DialogOrchestrationCoordinator::Dependencies dialogDeps;
    dialogDeps.mainWindow = this;
    dialogDeps.darkMode = &m_impl->m_DarkMode;
    dialogDeps.extensionBackgroundWorkCancellationToken = m_impl->m_ExtensionBackgroundWorkCancellationToken;
    dialogDeps.globalSettingsPersistence = [this]() { return m_impl->m_Core.globalSettingsStore(); };
    dialogDeps.recentFiles = &m_impl->m_Core.recentFiles().entriesForEdit();
    dialogDeps.mainDashboard = &m_impl->m_MainDashboard;
    dialogDeps.getRecentFilesLimit = [this]() { return getRecentFilesLimit(); };
    dialogDeps.iconProvider = [this](const std::string& iconName) {
        return m_impl->m_IconManager ? m_impl->m_IconManager->getIcon(QString::fromUtf8(iconName)) : QIcon();
    };
    dialogDeps.removeRecentFileThumbnail = [this](const std::string& fileName) {
        if (m_impl->m_FileManager)
        {
            m_impl->m_FileManager->removeRecentFileThumbnail(fileName);
        }
    };
    dialogDeps.persistRecentFiles = [this]() { persistRecentFiles(); };
    dialogDeps.settingsApplied = [this]() {
        if (m_impl->m_ExtensionHostLaunchCoordinator)
        {
            m_impl->m_ExtensionHostLaunchCoordinator->refreshPreviewFrameRate();
        }
    };
    dialogDeps.updateRecentFilesMenuEntries = [this]() {
        if (m_impl->m_FileManager)
        {
            m_impl->m_FileManager->updateRecentFilesMenuEntries();
        }
    };
    m_impl->m_DialogOrchestrationCoordinator = std::make_unique<an::ui::DialogOrchestrationCoordinator>(dialogDeps);
    an::ui::UiBootstrapCoordinator::Dependencies uiBootstrapDeps;
    uiBootstrapDeps.ui = &m_impl->ui;
    uiBootstrapDeps.mainWindow = this;
    uiBootstrapDeps.imageViewControlListComboBox = m_impl->m_ImageViewControlListComboBox.get();
    uiBootstrapDeps.imageInfoStatusBarWidget = &m_impl->m_ImageInfoStatusBarWidget;
    uiBootstrapDeps.mainDashboard = &m_impl->m_MainDashboard;
    uiBootstrapDeps.mainDashboardScrollArea = &m_impl->m_MainDashboardScrollArea;
    uiBootstrapDeps.dockMainWindow = &m_impl->m_DockMainWindow;
    uiBootstrapDeps.dockManagerToolBar = &m_impl->m_DockManagerToolBar;
    uiBootstrapDeps.toolbarLockLayout = &m_impl->m_ToolbarLockLayout;
    uiBootstrapDeps.connectActions = [this]() { connectActions(); };
    uiBootstrapDeps.refreshTheme = [this]() { refreshTheme(); };
    uiBootstrapDeps.onOpenFileRequested = [this]() {
        if (m_impl->m_FileManager)
        {
            m_impl->m_FileManager->openFile();
        }
    };
    uiBootstrapDeps.onNewSequenceRequested = [this]() { newSequence(); };
    uiBootstrapDeps.onImageIntegrationRequested = [this]() {
        if (!tryLaunchRegistryProcessById("multi_image_stacking.actionImage_Integration") && m_impl->m_ProcessLauncher)
        {
            m_impl->m_ProcessLauncher->executeImageIntegration();
        }
    };
    uiBootstrapDeps.onOpenExtensionManagerRequested = [this]() { openSettingsAt(extensionManagerPageTitle()); };
    uiBootstrapDeps.onClearRecentFilesRequested = [this]() {
        if (m_impl->m_FileManager)
        {
            m_impl->m_FileManager->clearRecentFilesList();
        }
    };
    uiBootstrapDeps.onOpenSettingsRequested = [this]() { openSettings(); };
    uiBootstrapDeps.onSystemInformationRequested = [this]() { systemInformation(); };
    uiBootstrapDeps.onAboutRequested = [this]() { about(); };
    uiBootstrapDeps.onOpenHelpRequested = [this]() { openHelp(); };
    uiBootstrapDeps.onOpenRecentFileRequested = [this](const QString& filePath) {
        if (m_impl->m_FileManager)
        {
            m_impl->m_FileManager->openRecentSingleFile(filePath);
        }
    };
    uiBootstrapDeps.onRemoveRecentFileRequested = [this](const QString& filePath) {
        removeRecentFile(filePath.toStdString());
    };
    m_impl->m_UiBootstrapCoordinator = std::make_unique<an::ui::UiBootstrapCoordinator>(uiBootstrapDeps);
    an::ui::ActionStateCoordinator::Dependencies actionStateDeps;
    actionStateDeps.ui = &m_impl->ui;
    actionStateDeps.imageViewControlManager = m_impl->m_ImageViewControlManager;
    actionStateDeps.timerContext = this;
    actionStateDeps.isTerminating = &m_impl->m_IsTerminating;
    actionStateDeps.isMaximizedViewActive = [this]() { return isMaximizedViewActive(); };
    actionStateDeps.iconProvider = [this](const std::string& iconName) {
        return m_impl->m_IconManager ? m_impl->m_IconManager->getIcon(QString::fromUtf8(iconName)) : QIcon();
    };
    m_impl->m_ActionStateCoordinator = std::make_unique<an::ui::ActionStateCoordinator>(actionStateDeps);
    an::ui::WindowLayoutCoordinator::Dependencies windowLayoutDeps;
    windowLayoutDeps.ui = &m_impl->ui;
    windowLayoutDeps.imageViewControlManager = m_impl->m_ImageViewControlManager;
    windowLayoutDeps.imageViewControlListComboBox = m_impl->m_ImageViewControlListComboBox.get();
    windowLayoutDeps.timerContext = this;
    windowLayoutDeps.mainDashboard = &m_impl->m_MainDashboard;
    windowLayoutDeps.mainDashboardScrollArea = &m_impl->m_MainDashboardScrollArea;
    windowLayoutDeps.dockMainWindow = &m_impl->m_DockMainWindow;
    windowLayoutDeps.dockManagerToolBar = &m_impl->m_DockManagerToolBar;
    windowLayoutDeps.toolbarLockLayout = &m_impl->m_ToolbarLockLayout;
    windowLayoutDeps.unlockLayoutWhenRestoring = &m_impl->m_UnlockLayoutWhenRestoring;
    windowLayoutDeps.iconProvider = [this](const std::string& iconName) {
        return m_impl->m_IconManager ? m_impl->m_IconManager->getIcon(QString::fromUtf8(iconName)) : QIcon();
    };
    windowLayoutDeps.setActiveImageViewControl = [this](ImageViewControl* control) {
        setActiveImageViewControl(control);
    };
    windowLayoutDeps.addToolBar = [this](QToolBar* toolBar) { addToolBar(toolBar); };
    windowLayoutDeps.toggleLayout = [this]() { toogleLayout(); };
    windowLayoutDeps.lockLayout = [this]() { lockLayout(); };
    windowLayoutDeps.unlockLayout = [this]() { unlockLayout(); };
    windowLayoutDeps.layoutIsLocked = [this]() { return layoutIsLocked(); };
    m_impl->m_WindowLayoutCoordinator = std::make_unique<an::ui::WindowLayoutCoordinator>(windowLayoutDeps);
    an::ui::AppLifecycleCoordinator::Dependencies appLifecycleDeps;
    appLifecycleDeps.mainWindow = this;
    appLifecycleDeps.globalSettingsPersistence = [this]() { return m_impl->m_Core.globalSettingsStore(); };
    appLifecycleDeps.imageViewControlManager = m_impl->m_ImageViewControlManager;
    appLifecycleDeps.isTerminating = &m_impl->m_IsTerminating;
    appLifecycleDeps.maxThreadsForBatchProcess = &m_impl->m_MaxThreadsForBatchProcess;
    appLifecycleDeps.maxThreadsForPreviewProcess = &m_impl->m_MaxThreadsForPreviewProcess;
    appLifecycleDeps.logModernizationFeatureFlags = []() { logModernizationFeatureFlags(); };
    appLifecycleDeps.initializePersistence = [this]() { initializePersistence(); };
    appLifecycleDeps.cleanTemporaryFiles = [this]() { cleanTemporaryFiles(); };
    appLifecycleDeps.cleanTemporaryFilesAsync = [this]() { cleanTemporaryFilesAsync(); };
    appLifecycleDeps.cleanStaleScratchRootsAsync = [this]() { cleanStaleScratchRootsAsync(); };
    appLifecycleDeps.applyTheme = [this](const QString& themeId) { applyTheme(themeId); };
    appLifecycleDeps.loadIcons = [this]() { loadIcons(); };
    appLifecycleDeps.setActionIcons = [this]() { setActionIcons(); };
    appLifecycleDeps.refreshProxyStyleTokens = []() {
        if (auto* proxyStyle = an::ui::installedAnProxyStyle())
        {
            proxyStyle->refreshThemeTokens();
        }
    };
    appLifecycleDeps.createAuxToolBars = [this]() { createAuxToolBars(); };
    appLifecycleDeps.setEnableImageActions = [this](const bool enable) { setEnableImageActions(enable); };
    appLifecycleDeps.updateRecentFilesMenuEntries = [this]() {
        if (m_impl->m_FileManager)
        {
            m_impl->m_FileManager->updateRecentFilesMenuEntries();
        }
    };
    appLifecycleDeps.initializeMainDashboard = [this]() { initializeMainDashboard(); };
    appLifecycleDeps.initializeDockManager = [this]() { initializeDockManager(); };
    appLifecycleDeps.initializeMaximizedView = [this]() { initializeMaximizedView(); };
    appLifecycleDeps.lockLayout = [this]() { lockLayout(); };
    appLifecycleDeps.cancelBackgroundWork = [this]() {
        m_impl->m_ExtensionBackgroundWorkCancellationToken.requestCancellation();
        m_impl->m_Core.processLaunch().cancelPreparations();
    };
    appLifecycleDeps.notifyApplicationClose = [this]() { notifyApplicationClose(); };
    appLifecycleDeps.getNumOfOpenProcesses = [this]() { return getNumOfOpenProcesses(); };
    appLifecycleDeps.closeOpenProcesses = [this]() { stopExtensionHostSessions(); };
    appLifecycleDeps.messageBoxYesNo = [this](QWidget* parent, const QString& title, const QString& text,
                                              const bool warn,
                                              int* ret) { messageBoxYesNo(parent, title, text, warn, ret); };
    appLifecycleDeps.availableScreenGeometries = []() {
        QList<QRect> geometries;
        for (QScreen* screen : QGuiApplication::screens())
        {
            if (screen)
            {
                geometries.push_back(screen->availableGeometry());
            }
        }
        return geometries;
    };
    m_impl->m_AppLifecycleCoordinator = std::make_unique<an::ui::AppLifecycleCoordinator>(appLifecycleDeps);

    // What the core's services ask of this window that is not the shell's to answer: the actions' state, the extension
    // sessions' views, the image selector, the context menu, the settings dialogs and the process menus.
    an::ui::shell::ViewService::Hooks viewHooks;
    viewHooks.updateActiveImageActions = [this]() { updateActiveImageActions(); };
    viewHooks.updateActiveImageHistory = [this]() { updateActiveImageHistory(); };
    viewHooks.refreshViewList = [this]() { updateImageViewControlList(); };
    viewHooks.viewCreated = [this](const ImageViewControl* view) {
        if (view && m_impl->m_ExtensionHostLaunchCoordinator)
        {
            const std::string viewName = view->getUniqueName();
            m_impl->m_ExtensionHostLaunchCoordinator->handleViewControlCreated(
                view, viewName, viewName, view->getFileName(), an::ui::buildImageSelectorTooltip(view).toStdString());
        }
    };
    viewHooks.viewDestroyed = [this](const ImageViewControl* view) {
        if (view && m_impl->m_ExtensionHostLaunchCoordinator)
        {
            m_impl->m_ExtensionHostLaunchCoordinator->handleViewControlDestroyed(view, view->getUniqueName());
        }
    };
    viewHooks.viewUpdated = [this](const ImageViewControl* view) {
        if (m_impl->m_ViewControlCoordinator)
        {
            m_impl->m_ViewControlCoordinator->viewUpdated(view);
        }
        if (m_impl->m_ExtensionHostLaunchCoordinator)
        {
            m_impl->m_ExtensionHostLaunchCoordinator->handleViewControlUpdated(view);
        }
    };
    viewHooks.viewRenamed = [this](const std::string& newName, const std::string& oldName,
                                   const ImageViewControl* view) {
        if (m_impl->m_ExtensionHostLaunchCoordinator)
        {
            m_impl->m_ExtensionHostLaunchCoordinator->handleViewControlRenamed(
                newName, oldName, view, view ? view->getFileName() : std::string(),
                view ? an::ui::buildImageSelectorTooltip(view).toStdString() : std::string());
        }
    };
    viewHooks.contextMenu = [this](ImageViewControl* view, QContextMenuEvent* event) {
        createContextMenuForViewControl(view, event);
    };
    viewHooks.isTerminating = [this]() { return m_impl->m_IsTerminating; };
    core.viewService().setHooks(std::move(viewHooks));

    an::ui::shell::DialogService::Hooks dialogHooks;
    dialogHooks.openSettingsAt = [this](const QString& pageName) { openSettingsAt(pageName); };
    dialogHooks.openExtensionsAt = [this](const QString& pageName) { openSettingsAt(pageName); };
    core.dialogService().setHooks(std::move(dialogHooks));

    an::ui::shell::ProcessLaunchService::Hooks launchHooks;
    launchHooks.recentProcessesChanged = [this]() { updateRecentProcessesMenuEntries(); };
    launchHooks.extensionsPrepared = [this](const std::vector<an::ui::extensions::ExtensionLoadResult>& discovered) {
        const std::filesystem::path appDir(QCoreApplication::applicationDirPath().toStdString());
        bindDiscoveredExtensionLaunches(m_impl->m_ExtensionHostLaunchCoordinator.get(), appDir, discovered);
        refreshDiscoveredExtensionActions(this, m_impl->m_MenuActionManager.get(), discovered);
        cleanupMissingStoredProcessIdsOnNextRefresh = true;
        refreshStoredProcessMenus();
        updateActiveImageActions();
    };
    core.processLaunch().setHooks(std::move(launchHooks));

    if (m_impl->m_UiBootstrapCoordinator)
    {
        m_impl->m_UiBootstrapCoordinator->finalizeConstructorUiSetup();
    }
}

void AfterNightApp::initialize(an::ui::ApplicationSession& session)
{
    // The session is opened once the settings store has been read, from the limits it states, before anything that
    // reads or shows an image.
    m_impl->m_Session = &session;

    if (m_impl->m_AppLifecycleCoordinator)
    {
        m_impl->m_AppLifecycleCoordinator->initialize();
    }

    if (m_impl->m_ExtensionHostLaunchCoordinator)
    {
        an::ui::extensions::ExtensionSdkUiInteractionHandler ui_interaction_handler;
        ui_interaction_handler.show_error = [this](const std::string_view, const std::string_view, const QString& title,
                                                   const QString& message, std::string*) -> bool {
            notifyErrorMessage(this, title, message);
            return true;
        };
        ui_interaction_handler.show_warning = [this](const std::string_view, const std::string_view,
                                                     const QString& title, const QString& message,
                                                     std::string*) -> bool {
            notifyWarnMessage(this, title, message);
            return true;
        };
        ui_interaction_handler.show_info = [this](const std::string_view, const std::string_view, const QString& title,
                                                  const QString& message, std::string*) -> bool {
            notifyInfoMessage(this, title, message);
            return true;
        };
        ui_interaction_handler.confirm = [this](const std::string_view, const std::string_view, const QString& title,
                                                const QString& message, const bool warn,
                                                an::ui::extensions::ExtensionSdkUiConfirmCompletion completion,
                                                std::string* error) -> bool {
            if (!completion)
            {
                if (error)
                {
                    *error = "Extension SDK confirmation completion is null";
                }
                return false;
            }

            if (!m_impl->m_DialogOrchestrationCoordinator)
            {
                if (error)
                {
                    *error = "Extension SDK confirmation dialog coordinator is unavailable";
                }
                return false;
            }

            m_impl->m_DialogOrchestrationCoordinator->openMessageBoxYesNo(
                this, title, message, warn, [completion = std::move(completion)](const int response) mutable {
                    completion(response == QMessageBox::Yes, {});
                });
            return true;
        };
        ui_interaction_handler.open_extension_settings =
            [this](const std::string_view, const std::string_view, const std::string_view extension_id,
                   an::ui::extensions::ExtensionSdkOpenExtensionSettingsCompletion completion,
                   std::string* error) -> bool {
            if (!completion)
            {
                if (error)
                {
                    *error = "Extension SDK settings completion is null";
                }
                return false;
            }

            if (!m_impl->m_DialogOrchestrationCoordinator)
            {
                if (error)
                {
                    *error = "Extension SDK settings dialog coordinator is unavailable";
                }
                return false;
            }

            const std::string extension_id_copy(extension_id);
            QMetaObject::invokeMethod(
                this,
                [this, extension_id_copy, completion = std::move(completion)]() mutable {
                    if (!m_impl->m_DialogOrchestrationCoordinator)
                    {
                        completion("Extension SDK settings dialog coordinator is unavailable");
                        return;
                    }

                    QString diagnostic;
                    if (!m_impl->m_DialogOrchestrationCoordinator->openExtensionSettingsDialog(extension_id_copy,
                                                                                               &diagnostic))
                    {
                        completion(diagnostic.trimmed().isEmpty() ? "Could not open extension settings."
                                                                  : diagnostic.toStdString());
                        return;
                    }

                    completion({});
                },
                Qt::QueuedConnection);
            return true;
        };

        m_impl->m_ExtensionHostLaunchCoordinator->configureSdkService(
            m_impl->m_Core.globalSettingsStore(),
            [this]() -> std::vector<an::ui::extensions::ExtensionViewState> {
                std::vector<an::ui::extensions::ExtensionViewState> view_states;
                if (!m_impl->m_ImageViewControlManager)
                {
                    return view_states;
                }

                const auto controls = m_impl->m_ImageViewControlManager->getListOfImageViewControls();
                view_states.reserve(controls.size());
                const ImageViewControl* active_view = m_impl->m_ImageViewControlManager->getActiveImageViewControl();
                for (ImageViewControl* control : controls)
                {
                    if (!control)
                    {
                        continue;
                    }

                    an::ui::extensions::ExtensionViewState view_state;
                    const std::optional<an::ImageDescriptor> image = control->imageDescriptor();
                    const bool has_sequence = control->document().sequence().has_value();
                    const QPoint viewport_offset = control->getViewportOffset();
                    view_state.view_name = control->getUniqueName();
                    view_state.file_name = control->getFileName();
                    view_state.selector_tooltip = an::ui::buildImageSelectorTooltip(control).toStdString();
                    view_state.view_mode = viewControlModeName(control->getViewControlMode());
                    view_state.image_display_mode = control->getViewWidget()
                                                        ? imageDisplayModeName(control->getViewWidget()->getViewMode())
                                                        : std::string();
                    view_state.channel_view_mode =
                        control->getViewWidget()
                            ? imageChannelViewModeName(control->getViewWidget()->getChannelViewMode())
                            : std::string();
                    view_state.mask_rendering_mode = maskRenderingModeName(control->getMaskRenderingMode());
                    view_state.width = image.has_value() ? static_cast<int>(image->extent().width()) : 0;
                    view_state.height = image.has_value() ? static_cast<int>(image->extent().height()) : 0;
                    view_state.channels = image.has_value() ? static_cast<int>(image->extent().channelCount()) : 0;
                    view_state.zoom_percent = control->getCurrentZoom();
                    view_state.viewport_offset_x = viewport_offset.x();
                    view_state.viewport_offset_y = viewport_offset.y();
                    view_state.is_active = control == active_view;
                    view_state.has_image = image.has_value();
                    view_state.has_sequence = has_sequence;
                    view_state.has_mask = control->isMasked();
                    view_state.mask_enabled = control->isMaskEnabled();
                    view_state.show_mask = control->isShowMaskEnabled();
                    view_state.mask_inverted = control->isMaskInverted();
                    view_state.has_alternate_image = control->hasAlternateImage();
                    if (auto* view_widget = control->getViewWidget())
                    {
                        const QRect selection_area = view_widget->getCurrentSelectionArea();
                        view_state.selection_area_enabled = view_widget->isSelectionAreaEnabled();
                        view_state.selection_area_x = selection_area.x();
                        view_state.selection_area_y = selection_area.y();
                        view_state.selection_area_width = selection_area.width();
                        view_state.selection_area_height = selection_area.height();
                    }
                    view_states.push_back(std::move(view_state));
                }

                return view_states;
            },
            [this](const std::string_view session_id, const std::string_view request_id,
                   const std::string_view view_name, an::ui::extensions::ExtensionViewSnapshot* out_snapshot,
                   std::string* error) -> bool {
                if (!out_snapshot)
                {
                    if (error)
                    {
                        *error = "Extension SDK snapshot output is null";
                    }
                    return false;
                }
                if (!m_impl->m_ImageViewControlManager)
                {
                    if (error)
                    {
                        *error = "ImageViewControlManager is unavailable";
                    }
                    return false;
                }

                ImageViewControl* control =
                    m_impl->m_ImageViewControlManager->getImageViewControlByName(std::string(view_name));
                if (!control)
                {
                    if (error)
                    {
                        *error = "Requested view no longer exists";
                    }
                    return false;
                }

                if (!control->hasImage())
                {
                    if (error)
                    {
                        *error = "Requested view has no exportable image";
                    }
                    return false;
                }

                *out_snapshot = an::ui::extensions::ExtensionViewSnapshot{};

                an::ui::extensions::ImageExportRequest request;
                request.sessionId = std::string(session_id);
                request.role = an::ui::extensions::SharedBufferRole::Preview;
                request.accessMode = an::ui::extensions::SharedBufferAccessMode::ReadOnly;
                request.bufferId = std::string(request_id) + ".view_snapshot";
                request.symbolicName = std::string(view_name) + ".snapshot";
                request.fileName = control->getFileName();
                std::optional<an::ui::extensions::SharedBufferDescriptor> source =
                    exportDocumentSnapshot(control->document(), request, error);
                if (!source.has_value())
                {
                    return false;
                }
                out_snapshot->source_buffer = std::move(*source);

                if (ImageViewControl* const current_mask = control->getCurrentMask())
                {
                    if (current_mask->hasImage())
                    {
                        an::ui::extensions::ImageExportRequest mask_request;
                        mask_request.sessionId = std::string(session_id);
                        mask_request.role = an::ui::extensions::SharedBufferRole::Mask;
                        mask_request.accessMode = an::ui::extensions::SharedBufferAccessMode::ReadOnly;
                        mask_request.bufferId = std::string(request_id) + ".view_snapshot.selection";
                        mask_request.symbolicName = "selection";
                        std::optional<an::ui::extensions::SharedBufferDescriptor> mask =
                            exportDocumentSnapshot(current_mask->document(), mask_request, error);
                        if (!mask.has_value())
                        {
                            return false;
                        }

                        out_snapshot->auxiliary_buffers.push_back(std::move(*mask));
                    }
                }

                return true;
            },
            []() -> an::ui::extensions::ExtensionThemeSnapshot { return currentExtensionThemeSnapshot(); },
            std::move(ui_interaction_handler),
            [this](const std::string_view session_id, const std::string_view view_name, std::string* error) -> bool {
                if (!m_impl->m_ExtensionHostLaunchCoordinator)
                {
                    if (error)
                    {
                        *error = "Extension host launch coordinator is unavailable";
                    }
                    return false;
                }

                return m_impl->m_ExtensionHostLaunchCoordinator->bindSessionTargetView(session_id, view_name, error);
            },
            [this](const std::string_view, const std::string_view,
                   const an::ui::extensions::SharedBufferDescriptor& buffer, const QString& title,
                   std::string* error) -> bool {
                an::ui::IEngineSessionProvider* const provider = engineSessionProvider();
                if (provider == nullptr)
                {
                    if (error)
                    {
                        *error = "ui.open_image found no engine session to open the image in";
                    }
                    return false;
                }

                const an::ui::extensions::SharedBufferManager shared_buffer_manager(
                    an::ui::extensions::extensionHostManagedRootPath());
                an::Result<an::ui::extensions::ImportedImage> imported =
                    shared_buffer_manager.importImage(provider->engineSession(), buffer);
                if (!imported.hasValue())
                {
                    if (error)
                    {
                        *error =
                            std::string("ui.open_image did not materialize an image: ") + imported.status().message();
                    }
                    return false;
                }

                std::optional<an::ui::ImageDocument> document = documentOfAsset(
                    std::move(imported->asset), title.isEmpty() ? std::string("Extension Image") : title.toStdString());
                if (!document.has_value())
                {
                    if (error)
                    {
                        *error = "ui.open_image did not materialize an image";
                    }
                    return false;
                }

                ImageViewControl* const created_control = createNewImageViewControlFromImage(
                    nullptr, std::move(*document), this, "Opened extension-generated image", ViewControlMode::standard,
                    nullptr, false, true);
                if (!created_control)
                {
                    if (error)
                    {
                        *error = "Failed to create an image view for the generated extension image";
                    }
                    return false;
                }

                return true;
            },
            // A sequence a workflow wrote is opened on a worker and shown once it is read; a sequence that cannot be
            // read or shown is said then, under the workflow's title, and logged.
            [this](const std::string_view, const std::vector<an::ui::extensions::ExtensionWorkflowOutput>& outputs,
                   std::string* error) -> bool {
                for (const auto& output : outputs)
                {
                    if (output.kind != "sequence")
                    {
                        continue;
                    }

                    const QString output_path = QString::fromStdWString(output.path.wstring());
                    const std::string output_path_text = output_path.toStdString();
                    const std::optional<an::SequenceFormat> format = an::sequenceFormatForPath(output_path_text);
                    if (!an::ui::dialogs::detail::isSupportedSequenceSuffix(QFileInfo(output_path).suffix()) ||
                        !format.has_value())
                    {
                        LOG_INFO("Workflow output '{}' is not a supported sequence file; leaving it on disk.",
                                 output_path_text);
                        continue;
                    }

                    an::ui::IEngineSessionProvider* const provider = engineSessionProvider();
                    if (provider == nullptr)
                    {
                        if (error)
                        {
                            *error = "Failed to open workflow sequence output: no engine session is open";
                        }
                        return false;
                    }
                    openWorkflowSequenceOutput(this, provider->engineSession(), output_path, *format);
                }

                return true;
            });
        // The settings store is open: the rate a native extension window previews at is read now, off the GUI thread,
        // so the first launch passes the stored one.
        m_impl->m_ExtensionHostLaunchCoordinator->refreshPreviewFrameRate();
    }

    refreshStoredProcessMenus();
}

void AfterNightApp::setActiveImageViewControl(ImageViewControl* view)
{
    m_impl->m_Core.viewService().setActiveImageViewControl(view);
}

void AfterNightApp::setActiveImageViewControlAsync(ImageViewControl* view, std::atomic_bool* finished)
{
    m_impl->m_Core.setActiveImageViewControlAsync(view, finished);
}

void AfterNightApp::setActiveImageViewControlByName(const std::string& name)
{
    m_impl->m_Core.viewService().setActiveViewByName(name);
}

void AfterNightApp::setActiveImageViewControlByNameAsync(const std::string& name, std::atomic_bool* finished)
{
    if (ImageViewControl* const view = m_impl->m_ImageViewControlManager->getImageViewControlByName(name))
    {
        setActiveImageViewControlAsync(view, finished);
    }
}

void AfterNightApp::addImageViewControl(ImageViewControl* view)
{
    m_impl->m_Core.viewService().addViewControl(view);
}

void AfterNightApp::addImageViewControlAsync(ImageViewControl* view)
{
    m_impl->m_Core.addImageViewControlAsync(view);
}

void AfterNightApp::removeImageViewControl(ImageViewControl* view, bool deleteObject)
{
    m_impl->m_Core.viewService().removeViewControl(view, deleteObject);
}

void AfterNightApp::removeImageViewControlAsync(ImageViewControl* view, bool deleteObject)
{
    m_impl->m_Core.removeImageViewControlAsync(view, deleteObject);
}

void AfterNightApp::renameImageViewControl(ImageViewControl* view)
{
    m_impl->m_Core.viewService().renameViewControl(view);
}

void AfterNightApp::updateImageViewControlList()
{
    if (m_impl->m_ViewControlCoordinator)
    {
        m_impl->m_ViewControlCoordinator->updateImageViewControlList();
    }
}

void AfterNightApp::updateImageViewControlListAsync(std::atomic_bool* finished)
{
    m_impl->m_Core.updateImageViewControlListAsync(finished);
}

ImageViewControlManager* AfterNightApp::getImageViewControlManager()
{
    return m_impl->m_ImageViewControlManager;
}

an::ui::ProcessLauncher* AfterNightApp::processLauncher() const
{
    return m_impl->m_ProcessLauncher.get();
}

an::ui::ChannelExtractor* AfterNightApp::channelExtractor() const
{
    return m_impl->m_ChannelExtractor.get();
}

an::ui::FileManager* AfterNightApp::fileManager() const
{
    return m_impl->m_FileManager.get();
}

an::ui::ImageOperationsCoordinator* AfterNightApp::imageOperations() const
{
    return m_impl->m_ImageOperationsCoordinator.get();
}

an::ui::MaskOverlayCoordinator* AfterNightApp::maskOverlay() const
{
    return m_impl->m_MaskOverlayCoordinator.get();
}

an::ui::WindowLayoutCoordinator* AfterNightApp::windowLayout() const
{
    return m_impl->m_WindowLayoutCoordinator.get();
}

an::ui::DialogOrchestrationCoordinator* AfterNightApp::dialogOrchestration() const
{
    return m_impl->m_DialogOrchestrationCoordinator.get();
}

an::ui::ActionStateCoordinator* AfterNightApp::actionState() const
{
    return m_impl->m_ActionStateCoordinator.get();
}

void AfterNightApp::createContextMenuForViewControl(ImageViewControl* view, QContextMenuEvent* event)
{
    if (m_impl->m_WindowLayoutCoordinator)
    {
        m_impl->m_WindowLayoutCoordinator->createContextMenuForViewControl(view, event);
    }
}

ImageViewControl*
AfterNightApp::createNewImageViewControlFromImage(ImageViewControl* customViewControl, an::ui::ImageDocument&& document,
                                                  QWidget* parent, const std::string& historyDesc,
                                                  const ViewControlMode mode, ImageViewControl* neighbour,
                                                  bool keepFocusedNeighbour, const bool pendingChanges)
{
    return m_impl->m_Core.viewService().createNewImageViewControlFromImage(customViewControl, std::move(document),
                                                                           parent, historyDesc, mode, neighbour,
                                                                           keepFocusedNeighbour, pendingChanges);
}

ImageViewControl* AfterNightApp::createNewImageViewControlFromSequence(
    ImageViewControl* customViewControl, an::ui::ImageDocument&& document, QWidget* parent, const bool autoDebayer,
    const std::string& historyDesc, const ViewControlMode mode, ImageViewControl* neighbour, bool keepFocusedNeighbour,
    const bool pendingChanges)
{
    return m_impl->m_Core.viewService().createNewImageViewControlFromSequence(
        customViewControl, std::move(document), parent, autoDebayer, historyDesc, mode, neighbour, keepFocusedNeighbour,
        pendingChanges);
}

void AfterNightApp::createNewImageViewControlFromImageAsync(ImageViewControl* customViewControl,
                                                            an::ui::ImageDocument&& document, QWidget* parent,
                                                            const std::string& historyDesc, const ViewControlMode mode,
                                                            ImageViewControl* neighbour, bool keepFocusedNeighbour,
                                                            const bool pendingChanges, std::atomic_bool* finished)
{
    m_impl->m_Core.createNewImageViewControlFromImageAsync(customViewControl, std::move(document), parent, historyDesc,
                                                           mode, neighbour, keepFocusedNeighbour, pendingChanges,
                                                           finished);
}

void AfterNightApp::createNewImageViewControlFromSequenceAsync(ImageViewControl* customViewControl,
                                                               an::ui::ImageDocument&& document, QWidget* parent,
                                                               const bool autoDebayer, const std::string& historyDesc,
                                                               const ViewControlMode mode, ImageViewControl* neighbour,
                                                               bool keepFocusedNeighbour, const bool pendingChanges,
                                                               std::atomic_bool* finished)
{
    m_impl->m_Core.createNewImageViewControlFromSequenceAsync(customViewControl, std::move(document), parent,
                                                              autoDebayer, historyDesc, mode, neighbour,
                                                              keepFocusedNeighbour, pendingChanges, finished);
}

void AfterNightApp::registerProcessInstance(TopLevelProcessBaseControl* process)
{
    m_impl->m_Core.openProcesses().add(process);
}

void AfterNightApp::unregisterProcessInstance(TopLevelProcessBaseControl* process)
{
    m_impl->m_Core.openProcesses().remove(process);
}

size_t AfterNightApp::getNumOfOpenProcesses()
{
    const size_t extensionProcessCount =
        m_impl->m_ExtensionHostLaunchCoordinator ? m_impl->m_ExtensionHostLaunchCoordinator->activeSessionCount() : 0;

    return m_impl->m_Core.openProcesses().size() + extensionProcessCount;
}

void AfterNightApp::stopExtensionHostSessions()
{
    if (m_impl->m_ExtensionHostPumpTimer)
    {
        m_impl->m_ExtensionHostPumpTimer->stop();
    }
    if (m_impl->m_ExtensionHostLaunchCoordinator)
    {
        m_impl->m_ExtensionHostLaunchCoordinator->stopAllSessions();
    }
}

KDDockWidgets::QtWidgets::MainWindow* AfterNightApp::getDockMainWindow()
{
    return m_impl->m_DockMainWindow;
}

ThumbnailPersistence* AfterNightApp::getThumbnailPersistence()
{
    return m_impl->m_Core.thumbnailStore();
}

FolderTrackerPersistence* AfterNightApp::getFolderTrackerPersistence()
{
    return m_impl->m_Core.folderStore();
}

ProcessSettingsPersistence* AfterNightApp::getProcessSettingsPersistence()
{
    return m_impl->m_Core.processSettingsStore();
}

GlobalSettingsPersistence* AfterNightApp::getGlobalSettingsPersistence()
{
    return m_impl->m_Core.globalSettingsStore();
}

bool AfterNightApp::isMaximizedViewActive() const
{
    return m_impl->ui.centralWidget->currentWidget() != m_impl->m_MainDashboardScrollArea &&
           m_impl->ui.centralWidget->currentWidget() != m_impl->m_DockMainWindow;
}

AfterNightApp::~AfterNightApp()
{
    // A Session Console still open is a child the base class deletes once this window's state is gone, so its
    // destroyed handler, which reads that state, must not run then.
    disconnect(m_impl->m_LogViewerClosedConnection);
    m_impl->m_ExtensionBackgroundWorkCancellationToken.requestCancellation();
    m_impl->m_Core.processLaunch().cancelPreparations();
    stopExtensionHostSessions();

    // The process windows go before the stores they read. A run's work calls its window's process body on a worker, so
    // no window is deleted under its work: each stops what it still runs, the thread pool is drained while every window
    // is still there, only then are they deleted, and the work their going leaves on the pool - a window's read of its
    // settings among it - is drained in turn, while the settings stores are still there.
    const std::vector<TopLevelProcessBaseControl*> open = m_impl->m_Core.openProcesses().snapshot();
    std::vector<QPointer<TopLevelProcessBaseControl>> openProcesses(open.begin(), open.end());
    an::ui::destroyProcessWindowsAtExit(openProcesses);

    // The stores go after the windows that read them, in their order.
    m_impl->m_Core.closeStores();

    // The work handed to this window and not run yet goes with it, and nothing handed over later asks anything of it.
    m_impl->m_Core.discardPendingEvents();
    m_impl->m_Core.setEventTarget(nullptr);

    // The context goes with this window, and the windows destroyed after it - its children - must find none rather
    // than one on its way out.
    if (an::ui::appContext() == m_impl->m_AppContext.get())
    {
        an::ui::clearAppContext();
    }

    // The services answer through this window no more: they are given no hooks into it and no shell, and the views
    // still listed go with it - its children, deleted after this, find no context to leave the list through.
    an::ui::shell::ViewService& views = m_impl->m_Core.viewService();
    views.setHooks({});
    views.clearTaskSubject();
    views.manager().clear();
    m_impl->m_Core.dialogService().setHooks({});
    m_impl->m_Core.processLaunch().setHooks({});
    m_impl->m_Core.setShell(nullptr);
    m_impl->m_Core.setEngineSessionProvider(nullptr);
    m_impl->m_Session = nullptr;
}

an::ui::IEngineSessionProvider* AfterNightApp::engineSessionProvider() const noexcept
{
    return m_impl->m_Session != nullptr && m_impl->m_Session->isOpen() ? m_impl->m_Session : nullptr;
}

void AfterNightApp::on_imageViewControlListComboBox_currentTextChanged(const QString& text)
{
    auto control = m_impl->m_ImageViewControlManager->getImageViewControlByName(text.toUtf8().toStdString());

    if (control)
    {
        if (viewControlInAMaximizedContainer(control) && !control->isMaximized() && !control->isInFloatingWindow())
        {
            control->switchMaximized();
        }

        setActiveImageViewControl(control);
    }
}

void AfterNightApp::closeEvent(QCloseEvent* closeEvent)
{
    if (m_impl->m_AppLifecycleCoordinator && !m_impl->m_AppLifecycleCoordinator->prepareForClose())
    {
        closeEvent->ignore();
        return;
    }

    QMainWindow::closeEvent(closeEvent);

    // Some child dialogs (Histogram, Statistics, FITS viewer, etc.) are shown
    // as independent top-level windows and would keep the process alive if they
    // outlive the main window.  Quitting the application here ensures the
    // event loop always exits once the main window has accepted its close event.
    QApplication::quit();
}

void AfterNightApp::contextMenuEvent(QContextMenuEvent* contextMenuEvent)
{
    if (m_impl->m_ImageViewControlManager->getNumOfImageViewControls() == 0)
        return;

    QMenu menu(this);
    menu.addAction(m_impl->ui.actionLock_Layout);
    menu.addAction(m_impl->ui.actionUnlock_Layout);
    menu.exec(contextMenuEvent->globalPos());
}

void AfterNightApp::dragEnterEvent(QDragEnterEvent* evt)
{
    auto urls = evt->mimeData()->urls();

    bool atLeastOneSupported = false;
    for (auto& url : urls)
    {
        auto path = url.toLocalFile();
        if (an::ui::isLikelySupportedImagePath(path))
        {
            atLeastOneSupported = true;
            break;
        }
    }

    if (atLeastOneSupported)
        evt->acceptProposedAction();
}

void AfterNightApp::dropEvent(QDropEvent* evt)
{
    if (!evt || !evt->mimeData())
    {
        return;
    }

    auto urls = evt->mimeData()->urls();
    QStringList files;

    for (auto& url : urls)
    {
        auto path = url.toLocalFile();
        if (!path.isEmpty())
        {
            files.append(path);
        }
    }

    if (files.isEmpty())
    {
        return;
    }

    openDroppedFiles(files);
    evt->acceptProposedAction();
}

void AfterNightApp::openDroppedFiles(const QStringList& files)
{
    if (m_impl->m_FileManager)
    {
        m_impl->m_FileManager->openDroppedFiles(files);
    }
}

bool AfterNightApp::tryLaunchRegistryProcessById(const std::string& processId)
{
    return m_impl->m_Core.processLaunch().launch(processId);
}

void AfterNightApp::connectActions()
{
    if (m_impl->m_MenuActionManager)
    {
        m_impl->m_MenuActionManager->connectProcessActions();
        m_impl->m_MenuActionManager->connectActions();
    }
}

void AfterNightApp::setActionIcons()
{
    if (m_impl->m_MenuActionManager)
    {
        m_impl->m_MenuActionManager->setActionIcons();
    }
}

void AfterNightApp::initializePersistence()
{
    m_impl->m_Core.openStores();
    if (m_impl->m_FileManager)
    {
        m_impl->m_FileManager->setPersistenceDependencies(m_impl->m_Core.thumbnailStore(), m_impl->m_Core.folderStore(),
                                                          m_impl->m_Core.globalSettingsStore());
    }
    // A settings database of another format was set aside: the person is told where, once the window is up.
    if (const QString notice = m_impl->m_Core.settingsStoreNotice(); !notice.isEmpty())
    {
        notifyInfoMessageAsync(this, tr("Settings"), notice);
    }

    if (m_impl->m_Session != nullptr && !m_impl->m_Session->isOpen())
    {
        GlobalSettingsPersistence* const settings = m_impl->m_Core.globalSettingsStore();
        const an::ui::ProcessingLimits limits =
            settings != nullptr ? an::ui::readProcessingLimits([settings](const QString& key, const QString& fallback) {
                return settings->select(key, fallback);
            })
                                : an::ui::ProcessingLimits{};
        const an::Status opened = m_impl->m_Session->open(limits);
        if (opened.failed())
        {
            const std::string message = fmt::format("The engine session cannot be opened: {}", opened.message());
            LOG_ERROR(message);
            throw std::runtime_error(message);
        }
    }

    // The core lends the session the context hands out once it is open.
    m_impl->m_Core.setEngineSessionProvider(engineSessionProvider());
}

void AfterNightApp::persistLastOpenDir()
{
    m_impl->m_Core.persistLastOpenDirectory();
}

void AfterNightApp::persistRecentFiles()
{
    m_impl->m_Core.recentFiles().save();
}

std::shared_ptr<const an::ui::RawOpenPolicy> AfterNightApp::rawOpenPolicy() const
{
    return m_impl->m_Core.rawOpenPolicy();
}

an::Status AfterNightApp::setRawOpenSettings(const an::ui::RawOpenSettings& settings)
{
    return m_impl->m_Core.setRawOpenSettings(settings);
}

an::Result<an::OutputIntent> AfterNightApp::buildOutputIntent(const an::ImageDescriptor& source,
                                                              const an::CodecFormat container) const
{
    return m_impl->m_Core.buildOutputIntent(source, container);
}

void AfterNightApp::addRecentFile(const std::string& fileName,
                                  const std::shared_ptr<const an::consumer::ImageAsset>& image)
{
    if (m_impl->m_FileManager)
    {
        m_impl->m_FileManager->addRecentFile(fileName, image);
    }
}

void AfterNightApp::addRecentFileAsync(const std::string& fileName,
                                       std::shared_ptr<const an::consumer::ImageAsset> image,
                                       std::atomic_bool* finished)
{
    m_impl->m_Core.addRecentFileAsync(fileName, std::move(image), finished);
}

void AfterNightApp::removeRecentFile(const std::string& fileName)
{
    if (m_impl->m_FileManager)
    {
        m_impl->m_FileManager->removeRecentFile(fileName);
    }
}

void AfterNightApp::removeRecentFileAsync(const std::string& fileName, std::atomic_bool* finished)
{
    m_impl->m_Core.removeRecentFileAsync(fileName, finished);
}

void AfterNightApp::setLogViewerChecked(bool checked)
{
    QSignalBlocker blocker(m_impl->ui.actionLog_Viewer);
    m_impl->ui.actionLog_Viewer->setChecked(checked);
}

void AfterNightApp::onLogViewerClosed()
{
    m_impl->m_LogViewerControl = nullptr;

    // If no image is active, switch to main dashboard
    if (m_impl->m_ImageViewControlManager->getNumOfImageViewControls() == 0)
    {
        switchToMainDashboard();
    }
}

std::thread::id AfterNightApp::getMainThreadId() const
{
    return m_impl->m_Core.guiThreadId();
}

bool AfterNightApp::isMainThread() const
{
    return m_impl->m_Core.isGuiThread();
}

bool AfterNightApp::isTempFile(const std::string& fileName)
{
    return m_impl->m_Core.isTempFile(fileName);
}

void AfterNightApp::loadSettings()
{
    if (m_impl->m_AppLifecycleCoordinator)
    {
        m_impl->m_AppLifecycleCoordinator->loadSettings();
    }
}

void AfterNightApp::updateActiveImageActions()
{
    if (m_impl->m_ActionStateCoordinator)
    {
        m_impl->m_ActionStateCoordinator->updateActiveImageActions();
    }
}

void AfterNightApp::updateActiveImageActionsAsync()
{
    m_impl->m_Core.updateActiveImageActionsAsync();
}

void AfterNightApp::updateActiveImageHistory()
{
    if (m_impl->m_ActionStateCoordinator)
    {
        m_impl->m_ActionStateCoordinator->updateActiveImageHistory();
    }
}

void AfterNightApp::setCustomImageActionEnabled(ImageViewControl* view, const GlobalViewControlActions action,
                                                bool enable)
{
    auto control = m_impl->m_ImageViewControlManager->getActiveImageViewControl();
    if (!control)
    {
        return;
    }

    if (control != view)
    {
        return;
    }

    if (action == GlobalViewControlActions::STATISTICS_ACTION)
    {
        m_impl->ui.actionStatistics->setEnabled(enable);
    }
    else if (action == GlobalViewControlActions::HISTOGRAM_ACTION)
    {
        m_impl->ui.actionHistogram->setEnabled(enable);
    }
    else if (action == GlobalViewControlActions::FITS_HEADER_ACTION)
    {
        m_impl->ui.actionFITSHeader->setEnabled(enable);
    }
    else if (action == GlobalViewControlActions::EXIF_ACTION)
    {
        m_impl->ui.actionImage_EXIFViewer->setEnabled(enable);
    }
    else if (action == GlobalViewControlActions::SUMMARY_ACTION)
    {
        m_impl->ui.actionSummary->setEnabled(enable);
    }
}

void AfterNightApp::updateImageInfoInStatusBar()
{
    showImageInfo(m_impl->m_ImageViewControlManager->getActiveImageViewControl());
}

void AfterNightApp::updateImageZoomInfoInStatusBar()
{
    showZoom(m_impl->m_ImageViewControlManager->getActiveImageViewControl());
}

void AfterNightApp::updateImageCelestialCoordsInStatusBar(const double ra_deg, const double dec_deg)
{
    showCelestialCoordinates(ra_deg, dec_deg);
}

void AfterNightApp::clearImageCelestialCoordsInStatusBar()
{
    clearCelestialCoordinates();
}

void AfterNightApp::subscribeListener(AfterNightAppListener* listener)
{
    m_impl->m_Core.eventBus().subscribe(listener);
}

void AfterNightApp::unsubscribeListener(AfterNightAppListener* listener)
{
    m_impl->m_Core.eventBus().unsubscribe(listener);
}

void AfterNightApp::notifyViewControlUpdated(const ImageViewControl* ctrl)
{
    m_impl->m_Core.viewService().notifyViewControlUpdated(ctrl);
}

void AfterNightApp::notifyProcessOpened(const std::string& processName, const ImageViewControl* targetCtrl)
{
    m_impl->m_Core.eventBus().notifyProcessOpened(processName, targetCtrl);
}

void AfterNightApp::notifyProcessExecuted(const std::string& processName, const ImageViewControl* targetCtrl)
{
    m_impl->m_Core.eventBus().notifyProcessExecuted(processName, targetCtrl);
}

void AfterNightApp::notifyApplicationClose()
{
    m_impl->m_Core.eventBus().notifyApplicationClose();
}

void AfterNightApp::setLastOpenDirectoryAndPersist(const QString& path)
{
    m_impl->m_Core.setLastOpenDirectory(path);
}

QStringList AfterNightApp::selectImageFilesDlg(QWidget* parent, const QString& title, const QString& folderContext,
                                               bool supportMultipleFiles)
{
    return m_impl->m_Core.fileDialogService().selectImageFiles(parent, title, folderContext, supportMultipleFiles);
}

QStringList AfterNightApp::selectFilesDlg(QWidget* parent, const QString& title, const QString& folderContext,
                                          const QStringList& fileExtensions)
{
    return m_impl->m_Core.fileDialogService().selectFiles(parent, title, folderContext, fileExtensions);
}

QStringList AfterNightApp::saveFileDlg(QWidget* parent, const QString& title, const QString& defaultDir,
                                       const QString& folderContext, const QStringList& fileExtensions)
{
    return m_impl->m_Core.fileDialogService().saveFiles(parent, title, defaultDir, folderContext, fileExtensions);
}

void AfterNightApp::newSequence()
{
    NewSequenceControl* newSequenceCtrl = new NewSequenceControl(this);

    newSequenceCtrl->show();
}

void AfterNightApp::on_LogViewerClicked(bool checked)
{
    if (checked)
    {
        // If we're on the main dashboard, we need to switch to dock manager first
        // since the session console is a dock widget that belongs to the dock manager
        if (m_impl->ui.centralWidget->currentWidget() == m_impl->m_MainDashboardScrollArea)
        {
            // We need to create a dummy view control to switch to dock manager
            // This will be closed when the user returns to main dashboard
            // For now, just switch to dock manager which will enable proper docking
            switchToDockManager();
        }

        if (m_impl->m_LogViewerControl)
        {
            m_impl->m_LogViewerControl->show();
            m_impl->m_LogViewerControl->getDockWidget()->show();
            return;
        }

        m_impl->m_LogViewerControl = new LogViewerControl(this);

        m_impl->m_LogViewerClosedConnection = connect(m_impl->m_LogViewerControl, &QObject::destroyed, this, [this]() {
            setLogViewerChecked(false);
            onLogViewerClosed();
        });

        m_impl->m_LogViewerControl->initialize();

        auto dockWidget = m_impl->m_LogViewerControl->getDockWidget();

        // New dock area
        m_impl->m_DockMainWindow->addDockWidget(dockWidget, KDDockWidgets::Location_OnBottom);
    }
    else
    {
        if (!m_impl->m_LogViewerControl)
        {
            return;
        }

        m_impl->m_LogViewerControl->getDockWidget()->forceClose();
        m_impl->m_LogViewerControl->close();
        // Note: m_impl->m_LogViewerControl is cleared and dashboard switching is handled
        // by the destroyed callback above.
    }
}

void AfterNightApp::lockLayout()
{
    m_impl->ui.actionUnlock_Layout->setEnabled(true);
    m_impl->ui.actionLock_Layout->setEnabled(false);
    m_impl->m_ToolbarLockLayout->setIcon(m_impl->m_IconManager->getIcon("unlock"));
    m_impl->m_ToolbarLockLayout->setToolTip("Unlock layout");

    KDDockWidgets::Config::self().setDropIndicatorsInhibited(true);

    KDDockWidgets::Config::self().setDragAboutToStartFunc(
        [](KDDockWidgets::Core::Draggable* draggable) -> bool { return false; });

    m_impl->m_IsLayoutLocked = true;
}

void AfterNightApp::unlockLayout()
{
    m_impl->ui.actionLock_Layout->setEnabled(true);
    m_impl->ui.actionUnlock_Layout->setEnabled(false);
    m_impl->m_ToolbarLockLayout->setIcon(m_impl->m_IconManager->getIcon("lock"));
    m_impl->m_ToolbarLockLayout->setToolTip("Lock layout");

    KDDockWidgets::Config::self().setDragAboutToStartFunc(nullptr);
    KDDockWidgets::Config::self().setDropIndicatorsInhibited(false);

    m_impl->m_IsLayoutLocked = false;
}

void AfterNightApp::toogleLayout()
{
    if (m_impl->ui.actionUnlock_Layout->isEnabled())
    {
        unlockLayout();
    }
    else
    {
        lockLayout();
    }
}

bool AfterNightApp::layoutIsLocked() const
{
    return m_impl->m_IsLayoutLocked;
}

void AfterNightApp::about()
{
    if (m_impl->m_DialogOrchestrationCoordinator)
    {
        m_impl->m_DialogOrchestrationCoordinator->about();
    }
}

void AfterNightApp::openHelp()
{
    if (m_impl->m_DialogOrchestrationCoordinator)
    {
        m_impl->m_DialogOrchestrationCoordinator->openHelp();
    }
}

void AfterNightApp::openSettings()
{
    if (m_impl->m_DialogOrchestrationCoordinator)
    {
        m_impl->m_DialogOrchestrationCoordinator->openSettings();
        loadRecentProcessesFromSettings();
        enforceRecentProcessesLimit();
        persistRecentProcessesToSettings();
        updateRecentProcessesMenuEntries();
        updateFavoriteProcessesMenuEntries();
    }
}

void AfterNightApp::openSettingsAt(const QString& pageName)
{
    if (m_impl->m_DialogOrchestrationCoordinator)
    {
        if (isExtensionDialogPageTitle(pageName))
        {
            m_impl->m_DialogOrchestrationCoordinator->openExtensionsAt(pageName);
            if (m_impl->m_ExtensionInstallRefreshTimer)
            {
                m_impl->m_ExtensionInstallRefreshTimer->start();
            }
            return;
        }

        m_impl->m_DialogOrchestrationCoordinator->openSettingsAt(pageName);
        loadRecentProcessesFromSettings();
        enforceRecentProcessesLimit();
        persistRecentProcessesToSettings();
        updateRecentProcessesMenuEntries();
        updateFavoriteProcessesMenuEntries();
    }
}

void AfterNightApp::systemInformation()
{
    if (m_impl->m_DialogOrchestrationCoordinator)
    {
        m_impl->m_DialogOrchestrationCoordinator->systemInformation();
    }
}

void AfterNightApp::postAfterNightEvent(AfterNightEvent* evt, int priority)
{
    m_impl->m_Core.postEvent(evt, priority);
}

void AfterNightApp::setEnableImageActions(bool enable)
{
    if (m_impl->m_ActionStateCoordinator)
    {
        m_impl->m_ActionStateCoordinator->setEnableImageActions(enable);
    }
}

void AfterNightApp::loadIcons()
{
    if (m_impl->m_ThemeManager)
    {
        m_impl->m_ThemeManager->loadIcons();
    }
}

void AfterNightApp::initializeMainDashboard()
{
    if (m_impl->m_UiBootstrapCoordinator)
    {
        m_impl->m_UiBootstrapCoordinator->initializeMainDashboard();
    }
}

void AfterNightApp::initializeDockManager()
{
    if (m_impl->m_UiBootstrapCoordinator)
    {
        m_impl->m_UiBootstrapCoordinator->initializeDockManager();
    }
}

void AfterNightApp::initializeMaximizedView()
{
    if (m_impl->m_UiBootstrapCoordinator)
    {
        m_impl->m_UiBootstrapCoordinator->initializeMaximizedView();
    }
}

void AfterNightApp::switchToMainDashboard()
{
    if (m_impl->m_WindowLayoutCoordinator)
    {
        m_impl->m_WindowLayoutCoordinator->switchToMainDashboard();
    }
}

void AfterNightApp::switchToDockManager()
{
    if (m_impl->m_WindowLayoutCoordinator)
    {
        m_impl->m_WindowLayoutCoordinator->switchToDockManager();
    }
}

std::deque<std::string> AfterNightApp::getRecentFilesList() const
{
    return m_impl->m_Core.recentFiles().entries();
}

void AfterNightApp::refreshStoredProcessMenus()
{
    const bool cleanupMissingProcessIds = cleanupMissingStoredProcessIdsOnNextRefresh;
    cleanupMissingStoredProcessIdsOnNextRefresh = false;

    loadRecentProcessesFromSettings();
    if (cleanupMissingProcessIds)
    {
        (void)m_impl->m_Core.recentProcesses().pruneMissing();
    }
    enforceRecentProcessesLimit();
    persistRecentProcessesToSettings();
    updateRecentProcessesMenuEntries();

    loadFavoriteProcessesFromSettings();
    if (cleanupMissingProcessIds)
    {
        (void)m_impl->m_Core.favoriteProcesses().pruneMissing();
    }
    updateFavoriteProcessesMenuEntries();
}

size_t AfterNightApp::getRecentFilesLimit() const
{
    return m_impl->m_Core.recentFiles().limit();
}

size_t AfterNightApp::getRecentProcessesLimit() const
{
    return m_impl->m_Core.recentProcesses().limit();
}

void AfterNightApp::loadRecentProcessesFromSettings()
{
    m_impl->m_Core.recentProcesses().load();
}

void AfterNightApp::persistRecentProcessesToSettings()
{
    m_impl->m_Core.recentProcesses().save();
}

void AfterNightApp::enforceRecentProcessesLimit()
{
    m_impl->m_Core.recentProcesses().enforceLimit();
}

void AfterNightApp::updateRecentProcessesMenuEntries()
{
    if (!m_impl->ui.menuRecent_Processes)
    {
        return;
    }

    enforceRecentProcessesLimit();
    m_impl->ui.menuRecent_Processes->clear();

    if (m_impl->m_Core.recentProcesses().entries().empty())
    {
        m_impl->ui.menuRecent_Processes->setEnabled(false);
        return;
    }

    m_impl->ui.menuRecent_Processes->setEnabled(true);

    for (const auto& recentProcess : m_impl->m_Core.recentProcesses().entries())
    {
        const std::string processId = recentProcess.id;
        QString actionText = QString::fromUtf8(recentProcess.displayName);
        if (actionText.isEmpty())
        {
            actionText = QString::fromUtf8(processId);
        }

        auto* action = m_impl->ui.menuRecent_Processes->addAction(actionText);
        action->setToolTip(QString::fromUtf8(processId));
        const auto* descriptor = an::ui::process_registry::ProcessRegistry::instance().find(processId);
        if (!descriptor)
        {
            const QString pending_reason =
                QStringLiteral("This recent extension process is still loading and cannot be launched yet.");
            action->setEnabled(false);
            action->setToolTip(pending_reason);
            action->setStatusTip(pending_reason);
            action->setWhatsThis(pending_reason);
        }
        QObject::connect(action, &QAction::triggered, this, [this, processId]() {
            if (!tryLaunchRegistryProcessById(processId))
            {
                LOG_WARNING("Recent process launch failed for descriptor id={}", processId);
            }
        });
    }

    m_impl->ui.menuRecent_Processes->addSeparator();
    auto* clearAction = m_impl->ui.menuRecent_Processes->addAction(tr("Clear recent processes list"));
    QObject::connect(clearAction, &QAction::triggered, this, [this]() { clearRecentProcessesList(); });
}

void AfterNightApp::clearRecentProcessesList()
{
    loadRecentProcessesFromSettings();
    if (m_impl->m_Core.recentProcesses().entries().empty())
    {
        persistRecentProcessesToSettings();
        updateRecentProcessesMenuEntries();
        return;
    }

    ScopedTopLevelFlagRestorer _;

    QMessageBox msgBox;
    msgBox.setText(tr("Are you sure you want to delete the recent used processes?"));
    msgBox.setInformativeText(tr("The history of used processes will be lost"));
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::Yes);

    if (msgBox.exec() != QMessageBox::Yes)
    {
        return;
    }

    m_impl->m_Core.recentProcesses().clear();
    updateRecentProcessesMenuEntries();
}

void AfterNightApp::loadFavoriteProcessesFromSettings()
{
    m_impl->m_Core.favoriteProcesses().load();
}

void AfterNightApp::persistFavoriteProcessesToSettings()
{
    m_impl->m_Core.favoriteProcesses().save();
}

void AfterNightApp::updateFavoriteProcessesMenuEntries()
{
    if (!m_impl->ui.menuFavorite_Processes)
    {
        return;
    }

    loadFavoriteProcessesFromSettings();
    m_impl->ui.menuFavorite_Processes->clear();

    const std::vector<std::pair<QString, std::string>> favoriteEntries = m_impl->m_Core.favoriteProcesses().entries();

    for (const auto& favoriteEntry : favoriteEntries)
    {
        const QString& displayName = favoriteEntry.first;
        const std::string processId = favoriteEntry.second;

        auto* action = m_impl->ui.menuFavorite_Processes->addAction(displayName);
        action->setToolTip(QString::fromUtf8(processId));
        QObject::connect(action, &QAction::triggered, this, [this, processId]() {
            if (!tryLaunchRegistryProcessById(processId))
            {
                LOG_WARNING("Favorite process launch failed for descriptor id={}", processId);
            }
        });
    }

    const an::ui::FavoriteProcessMenuLayout menuLayout = an::ui::buildFavoriteProcessMenuLayout(favoriteEntries.size());
    if (menuLayout.addSeparatorBeforeManage)
    {
        m_impl->ui.menuFavorite_Processes->addSeparator();
    }

    auto* manageAction = m_impl->ui.menuFavorite_Processes->addAction(tr("Manage Favorites..."));
    QObject::connect(manageAction, &QAction::triggered, this, [this]() { openManageFavoriteProcessesDialog(); });

    auto* clearAction = m_impl->ui.menuFavorite_Processes->addAction(tr("Clear favorites list"));
    clearAction->setEnabled(menuLayout.clearEnabled);
    QObject::connect(clearAction, &QAction::triggered, this, [this]() { clearFavoriteProcessesList(); });
}

void AfterNightApp::openManageFavoriteProcessesDialog()
{
    loadFavoriteProcessesFromSettings();

    std::vector<std::pair<QString, std::string>> allProcesses;
    const auto& descriptors = an::ui::process_registry::ProcessRegistry::instance().all();
    allProcesses.reserve(descriptors.size());
    for (const auto& descriptor : descriptors)
    {
        if (descriptor.id.empty())
        {
            continue;
        }

        const QString displayName = an::ui::processDisplayNameFromDescriptor(&descriptor, descriptor.id);
        allProcesses.emplace_back(displayName, descriptor.id);
    }

    std::sort(allProcesses.begin(), allProcesses.end(), [](const auto& lhs, const auto& rhs) {
        return QString::compare(lhs.first, rhs.first, Qt::CaseInsensitive) < 0;
    });

    ScopedTopLevelFlagRestorer _;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Manage Favorite Processes"));
    dialog.resize(540, 640);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto* searchInput = new QLineEdit(&dialog);
    searchInput->setPlaceholderText(tr("Search processes..."));
    layout->addWidget(searchInput);

    auto* processList = new QListWidget(&dialog);
    processList->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(processList);

    for (const auto& process : allProcesses)
    {
        const QString& displayName = process.first;
        const std::string& processId = process.second;

        auto* item = new QListWidgetItem(displayName, processList);
        item->setData(Qt::UserRole, QString::fromUtf8(processId));
        item->setToolTip(QString::fromUtf8(processId));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(m_impl->m_Core.favoriteProcesses().ids().contains(processId) ? Qt::CheckState::Checked
                                                                                         : Qt::CheckState::Unchecked);
    }

    connect(searchInput, &QLineEdit::textChanged, &dialog, [processList](const QString& text) {
        const QString normalizedText = text.trimmed();
        for (int i = 0; i < processList->count(); ++i)
        {
            QListWidgetItem* item = processList->item(i);
            if (!item)
            {
                continue;
            }

            const bool matches = normalizedText.isEmpty() ||
                                 item->text().contains(normalizedText, Qt::CaseInsensitive) ||
                                 item->toolTip().contains(normalizedText, Qt::CaseInsensitive);
            item->setHidden(!matches);
        }
    });

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    std::set<std::string> selectedFavoriteIds;
    for (int i = 0; i < processList->count(); ++i)
    {
        QListWidgetItem* item = processList->item(i);
        if (!item || item->checkState() != Qt::CheckState::Checked)
        {
            continue;
        }

        const QString processId = item->data(Qt::UserRole).toString();
        if (!processId.isEmpty())
        {
            selectedFavoriteIds.insert(processId.toUtf8().toStdString());
        }
    }

    m_impl->m_Core.favoriteProcesses().setIds(std::move(selectedFavoriteIds));
    updateFavoriteProcessesMenuEntries();
}

void AfterNightApp::clearFavoriteProcessesList()
{
    loadFavoriteProcessesFromSettings();
    if (m_impl->m_Core.favoriteProcesses().ids().empty())
    {
        updateFavoriteProcessesMenuEntries();
        return;
    }

    ScopedTopLevelFlagRestorer _;

    QMessageBox msgBox;
    msgBox.setText(tr("Are you sure you want to delete the favorite processes list?"));
    msgBox.setInformativeText(tr("The list of favorite processes will be lost"));
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::Yes);

    if (msgBox.exec() != QMessageBox::Yes)
    {
        return;
    }

    m_impl->m_Core.favoriteProcesses().clear();
    updateFavoriteProcessesMenuEntries();
}

void AfterNightApp::openRecentSingleFile(const QString& filePath)
{
    if (m_impl->m_FileManager)
    {
        m_impl->m_FileManager->openRecentSingleFile(filePath);
    }
}

void AfterNightApp::createAuxToolBars()
{
    if (m_impl->m_WindowLayoutCoordinator)
    {
        m_impl->m_WindowLayoutCoordinator->createAuxToolBars();
    }
}

void AfterNightApp::splitAllImages(ImageViewControl* currentViewControl)
{
    if (m_impl->m_WindowLayoutCoordinator)
    {
        m_impl->m_WindowLayoutCoordinator->splitAllImages(currentViewControl);
    }
}

void AfterNightApp::groupAllImages(ImageViewControl* currentViewControl)
{
    if (m_impl->m_WindowLayoutCoordinator)
    {
        m_impl->m_WindowLayoutCoordinator->groupAllImages(currentViewControl);
    }
}

void AfterNightApp::fitAllImagesToWindow(ImageViewControl* currentViewControl)
{
    if (m_impl->m_WindowLayoutCoordinator)
    {
        m_impl->m_WindowLayoutCoordinator->fitAllImagesToWindow(currentViewControl);
    }
}

void AfterNightApp::splitAllImages()
{
    if (m_impl->m_WindowLayoutCoordinator)
    {
        m_impl->m_WindowLayoutCoordinator->splitAllImages();
    }
}

void AfterNightApp::groupAllImages()
{
    if (m_impl->m_WindowLayoutCoordinator)
    {
        m_impl->m_WindowLayoutCoordinator->groupAllImages();
    }
}

bool AfterNightApp::viewControlInAMaximizedContainer(ImageViewControl* control)
{
    return m_impl->m_WindowLayoutCoordinator
               ? m_impl->m_WindowLayoutCoordinator->viewControlInAMaximizedContainer(control)
               : false;
}

ImageViewControl* AfterNightApp::getviewControlInAMaximizedContainer(ImageViewControl* control)
{
    return m_impl->m_WindowLayoutCoordinator
               ? m_impl->m_WindowLayoutCoordinator->getViewControlInAMaximizedContainer(control)
               : nullptr;
}

std::map<KDDockWidgets::QtWidgets::DockWidget*, ImageViewControl*> AfterNightApp::getViewControlsByDockWidgetMap()
{
    return m_impl->m_WindowLayoutCoordinator ? m_impl->m_WindowLayoutCoordinator->getViewControlsByDockWidgetMap()
                                             : std::map<KDDockWidgets::QtWidgets::DockWidget*, ImageViewControl*>();
}

std::map<ImageViewControl*, KDDockWidgets::QtWidgets::DockWidget*> AfterNightApp::getDockWidgetsByViewControlMap()
{
    return m_impl->m_WindowLayoutCoordinator ? m_impl->m_WindowLayoutCoordinator->getDockWidgetsByViewControlMap()
                                             : std::map<ImageViewControl*, KDDockWidgets::QtWidgets::DockWidget*>();
}

std::list<ImageViewControl*> AfterNightApp::getListOfViewControls()
{
    return m_impl->m_WindowLayoutCoordinator ? m_impl->m_WindowLayoutCoordinator->getListOfViewControls()
                                             : std::list<ImageViewControl*>();
}

std::list<ImageViewControl*>
AfterNightApp::getListOfViewControlsSharingTheSameWindow(ImageViewControl* currentViewControl)
{
    return m_impl->m_WindowLayoutCoordinator
               ? m_impl->m_WindowLayoutCoordinator->getListOfViewControlsSharingTheSameWindow(currentViewControl)
               : std::list<ImageViewControl*>();
}

std::list<ImageViewControl*> AfterNightApp::getListOfMaximizedViewControls()
{
    return m_impl->m_WindowLayoutCoordinator ? m_impl->m_WindowLayoutCoordinator->getListOfMaximizedViewControls()
                                             : std::list<ImageViewControl*>();
}

void AfterNightApp::notifyErrorMessage(QWidget* parent, const QString& title, const QString& text)
{
    m_impl->m_Core.dialogService().notifyErrorMessage(parent, title, text);
}

void AfterNightApp::notifyErrorMessageAsync(QWidget* parent, const QString& title, const QString& text)
{
    m_impl->m_Core.notifyErrorMessageAsync(parent, title, text);
}

void AfterNightApp::notifyWarnMessage(QWidget* parent, const QString& title, const QString& text)
{
    m_impl->m_Core.dialogService().notifyWarnMessage(parent, title, text);
}

void AfterNightApp::notifyWarnMessageAsync(QWidget* parent, const QString& title, const QString& text)
{
    m_impl->m_Core.notifyWarnMessageAsync(parent, title, text);
}

void AfterNightApp::notifyInfoMessage(QWidget* parent, const QString& title, const QString& text)
{
    m_impl->m_Core.dialogService().notifyInfoMessage(parent, title, text);
}

void AfterNightApp::notifyInfoMessageAsync(QWidget* parent, const QString& title, const QString& text)
{
    m_impl->m_Core.notifyInfoMessageAsync(parent, title, text);
}

void AfterNightApp::messageBoxYesNo(QWidget* parent, const QString& title, const QString& text, bool warn, int* ret)
{
    m_impl->m_Core.dialogService().messageBoxYesNo(parent, title, text, warn, ret);
}

void AfterNightApp::messageBoxYesNoAsync(QWidget* parent, const QString& title, const QString& text, bool warn,
                                         int* ret, std::atomic_bool* finished)
{
    m_impl->m_Core.messageBoxYesNoAsync(parent, title, text, warn, ret, finished);
}

void AfterNightApp::runCustomCode(const std::function<void()>& func)
{
    if (func)
    {
        func();
    }
}

void AfterNightApp::runCustomCodeAsync(const std::function<void()>& func)
{
    m_impl->m_Core.runCustomCodeAsync(func);
}

void AfterNightApp::cleanTemporaryFiles()
{
    m_impl->m_Core.runtimeStorage().cleanTemporaryFiles();
}

void AfterNightApp::cleanTemporaryFilesAsync()
{
    m_impl->m_Core.runtimeStorage().cleanTemporaryFilesAsync();
}

void AfterNightApp::cleanStaleScratchRootsAsync()
{
    m_impl->m_Core.runtimeStorage().cleanStaleScratchRootsAsync();
}

void AfterNightApp::ensureTempDirCreated()
{
    m_impl->m_Core.runtimeStorage().ensureTempDirCreated();
}

QString AfterNightApp::getTempDirPath() const
{
    return m_impl->m_Core.runtimeStorage().getTempDirPath();
}

void AfterNightApp::ensureThumbnailsDirCreated()
{
    m_impl->m_Core.runtimeStorage().ensureThumbnailsDirCreated();
}

QString AfterNightApp::getThumbnailsDirPath() const
{
    return m_impl->m_Core.runtimeStorage().getThumbnailsDirPath();
}

std::string AfterNightApp::createTempFilePathForNewSequence(const an::SequenceFormat format) const
{
    return m_impl->m_Core.runtimeStorage().createTempFilePathForNewSequence(format);
}

bool AfterNightApp::isDarkMode() const
{
    return m_impl->m_DarkMode;
}

void AfterNightApp::applyTheme(const QString& themeId)
{
    if (m_impl->m_ThemeManager)
    {
        m_impl->m_ThemeManager->applyTheme(themeId);
    }
    if (m_impl->m_ExtensionHostLaunchCoordinator)
    {
        m_impl->m_ExtensionHostLaunchCoordinator->handleThemeChanged();
    }
}

void AfterNightApp::refreshAllZoomSettings()
{
    if (m_impl->m_ImageViewControlManager)
    {
        for (auto* viewControl : m_impl->m_ImageViewControlManager->getListOfImageViewControls())
        {
            if (viewControl)
            {
                viewControl->refreshZoomSettings();
            }
        }
    }
}

void AfterNightApp::showWithSavedGeometry()
{
    if (m_impl->m_AppLifecycleCoordinator)
    {
        m_impl->m_AppLifecycleCoordinator->showWithSavedGeometry();
    }
    if (m_impl->m_DialogOrchestrationCoordinator)
    {
        m_impl->m_DialogOrchestrationCoordinator->checkExtensionUpdatesOnStartup();
    }
}

void AfterNightApp::saveWindowGeometry()
{
    if (m_impl->m_AppLifecycleCoordinator)
    {
        m_impl->m_AppLifecycleCoordinator->saveWindowGeometry();
    }
}

void AfterNightApp::maximizeViewControl(ImageViewControl* ctrl)
{
    m_impl->m_Core.viewService().maximizeViewControl(ctrl);
}

void AfterNightApp::restoreViewControl(ImageViewControl* ctrl)
{
    m_impl->m_Core.viewService().restoreViewControl(ctrl);
}

IconManagerSptr AfterNightApp::getIconManager()
{
    return m_impl->m_IconManager;
}

std::set<std::string> AfterNightApp::getListOfTemporaryDirs()
{
    return m_impl->m_Core.runtimeStorage().getListOfTemporaryDirs();
}

// ---- The shell

void AfterNightApp::build() {}

QWidget* AfterNightApp::mainWindow()
{
    return this;
}

an::ui::shell::IViewHost& AfterNightApp::viewHost()
{
    return *this;
}

an::ui::shell::IStatusDisplay& AfterNightApp::statusDisplay()
{
    return *this;
}

an::ui::IProcessPresenter* AfterNightApp::processPresenter()
{
    return nullptr;
}

void AfterNightApp::setSessionConsoleVisible(const bool visible)
{
    setLogViewerChecked(visible);
    on_LogViewerClicked(visible);
}

bool AfterNightApp::isSessionConsoleVisible() const
{
    return m_impl->m_LogViewerControl != nullptr;
}

void AfterNightApp::refreshTheme()
{
    if (m_impl->m_ThemeManager)
    {
        m_impl->m_ThemeManager->refreshTheme();
    }
}

void AfterNightApp::saveGeometry()
{
    saveWindowGeometry();
}

// ---- Where the views are

void AfterNightApp::place(ImageViewControl& view, const an::ui::shell::ViewPlacement& placement)
{
    QWidget* const current = m_impl->ui.centralWidget ? m_impl->ui.centralWidget->currentWidget() : nullptr;
    const bool onDashboard = current == m_impl->m_MainDashboardScrollArea;
    const bool onDockArea = current == m_impl->m_DockMainWindow;
    if (current && !onDashboard && !onDockArea && m_impl->m_WindowLayoutCoordinator)
    {
        // A view maximized on its page goes back to the dock area, which the new view joins.
        for (ImageViewControl* maximized : m_impl->m_WindowLayoutCoordinator->getListOfMaximizedViewControls())
        {
            if (maximized != &view && !maximized->isInFloatingWindow())
            {
                maximized->switchMaximized();
            }
        }
    }

    an::ui::dockCreatedView(view, placement.neighbour, m_impl->m_DockMainWindow);
    showViews();
}

void AfterNightApp::activate(ImageViewControl& view)
{
    if (m_impl->m_ViewControlCoordinator)
    {
        m_impl->m_ViewControlCoordinator->viewActivated(&view);
    }
}

void AfterNightApp::viewAdded(ImageViewControl& view, const an::ui::shell::ViewOrigin /*origin*/)
{
    if (m_impl->m_ViewControlCoordinator)
    {
        m_impl->m_ViewControlCoordinator->viewAdded(&view);
    }
}

void AfterNightApp::viewRemoved(ImageViewControl& view)
{
    if (m_impl->m_ViewControlCoordinator)
    {
        m_impl->m_ViewControlCoordinator->viewRemoved(&view);
    }
}

void AfterNightApp::viewRenamed(ImageViewControl& view, const std::string& previousName)
{
    if (m_impl->m_ViewControlCoordinator)
    {
        m_impl->m_ViewControlCoordinator->viewRenamed(&view, previousName);
    }
}

void AfterNightApp::maximize(ImageViewControl& view)
{
    if (m_impl->m_WindowLayoutCoordinator)
    {
        m_impl->m_WindowLayoutCoordinator->maximizeViewControl(&view);
    }
}

void AfterNightApp::restore(ImageViewControl& view)
{
    if (m_impl->m_WindowLayoutCoordinator)
    {
        m_impl->m_WindowLayoutCoordinator->restoreViewControl(&view);
    }
}

an::ui::shell::DockTarget AfterNightApp::dockTarget(const ImageViewControl* /*view*/)
{
    return an::ui::shell::DockTarget{.mainWindow = m_impl->m_DockMainWindow, .workspaceId = {}};
}

void AfterNightApp::showHome()
{
    // The Session Console is a dock of the dock area, which stays on screen while it is open.
    if (m_impl->m_LogViewerControl == nullptr)
    {
        switchToMainDashboard();
    }
}

void AfterNightApp::showViews()
{
    if (m_impl->m_DockMainWindow && m_impl->ui.centralWidget &&
        m_impl->ui.centralWidget->currentWidget() != m_impl->m_DockMainWindow)
    {
        switchToDockManager();
    }
}

// ---- The status line

void AfterNightApp::showImageInfo(ImageViewControl* view)
{
    if (m_impl->m_ImageInfoStatusBarWidget)
    {
        m_impl->m_ImageInfoStatusBarWidget->updateImageInfo(view);
    }
}

void AfterNightApp::showZoom(ImageViewControl* view)
{
    if (m_impl->m_ImageInfoStatusBarWidget)
    {
        m_impl->m_ImageInfoStatusBarWidget->updateImageZoomInfo(view);
    }
}

void AfterNightApp::showCelestialCoordinates(const double raDegrees, const double decDegrees)
{
    if (m_impl->m_ImageInfoStatusBarWidget)
    {
        m_impl->m_ImageInfoStatusBarWidget->updateCelestialCoords(raDegrees, decDegrees);
    }
}

void AfterNightApp::clearCelestialCoordinates()
{
    if (m_impl->m_ImageInfoStatusBarWidget)
    {
        m_impl->m_ImageInfoStatusBarWidget->clearCelestialCoords();
    }
}

void AfterNightApp::clearImageInfo()
{
    showImageInfo(nullptr);
}
