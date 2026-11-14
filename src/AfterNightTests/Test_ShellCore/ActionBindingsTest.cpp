// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the catalogue's actions do: each group reaches its handler once when an action of it is triggered; each process
/// action launches the descriptor the registry holds for it and names it by its text, leaving the name its process
/// attaches under; an action whose descriptor is missing waits and says so; an extension's action stays offered while
/// its environment can be prepared; the actions are connected once however often they are asked to be; and each shows
/// the icon of its key.

#include "ShellCoreRecords.h"
#include "ShellCoreViewSupport.h"

#include "actions/ActionBindings.h"
#include "actions/ActionCatalogue.h"
#include "actions/ActionIds.h"

#include "app/IconManager.h"
#include "app/ImageOperationsCoordinator.h"
#include "app/MaskOverlayCoordinator.h"
#include "app/ProcessRegistry.h"
#include "controls/ImageViewControlManager.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QColor>
#include <QDir>
#include <QIcon>
#include <QImage>
#include <QMessageBox>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{

using an::tests::shellcore::kProcessRecordPath;
using an::tests::shellcore::recordedRows;
using an::ui::process_registry::ProcessDescriptor;
using an::ui::process_registry::ProcessRegistry;
using an::ui::shell::ActionBindings;
using an::ui::shell::ActionCatalogue;
namespace ids = an::ui::shell::ids;

/// Registers a descriptor as the processes library registers its own: named after its action until the menu names it.
bool registerAsTheLibraryDoes(const std::string& id, const std::string& category, const std::string& action,
                              const std::string& processName = {})
{
    ProcessDescriptor descriptor;
    descriptor.id = id;
    descriptor.category = category;
    descriptor.action_object_name = action;
    descriptor.display_name = action;
    descriptor.process_name = processName.empty() ? action : processName;
    return ProcessRegistry::instance().registerProcess(std::move(descriptor));
}

class ActionBindingsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // The suite links no processes library: the registry holds only what a case puts in it.
        ProcessRegistry::instance().clear();
    }

    void TearDown() override
    {
        ProcessRegistry::instance().clear();
    }

    /// Bindings over the case's catalogue whose targets record what they are asked, and launch nothing.
    std::unique_ptr<ActionBindings> recordingBindings()
    {
        auto bindings = std::make_unique<ActionBindings>(m_catalogue, m_context);
        ActionBindings::Targets targets;
        const auto record = [this](const char* what) { return [this, what]() { m_calls.emplace_back(what); }; };
        targets.openFile = record("open");
        targets.saveFile = record("save");
        targets.newSequence = record("new sequence");
        targets.exit = record("exit");
        targets.userGuide = record("user guide");
        targets.about = record("about");
        targets.systemInformation = record("system information");
        targets.settings = record("settings");
        targets.extensionManager = record("extension manager");
        targets.lockLayout = record("lock");
        targets.unlockLayout = record("unlock");
        targets.groupAll = record("group");
        targets.splitAll = record("split");
        targets.sessionConsole = [this](const bool visible) {
            m_calls.emplace_back(visible ? "console shown" : "console hidden");
        };
        targets.launch = [this](const std::string& id) {
            m_launched.push_back(id);
            return true;
        };
        targets.imageOperations = [this]() { return m_imageOperations.get(); };
        targets.maskOverlay = [this]() { return m_masks.get(); };
        bindings->setTargets(std::move(targets));
        return bindings;
    }

    /// What triggering an action asked, and nothing else since the last trigger.
    std::vector<std::string> triggered(std::string_view id)
    {
        m_calls.clear();
        m_launched.clear();
        QAction* const action = m_catalogue.action(id);
        EXPECT_NE(action, nullptr) << id;
        if (action != nullptr)
        {
            action->setEnabled(true);
            action->trigger();
        }
        return m_calls;
    }

    QObject m_owner;
    QObject m_context;
    ActionCatalogue m_catalogue{m_owner};
    std::vector<std::string> m_calls;
    std::vector<std::string> m_launched;
    ImageViewControlManager m_views;
    std::unique_ptr<an::ui::ImageOperationsCoordinator> m_imageOperations;
    std::unique_ptr<an::ui::MaskOverlayCoordinator> m_masks;
};

} // namespace

// Each group of actions reaches its handler once: the files, the edits, the view, the image - through the image
// operations, which say there is no image for the operation each action names - the masks, the window, help and the
// extensions.
TEST_F(ActionBindingsTest, EachGroupReachesItsHandlerOnce)
{
    an::ui::ImageOperationsCoordinator::Dependencies imageDeps;
    imageDeps.activeViewControl = []() -> ImageViewControl* { return nullptr; };
    imageDeps.listViewControls = []() { return std::vector<ImageViewControl*>(); };
    imageDeps.notifyErrorMessage = [this](const QString& title, const QString&) {
        m_calls.push_back("image: " + title.toStdString());
    };
    m_imageOperations = std::make_unique<an::ui::ImageOperationsCoordinator>(imageDeps);
    an::ui::MaskOverlayCoordinator::Dependencies maskDeps;
    maskDeps.actions = m_catalogue.lookup();
    maskDeps.imageViewControlManager = &m_views;
    maskDeps.notifyErrorMessage = [this](const QString& title, const QString&) {
        m_calls.push_back("mask: " + title.toStdString());
    };
    m_masks = std::make_unique<an::ui::MaskOverlayCoordinator>(maskDeps);
    const std::unique_ptr<ActionBindings> bindings = recordingBindings();
    bindings->connectActions();

    const std::vector<std::pair<std::string_view, std::vector<std::string>>> expected = {
        // Files
        {ids::kOpen, {"open"}},
        {ids::kNewSequence, {"new sequence"}},
        {ids::kSaveAs, {"save"}},
        {ids::kExit, {"exit"}},
        // Edits
        {ids::kUndo, {"image: Undo change"}},
        {ids::kRedo, {"image: Redo change"}},
        {ids::kImageHistory, {"image: Image History"}},
        {ids::kSettings, {"settings"}},
        // The view
        {ids::kLogViewer, {"console shown"}},
        // The image
        {ids::kSaveFile, {"save"}},
        {ids::kDuplicate, {"image: Image duplicate error"}},
        {ids::kRename, {"image: Image rename error"}},
        {ids::kHorizontalMirror, {"image: Image horizontal mirror"}},
        {ids::kVerticalMirror, {"image: Image vertical mirror"}},
        {ids::kRotate180, {"image: Image rotate"}},
        {ids::kNegativeTransformation, {"image: Image negative transformation"}},
        {ids::kConvertToGrayscale, {"image: Convert to grayscale"}},
        {ids::kConvertToRGB, {"image: Convert to RGB"}},
        {ids::kZoomIn, {"image: ImageZoomIn"}},
        {ids::kZoomOut, {"image: ImageZoomOut"}},
        {ids::kZoom100, {"image: ImageZoom100"}},
        {ids::kFitToWindow, {"image: ImageFitToWindow"}},
        {ids::kMaximizeView, {"image: SwitchMaximizeView"}},
        {ids::kStatistics, {"image: ImageStatistics"}},
        {ids::kHistogram, {"image: ImageHistogram"}},
        {ids::kFITSHeader, {"image: ImageFITSHeaderViewer"}},
        {ids::kAstrometryInfo, {"image: ImageAstrometryInfo"}},
        {ids::kImageEXIFViewer, {"image: ImageEXIFViewer"}},
        {ids::kPlateSolve, {"image: PlateSolve"}},
        {ids::kAnnotateImage, {"image: AnnotateImage"}},
        {ids::kSummary, {"image: ImageSummaryViewer"}},
        // The masks
        {ids::kInvertMask, {"mask: Invert Mask"}},
        {ids::kEnableMask, {"mask: Enable Mask"}},
        {ids::kShowMask, {"mask: Show Mask"}},
        {ids::kOverlayRed, {"mask: Set Mask Render mode"}},
        {ids::kMultiply, {"mask: Set Mask Render mode"}},
        // The window
        {ids::kLockLayout, {"lock"}},
        {ids::kUnlockLayout, {"unlock"}},
        {ids::kGroupAllImages, {"group"}},
        {ids::kSplitAllImages, {"split"}},
        // Help and the extensions
        {ids::kUserGuide, {"user guide"}},
        {ids::kAboutAfterNight, {"about"}},
        {ids::kSystemInformation, {"system information"}},
        {ids::kManageExtensions, {"extension manager"}},
    };
    for (const auto& [id, calls] : expected)
    {
        EXPECT_EQ(triggered(id), calls) << id;
    }

    // The closes and Show in File Browser say nothing with no view; About Qt shows Qt's own box.
    for (const std::string_view id : {ids::kClose, ids::kClose2, ids::kCloseAll, ids::kCloseAllOthers,
                                      ids::kShowInFileBrowser, ids::kFitImagesToWindow})
    {
        EXPECT_TRUE(triggered(id).empty()) << id;
    }
    bool aboutQtShown = false;
    an::tests::shellcore::answerWhenShown<QMessageBox>([&aboutQtShown](QMessageBox& box) {
        aboutQtShown = true;
        box.close();
    });
    (void)triggered(ids::kAboutQt);
    EXPECT_TRUE(aboutQtShown);
}

// Each process action of the catalogue launches the descriptor the record of 1.1's registry holds for it and names
// the descriptor by its text, leaving the name its process attaches under; the bindings register no descriptor.
TEST_F(ActionBindingsTest, EveryProcessActionLaunchesTheRecordedDescriptor)
{
    const std::vector<QStringList> processes = recordedRows(kProcessRecordPath, "processes");
    ASSERT_GT(processes.size(), 100u) << "the record of the processes was not found";
    for (const QStringList& process : processes)
    {
        ASSERT_GE(process.size(), 6);
        const std::string name = process[1].toStdString();
        const std::string processName = process[2].toStdString();
        ASSERT_TRUE(registerAsTheLibraryDoes(process[0].toStdString(), process[3].toStdString(),
                                             process[5].toStdString(),
                                             processName == name ? std::string() : processName));
    }
    const std::size_t registered = ProcessRegistry::instance().all().size();

    const std::unique_ptr<ActionBindings> bindings = recordingBindings();
    bindings->connectProcessActions();
    EXPECT_EQ(ProcessRegistry::instance().all().size(), registered);

    for (const QStringList& process : processes)
    {
        const std::string id = process[0].toStdString();
        const std::string action = process[5].toStdString();
        (void)triggered(action);
        EXPECT_EQ(m_launched, std::vector<std::string>{id}) << action;
        const ProcessDescriptor* const descriptor = ProcessRegistry::instance().find(id);
        ASSERT_NE(descriptor, nullptr) << id;
        EXPECT_EQ(descriptor->display_name, process[1].toStdString()) << id;
        EXPECT_EQ(descriptor->process_name, process[2].toStdString()) << id;
    }
}

// The menu names each descriptor by its action's text when it binds the action, and leaves the name the process
// attaches to a view under, which the guards and an attached window's exclusive access read; an action that opens no
// process of its own is named by the menu throughout.
TEST_F(ActionBindingsTest, TheMenuNamesADescriptorWithoutRenamingItsProcess)
{
    const std::vector<QStringList> processes = recordedRows(kProcessRecordPath, "processes");
    ASSERT_FALSE(processes.empty());
    const std::unique_ptr<ActionBindings> bindings = recordingBindings();
    for (const QStringList& process : processes)
    {
        const std::string id = process[0].toStdString();
        const std::string ownName = process[1] == process[2] ? std::string() : process[2].toStdString();
        ASSERT_TRUE(registerAsTheLibraryDoes(id, process[3].toStdString(), process[5].toStdString(), ownName));

        QAction action(QStringLiteral("&Menu text of %1...").arg(process[5]));
        action.setObjectName(process[5]);
        bindings->bindRegistryAction(&action, process[3].toStdString());

        const ProcessDescriptor* const descriptor = ProcessRegistry::instance().find(id);
        ASSERT_NE(descriptor, nullptr) << id;
        EXPECT_EQ(descriptor->display_name, "Menu text of " + process[5].toStdString() + "...") << id;
        if (ownName.empty())
        {
            EXPECT_EQ(descriptor->process_name, descriptor->display_name) << id;
        }
        else
        {
            EXPECT_EQ(descriptor->process_name, ownName) << id;
        }
    }
}

// Binding names each colour calibration by its entry's text and launches it by its own descriptor, so the three stay
// apart wherever their names are shown; Image Integration Lite and DeepSNR launch their own descriptors too.
TEST_F(ActionBindingsTest, EachCalibrationAndTheMenusOtherProcessesLaunchTheirOwnDescriptor)
{
    ASSERT_TRUE(registerAsTheLibraryDoes("color.actionColor_Calibration_2", "color", "actionColor_Calibration_2"));
    ASSERT_TRUE(registerAsTheLibraryDoes("color.actionPhotometric_Color_Calibration", "color",
                                         "actionPhotometric_Color_Calibration"));
    ASSERT_TRUE(registerAsTheLibraryDoes("color.actionSpectrophotometric_Color_Calibration", "color",
                                         "actionSpectrophotometric_Color_Calibration"));
    ASSERT_TRUE(registerAsTheLibraryDoes("multi_image_stacking.actionImage_Integration_Lite", "multi_image_stacking",
                                         "actionImage_Integration_Lite"));
    ASSERT_TRUE(registerAsTheLibraryDoes("denoising.actionDeepSNR", "denoising", "actionDeepSNR"));
    ASSERT_TRUE(
        registerAsTheLibraryDoes("mask_generation.actionMask_Drawing", "mask_generation", "actionMask_Drawing"));
    const std::unique_ptr<ActionBindings> bindings = recordingBindings();
    bindings->connectProcessActions();

    const std::vector<std::pair<std::string_view, std::string>> launches = {
        {ids::kColorCalibration2, "color.actionColor_Calibration_2"},
        {ids::kPhotometricColorCalibration, "color.actionPhotometric_Color_Calibration"},
        {ids::kSpectrophotometricColorCalibration, "color.actionSpectrophotometric_Color_Calibration"},
        {ids::kImageIntegrationLite, "multi_image_stacking.actionImage_Integration_Lite"},
        {ids::kDeepSNR, "denoising.actionDeepSNR"},
        {ids::kMaskDrawing, "mask_generation.actionMask_Drawing"},
    };
    for (const auto& [action, id] : launches)
    {
        EXPECT_TRUE(m_catalogue.action(action)->isEnabled()) << action;
        (void)triggered(action);
        EXPECT_EQ(m_launched, std::vector<std::string>{id}) << action;
    }
    EXPECT_EQ(ProcessRegistry::instance().find("color.actionColor_Calibration_2")->display_name, "Color Calibration");
    EXPECT_EQ(ProcessRegistry::instance().find("color.actionPhotometric_Color_Calibration")->display_name,
              "Photometric Color Calibration");
    EXPECT_EQ(ProcessRegistry::instance().find("color.actionSpectrophotometric_Color_Calibration")->display_name,
              "Spectrophotometric Color Calibration");
    EXPECT_EQ(ProcessRegistry::instance().find("mask_generation.actionMask_Drawing")->display_name, "Mask Drawing");
}

// Over an empty registry the bindings register nothing: every process action waits and says so, and a triggered one
// launches nothing.
TEST_F(ActionBindingsTest, AnActionWhoseDescriptorIsMissingWaits)
{
    const std::unique_ptr<ActionBindings> bindings = recordingBindings();
    bindings->connectProcessActions();

    EXPECT_TRUE(ProcessRegistry::instance().empty());
    for (const std::string_view id : {ids::kColorCalibration2, ids::kDeepSNR, ids::kGaussianBlur,
                                      ids::kImageIntegrationLite, ids::kExtractRedChannel})
    {
        QAction* const action = m_catalogue.action(id);
        ASSERT_NE(action, nullptr) << id;
        EXPECT_FALSE(action->isEnabled()) << id;
        EXPECT_FALSE(action->toolTip().isEmpty()) << id;
        action->trigger();
    }
    EXPECT_TRUE(m_launched.empty());
}

// An extension's action stays offered while its environment can be prepared - a launch prepares it - and says why the
// environment is not ready yet; one whose environment cannot be prepared, an incompatible one here, is withdrawn.
TEST_F(ActionBindingsTest, AnExtensionsActionStaysEnabledWhileItsEnvironmentCanBePrepared)
{
    using an::ui::process_registry::ProcessAvailability;
    const auto registerExtensionProcess = [](const std::string& name, const ProcessAvailability availability,
                                             const std::string& reason) {
        ProcessDescriptor descriptor;
        descriptor.id = "extensions." + name;
        descriptor.display_name = name;
        descriptor.category = "extensions";
        descriptor.action_object_name = "action_" + name;
        descriptor.process_name = name;
        descriptor.is_extension_process = true;
        descriptor.extension_id = name;
        descriptor.availability = availability;
        descriptor.availability_reason = reason;
        return ProcessRegistry::instance().registerProcess(std::move(descriptor));
    };
    ASSERT_TRUE(registerExtensionProcess("preparable", ProcessAvailability::InstallNeeded,
                                         "Extension environment has not been provisioned yet."));
    ASSERT_TRUE(registerExtensionProcess("incompatible", ProcessAvailability::Incompatible,
                                         "Requires AfterNight 99.0 or newer."));
    const std::unique_ptr<ActionBindings> bindings = recordingBindings();

    QAction preparable(QStringLiteral("Preparable"));
    preparable.setObjectName(QStringLiteral("action_preparable"));
    bindings->bindRegistryAction(&preparable, "extensions", "extensions.preparable");
    EXPECT_TRUE(preparable.isEnabled());
    EXPECT_EQ(preparable.toolTip(), QStringLiteral("Extension environment has not been provisioned yet."));
    preparable.trigger();
    EXPECT_EQ(m_launched, std::vector<std::string>{"extensions.preparable"});

    QAction incompatible(QStringLiteral("Incompatible"));
    incompatible.setObjectName(QStringLiteral("action_incompatible"));
    bindings->bindRegistryAction(&incompatible, "extensions", "extensions.incompatible");
    EXPECT_FALSE(incompatible.isEnabled());
}

// The actions are connected once: asked again, the bindings connect nothing more, so a trigger reaches its handler
// once.
TEST_F(ActionBindingsTest, ConnectingTwiceConnectsOnce)
{
    ASSERT_TRUE(registerAsTheLibraryDoes("denoising.actionDeepSNR", "denoising", "actionDeepSNR"));
    const std::unique_ptr<ActionBindings> bindings = recordingBindings();
    bindings->connectActions();
    bindings->connectActions();
    bindings->connectProcessActions();
    bindings->connectProcessActions();

    EXPECT_EQ(triggered(ids::kOpen), std::vector<std::string>{"open"});
    (void)triggered(ids::kDeepSNR);
    EXPECT_EQ(m_launched, std::vector<std::string>{"denoising.actionDeepSNR"});
}

// Each action shows the icon of its key in the application's icon set - the colour adjustments and conversions the
// icons of their colour models - and an action of no key shows none.
TEST_F(ActionBindingsTest, EachActionShowsTheIconOfItsKey)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    const QDir icons(folder.path());
    const auto writeIcon = [&icons](const QString& name, const QColor& colour) {
        QImage image(16, 16, QImage::Format_ARGB32);
        image.fill(colour);
        ASSERT_TRUE(image.save(icons.filePath(name + QStringLiteral(".png"))));
    };
    writeIcon(QStringLiteral("hsl"), QColor(200, 40, 40));
    writeIcon(QStringLiteral("cielab"), QColor(40, 200, 40));
    writeIcon(QStringLiteral("rgb"), QColor(40, 40, 200));
    writeIcon(QStringLiteral("grayscale"), QColor(128, 128, 128));
    writeIcon(QStringLiteral("folder_open"), QColor(20, 20, 20));
    const auto manager = std::make_shared<IconManager>();
    manager->loadIcons(folder.path());
    ASSERT_TRUE(manager->hasIcon(QStringLiteral("hsl")));

    const std::unique_ptr<ActionBindings> bindings = recordingBindings();
    bindings->setActionIcons([&manager](const QString& key) { return manager->getIcon(key); });

    const auto iconOf = [&manager](const char* key) { return manager->getIcon(QString::fromLatin1(key)).cacheKey(); };
    EXPECT_EQ(m_catalogue.action(ids::kAdjustHSL)->icon().cacheKey(), iconOf("hsl"));
    EXPECT_EQ(m_catalogue.action(ids::kAdjustLightnessChromaHue)->icon().cacheKey(), iconOf("cielab"));
    EXPECT_EQ(m_catalogue.action(ids::kConvertToRGB)->icon().cacheKey(), iconOf("rgb"));
    EXPECT_EQ(m_catalogue.action(ids::kConvertToGrayscale)->icon().cacheKey(), iconOf("grayscale"));
    EXPECT_EQ(m_catalogue.action(ids::kOpen)->icon().cacheKey(), iconOf("folder_open"));
    EXPECT_NE(iconOf("hsl"), iconOf("cielab"));
    EXPECT_TRUE(m_catalogue.action(ids::kConvertTo8Bit)->icon().isNull());
}
