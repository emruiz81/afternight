// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The launcher's guards, which read the descriptor of the process a launch is for: an image to open on, the channels
/// and the still image the descriptor asks for, a process of the same name already attached, and a process that needs
/// its view to itself on either side. The application suite's guard cases moved here with the launcher; the launcher's
/// members run against the registry the processes library registers, its entry points standing in for the windows.

#include "Test_ProcessesShared/ProcessesSharedTestSupport.h"

#include "app/ProcessLauncher.h"
#include "app/ProcessRegistry.h"

#include <QString>

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace
{

using an::tests::processes::Channels;
using an::tests::processes::InstalledContext;
using an::tests::processes::makeDocument;
using an::tests::processes::makeTargetView;
using an::tests::processes::openProcessContext;
using an::tests::processes::ProcessHandle;
using an::tests::processes::ProcessTestContext;
using an::tests::processes::TestProcess;
using an::tests::processes::TestProcessOptions;
using an::tests::processes::ViewHandle;
using an::tests::processes::shared::openedProcessWindows;
using an::tests::processes::shared::RegistrySnapshot;
using an::ui::process_registry::LaunchGuardInput;
using an::ui::process_registry::ProcessDescriptor;

/// A descriptor of a single-image process of a name, as the registry states one.
[[nodiscard]] ProcessDescriptor descriptorOf(const std::string& processName, bool exclusive = false)
{
    ProcessDescriptor descriptor;
    descriptor.id = "filters.actionTest";
    descriptor.action_object_name = "actionTest";
    descriptor.display_name = "Test";
    descriptor.process_name = processName;
    descriptor.requires_exclusive_access = exclusive;
    return descriptor;
}

/// Counts the notifications a launch reports, by kind, and the titles they carry.
struct Reported
{
    int errors = 0;
    int warnings = 0;
    QString title;
    QString message;

    [[nodiscard]] an::ui::LaunchNotifications notifications()
    {
        an::ui::LaunchNotifications handlers;
        handlers.notifyError = [this](const QString& reportedTitle, const QString& reportedMessage) {
            ++errors;
            title = reportedTitle;
            message = reportedMessage;
        };
        handlers.notifyWarning = [this](const QString& reportedTitle, const QString& reportedMessage) {
            ++warnings;
            title = reportedTitle;
            message = reportedMessage;
        };
        return handlers;
    }
};

/// A launch state over a view that is not there, whose guard input says what the case does.
[[nodiscard]] an::ui::SingleImageLaunchState stateOf(const LaunchGuardInput& input)
{
    an::ui::SingleImageLaunchState state;
    state.activeControl = reinterpret_cast<ImageViewControl*>(0x1);
    state.input = input;
    return state;
}

/// A grey still image of eight by six, or a colour one.
[[nodiscard]] LaunchGuardInput imageInput(int channels = 1)
{
    LaunchGuardInput input;
    input.has_active_image = true;
    input.active_image_channels = channels;
    return input;
}

/// A process window over a view, attached to it under a name, deleted with the case.
[[nodiscard]] ProcessHandle attachProcess(ImageViewControl& view, const std::string& name)
{
    ProcessHandle process(new TestProcess(&view, TestProcessOptions{}, name));
    process->setAttribute(Qt::WA_DeleteOnClose, false);
    return process;
}

/// A view of a grey or colour Float32 image, made the context's active view.
[[nodiscard]] ViewHandle activeView(ProcessTestContext& context, Channels channels = Channels::Grey)
{
    std::optional<an::ui::ImageDocument> document = makeDocument<float>(
        context.engineSession(), 8u, 6u, channels,
        [](std::uint32_t x, std::uint32_t y, std::uint32_t c) { return static_cast<float>(x + y + c) / 32.0f; });
    EXPECT_TRUE(document.has_value());
    ViewHandle view = makeTargetView("target", std::move(*document));
    context.addViewControl(view.get());
    context.setActiveImageViewControl(view.get());
    return view;
}

} // namespace

// ---- The guards, over a descriptor.

// A launch the guards admit opens the process on the view, once, and reports nothing.
TEST(ProcessLaunchGuardsTest, ALaunchTheDescriptorAdmitsOpensTheProcessOnce)
{
    Reported reported;
    int launched = 0;
    const bool admitted =
        an::ui::launchSingleImageProcess(descriptorOf("Test Process"), stateOf(imageInput()), reported.notifications(),
                                         [&launched](ImageViewControl*) { ++launched; });
    EXPECT_TRUE(admitted);
    EXPECT_EQ(launched, 1);
    EXPECT_EQ(reported.errors + reported.warnings, 0);
}

// No image to open on is an error, titled by the process's name; nothing opens.
TEST(ProcessLaunchGuardsTest, NoImageIsAnErrorTitledByTheProcess)
{
    Reported reported;
    int launched = 0;
    const bool admitted =
        an::ui::launchSingleImageProcess(descriptorOf("Test Process"), an::ui::SingleImageLaunchState{},
                                         reported.notifications(), [&launched](ImageViewControl*) { ++launched; });
    EXPECT_FALSE(admitted);
    EXPECT_EQ(launched, 0);
    EXPECT_EQ(reported.errors, 1);
    EXPECT_EQ(reported.title, QStringLiteral("Test Process"));
    EXPECT_EQ(reported.message, QStringLiteral("No selected image to process"));
}

// A process of the same name already attached is a warning; nothing opens.
TEST(ProcessLaunchGuardsTest, TheSameProcessAttachedIsAWarning)
{
    Reported reported;
    LaunchGuardInput input = imageInput();
    input.has_same_process_attached = true;
    input.attached_process_count = 1;
    EXPECT_FALSE(an::ui::launchSingleImageProcess(descriptorOf("Test Process"), stateOf(input),
                                                  reported.notifications(), nullptr));
    EXPECT_EQ(reported.warnings, 1);
    EXPECT_EQ(reported.message, QStringLiteral("An existing process of this type is already running for the target "
                                               "image"));
}

// A process that needs the view to itself is refused while another process is attached, and any process is refused
// while one that needs the view to itself is attached.
TEST(ProcessLaunchGuardsTest, ExclusiveAccessIsHeldOnBothSides)
{
    Reported needsTheView;
    LaunchGuardInput attached = imageInput();
    attached.attached_process_count = 2;
    EXPECT_FALSE(an::ui::launchSingleImageProcess(descriptorOf("Exclusive", true), stateOf(attached),
                                                  needsTheView.notifications(), nullptr));
    EXPECT_EQ(needsTheView.warnings, 1);
    EXPECT_TRUE(needsTheView.message.startsWith(QStringLiteral("This process requires exclusive access")));

    Reported besideOne;
    LaunchGuardInput exclusiveAttached = imageInput();
    exclusiveAttached.attached_process_count = 1;
    exclusiveAttached.has_process_with_exclusive_access_attached = true;
    EXPECT_FALSE(an::ui::launchSingleImageProcess(descriptorOf("Shared"), stateOf(exclusiveAttached),
                                                  besideOne.notifications(), nullptr));
    EXPECT_EQ(besideOne.warnings, 1);
    EXPECT_TRUE(besideOne.message.startsWith(QStringLiteral("An existing process running for the target image "
                                                            "requires exclusive access")));
}

// The channels and the still image a descriptor asks for are guarded too: a colour process refuses a grey image and a
// still-image process a sequence, with a warning.
TEST(ProcessLaunchGuardsTest, TheChannelsAndTheStillImageTheDescriptorAsksForAreGuarded)
{
    ProcessDescriptor colour = descriptorOf("Colour Only");
    colour.source_channel_filter = {3};
    Reported grey;
    EXPECT_FALSE(an::ui::launchSingleImageProcess(colour, stateOf(imageInput(1)), grey.notifications(), nullptr));
    EXPECT_EQ(grey.warnings, 1);
    EXPECT_EQ(grey.message, QStringLiteral("This process requires an RGB image."));
    Reported rgb;
    EXPECT_TRUE(an::ui::launchSingleImageProcess(colour, stateOf(imageInput(3)), rgb.notifications(), nullptr));

    ProcessDescriptor still = descriptorOf("Still Only");
    still.requires_still_image = true;
    LaunchGuardInput sequence = imageInput();
    sequence.active_image_is_sequence = true;
    Reported refused;
    EXPECT_FALSE(an::ui::launchSingleImageProcess(still, stateOf(sequence), refused.notifications(), nullptr));
    EXPECT_EQ(refused.warnings, 1);
    EXPECT_EQ(refused.message, QStringLiteral("This process supports still images only, not sequences."));
}

// A launch with nothing to open is still a launch the guards admitted.
TEST(ProcessLaunchGuardsTest, ALaunchWithNothingToOpenIsStillAdmitted)
{
    Reported reported;
    EXPECT_TRUE(an::ui::launchSingleImageProcess(descriptorOf("Noop"), stateOf(imageInput()), reported.notifications(),
                                                 nullptr));
    EXPECT_EQ(reported.errors + reported.warnings, 0);
}

// ---- The launcher, acting for registered descriptors.

// The launcher acting for a registered process opens it on the active view, under that process's descriptor.
TEST(ProcessLauncherTest, AProcessOpensOnTheActiveViewUnderItsDescriptor)
{
    const RegistrySnapshot snapshot;
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    // The view is made first so the window the launch opens over it is closed, and has gone, before the view does.
    ViewHandle view = activeView(*context);
    const an::tests::processes::shared::ClosesOpenedWindows closesWindows;

    an::ui::ProcessLauncher("filters.actionGaussian_Blur").executeGaussianBlurFilter();
    ASSERT_EQ(openedProcessWindows().size(), 1u);
    EXPECT_EQ(openedProcessWindows()[0].className, "GaussianBlurProcess");
    EXPECT_EQ(openedProcessWindows()[0].view, view.get());
    EXPECT_TRUE(context->messages.isEmpty());
}

// A process window that goes after its application context has gone leaves nothing to unregister from: the order a
// shutdown gives, and the one a case gives that took its context down while a window still waited to be deleted.
TEST(ProcessLauncherTest, AProcessWindowThatGoesAfterItsContextLeavesNothingToUnregisterFrom)
{
    const RegistrySnapshot snapshot;
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    ViewHandle view;
    ProcessHandle process;
    {
        const InstalledContext installed(*context);
        view = activeView(*context);
        process = attachProcess(*view, "Exposure Correction");
    }
    ASSERT_EQ(an::ui::appContext(), nullptr) << "the context is gone before the window is";
    process.reset();
    {
        const InstalledContext installed(*context);
        view.reset();
    }
}

// With no active view the launch is an error titled by the process's own name, and nothing opens.
TEST(ProcessLauncherTest, NoActiveViewIsAnErrorTitledByTheProcess)
{
    const RegistrySnapshot snapshot;
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::tests::processes::shared::ClosesOpenedWindows closesWindows;

    an::ui::ProcessLauncher("tone_brightness.actionExposure").exposureCorrection();
    EXPECT_TRUE(openedProcessWindows().empty());
    EXPECT_EQ(context->messages, QStringList{QStringLiteral("error:Exposure Correction")});
    EXPECT_EQ(context->errorTexts, QStringList{QStringLiteral("No selected image to process")});
}

// A process attached under the name its descriptor states is the same process: a second one is refused with a warning.
TEST(ProcessLauncherTest, TheSameProcessAttachedUnderItsOwnNameIsRefused)
{
    const RegistrySnapshot snapshot;
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::tests::processes::shared::ClosesOpenedWindows closesWindows;
    ViewHandle view = activeView(*context);
    ProcessHandle open = attachProcess(*view, "Exposure Correction");

    an::ui::ProcessLauncher("tone_brightness.actionExposure").exposureCorrection();
    EXPECT_TRUE(openedProcessWindows().empty());
    EXPECT_EQ(context->messages, QStringList{QStringLiteral("warning:Exposure Correction")});

    // Another process beside it opens.
    an::ui::ProcessLauncher("tone_brightness.actionGamma_Correction").gammaCorrection();
    ASSERT_EQ(openedProcessWindows().size(), 1u);
    EXPECT_EQ(openedProcessWindows()[0].className, "GammaCorrectionProcess");
}

// Crop needs its view to itself: it is refused beside another process, and while it is attached no other process
// opens on its view - the attached window answers from its descriptor, as the launcher does.
TEST(ProcessLauncherTest, AProcessThatNeedsTheViewToItselfIsHeldOnBothSides)
{
    const RegistrySnapshot snapshot;
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::tests::processes::shared::ClosesOpenedWindows closesWindows;
    ViewHandle view = activeView(*context);

    {
        ProcessHandle other = attachProcess(*view, "Gaussian Blur");
        an::ui::ProcessLauncher("geometry.actionCrop").executeCrop();
        EXPECT_TRUE(openedProcessWindows().empty());
        EXPECT_EQ(context->messages, QStringList{QStringLiteral("warning:Crop")});
    }

    context->messages.clear();
    ProcessHandle crop = attachProcess(*view, "Crop");
    EXPECT_TRUE(view->hasProcessWithExclusiveAccessAttached());
    an::ui::ProcessLauncher("filters.actionGaussian_Blur").executeGaussianBlurFilter();
    EXPECT_TRUE(openedProcessWindows().empty());
    EXPECT_EQ(context->messages, QStringList{QStringLiteral("warning:Gaussian Blur")});
}

// A colour-only process refuses a grey view, and a still-image process a sequence's view, as the descriptor says.
TEST(ProcessLauncherTest, TheDescriptorsChannelsAndStillImageAreGuarded)
{
    const RegistrySnapshot snapshot;
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    // The views come first so the window the launch opens is closed, and has gone, before either view does.
    ViewHandle grey;
    ViewHandle colour;
    const an::tests::processes::shared::ClosesOpenedWindows closesWindows;

    // The grey view stays open beside the colour one: the combination's window lists every view the context holds.
    grey = activeView(*context, Channels::Grey);
    an::ui::ProcessLauncher("color.actionNBRGB_Combination").executeNBRGBCombination();
    EXPECT_TRUE(openedProcessWindows().empty());
    EXPECT_EQ(context->messages, QStringList{QStringLiteral("warning:NBRGB Combination")});
    context->messages.clear();
    colour = activeView(*context, Channels::Rgb);
    an::ui::ProcessLauncher("color.actionNBRGB_Combination").executeNBRGBCombination();
    ASSERT_EQ(openedProcessWindows().size(), 1u);
    EXPECT_EQ(openedProcessWindows()[0].className, "NBRGBCombinationProcess");
    EXPECT_TRUE(context->messages.isEmpty());
}

// A launcher acting for no registered descriptor opens no single-image process and says so; a top-level window reads
// no descriptor's guards and opens with no view at all.
TEST(ProcessLauncherTest, ALauncherForNoDescriptorOpensNoSingleImageProcess)
{
    const RegistrySnapshot snapshot;
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::tests::processes::shared::ClosesOpenedWindows closesWindows;
    ViewHandle view = activeView(*context);

    an::ui::ProcessLauncher().executeGaussianBlurFilter();
    an::ui::ProcessLauncher("filters.actionUnregistered").executeGaussianBlurFilter();
    EXPECT_TRUE(openedProcessWindows().empty());
    EXPECT_EQ(context->errorCount(), 2);

    context->setActiveImageViewControl(nullptr);
    an::ui::ProcessLauncher().executeImageStacking();
    ASSERT_EQ(openedProcessWindows().size(), 1u);
    EXPECT_EQ(openedProcessWindows()[0].className, "ImageStackingProcess");
    EXPECT_EQ(openedProcessWindows()[0].view, nullptr);
}
