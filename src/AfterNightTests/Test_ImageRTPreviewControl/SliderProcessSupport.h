// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the cases of a preview's frames share: a real-time preview process with one slider, made through the
/// framework's `INIT_SPINBOX_CTRL` as every process dialog makes its sliders, whose body scales every sample of its
/// source by the slider's value and records every run - its kind, its frame kind, the threads admission granted it, the
/// value it ran with and what it was handed - and can wait at a gate or fail with a message the case states; the
/// process may declare its frames reducible or run at full scale over a part of the target, its body a map of each
/// channel alone or the mean of a window around each sample, and show the dialog's histogram; and target views of a
/// flat grey image and of larger gradients.
#pragma once

#include "Test_ControlsProcessFramework/ProcessFrameworkTestSupport.h"

#include "controls/QSliderSpinBox.h"
#include "widgets/ImageViewWidget.h"
#include "widgets/waitingspinnerwidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>
#include <limits>
#include <type_traits>
#include <variant>

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace an::tests::processes::frames
{

/// A slider process's parameters.
struct SliderParams
{
    /// The slider's value; the body scales its source by one plus a hundredth of it.
    int value = 0;

    /// What the body fails with; empty for a body that succeeds.
    std::string failure;
};

/// One run a slider process's body saw.
struct SliderRun
{
    an::ui::ProcessRunKind kind = an::ui::ProcessRunKind::Apply;
    an::ui::PreviewFrameKind frameKind = an::ui::PreviewFrameKind::Settled;

    /// The threads admission granted the run.
    std::uint32_t granted = 0u;

    /// The slider's value the run was issued with.
    int value = 0;

    /// What the run said of its source: the reduction of the target it is, whether it is a part of it, its size and
    /// the identity of the asset it was read from.
    std::uint32_t proxyFactor = 1u;
    bool partOfTarget = false;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint64_t sourceIdentity = 0u;
};

/// The samples of a preview's result, as the framework handed it on once the body, the modulation and the mask were
/// done: every channel's, in row order, as doubles.
struct SliderResult
{
    an::ui::PreviewFrameKind frameKind = an::ui::PreviewFrameKind::Settled;
    std::uint32_t proxyFactor = 1u;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t channels = 0u;
    std::vector<double> samples;
};

/// How a slider process is set up.
struct SliderProcessOptions
{
    /// Whether a value of zero is the process's identity, which a preview then runs nothing for.
    bool identityAtZero = false;

    /// Whether the process shows the waiting spinner while a preview runs.
    bool spinner = false;

    /// Whether the process declares its interactive frames may run over what the preview view paints of the target.
    bool decimated = false;

    /// Whether the process declares its body maps each channel's samples alone.
    bool channelsAlone = false;

    /// Whether the process declares its body reads at full scale and nothing beyond the reach it states, so an
    /// interactive frame runs over the part of the target the preview view shows; it overrides `decimated`.
    bool nativeRegion = false;

    /// How many pixels beyond the part of the target the preview view shows the process declares an interactive frame
    /// at full scale reads around it; zero for a body that reads each pixel alone.
    std::uint32_t regionMargin = 0u;

    /// How far around each sample the body reads: it takes the mean of the samples within this many pixels each way,
    /// the window cut at the image's edge, before scaling it. Zero, the default, reads each sample alone.
    std::uint32_t bodyReach = 0u;

    /// Whether the process shows the dialog's histogram beside its preview.
    bool histogram = false;

    /// Whether the process offers a channel selector, which the body runs through as a process dialog's does.
    bool channels = false;

    /// The size of the preview area the process opens with, or an empty size for none.
    QSize previewArea{};
};

/// Writes every sample of a plane the scope hands over as the mean of the samples within `reach` pixels of it each
/// way - the window cut at the image's edge, so a sample near the edge is the mean of fewer - times a factor, rounded
/// to the representation as `scalePlane` rounds. Each window is summed along its rows and then down its column, in one
/// order wherever it lies, so a sample whose window lies inside a part of the image is the same whether the part or the
/// whole image is handed over.
inline an::Status meanOfWindowsPlane(const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output,
                                     std::uint32_t reach, double factor)
{
    const an::SampleType type = input.descriptor->sampleType();
    const bool dispatched = an::dispatchSampleType(type, [&]<typename SampleT>() {
        const an::ImageLayout& in = *input.layout;
        const an::ImageLayout& out = *output.layout;
        const std::int64_t width = in.extent().width();
        const std::int64_t height = in.extent().height();
        const auto r = static_cast<std::int64_t>(reach);
        std::vector<double> rowSums(static_cast<std::size_t>(width * height));
        for (std::uint32_t channel = 0u; channel < in.extent().channelCount(); ++channel)
        {
            const std::uint32_t inPlane = in.samplesPerPixel() == 1u ? channel : 0u;
            const std::uint32_t outPlane = out.samplesPerPixel() == 1u ? channel : 0u;
            const std::int64_t inStep = in.samplesPerPixel();
            const std::int64_t outStep = out.samplesPerPixel();
            const std::int64_t inOffset = in.samplesPerPixel() == 1u ? 0 : channel;
            const std::int64_t outOffset = out.samplesPerPixel() == 1u ? 0 : channel;
            for (std::int64_t y = 0; y < height; ++y)
            {
                const auto* source =
                    reinterpret_cast<const SampleT*>(static_cast<const std::byte*>(input.samples) +
                                                     *in.rowByteOffset(inPlane, static_cast<std::uint32_t>(y)));
                for (std::int64_t x = 0; x < width; ++x)
                {
                    double sum = 0.0;
                    for (std::int64_t at = (std::max)(std::int64_t{0}, x - r); at <= (std::min)(width - 1, x + r); ++at)
                    {
                        sum += static_cast<double>(source[at * inStep + inOffset]);
                    }
                    rowSums[static_cast<std::size_t>(y * width + x)] = sum;
                }
            }
            for (std::int64_t y = 0; y < height; ++y)
            {
                auto* destination =
                    reinterpret_cast<SampleT*>(static_cast<std::byte*>(output.samples) +
                                               *out.rowByteOffset(outPlane, static_cast<std::uint32_t>(y)));
                const std::int64_t top = (std::max)(std::int64_t{0}, y - r);
                const std::int64_t bottom = (std::min)(height - 1, y + r);
                for (std::int64_t x = 0; x < width; ++x)
                {
                    double sum = 0.0;
                    for (std::int64_t row = top; row <= bottom; ++row)
                    {
                        sum += rowSums[static_cast<std::size_t>(row * width + x)];
                    }
                    const std::int64_t columns = (std::min)(width - 1, x + r) - (std::max)(std::int64_t{0}, x - r) + 1;
                    const double mean = sum / static_cast<double>(columns * (bottom - top + 1));
                    destination[x * outStep + outOffset] =
                        an::convertSample<double, SampleT>(mean * factor,
                                                           an::ConversionPolicy{.domain = an::DomainMapping::CodeValue})
                            .value;
                }
            }
        }
    });
    return dispatched ? an::Status::success()
                      : an::Status::failure(an::ErrorCode::Unsupported, "not one of the eight representations");
}

/// A real-time preview process with one slider, which its constructor registers through the framework's macro.
class SliderProcess final : public an::ui::TypedProcessControl<SliderParams>
{
public:
    SliderProcess(ImageViewControl* target, const SliderProcessOptions& options)
        : TypedProcessControl<SliderParams>(target, "Slider Scale"), m_options(options)
    {
        auto* mainLayout = new QHBoxLayout(this);
        auto* splitter = new QSplitter(Qt::Horizontal);
        splitter->setObjectName(QStringLiteral("mainSplitter"));
        auto* controlsPanel = new QWidget(splitter);
        auto* controlsLayout = new QVBoxLayout(controlsPanel);
        auto* previewPanel = new QWidget(splitter);
        auto* previewLayout = new QVBoxLayout(previewPanel);
        mainLayout->addWidget(splitter);

        previewCheckbox = new QCheckBox(QStringLiteral("Preview"), controlsPanel);
        controlsLayout->addWidget(previewCheckbox);
        spinBox = new QSpinBox(controlsPanel);
        spinBox->setRange(0, 100);
        controlsLayout->addWidget(spinBox);
        slider = new QSlider(Qt::Horizontal, controlsPanel);
        controlsLayout->addWidget(slider);
        buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close | QDialogButtonBox::Reset,
                                       controlsPanel);
        controlsLayout->addWidget(buttons);
        if (options.channels)
        {
            channelSelector = new QComboBox(controlsPanel);
            controlsLayout->addWidget(channelSelector);
        }
        if (options.histogram)
        {
            histogramArea = new QScrollArea(controlsPanel);
            histogramArea->setMinimumHeight(80);
            controlsLayout->addWidget(histogramArea);
            plotResolution = new QComboBox(controlsPanel);
            controlsLayout->addWidget(plotResolution);
        }

        INIT_SPINBOX_CTRL(m_control, spinBox, slider, &SliderProcess::onValueChanged);

        ProcessInitOptions init;
        init.showWaitingSpinner = options.spinner;
        init.supportPerChannel = options.channels;
        initializeStandardProcess(mainLayout, previewLayout, previewCheckbox, buttons, nullptr, channelSelector, init);
        if (options.histogram)
        {
            const auto button = [controlsPanel, controlsLayout]() {
                auto* made = new QToolButton(controlsPanel);
                controlsLayout->addWidget(made);
                return made;
            };
            setupHistogramPreview(histogramArea, plotResolution, button(), button(), button(), button(), button(),
                                  button(), nullptr, nullptr);
        }
        setupPreviewControl(!options.previewArea.isEmpty(), options.previewArea);
    }

    /// Chooses what the channel selector runs the body on, which asks for a preview as a change does.
    ///
    /// @return whether the selector offers the scope.
    bool chooseChannel(an::ChannelScope scope)
    {
        const int index =
            channelSelector != nullptr ? channelSelector->findText(an::ui::processChannelLabel(scope)) : -1;
        if (index < 0)
        {
            return false;
        }
        channelSelector->setCurrentIndex(index);
        return true;
    }

    // ---- What the case steers and reads.

    /// Makes every body fail with a message, or succeed for an empty one, and asks for a preview, as a change does.
    void setFailure(const std::string& failure)
    {
        m_failure = failure;
        settingsUpdated();
    }

    /// The gate every body waits at.
    Gate gate;

    /// The runs every body saw, in the order they started.
    [[nodiscard]] std::vector<SliderRun> runs() const
    {
        const std::lock_guard lock(m_runsLock);
        return m_runs;
    }

    /// When each preview started on the GUI thread, in order.
    [[nodiscard]] const std::vector<std::chrono::steady_clock::time_point>& previewStarts() const
    {
        return m_starts;
    }

    /// Whether the waiting spinner spins now.
    [[nodiscard]] bool spinnerSpinning() const
    {
        return m_ImagePreviewSpinner != nullptr && m_ImagePreviewSpinner->isSpinning();
    }

    /// Every preview's result, as the framework handed it on, in the order they were made.
    [[nodiscard]] std::vector<SliderResult> previewResults() const
    {
        const std::lock_guard lock(m_runsLock);
        return m_previews;
    }

    /// The dialog's histogram, when the process shows one.
    [[nodiscard]] an::ui::IPreviewHistogram* histogram() const
    {
        return m_HistogramPreview.get();
    }

    [[nodiscard]] an::ui::PreviewScaling previewScaling() const override
    {
        if (m_options.nativeRegion)
        {
            return an::ui::PreviewScaling::NativeRegion;
        }
        return m_options.decimated ? an::ui::PreviewScaling::Decimated : an::ui::PreviewScaling::FullOnly;
    }

    [[nodiscard]] bool mapsEachChannelAlone() const override
    {
        return m_options.channelsAlone;
    }

    [[nodiscard]] std::uint32_t typedPreviewRegionMargin(const SliderParams&) const override
    {
        return m_options.regionMargin;
    }

    QCheckBox* previewCheckbox = nullptr;
    QSpinBox* spinBox = nullptr;
    QSlider* slider = nullptr;
    QDialogButtonBox* buttons = nullptr;
    QComboBox* channelSelector = nullptr;
    QScrollArea* histogramArea = nullptr;
    QComboBox* plotResolution = nullptr;

protected:
    [[nodiscard]] bool isPreviewEnabled() const override
    {
        return previewCheckbox != nullptr && previewCheckbox->isChecked();
    }

    void enablePreview(bool enabled) override
    {
        if (previewCheckbox != nullptr)
        {
            const QSignalBlocker blocker(previewCheckbox);
            previewCheckbox->setChecked(enabled);
        }
    }

    void settingsUpdatedImpl() override {}

    /// As a process dialog's reset does: the slider back to zero, which asks for a preview, and another asked for.
    void resetSettings() override
    {
        ImageRTPreviewControl::resetSettings();
        m_control->setValue(0);
        if (previewCheckbox->isChecked())
        {
            schedulePreviewProcessExecution();
        }
    }

    [[nodiscard]] SliderParams getParams() const override
    {
        return SliderParams{.value = m_control->getValue(), .failure = m_failure};
    }

    [[nodiscard]] bool isTypedIdentity(const SliderParams& params) const override
    {
        return m_options.identityAtZero && params.value == 0 && params.failure.empty();
    }

    void executeProcessStarted(const RTPreviewMode previewMode, const bool) override
    {
        if (previewMode == RTPreviewMode::AsyncPreview)
        {
            m_starts.push_back(std::chrono::steady_clock::now());
        }
    }

    void getBatchExecutionParameters(bool& createNewImage, bool& closeAtEnd, bool& executeInBackground,
                                     std::string& newImageName) override
    {
        createNewImage = false;
        closeAtEnd = false;
        executeInBackground = true;
        newImageName = "Slider Scale result";
    }

    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const SliderParams& params,
                                       an::ui::ExecutionScope& scope) override
    {
        {
            const std::lock_guard lock(m_runsLock);
            const std::optional<an::ImageDescriptor> described = source.descriptor();
            m_runs.push_back(SliderRun{.kind = run.kind,
                                       .frameKind = run.frameKind,
                                       .granted = scope.grantedConcurrency(),
                                       .value = params.value,
                                       .proxyFactor = run.proxyFactor,
                                       .partOfTarget = run.partOfTarget,
                                       .width = described.has_value() ? described->extent().width() : 0u,
                                       .height = described.has_value() ? described->extent().height() : 0u,
                                       .sourceIdentity = source.revision().assetIdentity});
        }
        if (!gate.pass([&scope]() { return scope.stopRequested(); }))
        {
            return an::Status::failure(an::ErrorCode::Cancelled, "the slider body was stopped at its gate");
        }
        if (!params.failure.empty())
        {
            return an::Status::failure(an::ErrorCode::InvalidData, params.failure);
        }
        return executeTypedProcessUsingChannels(source, destination, run, params, scope);
    }

    an::Status executeTypedProcessIteration(const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output,
                                            const an::ui::ProcessRun&, const SliderParams& params,
                                            an::ui::ExecutionScope&) override
    {
        const double factor = 1.0 + static_cast<double>(params.value) / 100.0;
        return m_options.bodyReach > 0u ? meanOfWindowsPlane(input, output, m_options.bodyReach, factor)
                                        : scalePlane(input, output, factor);
    }

    void imageReadyToRender(const an::ui::ProcessSource& result, const an::ui::ProcessRun& run) override
    {
        if (run.kind != an::ui::ProcessRunKind::Preview)
        {
            return;
        }
        SliderResult kept{.frameKind = run.frameKind, .proxyFactor = run.proxyFactor};
        std::visit(
            [&kept](const auto& typed) {
                const auto view = typed.view();
                const std::optional<an::ImageDescriptor> described = typed.descriptor();
                if (!view.has_value() || !described.has_value())
                {
                    return;
                }
                kept.width = described->extent().width();
                kept.height = described->extent().height();
                kept.channels = described->extent().channelCount();
                for (std::uint32_t channel = 0u; channel < kept.channels; ++channel)
                {
                    for (std::uint32_t y = 0u; y < kept.height; ++y)
                    {
                        for (std::uint32_t x = 0u; x < kept.width; ++x)
                        {
                            const auto* sample = view->sampleAt(channel, x, y, 0u);
                            kept.samples.push_back(sample != nullptr ? static_cast<double>(*sample) : 0.0);
                        }
                    }
                }
            },
            result.image());
        const std::lock_guard lock(m_runsLock);
        m_previews.push_back(std::move(kept));
    }

private:
    void onValueChanged(int)
    {
        settingsUpdated();
    }

    SliderProcessOptions m_options;
    QSliderSpinBoxUPtr m_control;
    std::string m_failure;
    std::vector<std::chrono::steady_clock::time_point> m_starts;
    mutable std::mutex m_runsLock;
    std::vector<SliderRun> m_runs;
    std::vector<SliderResult> m_previews;
};

/// A slider process window, deleted with the case once its runs have landed.
struct SliderProcessDeleter
{
    void operator()(SliderProcess* process) const
    {
        if (process == nullptr)
        {
            return;
        }
        process->gate.open();
        (void)pumpUntil([process]() { return process->isProcessIdle(); });
        delete process;
        QCoreApplication::processEvents();
    }
};
using SliderHandle = std::unique_ptr<SliderProcess, SliderProcessDeleter>;

/// A slider process window over a target, without the delete-on-close the framework sets so the case decides when it
/// goes.
[[nodiscard]] inline SliderHandle openSliderProcess(ImageViewControl& target, const SliderProcessOptions& options = {})
{
    SliderHandle process(new SliderProcess(&target, options));
    process->setAttribute(Qt::WA_DeleteOnClose, false);
    process->resize(800, 500);
    return process;
}

/// Turns the event loop until a window's runs and its preview view's work have all landed.
[[nodiscard]] inline bool settleSlider(const SliderProcess& process)
{
    if (!pumpUntil([&process]() { return process.isProcessIdle(); }))
    {
        return false;
    }
    pumpQuietly();
    return process.isProcessIdle();
}

/// A grey 8 by 6 Float32 target view, every sample a quarter; null when it cannot be made.
[[nodiscard]] inline ViewHandle quarterTarget(const an::consumer::EngineSession& session)
{
    std::optional<an::ui::ImageDocument> document =
        greyDocument(session, 8u, 6u, [](std::uint32_t, std::uint32_t) { return 0.25f; });
    if (!document.has_value())
    {
        return ViewHandle();
    }
    return makeTargetView("target", std::move(*document));
}

/// A 640 by 480 target view, colour or grey, of a gradient in the representation `SampleT`: every sample a fraction of
/// full scale, `(x + 2y + 37c) mod 251` over 250, so the blocks of a reduction average to values few samples hold; null
/// when it cannot be made.
template <typename SampleT>
[[nodiscard]] inline ViewHandle gradientTarget(const an::consumer::EngineSession& session, Channels channels)
{
    std::optional<an::ui::ImageDocument> document =
        makeDocument<SampleT>(session, 640u, 480u, channels, [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            const double fraction = static_cast<double>((x + 2u * y + 37u * c) % 251u) / 250.0;
            if constexpr (std::is_floating_point_v<SampleT>)
            {
                return static_cast<SampleT>(fraction);
            }
            else
            {
                return static_cast<SampleT>(
                    std::lround(fraction * static_cast<double>((std::numeric_limits<SampleT>::max)())));
            }
        });
    if (!document.has_value())
    {
        return ViewHandle();
    }
    return makeTargetView("gradient", std::move(*document));
}

/// Shows a slider process's window, waits for its preview to settle, and zooms its preview view to a scale, waiting
/// again; false when the preview never settles.
[[nodiscard]] inline bool zoomPreview(SliderProcess& process, double scale)
{
    process.show();
    if (!settleSlider(process) || process.previewView() == nullptr || process.previewView()->getViewWidget() == nullptr)
    {
        return false;
    }
    process.previewView()->getViewWidget()->scaleImageView(scale, true, true);
    return settleSlider(process);
}

/// The runs of a kind among `runs`, from the index `from` on.
[[nodiscard]] inline std::vector<SliderRun> runsFrom(const std::vector<SliderRun>& runs, std::size_t from,
                                                     an::ui::ProcessRunKind kind = an::ui::ProcessRunKind::Preview)
{
    std::vector<SliderRun> found;
    for (std::size_t index = from; index < runs.size(); ++index)
    {
        if (runs[index].kind == kind)
        {
            found.push_back(runs[index]);
        }
    }
    return found;
}

} // namespace an::tests::processes::frames
