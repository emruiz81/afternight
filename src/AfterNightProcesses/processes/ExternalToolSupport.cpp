// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "processes/ExternalToolSupport.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewFiles.h"
#include "controls/settings/DeepSNRCliTools.h"
#include "controls/settings/StarNetCliTools.h"
#include "processes/CliProgressParser.h"
#include "processes/MaskProcessUtils.h"
#include "processes/StretchProcessSupport.h"

#include <AfterNightEngine/Algorithms/Masks/RangeSelection.h>
#include <AfterNightEngine/Algorithms/Spatial/Gaussian.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/ImageTransfer.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Image/Provenance.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightPlatform/Logging.h>

#include <QByteArray>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <fmt/format.h>

#include <algorithm>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace an::ui::processes
{

namespace
{

[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

/// How long the program is waited for between reads of what it prints, so a stop is seen within it.
inline constexpr int kReadWaitMs = 300;

/// The processing-state operation a kept-metadata answer is recorded under in its lineage.
inline constexpr const char* kRestoreOperation = "restoreExternalToolAnswer";

/// How much of what the program printed before a chunk is read again with it, for a progress token split across
/// two reads.
inline constexpr qsizetype kProgressTailLength = 64;

/// A string for the engine's paths: UTF-8, as the codecs read them.
[[nodiscard]] std::string enginePath(const QString& path)
{
    return path.toUtf8().toStdString();
}

/// A source's metadata with a lineage entry naming what changed its samples.
[[nodiscard]] an::Result<an::ImageMetadata> withLineage(const an::ImageMetadata& metadata, std::string operation,
                                                        std::string detail, std::optional<an::ImageSemantics> semantics)
{
    an::Result<an::ProvenanceEntry> entry = an::ProvenanceEntry::create(std::move(operation), std::move(detail));
    if (!entry.hasValue())
    {
        return entry.status();
    }
    return an::applyMetadataTransition(
        metadata, an::MetadataTransition{.semantics = std::move(semantics), .record = std::move(*entry)});
}

/// The description an image takes: the source's extent, schema, origin and aspect, in a representation and a numeric
/// domain - the source's own for an answer brought back into its units, the normalized linear one for what a tool is
/// handed and for a mask, which is a weight - with no filter array.
[[nodiscard]] an::Result<an::ImageDescriptor> describedAs(const an::ImageDescriptor& source, an::SampleType sampleType,
                                                          an::NumericDomain domain)
{
    an::ImageDescriptorRequest request = source.toRequest();
    request.sampleType = sampleType;
    request.numericDomain = domain;
    request.cfa = {};
    const std::optional<an::ImageDescriptor> described = an::ImageDescriptor::create(request);
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "the answer cannot be described");
    }
    return *described;
}

/// The representation a format hands.
[[nodiscard]] an::SampleType sampleTypeOfFormat(an::ExternalToolFormat format) noexcept
{
    return format == an::ExternalToolFormat::Float32 ? an::SampleType::Float32 : an::SampleType::UInt16;
}

/// What the tool is told of the run's progress, from ten to ninety percent, never backwards.
class ToolProgress
{
public:
    ToolProgress(const an::ExecutionContext& execution, QString stage)
        : m_execution(execution), m_stage(stage.toStdString())
    {
    }

    /// Moves the run's progress to a percentage of the whole, when it is further than the last, under a stage that
    /// stays the stage of every report after it until another is given.
    void report(int percent, const char* stage = nullptr)
    {
        if (stage != nullptr)
        {
            m_stage = stage;
        }
        if (percent <= m_last)
        {
            return;
        }
        m_last = percent;
        m_execution.reportProgress(static_cast<std::uint64_t>(percent) * kExternalToolProgressUnitsPerPercent,
                                   100u * kExternalToolProgressUnitsPerPercent, m_stage.c_str());
    }

    /// Moves the run's progress by what the tool reported of its own, mapped into the tool's share.
    void reportTool(int toolPercent)
    {
        report(cli_progress::mapPercentToRange(toolPercent, kExternalToolProgressStart, kExternalToolProgressEnd));
    }

    [[nodiscard]] int last() const noexcept
    {
        return m_last;
    }

private:
    const an::ExecutionContext& m_execution;
    std::string m_stage;
    int m_last = -1;
};

/// Reads what the program printed to one stream: progress it holds, and every other line logged.
void consumePrinted(const QByteArray& data, const QString& toolName, QString& output, ToolProgress& progress)
{
    if (data.isEmpty())
    {
        return;
    }
    const QString chunk = QString::fromUtf8(data);
    output += chunk;
    // The chunk and the tail of what came before it, so a token split across two reads is still read.
    const QStringView recent = QStringView(output).right(chunk.size() + kProgressTailLength);
    if (const std::optional<int> percent = cli_progress::extractLatestProgressPercent(recent))
    {
        progress.reportTool(*percent);
    }
    static const QRegularExpression lineSeparator(QStringLiteral("[\\r\\n]+"));
    for (const QString& line : chunk.split(lineSeparator, Qt::SkipEmptyParts))
    {
        if (!cli_progress::extractLatestProgressPercent(QStringView(line)))
        {
            LOG_DEBUG("[{}] {}", toolName.toStdString(), line.toStdString());
        }
    }
}

/// Reads a batch of machine-readable events: progress reported, errors and diagnostics logged.
void consumeMachine(const cli_progress::MachineProgressParseBatch& batch, const QString& toolName,
                    ToolProgress& progress)
{
    for (const cli_progress::MachineProgressEvent& event : batch.events)
    {
        if (event.percent)
        {
            progress.reportTool(*event.percent);
        }
        if (event.event == QStringLiteral("error") && !event.message.isEmpty())
        {
            LOG_DEBUG("[{}] {}", toolName.toStdString(), event.message.toStdString());
        }
    }
    for (const QString& diagnostic : batch.diagnostics)
    {
        LOG_DEBUG("[{}] {}", toolName.toStdString(), diagnostic.toStdString());
    }
}

/// What running the program came to.
struct ProgramRun
{
    an::Status status = an::Status::success();
    QString output;
};

/// Starts the program in its own directory and reads it until it ends or the operation is stopped, which kills it.
[[nodiscard]] ProgramRun runProgram(const an::ui::ExecutionScope& scope, const ExternalToolProfile& tool,
                                    const QStringList& arguments, bool machineProgress, ToolProgress& progress)
{
    ProgramRun run;
    QProcess program;
    const QFileInfo executable(tool.executablePath);
    program.setWorkingDirectory(executable.absolutePath());
    program.setProcessChannelMode(machineProgress ? QProcess::SeparateChannels : QProcess::MergedChannels);
    program.start(tool.executablePath, arguments);

    cli_progress::MachineProgressStreamParser machineParser;
    const auto drain = [&]() {
        if (machineProgress)
        {
            consumePrinted(program.readAllStandardOutput(), tool.name, run.output, progress);
            const QByteArray errors = program.readAllStandardError();
            if (!errors.isEmpty())
            {
                run.output += QString::fromUtf8(errors);
                consumeMachine(machineParser.append(errors), tool.name, progress);
            }
        }
        else
        {
            consumePrinted(program.readAll(), tool.name, run.output, progress);
        }
    };

    while (program.state() == QProcess::Running || program.state() == QProcess::Starting)
    {
        (void)program.waitForReadyRead(kReadWaitMs);
        drain();
        if (scope.stopRequested())
        {
            program.kill();
            (void)program.waitForFinished(1000);
            LOG_INFO("{} was stopped by the user", tool.name.toStdString());
            run.status = refusal(an::ErrorCode::Cancelled, "the run was stopped");
            return run;
        }
    }
    drain();
    if (machineProgress)
    {
        consumeMachine(machineParser.finish(), tool.name, progress);
    }

    const std::string name = tool.name.toStdString();
    if (program.error() == QProcess::FailedToStart)
    {
        run.status = refusal(an::ErrorCode::ExternalFailure,
                             fmt::format("Could not start the {0} process. Check the {0} executable path in the "
                                         "configuration settings.",
                                         name));
        return run;
    }
    if (program.error() == QProcess::Crashed)
    {
        run.status = refusal(
            an::ErrorCode::ExternalFailure,
            fmt::format("{0} process crashed. Check the {0} executable path in the configuration settings.", name));
        return run;
    }
    if (program.exitStatus() != QProcess::NormalExit || program.exitCode() != 0)
    {
        run.status =
            refusal(an::ErrorCode::ExternalFailure, fmt::format("{} exited with code {}.\n{}", name, program.exitCode(),
                                                                run.output.trimmed().toStdString()));
        return run;
    }
    return run;
}

/// The shape of an image: what an answer is held to.
struct Shape
{
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t channels = 0u;

    [[nodiscard]] bool operator==(const Shape&) const = default;
};

[[nodiscard]] Shape shapeOf(const an::ImageDescriptor& described) noexcept
{
    return Shape{described.extent().width(), described.extent().height(), described.extent().channelCount()};
}

/// An answer the tool wrote, read through the codecs into an image of its own representation, planar.
[[nodiscard]] an::Result<an::consumer::AnyImage> readAnswer(const an::ui::ExecutionScope& scope, const QString& path,
                                                            const char* what, const Shape& expected)
{
    if (!QFileInfo::exists(path))
    {
        return refusal(an::ErrorCode::ExternalFailure, fmt::format("the tool wrote no {} file", what));
    }
    const std::string file = enginePath(path);
    an::Result<an::CodecDecodeResult> decoded =
        an::decodeImage(scope.engine(), scope.execution(), an::CodecReadRequest{.path = file});
    if (!decoded.hasValue())
    {
        return decoded.status();
    }
    an::Result<an::consumer::AnyImage> imported = an::ui::importViewFrame(
        scope.engine(), scope.execution(), std::move(decoded->frame), std::move(decoded->metadata), true);
    if (!imported.hasValue())
    {
        return imported.status();
    }
    const std::optional<an::ImageDescriptor> described =
        std::visit([](const auto& typed) { return typed.descriptor(); }, *imported);
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, fmt::format("the tool's {} holds nothing", what));
    }
    if (shapeOf(*described) != expected)
    {
        return refusal(an::ErrorCode::IncompatibleArguments,
                       fmt::format("the tool's {} is {}x{} with {} channels where the image is {}x{} with {}", what,
                                   described->extent().width(), described->extent().height(),
                                   described->extent().channelCount(), expected.width, expected.height,
                                   expected.channels));
    }
    return imported;
}

/// A view of an image's samples for the hand-off: its descriptor, first sample and layout, borrowed.
struct SampleView
{
    an::ImageDescriptor descriptor;
    const void* samples = nullptr;
    an::ImageLayout layout;
    an::SampleType sampleType = an::SampleType::Float32;
};

[[nodiscard]] an::Result<SampleView> viewOf(const an::consumer::AnyImage& image)
{
    return std::visit(
        [](const auto& typed) -> an::Result<SampleView> {
            using SampleT = typename std::remove_cvref_t<decltype(typed)>::Sample;
            const auto view = typed.view();
            const std::optional<an::ImageDescriptor> described = typed.descriptor();
            if (!view.has_value() || !described.has_value())
            {
                return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
            }
            return SampleView{*described, view->data(), view->layout(), an::kSampleTypeOf<SampleT>};
        },
        image);
}

/// Brings an answer back as an image or a star layer, written once in the source's representation under metadata.
[[nodiscard]] an::Result<an::consumer::AnyImage>
restoredAnswer(const an::ui::ExecutionScope& scope, const an::ExternalToolHandoffPlan& plan,
               const an::ExternalToolHandoff& handoff, an::ExternalToolLayer layer,
               const an::consumer::AnyImage& answer, const an::ImageDescriptor& described, an::ImageMetadata metadata)
{
    an::Result<SampleView> answerView = viewOf(answer);
    if (!answerView.hasValue())
    {
        return answerView.status();
    }
    an::Result<an::consumer::AnyImage> made =
        refusal(an::ErrorCode::Internal, "the source's representation is not one of the eight");
    const bool dispatched = an::dispatchSampleType(described.sampleType(), [&]<typename SampleT>() {
        an::Result<an::Image<SampleT>> image =
            an::Image<SampleT>::create(scope.engine(), described, an::StorageArrangement::Planar,
                                       an::StorageInitialization::Zeroed, std::move(metadata));
        if (!image.hasValue())
        {
            made = image.status();
            return;
        }
        {
            std::optional<an::ImageWriteAccess<SampleT>> access = image->write();
            if (!access.has_value())
            {
                made = refusal(an::ErrorCode::NotInitialized, "the restored image holds nothing");
                return;
            }
            const an::Result<an::ExternalToolRestoreResult> restored =
                an::restoreExternalToolAnswer(scope.execution(), plan, handoff, layer,
                                              an::ExternalToolSource{.descriptor = &answerView->descriptor,
                                                                     .samples = answerView->samples,
                                                                     .layout = &answerView->layout},
                                              access->view().data(), access->view().layout());
            if (!restored.hasValue())
            {
                made = restored.status();
                return;
            }
        }
        made = an::consumer::AnyImage(std::move(*image));
    });
    if (!dispatched)
    {
        return refusal(an::ErrorCode::Internal, "the source's representation is not one of the eight");
    }
    return made;
}

/// Narrows an answer to a representation as it is, to the nearest value and saturating, under metadata.
[[nodiscard]] an::Result<an::consumer::AnyImage> narrowedAnswer(const an::ui::ExecutionScope& scope,
                                                                const an::consumer::AnyImage& answer,
                                                                const an::ImageDescriptor& described,
                                                                an::ImageMetadata metadata)
{
    an::Result<SampleView> answerView = viewOf(answer);
    if (!answerView.hasValue())
    {
        return answerView.status();
    }
    an::Result<an::consumer::AnyImage> made =
        refusal(an::ErrorCode::Internal, "the representation is not one of the eight");
    const bool dispatched = an::dispatchSampleType(described.sampleType(), [&]<typename SampleT>() {
        an::Result<an::Image<SampleT>> image =
            an::Image<SampleT>::create(scope.engine(), described, an::StorageArrangement::Planar,
                                       an::StorageInitialization::Zeroed, std::move(metadata));
        if (!image.hasValue())
        {
            made = image.status();
            return;
        }
        {
            std::optional<an::ImageWriteAccess<SampleT>> access = image->write();
            if (!access.has_value())
            {
                made = refusal(an::ErrorCode::NotInitialized, "the narrowed image holds nothing");
                return;
            }
            an::ConversionCounters counters;
            const an::Status converted =
                an::convertImageSamples(scope.execution(), answerView->sampleType, answerView->samples,
                                        answerView->layout, an::kSampleTypeOf<SampleT>, access->view().data(),
                                        access->view().layout(), an::ConversionPolicy{}, counters);
            if (converted.failed())
            {
                made = converted;
                return;
            }
        }
        made = an::consumer::AnyImage(std::move(*image));
    });
    if (!dispatched)
    {
        return refusal(an::ErrorCode::Internal, "the representation is not one of the eight");
    }
    return made;
}

/// The metadata an answer carries: derived from the source's under a processing state, or the source's whole with
/// every sample now valid; each with a lineage entry naming the run.
[[nodiscard]] an::Result<an::ImageMetadata> answerMetadata(const an::ui::ProcessSource& source,
                                                           const an::ImageDescriptor& described,
                                                           const std::optional<std::string>& state,
                                                           const std::string& operation, const std::string& detail)
{
    if (state.has_value())
    {
        an::Result<an::ImageMetadata> derived =
            process_utils::derivedImageMetadata(source.metadata(), described, *state);
        if (!derived.hasValue())
        {
            return derived.status();
        }
        return withLineage(*derived, operation, detail, std::nullopt);
    }
    an::ImageSemanticsRequest semantics = source.metadata().semantics().toRequest();
    semantics.validity = an::ValidityDescriptor{};
    an::Result<an::ImageSemantics> restated = an::ImageSemantics::create(semantics);
    if (!restated.hasValue())
    {
        return restated.status();
    }
    return withLineage(source.metadata(), operation, detail, std::move(*restated));
}

[[nodiscard]] const char* preStretchName(an::ExternalToolPreStretch preStretch) noexcept
{
    switch (preStretch)
    {
    case an::ExternalToolPreStretch::Unlinked:
        return "unlinked";
    case an::ExternalToolPreStretch::Linked:
        return "linked";
    default:
        break;
    }
    return "none";
}

/// Selects the lightness between the limits and smooths it, in place over its floating samples, held under the
/// asset's write access.
template <typename SampleT>
[[nodiscard]] an::Status selectLightness(const an::ui::ExecutionScope& scope, const an::ImageDescriptor& described,
                                         an::ImageWriteAccess<SampleT>& access, const StarMaskShaping& shaping)
{
    an::Result<an::Image<SampleT>> scratch = an::Image<SampleT>::create(
        scope.engine(), described, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed);
    if (!scratch.hasValue())
    {
        return scratch.status();
    }
    std::optional<an::ImageWriteAccess<SampleT>> selected = scratch->write();
    if (!selected.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the selection holds nothing");
    }
    an::RangeSelectionRequest request;
    request.lowerLimit = shaping.lowerLimit;
    request.upperLimit = shaping.upperLimit;
    request.fuzziness = 0.0;
    request.screening = false;
    request.invert = false;
    const an::Result<an::RangeSelectionResult> ranged =
        an::rangeSelectImage(scope.execution(), an::kSampleTypeOf<SampleT>, access.view().data(),
                             access.view().layout(), selected->view().data(), selected->view().layout(), request);
    if (!ranged.hasValue())
    {
        return ranged.status();
    }
    if (shaping.smoothness > 0.0)
    {
        const an::Result<an::GaussianPlan> plan =
            an::planGaussianBlur(an::GaussianRequest{.sigmaX = shaping.smoothness, .sigmaY = shaping.smoothness});
        if (!plan.hasValue())
        {
            return plan.status();
        }
        const an::Result<an::SpatialFilterResult> blurred =
            an::blurImageGaussian(scope.execution(), an::kSampleTypeOf<SampleT>, selected->view().data(),
                                  selected->view().layout(), access.view().data(), access.view().layout(), *plan);
        return blurred.hasValue() ? an::Status::success() : blurred.status();
    }
    return an::copyImageSamples(scope.execution(), an::kSampleTypeOf<SampleT>, selected->view().data(),
                                selected->view().layout(), access.view().data(), access.view().layout());
}

} // namespace

ExternalToolCheck checkExternalTool(const ExternalToolProfile& tool)
{
    ExternalToolCheck check;
    if (tool.name == QStringLiteral("StarNet2"))
    {
        const an::ui::starnet::ExecutableCheckResult probed =
            an::ui::starnet::checkExecutable(tool.executablePath, 3000);
        check.supported = probed.supported;
        check.machineProgress = probed.machineProgressSupported;
        check.version = probed.versionText;
        check.detail = probed.detail;
        return check;
    }
    if (tool.name == QStringLiteral("DeepSNR"))
    {
        const an::ui::deepsnr::ExecutableCheckResult probed =
            an::ui::deepsnr::checkExecutable(tool.executablePath, 3000);
        check.supported = probed.supported;
        check.machineProgress = probed.machineProgressSupported;
        check.version = probed.versionText;
        check.detail = probed.detail;
        return check;
    }
    check.detail = QStringLiteral("No probe knows the tool '%1'.").arg(tool.name);
    return check;
}

an::Result<ExternalToolAnswers> runExternalTool(const an::ui::ExecutionScope& scope,
                                                const an::ui::ProcessSource& source, const ExternalToolRequest& request)
{
    const std::string toolName = request.tool.name.toStdString();
    if (!request.arguments)
    {
        return refusal(an::ErrorCode::InvalidArgument, "the request spells no arguments for the tool");
    }
    const std::optional<an::ImageDescriptor> described = source.descriptor();
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    if (described->isMosaiced())
    {
        return refusal(an::ErrorCode::Unsupported,
                       fmt::format("{} takes a grey or colour picture, not a colour filter array mosaic; demosaic the "
                                   "image first",
                                   toolName));
    }
    const Shape shape = shapeOf(*described);
    if (shape.channels != 1u && shape.channels != 3u)
    {
        return refusal(an::ErrorCode::Unsupported, fmt::format("{} supports grayscale and RGB images only.", toolName));
    }
    if (shape.width < request.tool.minimumWidth || shape.height < request.tool.minimumHeight)
    {
        return refusal(an::ErrorCode::Unsupported,
                       fmt::format("{} requires input images at least {}x{} pixels.", toolName,
                                   request.tool.minimumWidth, request.tool.minimumHeight));
    }

    if (scope.stopRequested())
    {
        return refusal(an::ErrorCode::Cancelled, "the run was stopped");
    }
    const an::ExecutionContext& execution = scope.execution();
    ToolProgress progress(execution, QStringLiteral("Preparing %1 execution...").arg(request.tool.name));
    // One percent, not zero: a codec's own report would outrank a report of nothing.
    progress.report(1);

    const ExternalToolCheck check = checkExternalTool(request.tool);
    if (!check.supported)
    {
        return refusal(an::ErrorCode::ExternalFailure, check.detail.toStdString());
    }

    // The engine decides what the tool sees.
    const an::Result<an::ExternalToolHandoffPlan> plan = an::planExternalToolHandoff(
        an::ExternalToolHandoffRequest{.format = request.tool.format, .preStretch = request.preStretch});
    if (!plan.hasValue())
    {
        return plan.status();
    }
    const an::SampleType handedType = sampleTypeOfFormat(request.tool.format);
    an::Result<an::ImageDescriptor> handedDescriptor =
        describedAs(*described, handedType, an::NumericDomain::NormalizedLinear);
    if (!handedDescriptor.hasValue())
    {
        return handedDescriptor.status();
    }
    an::Result<SampleView> sourceView = viewOf(source.image());
    if (!sourceView.hasValue())
    {
        return sourceView.status();
    }
    std::optional<an::ValidityMaskView<const std::uint8_t>> maskView;
    const an::KernelValidityView validity = stretchValidity(source.metadata(), source.validity(), maskView);

    QTemporaryDir directory(an::ui::ensureTempDirPathOrSystemTemp() + QStringLiteral("/") +
                            request.tool.name.toLower());
    if (!directory.isValid())
    {
        return refusal(an::ErrorCode::ExternalFailure,
                       fmt::format("Could NOT set temporary dir for {} execution", toolName));
    }
    ExternalToolFiles files;
    files.directory = directory.path();
    files.input = directory.filePath(QStringLiteral("srcFile.tiff"));
    files.image = directory.filePath(QStringLiteral("dstFile.tiff"));
    files.mask = directory.filePath(QStringLiteral("maskFile.tiff"));
    files.stars = directory.filePath(QStringLiteral("unscreenFile.tiff"));

    an::Result<an::ExternalToolHandoff> handoff = refusal(an::ErrorCode::Internal, "the hand-off was not prepared");
    {
        // The tool is handed one interleaved file in the format its profile states, written whole or not at all.
        an::Result<an::consumer::AnyImage> handed =
            refusal(an::ErrorCode::Internal, "the handed representation is not one the hand-off writes");
        const bool dispatched = an::dispatchSampleType(handedType, [&]<typename HandedT>() {
            an::Result<an::Image<HandedT>> image =
                an::Image<HandedT>::create(scope.engine(), *handedDescriptor, an::StorageArrangement::Interleaved,
                                           an::StorageInitialization::Zeroed);
            if (!image.hasValue())
            {
                handed = image.status();
                return;
            }
            {
                std::optional<an::ImageWriteAccess<HandedT>> access = image->write();
                if (!access.has_value())
                {
                    handed = refusal(an::ErrorCode::NotInitialized, "the handed image holds nothing");
                    return;
                }
                handoff = an::prepareExternalToolHandoff(execution, *plan,
                                                         an::ExternalToolSource{.descriptor = &sourceView->descriptor,
                                                                                .samples = sourceView->samples,
                                                                                .layout = &sourceView->layout,
                                                                                .validity = validity},
                                                         access->view().data(), access->view().layout());
                if (!handoff.hasValue())
                {
                    handed = handoff.status();
                    return;
                }
            }
            handed = an::consumer::AnyImage(std::move(*image));
        });
        if (!dispatched || !handed.hasValue())
        {
            return handed.hasValue() ? refusal(an::ErrorCode::Internal, "the handed representation is not supported")
                                     : handed.status();
        }
        const an::Result<an::OutputIntent> intent =
            an::OutputIntent::create(an::OutputIntentRequest{.sampleType = handedType, .preserveSourceCards = false});
        if (!intent.hasValue())
        {
            return intent.status();
        }
        const std::string inputPath = enginePath(files.input);
        const an::Result<an::OutputReport> written = std::visit(
            [&](const auto& typed) -> an::Result<an::OutputReport> {
                an::Result<an::RawFrame> frame = an::borrowImageFrame(typed);
                if (!frame.hasValue())
                {
                    return frame.status();
                }
                an::OutputRequest output;
                output.destinationPath = inputPath;
                output.format = an::CodecFormat::Tiff;
                output.intent = &*intent;
                output.replacePolicy = an::OutputReplacePolicy::ReplaceExisting;
                return an::writeImage(scope.engine(), execution, output, *frame, typed.metadata());
            },
            *handed);
        if (!written.hasValue())
        {
            return written.status();
        }
    }
    if (scope.stopRequested())
    {
        return refusal(an::ErrorCode::Cancelled, "the run was stopped");
    }
    const std::string executing = "Executing " + toolName + "...";
    progress.report(kExternalToolProgressStart, executing.c_str());

    ExternalToolAnswers answers;
    answers.machineProgress = check.machineProgress;
    answers.arguments = request.arguments(files, check.machineProgress);
    LOG_INFO("Executing {} {}", request.tool.executablePath.toStdString(),
             answers.arguments.join(QLatin1Char(' ')).toStdString());
    const ProgramRun run = runProgram(scope, request.tool, answers.arguments, check.machineProgress, progress);
    if (run.status.failed())
    {
        return run.status;
    }
    progress.report(95, "Processing output...");

    const std::string lineage =
        fmt::format("{} {}; {}; handed as {}; pre-stretch {}", request.lineage, check.version.toStdString(),
                    answers.arguments.join(QLatin1Char(' ')).toStdString(),
                    handedType == an::SampleType::Float32 ? "Float32" : "UInt16", preStretchName(request.preStretch));
    an::Result<an::ImageDescriptor> resultDescriptor =
        describedAs(*described, described->sampleType(), described->numericDomain());
    an::Result<an::ImageDescriptor> maskDescriptor =
        describedAs(*described, described->sampleType(), an::NumericDomain::NormalizedLinear);
    if (!resultDescriptor.hasValue() || !maskDescriptor.hasValue())
    {
        return resultDescriptor.hasValue() ? maskDescriptor.status() : resultDescriptor.status();
    }

    const auto bringBack = [&](const QString& path, const char* what, an::ExternalToolLayer layer,
                               const std::optional<std::string>& state,
                               bool narrowOnly) -> an::Result<an::consumer::ImageAsset> {
        an::Result<an::consumer::AnyImage> answer = readAnswer(scope, path, what, shape);
        if (!answer.hasValue())
        {
            return answer.status();
        }
        if (scope.stopRequested())
        {
            return refusal(an::ErrorCode::Cancelled, "the run was stopped");
        }
        const an::ImageDescriptor& answerDescriptor = narrowOnly ? *maskDescriptor : *resultDescriptor;
        an::Result<an::ImageMetadata> metadata =
            answerMetadata(source, answerDescriptor, state, narrowOnly ? "convertImageSamples" : kRestoreOperation,
                           fmt::format("{} {}", what, lineage));
        if (!metadata.hasValue())
        {
            return metadata.status();
        }
        an::Result<an::consumer::AnyImage> made =
            narrowOnly ? narrowedAnswer(scope, *answer, answerDescriptor, std::move(*metadata))
                       : restoredAnswer(scope, *plan, *handoff, layer, *answer, answerDescriptor, std::move(*metadata));
        if (!made.hasValue())
        {
            return made.status();
        }
        return an::consumer::ImageAsset::adopt(scope.session(), std::move(*made));
    };

    if (request.wantsImage)
    {
        an::Result<an::consumer::ImageAsset> image =
            bringBack(files.image, "image", an::ExternalToolLayer::Image, request.imageState, false);
        if (!image.hasValue())
        {
            return image.status();
        }
        answers.image = std::move(*image);
    }
    if (request.wantsMask)
    {
        an::Result<an::consumer::ImageAsset> mask =
            bringBack(files.mask, "mask", an::ExternalToolLayer::Image, request.maskState, true);
        if (!mask.hasValue())
        {
            return mask.status();
        }
        answers.mask = std::move(*mask);
    }
    if (request.wantsStars)
    {
        an::Result<an::consumer::ImageAsset> stars =
            bringBack(files.stars, "star layer", an::ExternalToolLayer::StarLayer, request.starsState, false);
        if (!stars.hasValue())
        {
            return stars.status();
        }
        answers.stars = std::move(*stars);
    }
    progress.report(100, "Done");
    return answers;
}

an::Result<an::consumer::ImageAsset> shapeStarMask(const an::ui::ExecutionScope& scope, an::consumer::ImageAsset&& mask,
                                                   const StarMaskShaping& shaping)
{
    const std::optional<an::ImageDescriptor> described = mask.descriptor();
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the mask holds nothing");
    }
    if (!shaping.lightness || described->extent().channelCount() == 1u)
    {
        return std::move(mask);
    }
    an::Result<an::consumer::AnyImage> lightness = refusal(an::ErrorCode::Internal, "the lightness was not extracted");
    {
        an::Result<an::consumer::AssetReadLease> lease = mask.read();
        if (!lease.hasValue())
        {
            return lease.status();
        }
        an::Result<an::consumer::ImageAsset> extracted =
            process_utils::extractLightness(scope, an::ui::ProcessSource::of(*lease), kStarnetStarMaskState);
        lease->release();
        if (!extracted.hasValue())
        {
            return extracted.status();
        }
        // Every lightness goes through the narrowing below, even one already in the mask's representation, so the
        // result records the extraction in its lineage whatever the mask's representation.
        an::Result<an::consumer::AssetWriteLease> writer = extracted->write();
        if (!writer.hasValue())
        {
            return writer.status();
        }
        const an::consumer::AnyImage& image = writer->image();
        const std::optional<an::ImageDescriptor> lightDescribed =
            std::visit([](const auto& typed) { return typed.descriptor(); }, image);
        if (!lightDescribed.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the lightness holds nothing");
        }
        an::Status selected = an::Status::success();
        if (shaping.binary)
        {
            selected = writer->visitWrite([&](auto access) -> an::Status {
                using SampleT = std::remove_pointer_t<decltype(access.view().data())>;
                if constexpr (std::is_floating_point_v<SampleT>)
                {
                    return selectLightness<SampleT>(scope, *lightDescribed, access, shaping);
                }
                else
                {
                    return refusal(an::ErrorCode::Internal, "the lightness is not a floating image");
                }
            });
        }
        if (selected.failed())
        {
            return selected;
        }
        // The shaped lightness goes back to the mask's own representation under the lightness's metadata.
        an::Result<an::ImageDescriptor> narrowedDescriptor =
            describedAs(*lightDescribed, described->sampleType(), an::NumericDomain::NormalizedLinear);
        if (!narrowedDescriptor.hasValue())
        {
            return narrowedDescriptor.status();
        }
        const an::ImageMetadata& lightMetadata =
            std::visit([](const auto& typed) -> const an::ImageMetadata& { return typed.metadata(); }, image);
        an::Result<an::ImageMetadata> metadata =
            withLineage(lightMetadata, "extractLightness", "the mask's CIE L* in the unit interval", std::nullopt);
        if (metadata.hasValue() && shaping.binary)
        {
            metadata = withLineage(*metadata, "rangeSelectImage",
                                   fmt::format("limits {:.6f} to {:.6f}, smoothness {:.6f}", shaping.lowerLimit,
                                               shaping.upperLimit, shaping.smoothness),
                                   std::nullopt);
        }
        if (!metadata.hasValue())
        {
            return metadata.status();
        }
        lightness = narrowedAnswer(scope, image, *narrowedDescriptor, std::move(*metadata));
    }
    if (!lightness.hasValue())
    {
        return lightness.status();
    }
    return an::consumer::ImageAsset::adopt(scope.session(), std::move(*lightness));
}

} // namespace an::ui::processes
