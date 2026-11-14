// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The state of the catalogue's actions and of the menus that hold them, as the action state coordinator sets it from
/// the image the image actions act on: no view, a still image of one channel or of three, a frame of a sequence, a
/// masked image, a maximized view - a table of what each offers - and Undo and Redo labelled by the view's history.

#include "ShellCoreTestSupport.h"
#include "ShellCoreViewSupport.h"

#include "actions/ActionCatalogue.h"
#include "actions/ActionIds.h"
#include "actions/ActionStateCoordinator.h"
#include "core/ApplicationContext.h"
#include "core/ApplicationCore.h"
#include "core/ApplicationEvents.h"
#include "views/ViewService.h"

#include "app/ProcessRegistry.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewCompanions.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlManager.h"
#include "session/ImageDocument.h"

#include <AfterNightEngine/IO/SequenceDescriptor.h>

#include <gtest/gtest.h>

#include <QAction>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTemporaryDir>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{

using an::tests::shellcore::RecordingShell;
using an::tests::shellcore::ScopedStorageRoot;
using an::tests::shellcore::SessionProvider;
using an::ui::shell::ActionCatalogue;
using an::ui::shell::ActionStateCoordinator;
using an::ui::shell::ApplicationCore;
namespace ids = an::ui::shell::ids;

/// What an action is to be in a state: offered or not, and - where stated - checked or not.
struct ActionExpected
{
    std::string_view id;
    bool enabled = true;
    std::optional<bool> checked;
};

/// What a menu of the menu tree is to be in a state.
struct MenuExpected
{
    const char* id = "";
    bool enabled = true;
};

/// A state of the application, and what the coordinator makes of it.
struct Scenario
{
    const char* name = "";
    /// Makes what the state is: the views, the active one, the shell's answer.
    std::function<void()> make;
    std::vector<ActionExpected> actions;
    std::vector<MenuExpected> menus;
};

/// The actions an image is needed for, offered or not together; the colour actions apart.
std::vector<ActionExpected> imageActions(const bool enabled)
{
    std::vector<ActionExpected> expected;
    for (const std::string_view id :
         {ids::kSaveAs, ids::kClose, ids::kCloseAll, ids::kCloseAllOthers, ids::kHorizontalMirror, ids::kVerticalMirror,
          ids::kImageBlend, ids::kImageCompare, ids::kNoiseGenerator, ids::kFitImagesToWindow})
    {
        expected.push_back({id, enabled});
    }
    return expected;
}

/// The colour actions: offered for an image of three channels, and Convert to RGB for one that is not.
std::vector<ActionExpected> colourActions(const bool colour)
{
    std::vector<ActionExpected> expected;
    for (const std::string_view id :
         {ids::kAdjustHSL, ids::kAdjustLightnessChromaHue, ids::kConvertToGrayscale, ids::kColorCalibration2,
          ids::kPhotometricColorCalibration, ids::kSpectrophotometricColorCalibration, ids::kBackgroundNeutralization,
          ids::kColorSaturation, ids::kSCNR, ids::kLRGBCombination, ids::kNBRGBCombination})
    {
        expected.push_back({id, colour});
    }
    expected.push_back({ids::kConvertToRGB, !colour});
    // Offered whatever the image is.
    expected.push_back({ids::kNarrowbandPaletteComposer, true});
    expected.push_back({ids::kImageListAnalyzer, true});
    return expected;
}

/// The menus an image is needed for; Color Spaces is offered always, Extract Channel for a colour image, Sequence for a
/// frame of a sequence.
std::vector<MenuExpected> imageMenus(const bool enabled, const bool sequence, const bool colour = false)
{
    std::vector<MenuExpected> expected;
    for (const char* id :
         {"menuActions", "menuView", "menuMask", "menuConvert_Format", "menuGeometry", "menuInformation",
          "menuAdjustments", "menuIntensity_Transformations", "menuStar_Processing", "menuNoise_Reduction",
          "menuGeometry_2", "menuBackground_Modelization", "menuDetails", "menuFilters", "menuMorphology",
          "menuColor_Calibration", "menuMask_Generation", "menuPixel_Math", "menuMultiscale"})
    {
        expected.push_back({id, enabled});
    }
    expected.push_back({"menuColor_Spaces", true});
    expected.push_back({"menuExtract_Channel", colour});
    expected.push_back({"menuSequence", sequence});
    return expected;
}

template <typename ValueT> void append(std::vector<ValueT>& to, const std::vector<ValueT>& more)
{
    to.insert(to.end(), more.begin(), more.end());
}

class ActionStateTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_root.isValid());
        an::ui::process_registry::ProcessRegistry::instance().clear();
        m_session = an::tests::shellcore::openSession();
        ASSERT_NE(m_session, nullptr);
        m_core.openStores();
        m_core.setShell(&m_shell);
        m_core.setEngineSessionProvider(m_session.get());
        m_target = std::make_unique<an::ui::shell::ServiceEventTarget>(
            m_core, an::ui::shell::ServiceEventTarget::Dependencies{});
        m_core.setEventTarget(m_target.get());
        m_context = std::make_unique<an::ui::shell::ApplicationContext>(m_core, m_shell);
        an::ui::clearAppContext();
        an::ui::setAppContext(m_context.get());

        ActionStateCoordinator::Dependencies deps;
        deps.actions = m_catalogue.lookup();
        deps.setMenuEnabled = [this](std::string_view menuId, const bool enabled) {
            m_menus[std::string(menuId)] = enabled;
        };
        deps.subjectView = [this]() { return m_core.viewService().subjectView(); };
        deps.lookingView = [this]() { return m_core.viewService().lookingView(); };
        deps.timerContext = &m_owner;
        deps.isMaximizedViewActive = [this]() { return m_maximizedViewActive; };
        m_coordinator = std::make_unique<ActionStateCoordinator>(std::move(deps));
    }

    void TearDown() override
    {
        m_coordinator.reset();
        // The views leave the list first, as the window's going takes them out of it: a view deleted while it is
        // listed would leave the list naming it for the work still queued to read.
        m_core.viewService().manager().clear();
        for (const QPointer<QWidget>& dock : m_docks)
        {
            delete dock.data();
        }
        for (const QPointer<ImageViewControl>& view : m_views)
        {
            delete view.data();
        }
        an::tests::shellcore::settle();
        m_core.discardPendingEvents();
        m_core.setEventTarget(nullptr);
        an::ui::clearAppContext();
        m_core.setEngineSessionProvider(nullptr);
        m_core.setShell(nullptr);
        m_core.closeStores();
        an::ui::process_registry::ProcessRegistry::instance().clear();
    }

    /// A view of a new document made by `make`, made the active view; the case deletes it.
    ImageViewControl* activeView(std::optional<an::ui::ImageDocument> document)
    {
        EXPECT_TRUE(document.has_value());
        if (!document.has_value())
        {
            return nullptr;
        }
        an::ui::IViewFactory& factory = m_core.viewService();
        ImageViewControl* const view = factory.createNewImageViewControlFromImage(
            nullptr, std::move(*document), nullptr, "made", ViewControlMode::standard, nullptr, false, false);
        EXPECT_NE(view, nullptr);
        if (view != nullptr)
        {
            m_views.emplace_back(view);
            m_docks.emplace_back(view->getDockWidget());
            m_core.viewService().setActiveImageViewControl(view);
        }
        return view;
    }

    template <typename SampleT>
    ImageViewControl* activeViewOf(const char* name, const bool rgb = false,
                                   const std::optional<an::SequenceFormat> sequence = std::nullopt)
    {
        return activeView(an::tests::shellcore::documentOf<SampleT>(m_session->engineSession(),
                                                                    QString::fromLatin1(name), rgb, sequence));
    }

    /// Sets the state of a scenario and asks the coordinator, then checks every action and menu it states.
    void check(const Scenario& scenario)
    {
        SCOPED_TRACE(scenario.name);
        m_menus.clear();
        if (scenario.make)
        {
            scenario.make();
        }
        m_coordinator->updateActiveImageActions();
        an::tests::shellcore::settle();
        for (const ActionExpected& expected : scenario.actions)
        {
            QAction* const action = m_catalogue.action(expected.id);
            ASSERT_NE(action, nullptr) << expected.id;
            EXPECT_EQ(action->isEnabled(), expected.enabled) << expected.id;
            if (expected.checked.has_value())
            {
                EXPECT_EQ(action->isChecked(), *expected.checked) << expected.id;
            }
        }
        for (const MenuExpected& expected : scenario.menus)
        {
            const auto told = m_menus.find(expected.id);
            ASSERT_NE(told, m_menus.end()) << expected.id << " was not told its state";
            EXPECT_EQ(told->second, expected.enabled) << expected.id;
        }
    }

    QTemporaryDir m_root;
    ScopedStorageRoot m_scopedRoot{m_root.path()};
    std::unique_ptr<SessionProvider> m_session;
    RecordingShell m_shell;
    ApplicationCore m_core;
    std::unique_ptr<an::ui::shell::ServiceEventTarget> m_target;
    std::unique_ptr<an::ui::shell::ApplicationContext> m_context;
    QObject m_owner;
    ActionCatalogue m_catalogue{m_owner};
    std::unique_ptr<ActionStateCoordinator> m_coordinator;
    std::map<std::string, bool> m_menus;
    bool m_maximizedViewActive = false;
    std::vector<QPointer<ImageViewControl>> m_views;
    std::vector<QPointer<QWidget>> m_docks;
};

} // namespace

// What the coordinator implements, as a table: for each state of the application, the actions and menus it offers.
TEST_F(ActionStateTest, EachStateOffersTheActionsTheTableStates)
{
    std::vector<Scenario> scenarios;

    {
        Scenario none{.name = "no view"};
        append(none.actions, imageActions(false));
        append(none.actions, colourActions(false));
        append(none.actions, {{ids::kStatistics, false},
                              {ids::kHistogram, false},
                              {ids::kFITSHeader, false},
                              {ids::kAstrometryInfo, false},
                              {ids::kImageEXIFViewer, false},
                              {ids::kPlateSolve, false},
                              {ids::kAnnotateImage, false},
                              {ids::kSummary, false},
                              {ids::kShowInFileBrowser, false},
                              {ids::kGroupAllImages, false},
                              {ids::kSplitAllImages, false}});
        none.menus = imageMenus(false, false);
        scenarios.push_back(std::move(none));
    }
    {
        Scenario mono{.name = "a still Float32 image of one channel",
                      .make = [this]() { (void)activeViewOf<float>("mono"); }};
        append(mono.actions, imageActions(true));
        append(mono.actions, colourActions(false));
        append(mono.actions, {{ids::kConvertTo32BitFloat, false},
                              {ids::kConvertToUnsigned16Bit, true},
                              {ids::kConvertTo8Bit, true},
                              {ids::kConvertTo64BitFloat, true},
                              {ids::kAstrometryInfo, true},
                              {ids::kSummary, true},
                              {ids::kPlateSolve, true},
                              {ids::kAnnotateImage, false},
                              {ids::kShowInFileBrowser, false},
                              {ids::kGroupAllImages, true},
                              {ids::kSplitAllImages, true},
                              {ids::kSelectMask, true},
                              {ids::kRemoveMask, false},
                              {ids::kInvertMask, false, false},
                              {ids::kEnableMask, false, false},
                              {ids::kShowMask, false, false}});
        mono.menus = imageMenus(true, false);
        mono.menus.push_back({"menuRendering_Mode", false});
        scenarios.push_back(std::move(mono));
    }
    {
        Scenario colour{.name = "a still UInt16 image of three channels",
                        .make = [this]() { (void)activeViewOf<std::uint16_t>("colour", true); }};
        append(colour.actions, imageActions(true));
        append(colour.actions, colourActions(true));
        append(colour.actions, {{ids::kConvertToUnsigned16Bit, false}, {ids::kConvertTo32BitFloat, true}});
        colour.menus = imageMenus(true, false, true);
        scenarios.push_back(std::move(colour));
    }
    {
        Scenario ser{.name = "a frame of a SER sequence",
                     .make = [this]() { (void)activeViewOf<std::uint16_t>("ser", false, an::SequenceFormat::Ser); }};
        append(ser.actions, {{ids::kConvertToSER, false},
                             {ids::kConvertToImageList, true},
                             {ids::kExport, true},
                             {ids::kConvertToFITS, false},
                             {ids::kConvertToSigned16Bit, false},
                             {ids::kConvertToSigned32Bit, false},
                             {ids::kConvertToUnsigned32Bit, false},
                             {ids::kConvertTo32BitFloat, false},
                             {ids::kConvertTo64BitFloat, false},
                             {ids::kConvertToUnsigned16Bit, false},
                             {ids::kConvertTo8Bit, true},
                             {ids::kPlateSolve, false}});
        ser.menus = imageMenus(true, true);
        scenarios.push_back(std::move(ser));
    }
    {
        Scenario list{.name = "a frame of an image list",
                      .make = [this]() { (void)activeViewOf<float>("list", false, an::SequenceFormat::ImageList); }};
        append(list.actions, {{ids::kConvertToSER, true},
                              {ids::kConvertToImageList, false},
                              {ids::kExport, true},
                              {ids::kConvertToFITS, false},
                              {ids::kConvertToSigned16Bit, true},
                              {ids::kConvertTo32BitFloat, false}});
        list.menus = imageMenus(true, true);
        scenarios.push_back(std::move(list));
    }
    {
        Scenario masked{.name = "a masked image", .make = [this]() {
                            ImageViewControl* const mask = activeViewOf<float>("mask");
                            ImageViewControl* const image = activeViewOf<float>("masked");
                            ASSERT_NE(mask, nullptr);
                            ASSERT_NE(image, nullptr);
                            image->setCurrentMask(mask);
                            m_core.viewService().setActiveImageViewControl(image);
                        }};
        masked.actions = {
            {ids::kSelectMask, true},          {ids::kRemoveMask, true},     {ids::kInvertMask, true, false},
            {ids::kEnableMask, true, true},    {ids::kShowMask, true, true}, {ids::kOverlayRed, true, true},
            {ids::kOverlayGreen, true, false}, {ids::kReplace, true, false}, {ids::kMultiply, true, false}};
        masked.menus = {{"menuRendering_Mode", true}, {"menuMask", true}};
        scenarios.push_back(std::move(masked));
    }
    {
        Scenario maximized{.name = "a maximized view on screen", .make = [this]() {
                               (void)activeViewOf<float>("maximized");
                               m_maximizedViewActive = true;
                           }};
        maximized.actions = {{ids::kGroupAllImages, false}, {ids::kSplitAllImages, false}, {ids::kSaveAs, true}};
        scenarios.push_back(std::move(maximized));
    }

    for (const Scenario& scenario : scenarios)
    {
        check(scenario);
    }
}

// The information actions follow what the active view's own toolbar offers, and Show in File Browser the file the image
// names: none for an image a process made, offered once the image names its file, and none again with no view.
TEST_F(ActionStateTest, TheInformationActionsFollowTheViewAndShowInFileBrowserItsFile)
{
    ImageViewControl* const view = activeViewOf<float>("made");
    ASSERT_NE(view, nullptr);
    m_coordinator->updateActiveImageActions();
    EXPECT_EQ(m_catalogue.action(ids::kStatistics)->isEnabled(), view->isStatisticsActionEnabled());
    EXPECT_EQ(m_catalogue.action(ids::kHistogram)->isEnabled(), view->isHistogramActionEnabled());
    EXPECT_EQ(m_catalogue.action(ids::kFITSHeader)->isEnabled(), view->isFitsHeaderActionEnabled());
    EXPECT_EQ(m_catalogue.action(ids::kImageEXIFViewer)->isEnabled(), view->isExifActionEnabled());
    EXPECT_FALSE(m_catalogue.action(ids::kShowInFileBrowser)->isEnabled()) << "an image that has never been a file";

    an::ui::DocumentNames names = view->document().names();
    names.sourcePath = QStringLiteral("/somewhere/saved.fits");
    view->document().setNames(std::move(names));
    m_coordinator->updateActiveImageActions();
    EXPECT_TRUE(m_catalogue.action(ids::kShowInFileBrowser)->isEnabled()) << "an image that names its file";

    m_core.viewService().removeViewControl(view, true);
    m_coordinator->updateActiveImageActions();
    EXPECT_FALSE(m_catalogue.action(ids::kShowInFileBrowser)->isEnabled()) << "no view again";
    an::tests::shellcore::settle();
}

// Undo and Redo are labelled with the steps the view's history names; a view with no step offers neither, and with no
// view Image History is not offered either.
TEST_F(ActionStateTest, UndoAndRedoFollowTheViewsHistory)
{
    m_coordinator->updateActiveImageHistory();
    EXPECT_FALSE(m_catalogue.action(ids::kImageHistory)->isEnabled());
    EXPECT_FALSE(m_catalogue.action(ids::kUndo)->isEnabled());

    ImageViewControl* const view = activeViewOf<float>("history");
    ASSERT_NE(view, nullptr);
    m_coordinator->updateActiveImageHistory();
    EXPECT_TRUE(m_catalogue.action(ids::kImageHistory)->isEnabled());
    EXPECT_FALSE(m_catalogue.action(ids::kUndo)->isEnabled());
    ASSERT_NE(view->viewHistory(), nullptr);
    ASSERT_TRUE(view->viewHistory()->registerShownState("Brighten", "made brighter"));
    m_coordinator->updateActiveImageHistory();
    EXPECT_TRUE(m_catalogue.action(ids::kUndo)->isEnabled());
    EXPECT_EQ(m_catalogue.action(ids::kUndo)->text(), QStringLiteral("Undo <Brighten>"));
    EXPECT_FALSE(m_catalogue.action(ids::kRedo)->isEnabled());
    an::tests::shellcore::settle();
}

// The action of an extension's process whose environment is still to be prepared follows the guards its launch meets
// once the environment is ready - its launch prepares it - and says why the environment is not ready; one whose
// environment cannot be prepared is disabled with its reason.
TEST_F(ActionStateTest, AnExtensionsActionThatCanBePreparedFollowsItsLaunchGuards)
{
    using an::ui::process_registry::ProcessAvailability;
    using an::ui::process_registry::ProcessLaunchMode;

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

    QObject owner;
    const auto actionFor = [&owner](const std::string& name) {
        auto* action = new QAction(QString::fromStdString(name), &owner);
        action->setProperty("anRegistryDescriptorId", QString::fromStdString("extensions." + name));
        return action;
    };
    QAction* const preparableWindow = actionFor("preparable_window");
    QAction* const preparableImage = actionFor("preparable_image");
    QAction* const incompatible = actionFor("incompatible");

    ActionStateCoordinator::Dependencies deps;
    deps.actions = m_catalogue.lookup();
    deps.timerContext = &m_owner;
    deps.registryActions = [&]() { return std::vector<QAction*>{preparableWindow, preparableImage, incompatible}; };
    const ActionStateCoordinator coordinator(std::move(deps));
    coordinator.updateRegistryProcessActionStates(nullptr);

    EXPECT_TRUE(preparableWindow->isEnabled());
    EXPECT_EQ(preparableWindow->toolTip(), QStringLiteral("Reason of preparable_window"));
    EXPECT_FALSE(preparableImage->isEnabled()) << "it needs an image to open on";
    EXPECT_EQ(preparableImage->toolTip(), QStringLiteral("No selected image to process"));
    EXPECT_FALSE(incompatible->isEnabled());
    EXPECT_EQ(incompatible->toolTip(), QStringLiteral("Reason of incompatible"));
}

// The Filters and Noise Reduction menus are offered and withdrawn with the other image actions.
TEST_F(ActionStateTest, TheFiltersMenuFollowsTheImageActions)
{
    m_coordinator->setEnableImageActions(false);
    EXPECT_FALSE(m_menus.at("menuFilters"));
    EXPECT_FALSE(m_menus.at("menuNoise_Reduction"));
    m_coordinator->setEnableImageActions(true);
    EXPECT_TRUE(m_menus.at("menuFilters"));
    EXPECT_TRUE(m_menus.at("menuNoise_Reduction"));
}

// The two questions the coordinator asks first: whether the image actions are withdrawn - with no active view or no
// image in it - and whether the mask rendering menu is offered - for a masked image.
TEST(ActionState, WithdrawsTheImageActionsWithoutAnImageAndOffersRenderingForAMask)
{
    EXPECT_TRUE(an::ui::shell::shouldDisableImageActions(false, false));
    EXPECT_TRUE(an::ui::shell::shouldDisableImageActions(true, false));
    EXPECT_FALSE(an::ui::shell::shouldDisableImageActions(true, true));
    EXPECT_TRUE(an::ui::shell::shouldEnableMaskRenderingMenu(true));
    EXPECT_FALSE(an::ui::shell::shouldEnableMaskRenderingMenu(false));
}
