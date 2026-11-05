// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The two star-profiler windows as a person runs them: the launch through the registry, a detection over a planted
/// field shown in the table, a mask left unapplied, a sequence's shown frame, the image and its history untouched,
/// the dialog's defaults and a profile saved and read back - one the engine's writer wrote among them - a stopped
/// or closed detection, the dialog read when the detection was asked for, every representation, a detection the
/// engine refuses, the curation of the table, the filter and mask windows shown without a modal loop, and the event
/// loop turning while a detection runs.

#include "Test_ProcessesStarProfilers/StarProfilerTestSupport.h"

#include "controls/ImageViewControl.h"
#include "controls/ProfiledStarMaskCreatorControl.h"
#include "controls/StarsFilterWindow.h"

#include <AfterNightEngine/IO/StarProfileFile.h>

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace
{

using namespace an::tests::processes;
using namespace an::tests::processes::stars;
namespace profiler = an::ui::processes;

/// What a case needs to know of each layout.
template <typename WindowT> struct Layout;

template <> struct Layout<StarProfilerProcessV1>
{
    static constexpr const char* kDescriptorId = "star_object.actionStar_Profiler_v1";
    static constexpr const char* kAction = "actionStar_Profiler_v1";
    static constexpr const char* kClassName = "StarProfilerProcessV1";
    static constexpr const char* kProcessName = "StarProfilerV1";
    static constexpr const char* kExtension = "anspv1";
    static constexpr an::StarProfileGeneration kGeneration = an::StarProfileGeneration::Version1;
    static constexpr int kColumns = 13;

    /// Changes the dialog away from its defaults.
    static void change(StarProfilerProcessV1& window)
    {
        window.ui().detectionThresholdValue->setValue(0.3);
        window.ui().roundnessToleranceValue->setValue(4.0);
        window.ui().minSizeValue->setValue(4);
        window.ui().maxSizeValue->setValue(12);
    }

    /// Whether the dialog shows its defaults.
    static bool showsDefaults(StarProfilerProcessV1& window)
    {
        const profiler::FirstProfilerSettings settings = window.settings();
        return settings.detectionThreshold == 0.1 && settings.roundnessTolerance == 2.0 && settings.minimumSize == 2 &&
               settings.maximumSize == 25;
    }

    /// The dialog's recorded parameters in a header, as a threshold.
    static double recordedThreshold(const an::StarProfilePayload& header)
    {
        return std::get<an::StarProfileV1>(header).parameters.detectionThreshold;
    }

    /// Sets the dialog's threshold.
    static void setThreshold(StarProfilerProcessV1& window, double value)
    {
        window.ui().detectionThresholdValue->setValue(value);
    }
};

template <> struct Layout<StarProfilerProcessV2>
{
    static constexpr const char* kDescriptorId = "star_object.actionStar_Profiler_v2";
    static constexpr const char* kAction = "actionStar_Profiler_v2";
    static constexpr const char* kClassName = "StarProfilerProcessV2";
    static constexpr const char* kProcessName = "StarProfiler";
    static constexpr const char* kExtension = "anspv2";
    static constexpr an::StarProfileGeneration kGeneration = an::StarProfileGeneration::Version2;
    static constexpr int kColumns = 15;

    static void change(StarProfilerProcessV2& window)
    {
        window.ui().radiusValue->setValue(8);
        window.ui().thresholdValue->setValue(2.0);
        window.ui().roundnessThresholdValue->setValue(0.3);
        window.ui().convergenceValue->setValue(2);
        window.ui().relaxPSFChecks->setChecked(true);
        window.ui().profileTypeSelector->setCurrentText(QStringLiteral("Moffat"));
    }

    static bool showsDefaults(StarProfilerProcessV2& window)
    {
        const profiler::SecondProfilerSettings settings = window.settings();
        return settings.radius == 5 && settings.sigma == 1.0 && !settings.amplitudeRange && settings.roundness == 0.5 &&
               !settings.roundnessRange && settings.convergence == 1 && !settings.relaxChecks && !settings.moffat &&
               settings.minimumBeta == 1.5 && !settings.useLightness;
    }

    static double recordedThreshold(const an::StarProfilePayload& header)
    {
        return std::get<an::StarProfileV2>(header).parameters.sigma;
    }

    static void setThreshold(StarProfilerProcessV2& window, double value)
    {
        window.ui().thresholdValue->setValue(value);
    }
};

/// A layout whose detection waits at a gate until the case opens it, or until it is stopped.
template <typename WindowT> class GatedProfiler final : public WindowT
{
public:
    GatedProfiler(ImageViewControl* view, std::shared_ptr<Gate> gate) : WindowT(view), m_gate(std::move(gate)) {}

protected:
    [[nodiscard]] profiler::StarProfileWork detectionWork() const override
    {
        profiler::StarProfileWork inner = WindowT::detectionWork();
        std::shared_ptr<Gate> gate = m_gate;
        return [inner, gate](const an::ui::ExecutionScope& scope,
                             const an::ui::ProcessSource& source) -> an::Result<profiler::StarProfileResult> {
            if (!gate->pass([&scope]() { return scope.stopRequested(); }))
            {
                return an::Status::failure(an::ErrorCode::Cancelled,
                                           an::StatusDetail{.message = "stopped at the gate"});
            }
            return inner(scope, source);
        };
    }

private:
    std::shared_ptr<Gate> m_gate;
};

/// A gated layout that lets the progress window Cancel be pressed.
template <typename WindowT> class CancellableProfiler final : public WindowT
{
public:
    CancellableProfiler(ImageViewControl* view, std::shared_ptr<Gate> gate) : WindowT(view), m_gate(std::move(gate)) {}

    /// Presses Cancel on the progress window of the run on its way.
    [[nodiscard]] bool pressCancel()
    {
        ProgressDialogControl* dialog = this->currentProgressDialog();
        if (dialog == nullptr)
        {
            return false;
        }
        dialog->requestCancel();
        return true;
    }

protected:
    [[nodiscard]] profiler::StarProfileWork detectionWork() const override
    {
        std::shared_ptr<Gate> gate = m_gate;
        return [gate](const an::ui::ExecutionScope& scope,
                      const an::ui::ProcessSource&) -> an::Result<profiler::StarProfileResult> {
            if (!gate->pass([&scope]() { return scope.stopRequested(); }))
            {
                return an::Status::failure(an::ErrorCode::Cancelled,
                                           an::StatusDetail{.message = "stopped at the gate"});
            }
            return an::Status::failure(an::ErrorCode::Internal, an::StatusDetail{.message = "gate opened"});
        };
    }

private:
    std::shared_ptr<Gate> m_gate;
};

/// A layout whose detection ignores every stop and answers one star once the gate opens: a run that was already past
/// its last checkpoint when it was superseded.
template <typename WindowT> class StubbornProfiler final : public WindowT
{
public:
    StubbornProfiler(ImageViewControl* view, std::shared_ptr<Gate> gate) : WindowT(view), m_gate(std::move(gate)) {}

protected:
    [[nodiscard]] profiler::StarProfileWork detectionWork() const override
    {
        std::shared_ptr<Gate> gate = m_gate;
        return [gate](const an::ui::ExecutionScope&,
                      const an::ui::ProcessSource&) -> an::Result<profiler::StarProfileResult> {
            (void)gate->pass([]() { return false; });
            profiler::StarProfileResult result;
            an::DetectedStar star;
            star.positionX = 3.5;
            star.positionY = 4.5;
            star.psf.fullWidthX = 3.0;
            star.psf.fullWidthY = 3.0;
            result.stars.push_back(star);
            result.header = an::StarProfileV1{};
            return result;
        };
    }

private:
    std::shared_ptr<Gate> m_gate;
};

/// A layout whose detection is stopped as it is asked for.
template <typename WindowT> class StoppingProfiler final : public WindowT
{
public:
    using WindowT::WindowT;

protected:
    void detectionStarting(std::stop_source& stop) override
    {
        stop.request_stop();
    }
};

template <typename WindowT> class StarProfilerWindowTest : public ProfilerCase
{
public:
    using Traits = Layout<WindowT>;

protected:
    void SetUp() override
    {
        ProfilerCase::SetUp();
        m_field = makeStarField();
    }

public:
    [[nodiscard]] const StarField& field() const
    {
        return m_field;
    }

    [[nodiscard]] ViewHandle target(const std::string& name = "target")
    {
        return fieldView<float>(session(), m_field, fieldMetadata(m_field), name);
    }

private:
    StarField m_field;
};

using FirstProfilerWindowTest = StarProfilerWindowTest<StarProfilerProcessV1>;
using SecondProfilerWindowTest = StarProfilerWindowTest<StarProfilerProcessV2>;

// launch: the registered action opens the layout's window over the active view, attached under its name and needing
// the view to itself.
template <typename WindowT> void theRegisteredActionOpensTheWindow(StarProfilerWindowTest<WindowT>& test)
{
    using Traits = Layout<WindowT>;
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    ViewHandle view = test.target();
    ASSERT_NE(view, nullptr);
    test.context().setActiveImageViewControl(view.get());
    const an::ui::process_registry::ProcessDescriptor* descriptor = shared::registered(Traits::kDescriptorId);
    ASSERT_NE(descriptor, nullptr) << Traits::kDescriptorId;
    EXPECT_EQ(descriptor->action_object_name, Traits::kAction);
    ASSERT_TRUE(descriptor->launch_single_image);
    descriptor->launch_single_image(view.get());
    ASSERT_EQ(shared::openedProcessWindows().size(), 1u);
    EXPECT_EQ(shared::openedProcessWindows()[0].className, Traits::kClassName);
    ASSERT_EQ(test.context().openProcesses.size(), 1u);
    auto* window = dynamic_cast<WindowT*>(test.context().openProcesses.front());
    ASSERT_NE(window, nullptr);
    EXPECT_TRUE(window->isVisible());
    EXPECT_EQ(window->getProcessName(), Traits::kProcessName);
    EXPECT_TRUE(window->requiresExclusiveAccess());
    window->close();
    pumpQuietly();
    test.context().setActiveImageViewControl(nullptr);
}

TEST_F(FirstProfilerWindowTest, TheRegisteredActionOpensTheWindow)
{
    theRegisteredActionOpensTheWindow(*this);
}

TEST_F(SecondProfilerWindowTest, TheRegisteredActionOpensTheWindow)
{
    theRegisteredActionOpensTheWindow(*this);
}

// apply: a detection lands in the table - one row per star, every planted star found at its engine pixel coordinate,
// the count shown, the stars drawn over the view - and says how many it found.
template <typename WindowT> void aDetectionShowsEveryPlantedStarInTheTable(StarProfilerWindowTest<WindowT>& test)
{
    using Traits = Layout<WindowT>;
    ViewHandle view = test.target();
    ASSERT_NE(view, nullptr);
    auto window = openProfiler<WindowT>(*view);
    ASSERT_TRUE(detectAndSettle(*window));
    const an::ui::StarList stars = heldStars(*window);
    EXPECT_EQ(plantedStarsFound(test.field(), stars, 0.1), test.field().stars.size());
    const auto& ui = window->ui();
    EXPECT_EQ(ui.tableWidget->rowCount(), static_cast<int>(stars.size()));
    EXPECT_EQ(ui.tableWidget->columnCount(), Traits::kColumns);
    EXPECT_EQ(ui.detectedStarsLabel->text(), QString::number(stars.size()));
    ASSERT_FALSE(stars.empty());
    EXPECT_EQ(cellText(*ui.tableWidget, 0, 0), QString::number(stars[0].positionX, 'f', 2));
    EXPECT_EQ(cellText(*ui.tableWidget, 0, 1), QString::number(stars[0].positionY, 'f', 2));
    EXPECT_TRUE(ui.saveProfileButton->isEnabled());
    EXPECT_TRUE(ui.createMaskButton->isEnabled());
    EXPECT_TRUE(test.context().messages.contains(QStringLiteral("info:Detect star profile")));
    EXPECT_EQ(test.context().errorCount(), 0);
    if constexpr (std::is_same_v<WindowT, StarProfilerProcessV1>)
    {
        for (std::size_t index = 1u; index < stars.size(); ++index)
        {
            EXPECT_GE(stars[index - 1u].psf.amplitude, stars[index].psf.amplitude) << "the first layout shows the "
                                                                                      "brightest first";
        }
    }
}

TEST_F(FirstProfilerWindowTest, ADetectionShowsEveryPlantedStarInTheTable)
{
    aDetectionShowsEveryPlantedStarInTheTable(*this);
}

TEST_F(SecondProfilerWindowTest, ADetectionShowsEveryPlantedStarInTheTable)
{
    aDetectionShowsEveryPlantedStarInTheTable(*this);
}

// apply-mask: a mask on the target is not applied - the stars are the image's, read unmasked.
template <typename WindowT> void aMaskOnTheTargetIsNotApplied(StarProfilerWindowTest<WindowT>& test)
{
    ViewHandle view = test.target();
    ASSERT_NE(view, nullptr);
    StarField blank = test.field();
    blank.stars.clear();
    blank.background = 0.0;
    std::fill(blank.samples.begin(), blank.samples.end(), 0.0f);
    ViewHandle mask = fieldView<float>(test.session(), blank, an::ImageMetadata{}, "mask");
    ASSERT_NE(mask, nullptr);
    view->setCurrentMask(mask.get());
    view->setEnableMask(true);
    ASSERT_TRUE(view->isMasked());
    auto window = openProfiler<WindowT>(*view);
    ASSERT_TRUE(detectAndSettle(*window));
    EXPECT_EQ(plantedStarsFound(test.field(), heldStars(*window), 0.1), test.field().stars.size());
    window.reset();
    view->removeCurrentMask();
}

TEST_F(FirstProfilerWindowTest, AMaskOnTheTargetIsNotApplied)
{
    aMaskOnTheTargetIsNotApplied(*this);
}

TEST_F(SecondProfilerWindowTest, AMaskOnTheTargetIsNotApplied)
{
    aMaskOnTheTargetIsNotApplied(*this);
}

// sequence: over a sequence the frame the view shows is profiled, as a still image of it is.
template <typename WindowT> void overASequenceTheShownFrameIsProfiled(StarProfilerWindowTest<WindowT>& test)
{
    ViewHandle view =
        fieldSequenceView(test.session(), test.temporaryPath(QStringLiteral("frames.ser")), test.field(), 3u);
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(view->isSequence());
    auto window = openProfiler<WindowT>(*view);
    ASSERT_TRUE(detectAndSettle(*window));
    EXPECT_EQ(plantedStarsFound(test.field(), heldStars(*window), 0.1), test.field().stars.size());
    EXPECT_EQ(test.context().errorCount(), 0);
}

TEST_F(FirstProfilerWindowTest, OverASequenceTheShownFrameIsProfiled)
{
    overASequenceTheShownFrameIsProfiled(*this);
}

TEST_F(SecondProfilerWindowTest, OverASequenceTheShownFrameIsProfiled)
{
    overASequenceTheShownFrameIsProfiled(*this);
}

// history: a detection changes neither the image nor its history - the document's revision stands - and the profile
// is the window's alone.
template <typename WindowT> void aDetectionChangesNeitherTheImageNorItsHistory(StarProfilerWindowTest<WindowT>& test)
{
    ViewHandle view = test.target();
    ASSERT_NE(view, nullptr);
    const an::ui::DocumentRevision before = view->document().revision();
    const std::shared_ptr<const an::consumer::ImageAsset> asset = view->document().asset();
    auto window = openProfiler<WindowT>(*view);
    ASSERT_TRUE(detectAndSettle(*window));
    EXPECT_TRUE(view->document().revision() == before);
    EXPECT_EQ(view->document().asset(), asset);
    EXPECT_FALSE(window->manuallyEdited());
}

TEST_F(FirstProfilerWindowTest, ADetectionChangesNeitherTheImageNorItsHistory)
{
    aDetectionChangesNeitherTheImageNorItsHistory(*this);
}

TEST_F(SecondProfilerWindowTest, ADetectionChangesNeitherTheImageNorItsHistory)
{
    aDetectionChangesNeitherTheImageNorItsHistory(*this);
}

// metadata: the image's metadata is only read; the profile records the dialog it was found with and what it says of
// the frame.
template <typename WindowT> void theProfileRecordsTheDialogAndTheFrame(StarProfilerWindowTest<WindowT>& test)
{
    using Traits = Layout<WindowT>;
    ViewHandle view = test.target();
    ASSERT_NE(view, nullptr);
    auto window = openProfiler<WindowT>(*view);
    Traits::setThreshold(*window, 0.2);
    ASSERT_TRUE(detectAndSettle(*window));
    ASSERT_TRUE(window->profileHeader().has_value());
    EXPECT_DOUBLE_EQ(Traits::recordedThreshold(*window->profileHeader()), 0.2);
    const std::optional<an::StarProfileFile> document = window->currentDocument();
    ASSERT_TRUE(document.has_value());
    EXPECT_EQ(document->generation, Traits::kGeneration);
    std::visit([](const auto& payload) { EXPECT_GT(payload.global.fullWidth, 0.0); }, document->payload);
}

TEST_F(FirstProfilerWindowTest, TheProfileRecordsTheDialogAndTheFrame)
{
    theProfileRecordsTheDialogAndTheFrame(*this);
}

TEST_F(SecondProfilerWindowTest, TheProfileRecordsTheDialogAndTheFrame)
{
    theProfileRecordsTheDialogAndTheFrame(*this);
}

// persistence: a window opens at the dialog's own values, and Reset brings them back after a change.
template <typename WindowT> void aWindowOpensAtItsDefaultsAndResetRestoresThem(StarProfilerWindowTest<WindowT>& test)
{
    using Traits = Layout<WindowT>;
    ViewHandle view = test.target();
    ASSERT_NE(view, nullptr);
    {
        auto window = openProfiler<WindowT>(*view);
        EXPECT_TRUE(Traits::showsDefaults(*window));
        Traits::change(*window);
        EXPECT_FALSE(Traits::showsDefaults(*window));
        window->ui().resetButton->click();
        EXPECT_TRUE(Traits::showsDefaults(*window));
        Traits::change(*window);
    }
    auto again = openProfiler<WindowT>(*view);
    EXPECT_TRUE(Traits::showsDefaults(*again)) << "a window keeps nothing between openings";
}

TEST_F(FirstProfilerWindowTest, AWindowOpensAtItsDefaultsAndResetRestoresThem)
{
    aWindowOpensAtItsDefaultsAndResetRestoresThem(*this);
}

TEST_F(SecondProfilerWindowTest, AWindowOpensAtItsDefaultsAndResetRestoresThem)
{
    aWindowOpensAtItsDefaultsAndResetRestoresThem(*this);
}

// persistence: a profile saved is the profile read back - the same stars at the same positions, the same header,
// nothing moved by half a pixel - in the window's own generation, published whole.
template <typename WindowT> void aSavedProfileIsReadBackAsItWasSaved(StarProfilerWindowTest<WindowT>& test)
{
    using Traits = Layout<WindowT>;
    ViewHandle view = test.target();
    ASSERT_NE(view, nullptr);
    auto window = openProfiler<WindowT>(*view);
    ASSERT_TRUE(detectAndSettle(*window));
    const an::ui::StarList saved = heldStars(*window);
    ASSERT_FALSE(saved.empty());
    const QString path = test.temporaryPath(QStringLiteral("profile.") + Traits::kExtension);
    ASSERT_TRUE(window->saveProfileTo(path));
    ASSERT_TRUE(settleProfiler(*window));
    EXPECT_TRUE(test.context().messages.contains(QStringLiteral("info:Save star profile")));
    EXPECT_TRUE(QFileInfo::exists(path));

    auto reader = openProfiler<WindowT>(*view);
    ASSERT_TRUE(reader->loadProfileFrom(path));
    ASSERT_TRUE(settleProfiler(*reader));
    const an::ui::StarList read = heldStars(*reader);
    ASSERT_EQ(read.size(), saved.size());
    for (std::size_t index = 0u; index < saved.size(); ++index)
    {
        EXPECT_DOUBLE_EQ(read[index].positionX, saved[index].positionX);
        EXPECT_DOUBLE_EQ(read[index].positionY, saved[index].positionY);
        if constexpr (std::is_same_v<WindowT, StarProfilerProcessV1>)
        {
            // A first-generation document holds one width a star: the mean.
            EXPECT_NEAR(read[index].psf.fullWidthX, saved[index].metrics.meanFullWidth, 1.0e-9);
        }
        else
        {
            EXPECT_DOUBLE_EQ(read[index].psf.fullWidthX, saved[index].psf.fullWidthX);
            EXPECT_DOUBLE_EQ(read[index].psf.amplitude, saved[index].psf.amplitude);
        }
    }
    EXPECT_TRUE(test.context().messages.contains(QStringLiteral("info:Load star profile")));
    EXPECT_EQ(reader->ui().tableWidget->rowCount(), static_cast<int>(saved.size()));
}

TEST_F(FirstProfilerWindowTest, ASavedProfileIsReadBackAsItWasSaved)
{
    aSavedProfileIsReadBackAsItWasSaved(*this);
}

TEST_F(SecondProfilerWindowTest, ASavedProfileIsReadBackAsItWasSaved)
{
    aSavedProfileIsReadBackAsItWasSaved(*this);
}

// persistence: a profile of this layout's generation written outside the window - by the engine's writer, over the
// field's planted stars - loads in this window with every star where the document puts it, and the window's own save of
// it reads back the same: nothing is shifted on the way in or on the way out. The other layout's window refuses it by
// name.
template <typename WindowT> void aProfileWrittenElsewhereLoadsAsItWasWritten(StarProfilerWindowTest<WindowT>& test)
{
    using Traits = Layout<WindowT>;
    const QString document = test.writtenProfile(QStringLiteral("planted.") + Traits::kExtension,
                                                 plantedProfile(Traits::kGeneration, test.field()));
    ASSERT_FALSE(document.isEmpty()) << "the profile could not be written";
    std::vector<std::pair<double, double>> written;
    {
        QFile file(document);
        ASSERT_TRUE(file.open(QIODevice::ReadOnly));
        const QJsonObject profile =
            QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("stars_profile")).toObject();
        const bool first = Traits::kGeneration == an::StarProfileGeneration::Version1;
        for (const auto& value : profile.value(QStringLiteral("stars")).toArray())
        {
            const QJsonObject star = value.toObject();
            written.emplace_back(star.value(first ? QStringLiteral("x") : QStringLiteral("xpos")).toDouble(),
                                 star.value(first ? QStringLiteral("y") : QStringLiteral("ypos")).toDouble());
        }
    }
    ASSERT_FALSE(written.empty());

    ViewHandle view = test.target();
    ASSERT_NE(view, nullptr);
    auto window = openProfiler<WindowT>(*view);
    ASSERT_TRUE(window->loadProfileFrom(document));
    ASSERT_TRUE(settleProfiler(*window));
    EXPECT_TRUE(test.context().messages.contains(QStringLiteral("info:Load star profile")));
    const an::ui::StarList loaded = heldStars(*window);
    ASSERT_EQ(loaded.size(), written.size());
    EXPECT_EQ(window->ui().tableWidget->rowCount(), static_cast<int>(written.size()));
    for (std::size_t index = 0u; index < written.size(); ++index)
    {
        EXPECT_DOUBLE_EQ(loaded[index].positionX, written[index].first) << index;
        EXPECT_DOUBLE_EQ(loaded[index].positionY, written[index].second) << index;
    }

    const QString saved = test.temporaryPath(QStringLiteral("saved-again.") + Traits::kExtension);
    ASSERT_TRUE(window->saveProfileTo(saved));
    ASSERT_TRUE(settleProfiler(*window));
    auto reader = openProfiler<WindowT>(*view);
    ASSERT_TRUE(reader->loadProfileFrom(saved));
    ASSERT_TRUE(settleProfiler(*reader));
    const an::ui::StarList again = heldStars(*reader);
    ASSERT_EQ(again.size(), written.size());
    for (std::size_t index = 0u; index < written.size(); ++index)
    {
        EXPECT_DOUBLE_EQ(again[index].positionX, written[index].first) << index;
        EXPECT_DOUBLE_EQ(again[index].positionY, written[index].second) << index;
    }

    using OtherWindowT = std::conditional_t<std::is_same_v<WindowT, StarProfilerProcessV1>, StarProfilerProcessV2,
                                            StarProfilerProcessV1>;
    reader.reset();
    window.reset();
    auto other = openProfiler<OtherWindowT>(*view);
    ASSERT_TRUE(other->loadProfileFrom(document));
    ASSERT_TRUE(settleProfiler(*other));
    EXPECT_TRUE(test.context().messages.contains(QStringLiteral("error:Load Star Profile")));
    EXPECT_TRUE(heldStars(*other).empty());
}

TEST_F(FirstProfilerWindowTest, AProfileWrittenElsewhereLoadsAsItWasWritten)
{
    aProfileWrittenElsewhereLoadsAsItWasWritten(*this);
}

TEST_F(SecondProfilerWindowTest, AProfileWrittenElsewhereLoadsAsItWasWritten)
{
    aProfileWrittenElsewhereLoadsAsItWasWritten(*this);
}

// persistence: a profile of the other generation is refused by name, and the stars the window holds stay.
template <typename WindowT> void aProfileOfTheOtherGenerationIsRefused(StarProfilerWindowTest<WindowT>& test)
{
    using Traits = Layout<WindowT>;
    an::StarProfileFile other;
    if (Traits::kGeneration == an::StarProfileGeneration::Version1)
    {
        other.generation = an::StarProfileGeneration::Version2;
        other.payload = an::StarProfileV2{};
    }
    else
    {
        other.generation = an::StarProfileGeneration::Version1;
        other.payload = an::StarProfileV1{};
    }
    const QString path = test.temporaryPath(QStringLiteral("other.profile"));
    {
        std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(test.session());
        ASSERT_TRUE(scope.has_value());
        const an::Result<an::StarProfileWriteReport> written =
            an::writeStarProfileFile(scope->engine(), scope->execution(),
                                     an::StarProfileWriteRequest{.destinationPath = path.toStdString()}, other);
        ASSERT_TRUE(written.hasValue()) << written.status().message();
    }
    ViewHandle view = test.target();
    ASSERT_NE(view, nullptr);
    auto window = openProfiler<WindowT>(*view);
    ASSERT_TRUE(detectAndSettle(*window));
    const std::size_t held = heldStars(*window).size();
    ASSERT_TRUE(window->loadProfileFrom(path));
    ASSERT_TRUE(settleProfiler(*window));
    EXPECT_TRUE(test.context().messages.contains(QStringLiteral("error:Load Star Profile")));
    EXPECT_EQ(heldStars(*window).size(), held);
}

TEST_F(FirstProfilerWindowTest, AProfileOfTheOtherGenerationIsRefused)
{
    aProfileOfTheOtherGenerationIsRefused(*this);
}

TEST_F(SecondProfilerWindowTest, AProfileOfTheOtherGenerationIsRefused)
{
    aProfileOfTheOtherGenerationIsRefused(*this);
}

// persistence: a read the window refuses supersedes nothing - a detection running when it was asked for is not
// stopped, and lands and publishes its stars as if no read had been asked for.
template <typename WindowT> void aRefusedReadLeavesTheRunningDetection(StarProfilerWindowTest<WindowT>& test)
{
    const QString path = test.temporaryPath(QStringLiteral("malformed.profile"));
    {
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        ASSERT_GT(file.write("{ this is not a star profile"), 0);
    }
    ViewHandle view = test.target();
    ASSERT_NE(view, nullptr);
    auto gate = std::make_shared<Gate>();
    gate->close();
    WindowHandle<GatedProfiler<WindowT>> window(new GatedProfiler<WindowT>(view.get(), gate));
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    ASSERT_TRUE(window->detectStars());
    ASSERT_TRUE(pumpUntil([&gate]() { return gate->arrived() > 0; }));
    ASSERT_TRUE(window->loadProfileFrom(path));
    pumpQuietly();
    EXPECT_TRUE(window->isDetecting()) << "a read not yet accepted leaves the detection running";
    gate->open();
    ASSERT_TRUE(settleProfiler(*window));
    EXPECT_TRUE(test.context().messages.contains(QStringLiteral("error:Load Star Profile")));
    EXPECT_TRUE(test.context().messages.contains(QStringLiteral("info:Detect star profile")))
        << "the detection the refused read did not supersede publishes its stars";
    EXPECT_FALSE(heldStars(*window).empty());
    ASSERT_TRUE(window->profileHeader().has_value());
}

TEST_F(FirstProfilerWindowTest, ARefusedReadLeavesTheRunningDetection)
{
    aRefusedReadLeavesTheRunningDetection(*this);
}

TEST_F(SecondProfilerWindowTest, ARefusedReadLeavesTheRunningDetection)
{
    aRefusedReadLeavesTheRunningDetection(*this);
}

// cancel: a detection whose stop is asked for publishes nothing and says nothing.
template <typename WindowT> void aStoppedDetectionPublishesNothingAndSaysNothing(StarProfilerWindowTest<WindowT>& test)
{
    ViewHandle view = test.target();
    ASSERT_NE(view, nullptr);
    WindowHandle<StoppingProfiler<WindowT>> window(new StoppingProfiler<WindowT>(view.get()));
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    ASSERT_TRUE(detectAndSettle(*window));
    EXPECT_TRUE(heldStars(*window).empty());
    EXPECT_EQ(test.context().errorCount(), 0);
    EXPECT_FALSE(test.context().messages.contains(QStringLiteral("info:Detect star profile")));
    EXPECT_TRUE(window->ui().detectStarsButton->isEnabled());
}

TEST_F(FirstProfilerWindowTest, AStoppedDetectionPublishesNothingAndSaysNothing)
{
    aStoppedDetectionPublishesNothingAndSaysNothing(*this);
}

TEST_F(SecondProfilerWindowTest, AStoppedDetectionPublishesNothingAndSaysNothing)
{
    aStoppedDetectionPublishesNothingAndSaysNothing(*this);
}

// cancel: closing the window while a detection runs stops it - the window goes out of sight at once and closes when
// the run lands, publishing nothing.
template <typename WindowT> void closingTheWindowStopsItsDetection(StarProfilerWindowTest<WindowT>& test)
{
    ViewHandle view = test.target();
    ASSERT_NE(view, nullptr);
    auto gate = std::make_shared<Gate>();
    gate->close();
    WindowHandle<GatedProfiler<WindowT>> window(new GatedProfiler<WindowT>(view.get(), gate));
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    window->show();
    ASSERT_TRUE(window->detectStars());
    ASSERT_TRUE(pumpUntil([&gate]() { return gate->arrived() > 0; }));
    window->close();
    EXPECT_FALSE(window->isVisible());
    ASSERT_TRUE(settleProfiler(*window));
    EXPECT_TRUE(heldStars(*window).empty());
    EXPECT_EQ(test.context().errorCount(), 0);
}

TEST_F(FirstProfilerWindowTest, ClosingTheWindowStopsItsDetection)
{
    closingTheWindowStopsItsDetection(*this);
}

TEST_F(SecondProfilerWindowTest, ClosingTheWindowStopsItsDetection)
{
    closingTheWindowStopsItsDetection(*this);
}

// parameter-snapshot: a detection reads the dialog as it was when it was asked for; a value changed while it runs
// does not reach it.
template <typename WindowT> void aDetectionReadsTheDialogAsItWasWhenAsked(StarProfilerWindowTest<WindowT>& test)
{
    using Traits = Layout<WindowT>;
    ViewHandle view = test.target();
    ASSERT_NE(view, nullptr);
    auto gate = std::make_shared<Gate>();
    gate->close();
    WindowHandle<GatedProfiler<WindowT>> window(new GatedProfiler<WindowT>(view.get(), gate));
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    Traits::setThreshold(*window, 0.15);
    ASSERT_TRUE(window->detectStars());
    ASSERT_TRUE(pumpUntil([&gate]() { return gate->arrived() > 0; }));
    Traits::setThreshold(*window, 0.9);
    gate->open();
    ASSERT_TRUE(settleProfiler(*window));
    ASSERT_TRUE(window->profileHeader().has_value());
    EXPECT_DOUBLE_EQ(Traits::recordedThreshold(*window->profileHeader()), 0.15);
    EXPECT_FALSE(heldStars(*window).empty());
}

TEST_F(FirstProfilerWindowTest, ADetectionReadsTheDialogAsItWasWhenAsked)
{
    aDetectionReadsTheDialogAsItWasWhenAsked(*this);
}

TEST_F(SecondProfilerWindowTest, ADetectionReadsTheDialogAsItWasWhenAsked)
{
    aDetectionReadsTheDialogAsItWasWhenAsked(*this);
}

// The event loop turns while a detection runs: nothing on the GUI thread waits for the worker, so a timer fires, the
// dialog answers and the window can be cleared, which supersedes the detection.
template <typename WindowT> void theEventLoopTurnsWhileADetectionRuns(StarProfilerWindowTest<WindowT>& test)
{
    ViewHandle view = test.target();
    ASSERT_NE(view, nullptr);
    auto gate = std::make_shared<Gate>();
    gate->close();
    WindowHandle<GatedProfiler<WindowT>> window(new GatedProfiler<WindowT>(view.get(), gate));
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    ASSERT_TRUE(window->detectStars());
    EXPECT_TRUE(window->isDetecting());
    EXPECT_FALSE(window->detectStars()) << "a second detection is not started beside the first";
    std::atomic<int> ticks{0};
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&ticks]() { ++ticks; });
    timer.start(1);
    ASSERT_TRUE(pumpUntil([&ticks]() { return ticks.load() >= 5; }));
    window->ui().resetButton->click();
    window->clearProfile();
    EXPECT_FALSE(window->isDetecting());
    gate->open();
    ASSERT_TRUE(settleProfiler(*window));
    EXPECT_TRUE(heldStars(*window).empty()) << "a cleared window shows no detection that was superseded";
}

TEST_F(FirstProfilerWindowTest, TheEventLoopTurnsWhileADetectionRuns)
{
    theEventLoopTurnsWhileADetectionRuns(*this);
}

TEST_F(SecondProfilerWindowTest, TheEventLoopTurnsWhileADetectionRuns)
{
    theEventLoopTurnsWhileADetectionRuns(*this);
}

// representation: every representation is read as fractions of its full scale, so the same field gives the same
// stars and the same amplitudes in each.
template <typename WindowT> void everyRepresentationIsReadAsFractionsOfFullScale(StarProfilerWindowTest<WindowT>& test)
{
    const auto run = [&test](ViewHandle view, const char* name) {
        ASSERT_NE(view, nullptr) << name;
        auto window = openProfiler<WindowT>(*view);
        ASSERT_TRUE(detectAndSettle(*window)) << name;
        const an::ui::StarList stars = heldStars(*window);
        EXPECT_GE(plantedStarsFound(test.field(), stars, 0.25), (test.field().stars.size() * 9u) / 10u) << name;
        for (const an::DetectedStar& star : stars)
        {
            const NearestStar nearest = nearestPlanted(test.field(), star.positionX, star.positionY);
            if (nearest.star != nullptr && nearest.distance < 0.25)
            {
                EXPECT_NEAR(star.psf.amplitude, nearest.star->amplitude, 0.05) << name;
            }
        }
    };
    const an::ImageMetadata metadata = fieldMetadata(test.field());
    run(fieldView<float>(test.session(), test.field(), metadata), "Float32");
    run(fieldView<double>(test.session(), test.field(), metadata), "Float64");
    run(fieldView<std::uint8_t>(test.session(), test.field(), metadata), "UInt8");
    run(fieldView<std::int8_t>(test.session(), test.field(), metadata), "Int8");
    run(fieldView<std::uint16_t>(test.session(), test.field(), metadata), "UInt16");
    run(fieldView<std::int16_t>(test.session(), test.field(), metadata), "Int16");
    run(fieldView<std::uint32_t>(test.session(), test.field(), metadata), "UInt32");
    run(fieldView<std::int32_t>(test.session(), test.field(), metadata), "Int32");
}

TEST_F(FirstProfilerWindowTest, EveryRepresentationIsReadAsFractionsOfFullScale)
{
    everyRepresentationIsReadAsFractionsOfFullScale(*this);
}

TEST_F(SecondProfilerWindowTest, EveryRepresentationIsReadAsFractionsOfFullScale)
{
    everyRepresentationIsReadAsFractionsOfFullScale(*this);
}

// error: a detection the engine refuses - a frame too small to search - says why under "Error running Star Profiler"
// and leaves the image and the window's stars as they were.
template <typename WindowT>
void aDetectionTheEngineRefusesSaysWhyAndChangesNothing(StarProfilerWindowTest<WindowT>& test)
{
    const StarField tiny = makeStarField(16u, 16u);
    ViewHandle view = fieldView<float>(test.session(), tiny, fieldMetadata(tiny));
    ASSERT_NE(view, nullptr);
    const an::ui::DocumentRevision before = view->document().revision();
    auto window = openProfiler<WindowT>(*view);
    ASSERT_TRUE(detectAndSettle(*window));
    EXPECT_TRUE(test.context().messages.contains(QStringLiteral("error:Error running Star Profiler")));
    EXPECT_TRUE(heldStars(*window).empty());
    EXPECT_TRUE(view->document().revision() == before);
    EXPECT_TRUE(window->ui().detectStarsButton->isEnabled());
}

TEST_F(FirstProfilerWindowTest, ADetectionTheEngineRefusesSaysWhyAndChangesNothing)
{
    aDetectionTheEngineRefusesSaysWhyAndChangesNothing(*this);
}

TEST_F(SecondProfilerWindowTest, ADetectionTheEngineRefusesSaysWhyAndChangesNothing)
{
    aDetectionTheEngineRefusesSaysWhyAndChangesNothing(*this);
}

// The table is curated: a column orders it, largest first and then smallest, its header marked; a star deleted and
// a filter applied mark the list edited; Clear asks first and forgets the stars.
template <typename WindowT> void theTableIsOrderedDeletedFilteredAndCleared(StarProfilerWindowTest<WindowT>& test)
{
    ViewHandle view = test.target();
    ASSERT_NE(view, nullptr);
    auto window = openProfiler<WindowT>(*view);
    ASSERT_TRUE(detectAndSettle(*window));
    const std::size_t found = heldStars(*window).size();
    ASSERT_GT(found, 3u);

    window->orderByColumn(0);
    an::ui::StarList stars = heldStars(*window);
    for (std::size_t index = 1u; index < stars.size(); ++index)
    {
        EXPECT_GE(stars[index - 1u].positionX, stars[index].positionX);
    }
    EXPECT_TRUE(window->ui().tableWidget->horizontalHeaderItem(0)->text().endsWith(QStringLiteral("<")));
    window->orderByColumn(0);
    stars = heldStars(*window);
    for (std::size_t index = 1u; index < stars.size(); ++index)
    {
        EXPECT_LE(stars[index - 1u].positionX, stars[index].positionX);
    }

    window->selectStar(1);
    EXPECT_TRUE(window->ui().deleteStarButton->isEnabled());
    const double deleted = stars[1].positionX;
    window->deleteSelectedStar();
    EXPECT_EQ(heldStars(*window).size(), found - 1u);
    EXPECT_TRUE(window->manuallyEdited());
    for (const an::DetectedStar& star : heldStars(*window))
    {
        EXPECT_NE(star.positionX, deleted);
    }

    // A click over the view lands on a drawn star: its row is selected, found by its position.
    window->onStarSelected(2u);
    EXPECT_EQ(window->ui().tableWidget->currentRow(), 2);

    test.context().answerYes = false;
    window->ui().clearProfileButton->click();
    EXPECT_TRUE(test.context().messages.contains(QStringLiteral("question:Clear Star profile list")));
    EXPECT_EQ(heldStars(*window).size(), found - 1u);
    test.context().answerYes = true;
    window->ui().clearProfileButton->click();
    EXPECT_TRUE(heldStars(*window).empty());
    EXPECT_EQ(window->ui().tableWidget->rowCount(), 0);
    EXPECT_FALSE(window->ui().saveProfileButton->isEnabled());
}

TEST_F(FirstProfilerWindowTest, TheTableIsOrderedDeletedFilteredAndCleared)
{
    theTableIsOrderedDeletedFilteredAndCleared(*this);
}

TEST_F(SecondProfilerWindowTest, TheTableIsOrderedDeletedFilteredAndCleared)
{
    theTableIsOrderedDeletedFilteredAndCleared(*this);
}

// The stars filter and the mask window are shown, not run in a modal loop: the call returns at once with the window
// on screen, the event loop keeps turning, and the mask they paint lands in a new view beside the target.
template <typename WindowT> void theFilterAndMaskWindowsAreShownAndNotModal(StarProfilerWindowTest<WindowT>& test)
{
    ViewHandle view = test.target();
    ASSERT_NE(view, nullptr);
    auto window = openProfiler<WindowT>(*view);
    EXPECT_EQ(window->openMaskCreator(), nullptr) << "no mask is painted from no stars";
    EXPECT_TRUE(test.context().messages.contains(QStringLiteral("error:Create Mask")));
    ASSERT_TRUE(detectAndSettle(*window));

    StarsFilterControlBase* filter = window->openStarsFilter();
    ASSERT_NE(filter, nullptr);
    auto* filterWindow = dynamic_cast<QDialog*>(filter);
    ASSERT_NE(filterWindow, nullptr);
    EXPECT_TRUE(filterWindow->isVisible());
    EXPECT_FALSE(filterWindow->isModal());
    filterWindow->close();
    pumpQuietly();

    ProfiledStarMaskCreatorControl* mask = window->openMaskCreator();
    ASSERT_NE(mask, nullptr);
    EXPECT_TRUE(mask->isVisible());
    EXPECT_FALSE(mask->isModal());
    std::atomic<int> ticks{0};
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&ticks]() { ++ticks; });
    timer.start(1);
    ASSERT_TRUE(pumpUntil([&ticks]() { return ticks.load() >= 3; })) << "the event loop turns with the window open";
    const std::size_t before = test.context().madeViews.size();
    ASSERT_TRUE(mask->generateMask());
    ASSERT_TRUE(pumpUntil([&test, before]() { return test.context().madeViews.size() > before; }));
    mask->close();
    pumpQuietly();
}

TEST_F(FirstProfilerWindowTest, TheFilterAndMaskWindowsAreShownAndNotModal)
{
    theFilterAndMaskWindowsAreShownAndNotModal(*this);
}

TEST_F(SecondProfilerWindowTest, TheFilterAndMaskWindowsAreShownAndNotModal)
{
    theFilterAndMaskWindowsAreShownAndNotModal(*this);
}

// Closing a window that holds stars asks first, and a "no" keeps it open with its stars.
template <typename WindowT> void closingAWindowThatHoldsStarsAsksFirst(StarProfilerWindowTest<WindowT>& test)
{
    ViewHandle view = test.target();
    ASSERT_NE(view, nullptr);
    auto window = openProfiler<WindowT>(*view);
    window->show();
    ASSERT_TRUE(detectAndSettle(*window));
    test.context().answerYes = false;
    window->close();
    EXPECT_TRUE(test.context().messages.contains(QStringLiteral("question:Close Star Profiler")));
    EXPECT_TRUE(window->isVisible());
    EXPECT_FALSE(heldStars(*window).empty());
    test.context().answerYes = true;
    window->close();
    pumpQuietly();
    EXPECT_FALSE(window->isVisible());
}

TEST_F(FirstProfilerWindowTest, ClosingAWindowThatHoldsStarsAsksFirst)
{
    closingAWindowThatHoldsStarsAsksFirst(*this);
}

TEST_F(SecondProfilerWindowTest, ClosingAWindowThatHoldsStarsAsksFirst)
{
    closingAWindowThatHoldsStarsAsksFirst(*this);
}

// A click over the view finds the clicked star in the profile by its position: the view may draw another list than the
// table, a filter preview among them, so the drawn index is not the row.
template <typename WindowT> void aClickFindsTheStarByItsPosition(StarProfilerWindowTest<WindowT>& test)
{
    ViewHandle view = test.target();
    ASSERT_NE(view, nullptr);
    auto window = openProfiler<WindowT>(*view);
    ASSERT_TRUE(detectAndSettle(*window));
    an::ui::StarList drawn = heldStars(*window);
    ASSERT_GT(drawn.size(), 3u);
    const std::size_t last = drawn.size() - 1u;
    const double lastX = drawn[last].positionX;
    std::reverse(drawn.begin(), drawn.end());
    window->updateStarSelection(std::make_shared<const an::ui::StarList>(drawn));
    window->onStarSelected(0u);
    const int row = window->ui().tableWidget->currentRow();
    ASSERT_EQ(row, static_cast<int>(last));
    EXPECT_DOUBLE_EQ(heldStars(*window)[static_cast<std::size_t>(row)].positionX, lastX);
}

TEST_F(FirstProfilerWindowTest, AClickFindsTheStarByItsPosition)
{
    aClickFindsTheStarByItsPosition(*this);
}

TEST_F(SecondProfilerWindowTest, AClickFindsTheStarByItsPosition)
{
    aClickFindsTheStarByItsPosition(*this);
}

// The progress window Cancel stops the run, which publishes nothing and says nothing.
template <typename WindowT> void theProgressWindowCancelStopsTheRun(StarProfilerWindowTest<WindowT>& test)
{
    ViewHandle view = test.target();
    ASSERT_NE(view, nullptr);
    auto gate = std::make_shared<Gate>();
    gate->close();
    WindowHandle<CancellableProfiler<WindowT>> window(new CancellableProfiler<WindowT>(view.get(), gate));
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    ASSERT_TRUE(window->detectStars());
    ASSERT_TRUE(pumpUntil([&gate]() { return gate->arrived() > 0; }));
    ASSERT_TRUE(window->pressCancel());
    const bool stopped = pumpUntil([&window]() { return !window->isDetecting(); });
    gate->open();
    ASSERT_TRUE(stopped) << "Cancel did not stop the run";
    ASSERT_TRUE(settleProfiler(*window));
    EXPECT_TRUE(heldStars(*window).empty());
    EXPECT_EQ(test.context().errorCount(), 0);
}

TEST_F(FirstProfilerWindowTest, TheProgressWindowCancelStopsTheRun)
{
    theProgressWindowCancelStopsTheRun(*this);
}

TEST_F(SecondProfilerWindowTest, TheProgressWindowCancelStopsTheRun)
{
    theProgressWindowCancelStopsTheRun(*this);
}

// A detection that lands after a Clear, after a read, or after the window was closed is not the run last asked for and
// publishes nothing, even when it was past its last checkpoint and finished.
template <typename WindowT> void aSupersededDetectionPublishesNothing(StarProfilerWindowTest<WindowT>& test)
{
    ViewHandle view = test.target();
    ASSERT_NE(view, nullptr);
    {
        auto gate = std::make_shared<Gate>();
        gate->close();
        WindowHandle<StubbornProfiler<WindowT>> window(new StubbornProfiler<WindowT>(view.get(), gate));
        window->setAttribute(Qt::WA_DeleteOnClose, false);
        ASSERT_TRUE(window->detectStars());
        ASSERT_TRUE(pumpUntil([&gate]() { return gate->arrived() > 0; }));
        window->clearProfile();
        gate->open();
        ASSERT_TRUE(settleProfiler(*window));
        EXPECT_TRUE(heldStars(*window).empty()) << "a cleared window shows no detection that was superseded";
    }
    {
        auto gate = std::make_shared<Gate>();
        gate->close();
        WindowHandle<StubbornProfiler<WindowT>> window(new StubbornProfiler<WindowT>(view.get(), gate));
        window->setAttribute(Qt::WA_DeleteOnClose, false);
        window->show();
        ASSERT_TRUE(window->detectStars());
        ASSERT_TRUE(pumpUntil([&gate]() { return gate->arrived() > 0; }));
        window->close();
        gate->open();
        ASSERT_TRUE(settleProfiler(*window));
        EXPECT_TRUE(heldStars(*window).empty()) << "a closed window shows no detection";
        EXPECT_FALSE(test.context().messages.contains(QStringLiteral("info:Detect star profile")));
    }
    {
        // A profile read after the detection was asked for supersedes it; the read waits for the admission the older
        // run holds.
        auto saver = openProfiler<WindowT>(*view);
        ASSERT_TRUE(detectAndSettle(*saver));
        const std::size_t saved = heldStars(*saver).size();
        ASSERT_GT(saved, 1u);
        const QString path = test.temporaryPath(QStringLiteral("superseded.profile"));
        ASSERT_TRUE(saver->saveProfileTo(path));
        ASSERT_TRUE(settleProfiler(*saver));
        test.context().messages.clear();

        auto gate = std::make_shared<Gate>();
        gate->close();
        WindowHandle<StubbornProfiler<WindowT>> window(new StubbornProfiler<WindowT>(view.get(), gate));
        window->setAttribute(Qt::WA_DeleteOnClose, false);
        ASSERT_TRUE(window->detectStars());
        ASSERT_TRUE(pumpUntil([&gate]() { return gate->arrived() > 0; }));
        ASSERT_TRUE(window->loadProfileFrom(path));
        gate->open();
        ASSERT_TRUE(settleProfiler(*window));
        EXPECT_FALSE(test.context().messages.contains(QStringLiteral("info:Detect star profile")));
        EXPECT_EQ(heldStars(*window).size(), saved) << "the read stands; the older detection does not replace it";
    }
}

TEST_F(FirstProfilerWindowTest, ASupersededDetectionPublishesNothing)
{
    aSupersededDetectionPublishesNothing(*this);
}

TEST_F(SecondProfilerWindowTest, ASupersededDetectionPublishesNothing)
{
    aSupersededDetectionPublishesNothing(*this);
}

// ---- What only one layout does.

// The first layout's size window keeps only the stars whose mean full width lies inside it, and its threshold only
// those whose amplitude stands above it.
TEST_F(FirstProfilerWindowTest, TheSizeWindowAndTheThresholdHoldTheStars)
{
    ViewHandle view = this->target();
    ASSERT_NE(view, nullptr);
    auto window = openProfiler<StarProfilerProcessV1>(*view);
    window->ui().detectionThresholdValue->setValue(0.45);
    ASSERT_TRUE(detectAndSettle(*window));
    const an::ui::StarList bright = heldStars(*window);
    ASSERT_FALSE(bright.empty());
    for (const an::DetectedStar& star : bright)
    {
        EXPECT_GE(star.psf.amplitude, 0.45);
    }
    EXPECT_LT(bright.size(), field().stars.size());

    window->ui().detectionThresholdValue->setValue(0.1);
    window->ui().minSizeValue->setValue(20);
    window->ui().maxSizeValue->setValue(25);
    ASSERT_TRUE(detectAndSettle(*window));
    EXPECT_TRUE(heldStars(*window).empty()) << "no planted star is twenty pixels wide";
    EXPECT_TRUE(context().messages.contains(QStringLiteral("warning:Detect star profile")));
}

// The second layout places every star on the sky through the image's solution and matches it against the installed
// Gaia catalog; its table shows the coordinates, and its saved document carries them and the matches.
TEST_F(SecondProfilerWindowTest, EveryStarIsPlacedOnTheSkyAndMatchedAgainstTheCatalog)
{
    InstalledCatalog catalog(context());
    ASSERT_TRUE(catalog.install(field()));
    ViewHandle view = this->target();
    ASSERT_NE(view, nullptr);
    auto window = openProfiler<StarProfilerProcessV2>(*view);
    ASSERT_TRUE(detectAndSettle(*window));
    const an::ui::StarList stars = heldStars(*window);
    ASSERT_FALSE(stars.empty());
    std::size_t matched = 0u;
    for (std::size_t row = 0u; row < stars.size(); ++row)
    {
        const profiler::StarEnrichment* enrichment = profiler::enrichmentOf(window->enrichment(), stars[row]);
        ASSERT_NE(enrichment, nullptr);
        ASSERT_TRUE(enrichment->direction.has_value());
        const an::Result<an::SkyCoordinate> expected =
            field().solution->pixelToWorld({.x = stars[row].positionX, .y = stars[row].positionY});
        ASSERT_TRUE(expected.hasValue());
        EXPECT_NEAR(enrichment->direction->rightAscensionDegrees, expected->rightAscensionDegrees, 1.0e-9);
        EXPECT_NE(cellText(*window->ui().tableWidget, static_cast<int>(row), 5), QStringLiteral("-"));
        matched += enrichment->match.has_value() ? 1u : 0u;
    }
    EXPECT_EQ(matched, stars.size());

    const QString path = temporaryPath(QStringLiteral("enriched.anspv2"));
    ASSERT_TRUE(window->saveProfileTo(path));
    ASSERT_TRUE(settleProfiler(*window));
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session());
    ASSERT_TRUE(scope.has_value());
    const an::Result<an::StarProfileFile> read = an::readStarProfileFile(scope->execution(), path.toStdString());
    ASSERT_TRUE(read.hasValue()) << read.status().message();
    const auto& written = std::get<an::StarProfileV2>(read->payload);
    EXPECT_EQ(written.global.catalogMatchedCount, stars.size());
    EXPECT_GT(written.global.limitingMagnitude, 0.0);
    for (const an::StarProfileV2Star& star : written.stars)
    {
        EXPECT_TRUE(star.astrometry.hasWorldCoordinates);
        EXPECT_TRUE(star.astrometry.hasCatalogMatch);
        EXPECT_FALSE(star.astrometry.catalogId.empty());
    }
}

// The dialog offers the detector largest search radius and no more.
TEST_F(SecondProfilerWindowTest, TheRadiusIsHeldToTheDetectorsLargest)
{
    ViewHandle view = this->target();
    ASSERT_NE(view, nullptr);
    auto window = openProfiler<StarProfilerProcessV2>(*view);
    window->ui().radiusValue->setValue(64);
    EXPECT_EQ(window->ui().radiusValue->value(), 32);
    EXPECT_EQ(window->settings().radius, 32);
    EXPECT_EQ(window->ui().radiusSlider->maximum(), 32);
}

/// The second layout with its detection work reachable.
class ExposedSecondProfiler final : public StarProfilerProcessV2
{
public:
    using StarProfilerProcessV2::detectionWork;
    using StarProfilerProcessV2::StarProfilerProcessV2;
};

// A catalog directory that is not ready, a chunk its manifest lists being missing, is not opened: the stars are placed
// and matched against nothing, and the detection reports no catalog failure.
TEST_F(SecondProfilerWindowTest, AnUnreadyCatalogIsNotOpened)
{
    InstalledCatalog catalog(context());
    ASSERT_TRUE(catalog.install(field()));
    ASSERT_TRUE(QFile::remove(QDir(catalog.directory()).filePath(QStringLiteral("gaia_dr3_chunk_00.ansp"))));
    ViewHandle view = this->target();
    ASSERT_NE(view, nullptr);
    ExposedSecondProfiler window(view.get());
    const profiler::StarProfileWork work = window.detectionWork();
    an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
        session(), an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply},
        an::ui::ExecutionObserver{});
    ASSERT_TRUE(scope.hasValue()) << scope.status().message();
    std::shared_ptr<const an::consumer::ImageAsset> asset = view->document().asset();
    ASSERT_NE(asset, nullptr);
    an::Result<an::consumer::AssetReadLease> lease = asset->read();
    ASSERT_TRUE(lease.hasValue());
    const an::Result<profiler::StarProfileResult> result = work(*scope, an::ui::ProcessSource::of(*lease));
    scope->finish(an::Status::success());
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    EXPECT_FALSE(result->catalogFailure.has_value());
    EXPECT_FALSE(result->stars.empty());
}

// An image without a solution is profiled without world coordinates: its table shows "-" and nothing is matched.
TEST_F(SecondProfilerWindowTest, AnUnsolvedImageIsProfiledWithoutWorldCoordinates)
{
    ViewHandle view = fieldView<float>(session(), field(), fieldMetadata(field(), false));
    ASSERT_NE(view, nullptr);
    auto window = openProfiler<StarProfilerProcessV2>(*view);
    ASSERT_TRUE(detectAndSettle(*window));
    ASSERT_FALSE(heldStars(*window).empty());
    EXPECT_TRUE(window->enrichment().empty());
    EXPECT_EQ(cellText(*window->ui().tableWidget, 0, 5), QStringLiteral("-"));
    EXPECT_EQ(cellText(*window->ui().tableWidget, 0, 6), QStringLiteral("-"));
}

// The second layout finds a colour image's stars in its green channel, or in its lightness when asked, and a Moffat
// profile's stars carry their exponent.
TEST_F(SecondProfilerWindowTest, AColourImagesStarsAreFoundInItsGreenOrItsLightness)
{
    const StarField colour = makeStarField(320u, 240u, 3u);
    ViewHandle view = fieldView<float>(session(), colour, fieldMetadata(colour));
    ASSERT_NE(view, nullptr);
    auto window = openProfiler<StarProfilerProcessV2>(*view);
    ASSERT_TRUE(detectAndSettle(*window));
    an::ui::StarList stars = heldStars(*window);
    EXPECT_EQ(plantedStarsFound(colour, stars, 0.1), colour.stars.size());
    ASSERT_FALSE(stars.empty());
    EXPECT_EQ(stars[0].channelIndex, 1u);

    window->ui().useLightness->setChecked(true);
    window->ui().profileTypeSelector->setCurrentText(QStringLiteral("Moffat"));
    EXPECT_TRUE(window->ui().minimumBetaValue->isEnabled());
    ASSERT_TRUE(detectAndSettle(*window));
    stars = heldStars(*window);
    EXPECT_GE(plantedStarsFound(colour, stars, 0.25), (colour.stars.size() * 9u) / 10u);
    ASSERT_FALSE(stars.empty());
    EXPECT_EQ(stars[0].channelIndex, 0u);
    EXPECT_EQ(stars[0].psf.model, an::PsfModel::MoffatFreeBeta);
    EXPECT_TRUE(stars[0].psf.beta.has_value());
}

} // namespace
