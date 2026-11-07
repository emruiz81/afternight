// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the spatial and noise processes' cases share: a process window over a target view, opened as the launcher opens
/// it and deleted with the case once its runs have landed; its widgets set by their object names, as a person sets
/// them; documents built from the fixture layer's descriptions; and what a view shows, read back in the fixture layer's
/// terms and compared with a behaviour record.
#pragma once

#include "Test_ControlsProcessFramework/ProcessFrameworkTestSupport.h"

#include "fixtures/common/FixtureCatalogue.h"
#include "fixtures/common/FixtureExpectation.h"
#include "fixtures/common/FixtureImage.h"
#include "fixtures/engine/EngineFixtureAdapter.h"

#include "controls/ImageRTPreviewControl.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QRadioButton>
#include <QSpinBox>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace an::tests::processes::spatial
{

namespace fixtures = an::tests::fixtures;

/// How long a case waits for a process's run: a G'MIC command or the patch denoiser over a hundred-pixel frame takes
/// seconds in a Debug build, more than the framework suites' patience allows.
inline constexpr std::chrono::seconds kRunPatience{60};

/// Turns the event loop until a condition holds or the run patience runs out.
[[nodiscard]] inline bool pumpUntilSettled(const std::function<bool()>& condition)
{
    const auto until = std::chrono::steady_clock::now() + kRunPatience;
    while (std::chrono::steady_clock::now() < until)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        if (condition())
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    return condition();
}

/// A process window, deleted with the case once its runs and its preview's work have landed.
struct SpatialProcessDeleter
{
    void operator()(ImageRTPreviewControl* process) const
    {
        if (process == nullptr)
        {
            return;
        }
        (void)pumpUntilSettled([process]() { return process->isProcessIdle(); });
        delete process;
        QCoreApplication::processEvents();
    }
};

/// A process window of a type, owned by the case.
template <typename ProcessT> using SpatialHandle = std::unique_ptr<ProcessT, SpatialProcessDeleter>;

/// A process window over a target, as the launcher's entry point opens one, without the delete-on-close the framework
/// sets so the case decides when it goes.
template <typename ProcessT> [[nodiscard]] SpatialHandle<ProcessT> openSpatial(ImageViewControl& target)
{
    SpatialHandle<ProcessT> process(new ProcessT(&target));
    process->setAttribute(Qt::WA_DeleteOnClose, false);
    process->resize(900, 600);
    return process;
}

/// A widget of a window by its object name; fails the case when there is none.
template <typename WidgetT> [[nodiscard]] WidgetT* widgetNamed(const QWidget& window, const char* name)
{
    auto* widget = window.findChild<WidgetT*>(QString::fromLatin1(name));
    EXPECT_NE(widget, nullptr) << "no widget named " << name;
    return widget;
}

/// Sets a window's spin box of real numbers, as typing a value does.
inline void setReal(const QWidget& window, const char* name, double value)
{
    if (auto* spin = widgetNamed<QDoubleSpinBox>(window, name))
    {
        spin->setValue(value);
    }
}

/// Sets a window's spin box of whole numbers, as typing a value does.
inline void setWhole(const QWidget& window, const char* name, int value)
{
    if (auto* spin = widgetNamed<QSpinBox>(window, name))
    {
        spin->setValue(value);
    }
}

/// Checks or clears a window's check box or radio button, as a click does.
inline void setChecked(const QWidget& window, const char* name, bool checked)
{
    if (auto* button = widgetNamed<QAbstractButton>(window, name))
    {
        button->setChecked(checked);
    }
}

/// Selects a window's combo box entry by its text, as a choice does; fails the case when it offers no such entry.
inline void choose(const QWidget& window, const char* name, const QString& text)
{
    if (auto* combo = widgetNamed<QComboBox>(window, name))
    {
        const int index = combo->findText(text);
        EXPECT_GE(index, 0) << name << " offers no entry " << text.toStdString();
        if (index >= 0)
        {
            combo->setCurrentIndex(index);
        }
    }
}

/// A target view a case makes and lets go: registered with the context as a view the application shows, and taken out
/// of its registry before it is deleted, so a window opened after it never lists a view that is gone.
class TargetView
{
public:
    TargetView(ProcessTestContext& context, const std::string& name, an::ui::ImageDocument&& document)
        : m_context(context), m_view(makeTargetView(name, std::move(document)))
    {
    }

    ~TargetView()
    {
        if (m_view)
        {
            m_context.removeViewControl(m_view.get(), false);
        }
    }

    TargetView(const TargetView&) = delete;
    TargetView& operator=(const TargetView&) = delete;

    [[nodiscard]] ImageViewControl& view() const
    {
        return *m_view;
    }

private:
    ProcessTestContext& m_context;
    ViewHandle m_view;
};

/// A document holding a fixture's samples, described as the engine's fixture adapter describes them; fails the case
/// when it cannot be built.
[[nodiscard]] inline std::optional<an::ui::ImageDocument> fixtureDocument(const an::consumer::EngineSession& session,
                                                                          std::string_view name)
{
    const fixtures::FixtureImage* image = fixtures::fixtureNamed(name);
    EXPECT_NE(image, nullptr) << "no fixture named " << name;
    if (image == nullptr)
    {
        return std::nullopt;
    }
    std::string error;
    const std::optional<fixtures::FixtureSamples> samples = fixtures::materializeFixture(*image, error);
    EXPECT_TRUE(samples.has_value()) << error;
    if (!samples.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::consumer::ImageAsset> asset = fixtures::engine::makeImageAsset(session, *image, *samples);
    EXPECT_TRUE(asset.hasValue()) << asset.status().message();
    if (!asset.hasValue())
    {
        return std::nullopt;
    }
    an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(
        std::move(*asset), an::ui::DocumentNames{.displayName = QString::fromUtf8(std::string(name).c_str())});
    EXPECT_TRUE(document.hasValue()) << document.status().message();
    if (!document.hasValue())
    {
        return std::nullopt;
    }
    return std::move(*document);
}

/// What a document holds now, read under a lease in the fixture layer's terms; fails the case when it cannot be read.
[[nodiscard]] inline std::optional<fixtures::FixtureSamples> documentSamples(const an::ui::ImageDocument& document)
{
    an::Result<an::consumer::AssetReadLease> lease = document.read();
    EXPECT_TRUE(lease.hasValue()) << lease.status().message();
    if (!lease.hasValue())
    {
        return std::nullopt;
    }
    std::string error;
    std::optional<fixtures::FixtureSamples> samples = fixtures::engine::readImageSamples(lease->image(), error);
    EXPECT_TRUE(samples.has_value()) << error;
    return samples;
}

/// This build's name among the behaviour corpus's per-build records: its compiler and its platform, as the capture
/// names the directory a build that answers differently checks its own records into.
[[nodiscard]] inline std::string thisBuildName()
{
    const fixtures::ExpectationProvenance provenance = fixtures::provenanceOfThisBuild({}, {}, {});
    return provenance.compiler.substr(0u, provenance.compiler.find(' ')) + "-" + provenance.platform;
}

/// Every record of one of the behaviour corpus's files, each replaced by this build's own record where the
/// corpus holds one - the answer the same library gave when this compiler built it; fails the case when the common
/// file cannot be read.
[[nodiscard]] inline std::vector<fixtures::Expectation> behaviorRecords(std::string_view file)
{
    std::string error;
    const std::optional<std::filesystem::path> directory = fixtures::expectationDirectory(error);
    EXPECT_TRUE(directory.has_value()) << error;
    if (!directory.has_value())
    {
        return {};
    }
    const std::string name = std::string(file) + ".expectations";
    std::optional<std::vector<fixtures::Expectation>> records =
        fixtures::readExpectationFile(*directory / "behavior" / name, error);
    EXPECT_TRUE(records.has_value()) << error;
    if (!records.has_value())
    {
        return {};
    }
    const std::filesystem::path own = *directory / "behavior" / thisBuildName() / name;
    if (std::filesystem::is_regular_file(own))
    {
        std::optional<std::vector<fixtures::Expectation>> ownRecords = fixtures::readExpectationFile(own, error);
        EXPECT_TRUE(ownRecords.has_value()) << error;
        for (fixtures::Expectation& record : ownRecords.value_or(std::vector<fixtures::Expectation>{}))
        {
            for (fixtures::Expectation& common : *records)
            {
                if (common.id == record.id)
                {
                    common = std::move(record);
                    break;
                }
            }
        }
    }
    return std::move(*records);
}

/// The record of a behavior file with an id; fails the case when there is none.
[[nodiscard]] inline std::optional<fixtures::Expectation> behaviorRecord(std::string_view file, std::string_view id)
{
    for (fixtures::Expectation& record : behaviorRecords(file))
    {
        if (record.id == id)
        {
            return std::move(record);
        }
    }
    ADD_FAILURE() << "no record " << id << " in " << file;
    return std::nullopt;
}

/// One preview a window's body made, as the framework handed it on once the modulation and the mask were done.
struct RecordedFrame
{
    /// Which kind of frame it was.
    an::ui::PreviewFrameKind kind = an::ui::PreviewFrameKind::Settled;

    /// Whether its source was a part of the target, and where in the target that part lies.
    bool partOfTarget = false;
    QRect part;

    /// How many of the target's pixels each way one pixel of its source stood for: more than one over a reduction.
    std::uint32_t proxyFactor = 1u;

    /// Its samples.
    std::optional<fixtures::FixtureSamples> samples;
};

/// What a case reaches of a process window beyond its public face: the history step its parameters describe now, its
/// body run directly under an execution the case admits, a preview area of a size the case chooses, a preview asked for
/// as a change of its parameters asks for one, the reach it states around the part a frame runs over, and the previews
/// its body made.
class ProbeAccess
{
public:
    virtual ~ProbeAccess() = default;

    /// The window itself.
    [[nodiscard]] virtual ImageRTPreviewControl& window() = 0;

    /// The step an Apply with the parameters the window holds now records.
    [[nodiscard]] virtual an::ui::ProcessStep step() = 0;

    /// The window's body over a source and a destination, with the parameters it holds now, as a worker runs it.
    [[nodiscard]] virtual an::Status runBody(const an::ui::ProcessSource& source,
                                             an::ui::ProcessDestination& destination, const an::ui::ProcessRun& run,
                                             an::ui::ExecutionScope& scope) = 0;

    /// Turns the preview area on, centred and of a size: what the preview runs over from then on.
    virtual void usePreviewArea(QSize size) = 0;

    /// The progress dialog an Apply over a still image, or over a sequence, would show; the caller owns it.
    [[nodiscard]] virtual ProgressDialogControl* progressDialog(bool forSequence) = 0;

    /// Asks for a preview as a change of the window's parameters made without a held slider asks for one.
    virtual void askForPreview() = 0;

    /// How far beyond the part of the target a frame over it runs over the body reads, for the parameters the window
    /// holds now.
    [[nodiscard]] virtual std::uint32_t regionMargin() = 0;

    /// The previews the body made since the window opened or `forgetFrames` was called, in the order they were made.
    [[nodiscard]] virtual std::vector<RecordedFrame> recordedFrames() = 0;

    /// Forgets the previews recorded so far.
    virtual void forgetFrames() = 0;
};

/// Reaches the progress dialog a window's Apply would show. The hook is protected where the framework declares it and
/// private where a process overrides it, so it is named through the framework's class, and the call still reaches the
/// process's override. Never constructed.
struct DialogHook : ImageRTPreviewControl
{
    [[nodiscard]] static ProgressDialogControl* of(ImageRTPreviewControl& window, const bool forSequence)
    {
        return (window.*(&DialogHook::getProgressDialogControl))(forSequence);
    }
};

/// A process window a case can probe.
template <typename ProcessT> class Probe final : public ProcessT, public ProbeAccess
{
public:
    using ProcessT::ProcessT;

    [[nodiscard]] ImageRTPreviewControl& window() override
    {
        return *this;
    }

    [[nodiscard]] an::ui::ProcessStep step() override
    {
        const auto params = this->getProcessParamsForExecution();
        return this->describeProcessStep(params.get());
    }

    [[nodiscard]] an::Status runBody(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                     const an::ui::ProcessRun& run, an::ui::ExecutionScope& scope) override
    {
        const auto params = this->getProcessParamsForExecution();
        return this->executeProcessImpl(source, destination, run, params.get(), scope);
    }

    void usePreviewArea(QSize size) override
    {
        this->m_InitiaSubAreaSize = size;
        this->m_rt_preview_sub_area = QRect();
        this->on_rt_preview_area_toggled(false);
        this->on_rt_preview_area_toggled(true);
        this->schedulePreviewProcessExecution();
    }

    [[nodiscard]] ProgressDialogControl* progressDialog(const bool forSequence) override
    {
        return DialogHook::of(*this, forSequence);
    }

    void askForPreview() override
    {
        this->schedulePreviewProcessExecution();
    }

    [[nodiscard]] std::uint32_t regionMargin() override
    {
        const auto params = this->getProcessParamsForExecution();
        return this->previewRegionMargin(params.get());
    }

    [[nodiscard]] std::vector<RecordedFrame> recordedFrames() override
    {
        const std::lock_guard lock(m_framesLock);
        return m_frames;
    }

    void forgetFrames() override
    {
        const std::lock_guard lock(m_framesLock);
        m_frames.clear();
    }

protected:
    void imageReadyToRender(const an::ui::ProcessSource& result, const an::ui::ProcessRun& run) override
    {
        if (run.kind != an::ui::ProcessRunKind::Preview)
        {
            return;
        }
        std::string error;
        RecordedFrame frame{.kind = run.frameKind,
                            .partOfTarget = run.partOfTarget,
                            .part = run.part,
                            .proxyFactor = run.proxyFactor,
                            .samples = fixtures::engine::readImageSamples(result.image(), error)};
        const std::lock_guard lock(m_framesLock);
        m_frames.push_back(std::move(frame));
    }

private:
    std::mutex m_framesLock;
    std::vector<RecordedFrame> m_frames;
};

/// A probed process window, deleted with the case once its runs have landed.
struct ProbedWindow
{
    SpatialHandle<ImageRTPreviewControl> window;
    ProbeAccess* probe = nullptr;

    [[nodiscard]] ImageRTPreviewControl* operator->() const
    {
        return window.get();
    }

    [[nodiscard]] ImageRTPreviewControl& operator*() const
    {
        return *window;
    }
};

/// A probed process window over a target, as the launcher's entry point opens one.
template <typename ProcessT> [[nodiscard]] ProbedWindow openProbed(ImageViewControl& target)
{
    auto* probe = new Probe<ProcessT>(&target);
    probe->setAttribute(Qt::WA_DeleteOnClose, false);
    probe->resize(900, 600);
    return ProbedWindow{.window = SpatialHandle<ImageRTPreviewControl>(probe), .probe = probe};
}

/// How a process reads a sample that is not a finite number in an image that states every sample valid.
enum class NonFiniteReading : std::uint8_t
{
    /// Its arithmetic takes the sample as it is, and the result is what the arithmetic makes of it.
    Computed,

    /// It ranks the finite neighbours and leaves the non-finite sample where it is.
    Excluded,

    /// It refuses the image by name and changes nothing.
    Refused
};

/// One of the nineteen spatial and noise processes, as the cases that hold every one of them to a behavior see it.
struct SpatialProcessSpec
{
    /// The registry's action and the class the launcher opens for it.
    std::string action;
    std::string className;

    /// The name the process attaches to a view under, and whether it needs the view to itself, as its class says.
    std::string name;
    bool exclusive = false;

    /// Opens the window over a target.
    std::function<ProbedWindow(ImageViewControl&)> open;

    /// Sets the window to something other than what it opens with that changes the image and the history step.
    std::function<void(QWidget&)> configure;

    /// Whether the process runs a G'MIC command.
    bool gmic = false;

    /// The operation its result's processing-state cards say left the samples nonlinear; empty for a process that
    /// leaves the state as it was.
    std::string nonlinearOperation{};

    /// How it reads a non-finite sample.
    NonFiniteReading nonFinite = NonFiniteReading::Computed;
};

/// The nineteen, in the registry's order of their actions.
[[nodiscard]] const std::vector<SpatialProcessSpec>& spatialProcesses();

/// The spec of an action; fails the case when there is none.
[[nodiscard]] const SpatialProcessSpec* spatialProcess(std::string_view action);

/// Whether a spec can run here: every process but the G'MIC ones, and those when the build carries the library.
[[nodiscard]] bool runsHere(const SpatialProcessSpec& spec);

/// What running a window's body directly came to: the published asset, or why there is none.
[[nodiscard]] an::Result<an::consumer::ImageAsset> runProbeBody(ProbeAccess& probe,
                                                                const an::consumer::EngineSession& session,
                                                                const an::ui::ImageDocument& document,
                                                                an::ui::ProcessRun run, bool stopFirst = false);

/// Turns the event loop until a process's runs have landed and the view it targets has shown what they published.
[[nodiscard]] inline bool settleRun(const ImageRTPreviewControl& process, const ImageViewControl& target)
{
    if (!pumpUntilSettled([&process]() { return process.isProcessIdle(); }))
    {
        return false;
    }
    if (!pumpUntilSettled([&target]() { return target.isWorkIdle(); }))
    {
        return false;
    }
    pumpQuietly();
    return process.isProcessIdle() && target.isWorkIdle();
}

} // namespace an::tests::processes::spatial
