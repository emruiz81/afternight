// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The application's process menus as the Controls library builds and wires them over the registry the processes
/// library registers: which menu each process action sits in, the descriptor each action launches when it is triggered,
/// the icon each colour and conversion action carries, the Filters menu following the image actions, and the wiring
/// registering nothing of its own - an action whose descriptor is missing waits rather than launching.

#include "Test_ProcessFramework/ProcessFrameworkSuiteSupport.h"

#include "app/ActionStateCoordinator.h"
#include "app/AfterNightAppUiBuilder.h"
#include "app/IconManager.h"
#include "app/MenuActionManager.h"
#include "app/ProcessRegistry.h"

#include <QAction>
#include <QColor>
#include <QDir>
#include <QIcon>
#include <QImage>
#include <QList>
#include <QMainWindow>
#include <QMenu>
#include <QString>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace
{

using an::tests::processes::shared::registered;
using an::tests::processes::shared::RegistrySnapshot;

/// The application's main window and its menus, wired to the registry as the application wires them, recording every
/// descriptor a triggered action asks to launch.
class WiredMenus
{
public:
    WiredMenus() : ui(an::ui::app::buildAfterNightAppUi(&window))
    {
        an::ui::MenuActionManager::Dependencies dependencies;
        dependencies.signalContext = &window;
        dependencies.ui = &ui;
        dependencies.tryLaunchRegistryProcessById = [this](const std::string& id) {
            launched.push_back(id);
            return true;
        };
        dependencies.iconManager = [this]() { return icons; };
        manager = std::make_unique<an::ui::MenuActionManager>(dependencies);
    }

    QMainWindow window;
    an::ui::app::AfterNightAppUiElements ui;
    std::unique_ptr<an::ui::MenuActionManager> manager;
    std::vector<std::string> launched;
    IconManagerSptr icons;
};

/// The descriptor ids triggering an action asked to launch.
[[nodiscard]] std::vector<std::string> launchedBy(WiredMenus& menus, QAction* action)
{
    menus.launched.clear();
    EXPECT_NE(action, nullptr);
    if (action != nullptr)
    {
        EXPECT_TRUE(action->isEnabled()) << action->objectName().toStdString();
        action->trigger();
    }
    return menus.launched;
}

/// Writes a square icon of one colour under a name, as the application's icon folders hold them.
void writeIcon(const QDir& folder, const QString& name, const QColor& colour)
{
    QImage image(16, 16, QImage::Format_ARGB32);
    image.fill(colour);
    ASSERT_TRUE(image.save(folder.filePath(name + QStringLiteral(".png"))));
}

} // namespace

// The Color Calibration menu offers the manual, the photometric and the spectrophotometric calibration side by side
// under their own names, each launching its own descriptor - the photometric entry was added beside the manual one,
// not in its place.
TEST(ProcessMenuWiringTest, TheColourCalibrationMenuLaunchesEachCalibrationByItsOwnDescriptor)
{
    const RegistrySnapshot snapshot;
    WiredMenus menus;
    menus.manager->connectProcessActions();

    const QList<QAction*> entries = menus.ui.menuColor_Calibration->actions();
    EXPECT_EQ(menus.ui.menuColor_Calibration->title(), QStringLiteral("Color Calibration"));
    EXPECT_TRUE(entries.contains(menus.ui.actionColor_Calibration_2));
    EXPECT_TRUE(entries.contains(menus.ui.actionPhotometric_Color_Calibration));
    EXPECT_TRUE(entries.contains(menus.ui.actionSpectrophotometric_Color_Calibration));
    EXPECT_EQ(menus.ui.actionColor_Calibration_2->text(), QStringLiteral("Color Calibration"));
    EXPECT_EQ(menus.ui.actionPhotometric_Color_Calibration->text(), QStringLiteral("Photometric Color Calibration"));
    EXPECT_EQ(menus.ui.actionSpectrophotometric_Color_Calibration->text(),
              QStringLiteral("Spectrophotometric Color Calibration"));

    EXPECT_EQ(launchedBy(menus, menus.ui.actionColor_Calibration_2),
              std::vector<std::string>{"color.actionColor_Calibration_2"});
    EXPECT_EQ(launchedBy(menus, menus.ui.actionPhotometric_Color_Calibration),
              std::vector<std::string>{"color.actionPhotometric_Color_Calibration"});
    EXPECT_EQ(launchedBy(menus, menus.ui.actionSpectrophotometric_Color_Calibration),
              std::vector<std::string>{"color.actionSpectrophotometric_Color_Calibration"});

    // Binding names each descriptor by its entry's text, so the three stay apart wherever their names are shown.
    const auto* manual = registered("color.actionColor_Calibration_2");
    const auto* photometric = registered("color.actionPhotometric_Color_Calibration");
    const auto* spectrophotometric = registered("color.actionSpectrophotometric_Color_Calibration");
    ASSERT_NE(manual, nullptr);
    ASSERT_NE(photometric, nullptr);
    ASSERT_NE(spectrophotometric, nullptr);
    EXPECT_EQ(manual->display_name, "Color Calibration");
    EXPECT_EQ(photometric->display_name, "Photometric Color Calibration");
    EXPECT_EQ(spectrophotometric->display_name, "Spectrophotometric Color Calibration");
}

// Image Integration Lite sits in the Preprocess menu under its own name and launches its own top-level descriptor.
TEST(ProcessMenuWiringTest, ImageIntegrationLiteIsOfferedInThePreprocessMenuAndLaunchesItsDescriptor)
{
    const RegistrySnapshot snapshot;
    WiredMenus menus;
    menus.manager->connectProcessActions();

    EXPECT_EQ(menus.ui.menuPreprocess->title(), QStringLiteral("Preprocess"));
    EXPECT_TRUE(menus.ui.menuPreprocess->actions().contains(menus.ui.actionImage_Integration_Lite));
    EXPECT_EQ(menus.ui.actionImage_Integration_Lite->text(), QStringLiteral("Image Integration Lite"));
    EXPECT_EQ(launchedBy(menus, menus.ui.actionImage_Integration_Lite),
              std::vector<std::string>{"multi_image_stacking.actionImage_Integration_Lite"});

    const auto* lite = registered("multi_image_stacking.actionImage_Integration_Lite");
    ASSERT_NE(lite, nullptr);
    EXPECT_EQ(lite->launch_mode, an::ui::process_registry::ProcessLaunchMode::TopLevel);
}

// DeepSNR sits in the Noise Reduction menu and launches its own denoising descriptor.
TEST(ProcessMenuWiringTest, DeepSnrIsOfferedInTheNoiseReductionMenuAndLaunchesItsDescriptor)
{
    const RegistrySnapshot snapshot;
    WiredMenus menus;
    menus.manager->connectProcessActions();

    EXPECT_EQ(menus.ui.menuNoise_Reduction->title(), QStringLiteral("Noise Reduction"));
    EXPECT_TRUE(menus.ui.menuNoise_Reduction->actions().contains(menus.ui.actionDeepSNR));
    EXPECT_EQ(launchedBy(menus, menus.ui.actionDeepSNR), std::vector<std::string>{"denoising.actionDeepSNR"});
}

// The Process menu holds a Filters menu with the Gaussian, median, bilateral, banding and cosmetic filters, none of
// which the Noise Reduction menu repeats, and SCNR sits in the Color Calibration menu rather than Noise Reduction.
TEST(ProcessMenuWiringTest, TheFiltersAndScnrSitInTheirOwnMenus)
{
    QMainWindow window;
    const an::ui::app::AfterNightAppUiElements ui = an::ui::app::buildAfterNightAppUi(&window);

    EXPECT_EQ(ui.menuFilters->title(), QStringLiteral("Filters"));
    EXPECT_TRUE(ui.menuProcess->actions().contains(ui.menuFilters->menuAction()));
    const QList<QAction*> filters = ui.menuFilters->actions();
    const QList<QAction*> noiseReduction = ui.menuNoise_Reduction->actions();
    for (QAction* filter : {ui.actionGaussian_Blur, ui.actionMedian_Filter, ui.actionBilateral_Filter,
                            ui.actionBanding_Reduction_Filter, ui.actionCosmetic_Correction_Filter})
    {
        EXPECT_TRUE(filters.contains(filter)) << filter->objectName().toStdString();
        EXPECT_FALSE(noiseReduction.contains(filter)) << filter->objectName().toStdString();
    }
    EXPECT_TRUE(ui.menuColor_Calibration->actions().contains(ui.actionSCNR));
    EXPECT_FALSE(noiseReduction.contains(ui.actionSCNR));
}

// The action of an extension's process whose environment is still to be prepared follows the guards its launch meets
// once the environment is ready, not the environment - its launch prepares it - and says why the environment is not
// ready; one whose environment cannot be prepared is disabled with its reason.
TEST(ProcessMenuWiringTest, AnExtensionsActionThatCanBePreparedFollowsItsLaunchGuards)
{
    using an::ui::process_registry::ProcessAvailability;
    using an::ui::process_registry::ProcessLaunchMode;

    const RegistrySnapshot snapshot;
    auto& registry = an::ui::process_registry::ProcessRegistry::instance();
    const auto registerExtensionProcess = [&registry](const std::string& name, const ProcessAvailability availability,
                                                      const ProcessLaunchMode launchMode) {
        an::ui::process_registry::ProcessDescriptor descriptor;
        descriptor.id = "extensions." + name;
        descriptor.display_name = name;
        descriptor.category = "extensions";
        descriptor.action_object_name = "action_" + name;
        descriptor.process_name = name;
        descriptor.launch_mode = launchMode;
        descriptor.is_extension_process = true;
        descriptor.extension_id = name;
        descriptor.availability = availability;
        descriptor.availability_reason = "Reason of " + name;
        return registry.registerProcess(std::move(descriptor));
    };
    ASSERT_TRUE(
        registerExtensionProcess("preparable_window", ProcessAvailability::RepairNeeded, ProcessLaunchMode::TopLevel));
    ASSERT_TRUE(registerExtensionProcess("preparable_image", ProcessAvailability::InstallNeeded,
                                         ProcessLaunchMode::SingleImage));
    ASSERT_TRUE(
        registerExtensionProcess("incompatible", ProcessAvailability::Incompatible, ProcessLaunchMode::TopLevel));

    QMainWindow window;
    an::ui::app::AfterNightAppUiElements ui = an::ui::app::buildAfterNightAppUi(&window);
    const auto actionFor = [&ui](const std::string& name) {
        QAction* action = ui.menuProcess->addAction(QString::fromStdString(name));
        action->setProperty("anRegistryDescriptorId", QString::fromStdString("extensions." + name));
        return action;
    };
    QAction* preparableWindow = actionFor("preparable_window");
    QAction* preparableImage = actionFor("preparable_image");
    QAction* incompatible = actionFor("incompatible");

    an::ui::ActionStateCoordinator::Dependencies dependencies;
    dependencies.ui = &ui;
    dependencies.timerContext = &window;
    const an::ui::ActionStateCoordinator coordinator(dependencies);
    coordinator.updateRegistryProcessActionStates(nullptr);

    EXPECT_TRUE(preparableWindow->isEnabled());
    EXPECT_EQ(preparableWindow->toolTip(), QStringLiteral("Reason of preparable_window"));
    EXPECT_FALSE(preparableImage->isEnabled()) << "it needs an image to open on";
    EXPECT_EQ(preparableImage->toolTip(), QStringLiteral("No selected image to process"));
    EXPECT_FALSE(incompatible->isEnabled());
    EXPECT_EQ(incompatible->toolTip(), QStringLiteral("Reason of incompatible"));
}

// The Filters menu is enabled and disabled with the other image actions, so it is offered only while an image is.
TEST(ProcessMenuWiringTest, TheFiltersMenuFollowsTheImageActions)
{
    QMainWindow window;
    an::ui::app::AfterNightAppUiElements ui = an::ui::app::buildAfterNightAppUi(&window);
    an::ui::ActionStateCoordinator::Dependencies dependencies;
    dependencies.ui = &ui;
    dependencies.timerContext = &window;
    const an::ui::ActionStateCoordinator coordinator(dependencies);

    coordinator.setEnableImageActions(false);
    EXPECT_FALSE(ui.menuFilters->isEnabled());
    EXPECT_FALSE(ui.menuNoise_Reduction->isEnabled());
    coordinator.setEnableImageActions(true);
    EXPECT_TRUE(ui.menuFilters->isEnabled());
    EXPECT_TRUE(ui.menuNoise_Reduction->isEnabled());
}

// The colour adjustments and the colour conversions carry the icons of their colour models - hue, saturation and
// lightness; CIE L*a*b*; RGB; greyscale - from the application's icon set, so their glyphs keep their meaning in every
// theme.
TEST(ProcessMenuWiringTest, TheColourActionsCarryTheIconsOfTheirColourModels)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    const QDir icons(folder.path());
    writeIcon(icons, QStringLiteral("hsl"), QColor(200, 40, 40));
    writeIcon(icons, QStringLiteral("cielab"), QColor(40, 200, 40));
    writeIcon(icons, QStringLiteral("rgb"), QColor(40, 40, 200));
    writeIcon(icons, QStringLiteral("grayscale"), QColor(128, 128, 128));

    WiredMenus menus;
    menus.icons = std::make_shared<IconManager>();
    menus.icons->loadIcons(folder.path());
    ASSERT_TRUE(menus.icons->hasIcon(QStringLiteral("hsl")));
    menus.manager->setActionIcons();

    const auto iconKeyOf = [&menus](const char* name) {
        return menus.icons->getIcon(QString::fromLatin1(name)).cacheKey();
    };
    EXPECT_EQ(menus.ui.actionAdjustHSL->icon().cacheKey(), iconKeyOf("hsl"));
    EXPECT_EQ(menus.ui.actionAdjustLightness_Chroma_Hue->icon().cacheKey(), iconKeyOf("cielab"));
    EXPECT_EQ(menus.ui.actionConvert_to_RGB->icon().cacheKey(), iconKeyOf("rgb"));
    EXPECT_EQ(menus.ui.actionConvert_to_Grayscale->icon().cacheKey(), iconKeyOf("grayscale"));
    EXPECT_NE(iconKeyOf("hsl"), iconKeyOf("cielab"));
}

// Wiring the menus binds each action to the descriptor the processes library registered and registers none itself:
// over an empty registry it leaves the registry empty, every process action waiting and saying so, and a triggered
// action launches nothing.
TEST(ProcessMenuWiringTest, WiringTheMenusRegistersNoDescriptor)
{
    const RegistrySnapshot snapshot;
    an::ui::process_registry::ProcessRegistry& registry = an::ui::process_registry::ProcessRegistry::instance();
    const std::size_t registeredBefore = registry.all().size();
    ASSERT_GT(registeredBefore, 0u) << "the processes library registers its descriptors at start-up";
    {
        WiredMenus menus;
        menus.manager->connectProcessActions();
        EXPECT_EQ(registry.all().size(), registeredBefore);
    }

    registry.clear();
    WiredMenus menus;
    menus.manager->connectProcessActions();
    EXPECT_TRUE(registry.empty());
    for (QAction* action : {menus.ui.actionColor_Calibration_2, menus.ui.actionDeepSNR, menus.ui.actionGaussian_Blur,
                            menus.ui.actionImage_Integration_Lite})
    {
        EXPECT_FALSE(action->isEnabled()) << action->objectName().toStdString();
        EXPECT_FALSE(action->toolTip().isEmpty()) << action->objectName().toStdString();
        action->trigger();
    }
    EXPECT_TRUE(menus.launched.empty());
}
