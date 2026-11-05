// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A process applied over a sequence through the real sequence companions: every frame of a SER read by the frame
/// reader the framework opens, processed, and written to a SER the run publishes only when it finishes; the view then
/// shows the sequence written, its player following it.

#include "Test_ControlsSequences/SequencesTestSupport.h"

#include "app/FileCompanions.h"
#include "controls/ProcessExecution.h"
#include "controls/SequencePlayerControl.h"
#include "controls/TypedProcessControl.h"

#include <AfterNightEngine/Image/SampleTraits.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QSplitter>
#include <QVBoxLayout>

#include <gtest/gtest.h>

namespace
{

using namespace an::tests::sequences;

/// What the offset process adds.
struct OffsetParams
{
    std::uint16_t offset = 100u;
};

/// A process that adds an offset to every sample of a sixteen-bit frame.
class OffsetProcess final : public an::ui::TypedProcessControl<OffsetParams>
{
public:
    explicit OffsetProcess(ImageViewControl* target) : TypedProcessControl<OffsetParams>(target, "Offset")
    {
        auto* mainLayout = new QHBoxLayout(this);
        auto* splitter = new QSplitter(Qt::Horizontal);
        splitter->setObjectName(QStringLiteral("mainSplitter"));
        auto* controlsPanel = new QWidget(splitter);
        auto* controlsLayout = new QVBoxLayout(controlsPanel);
        auto* previewPanel = new QWidget(splitter);
        auto* previewLayout = new QVBoxLayout(previewPanel);
        mainLayout->addWidget(splitter);
        m_preview = new QCheckBox(QStringLiteral("Preview"), controlsPanel);
        controlsLayout->addWidget(m_preview);
        m_channels = new QComboBox(controlsPanel);
        controlsLayout->addWidget(m_channels);
        m_buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close | QDialogButtonBox::Reset,
                                         controlsPanel);
        controlsLayout->addWidget(m_buttons);
        initializeStandardProcess(mainLayout, previewLayout, m_preview, m_buttons, nullptr, m_channels,
                                  ProcessInitOptions{});
        setupPreviewControl(false, QSize(4, 4), QStringLiteral("Preview area"));
    }

    /// How many frames the body saw.
    std::atomic<int> framesSeen{0};

protected:
    [[nodiscard]] bool isPreviewEnabled() const override
    {
        return false;
    }
    void enablePreview(bool) override {}
    void settingsUpdatedImpl() override {}
    [[nodiscard]] OffsetParams getParams() const override
    {
        return OffsetParams{};
    }
    double getTypedModulationFromParams(const OffsetParams&) override
    {
        return 1.0;
    }
    [[nodiscard]] an::ui::ProcessStep describeTypedProcessStep(const OffsetParams& params) const override
    {
        return an::ui::ProcessStep{.name = "Offset", .description = "offset " + std::to_string(params.offset)};
    }
    ProgressDialogControl* getProgressDialogControl(const bool) override
    {
        return nullptr;
    }
    void getBatchExecutionParameters(bool& createNewImage, bool& closeAtEnd, bool& executeInBackground,
                                     std::string& newImageName) override
    {
        createNewImage = false;
        closeAtEnd = false;
        executeInBackground = true;
        newImageName = "Offset result";
    }
    void executeProcessEndedImpl(const RTPreviewMode, const bool) override {}
    void executeProcessCompletedImpl(const RTPreviewMode, const bool) override {}

    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const OffsetParams& params,
                                       an::ui::ExecutionScope& scope) override
    {
        ++framesSeen;
        return executeTypedProcessUsingChannels(source, destination, run, params, scope);
    }

    an::Status executeTypedProcessIteration(const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output,
                                            const an::ui::ProcessRun&, const OffsetParams& params,
                                            an::ui::ExecutionScope&) override
    {
        if (input.descriptor->sampleType() != an::SampleType::UInt16)
        {
            return an::Status::failure(an::ErrorCode::Unsupported, "the offset process reads sixteen-bit frames");
        }
        const an::ImageLayout& in = *input.layout;
        const an::ImageLayout& out = *output.layout;
        for (std::uint32_t channel = 0u; channel < in.extent().channelCount(); ++channel)
        {
            for (std::uint32_t y = 0u; y < in.extent().height(); ++y)
            {
                const auto* from = reinterpret_cast<const std::uint16_t*>(static_cast<const std::byte*>(input.samples) +
                                                                          *in.rowByteOffset(channel, y));
                auto* to = reinterpret_cast<std::uint16_t*>(static_cast<std::byte*>(output.samples) +
                                                            *out.rowByteOffset(channel, y));
                for (std::uint32_t x = 0u; x < in.extent().width(); ++x)
                {
                    to[x] = static_cast<std::uint16_t>(from[x] + params.offset);
                }
            }
        }
        return an::Status::success();
    }

private:
    QCheckBox* m_preview = nullptr;
    QComboBox* m_channels = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
};

} // namespace

// A process applied over a SER reads every frame through the frame reader, writes every processed frame to a SER the
// run publishes when it finishes, and the view shows the sequence written at the frame it showed, its player following.
TEST(SequenceProcessRunTest, AProcessOverASerWritesEveryFrameToASerTheViewThenShows)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const FixtureSequence fixture{.frames = 3u};
    const QString path = context->pathOf(QStringLiteral("capture.ser"));
    ASSERT_TRUE(writeFixture(context->engineSession(), path, an::SequenceFormat::Ser, fixture));
    an::Result<an::ui::ImageDocument> document =
        an::ui::openSequenceDocument(context->engineSession(), path, an::SequenceFormat::Ser, {});
    ASSERT_TRUE(document.hasValue()) << document.status().message();
    ImageViewControl* view = context->createNewImageViewControlFromSequence(
        nullptr, std::move(*document), nullptr, false, "capture", ViewControlMode::standard, nullptr, false, false);
    ASSERT_NE(view, nullptr);
    auto* player = dynamic_cast<SequencePlayerControl*>(view->sequencePlayer());
    ASSERT_NE(player, nullptr);
    player->setCurrentFrame(1u, {});
    ASSERT_TRUE(pumpUntil([&]() { return player->isIdle() && player->framesShown() == 1u && view->isWorkIdle(); }));

    std::unique_ptr<OffsetProcess> process(new OffsetProcess(view));
    process->setAttribute(Qt::WA_DeleteOnClose, false);
    process->applyProcess();
    ASSERT_TRUE(pumpUntil([&]() { return process->isProcessIdle() && view->isWorkIdle(); }));
    pumpQuietly();
    EXPECT_EQ(process->framesSeen.load(), 3);

    const std::optional<an::ui::DocumentSequence> shown = view->document().sequence();
    ASSERT_TRUE(shown.has_value());
    EXPECT_NE(shown->sourcePath, path);
    EXPECT_TRUE(shown->sourcePath.endsWith(QStringLiteral(".ser")));
    EXPECT_EQ(shown->descriptor.format, an::SequenceFormat::Ser);
    EXPECT_EQ(shown->descriptor.frameCount, 3u);
    EXPECT_EQ(shown->currentFrame, 1u);
    EXPECT_EQ(player->frameCount(), 3u);

    const std::optional<ReadBack> written =
        readBack(context->engineSession(), shown->sourcePath, an::SequenceFormat::Ser);
    ASSERT_TRUE(written.has_value());
    ASSERT_EQ(written->samples.size(), 3u);
    for (std::uint32_t frame = 0u; frame < 3u; ++frame)
    {
        std::vector<double> expected = fixtureSamples(frame, fixture, false);
        for (double& sample : expected)
        {
            sample += 100.0;
        }
        EXPECT_EQ(largestDifference(written->samples[frame], expected), 0.0) << "frame " << frame;
    }
    std::vector<double> shownFrame = fixtureSamples(1u, fixture, false);
    for (double& sample : shownFrame)
    {
        sample += 100.0;
    }
    EXPECT_EQ(largestDifference(samplesOf(*view->document().asset()), shownFrame), 0.0);

    // The player reads the sequence written.
    player->setCurrentFrame(2u, {});
    ASSERT_TRUE(pumpUntil(
        [&]() { return player->isIdle() && view->isWorkIdle() && view->document().sequence()->currentFrame == 2u; }));
    EXPECT_EQ(largestDifference(samplesOf(*view->document().asset()), written->samples[2]), 0.0);
    process.reset();
}
