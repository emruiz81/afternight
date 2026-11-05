// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The sequence companions the file workflows and the process framework reach through their seams, defined beside the
/// sequence player: a sequence file opened as a document showing its first frame, a document's sequence written to
/// another file, and the frames a process reads from a sequence and writes to the sequence it makes - each over the
/// engine's sources and sinks, and each output published whole or not at all.

#include "app/FileCompanions.h"
#include "controls/ImageViewFiles.h"
#include "controls/ProcessCompanions.h"
#include "controls/SequenceContainers.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/FrameSource.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <AfterNightPlatform/Logging.h>

#include <QDateTime>
#include <QFileInfo>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace an::ui
{

namespace
{

/// A failure Controls decided, which no engine call produced.
[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

/// Whether an image is a mosaic its filter array describes.
[[nodiscard]] bool isMosaic(const an::consumer::AnyImage& image)
{
    return std::visit(
        [](const auto& typed) {
            const std::optional<an::ImageDescriptor> stored = typed.descriptor();
            return stored.has_value() && stored->isMosaiced();
        },
        image);
}

/// The size in bytes of every file an image list names, or nothing when one of them cannot be measured.
[[nodiscard]] std::optional<std::uint64_t> sizeOfEveryFile(const std::vector<std::string>& paths)
{
    std::uint64_t total = 0u;
    for (const std::string& path : paths)
    {
        const QFileInfo file(QString::fromUtf8(path.data(), static_cast<qsizetype>(path.size())));
        if (!file.isFile())
        {
            return std::nullopt;
        }
        total += static_cast<std::uint64_t>(file.size());
    }
    return total;
}

/// Admits one operation that reads or writes a whole sequence.
[[nodiscard]] an::Result<an::consumer::OperationScope> admitSequenceWork(const an::consumer::EngineSession& session,
                                                                         const std::stop_token& stop)
{
    if (stop.stop_requested())
    {
        return refusal(an::ErrorCode::Cancelled, "the request was stopped");
    }
    return session.admit(
        an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply, .stopToken = stop});
}

/// The frames a process reads from a sequence, from a source it opens with the first read and closes when it goes.
class SequenceFrameReader final : public IProcessFrameReader
{
public:
    SequenceFrameReader(const an::consumer::EngineSession& session, DocumentSequence sequence, bool autoDebayer)
        : m_session(session), m_sequence(std::move(sequence)), m_autoDebayer(autoDebayer)
    {
    }

    [[nodiscard]] std::uint64_t frameCount() const override
    {
        if (m_sequence.selectedFrames.has_value())
        {
            return m_sequence.selectedFrames->size();
        }
        return m_sequence.descriptor.frameCount.value_or(0u);
    }

    [[nodiscard]] an::Result<std::optional<ProcessSequenceFrame>> next(const an::EngineContext& engine,
                                                                       const an::ExecutionContext& execution) override
    {
        if (!m_source.has_value())
        {
            an::Result<OpenedSequence> opened =
                openSequenceSource(engine, execution, m_sequence.sourcePath, m_sequence.descriptor.format);
            if (!opened.hasValue())
            {
                return opened.status();
            }
            m_source.emplace(std::move(opened->source));
        }
        std::optional<std::uint64_t> index;
        if (m_sequence.selectedFrames.has_value())
        {
            if (m_nextSelected >= m_sequence.selectedFrames->size())
            {
                return std::optional<ProcessSequenceFrame>{};
            }
            index = (*m_sequence.selectedFrames)[m_nextSelected];
        }
        an::Result<std::optional<std::pair<std::uint64_t, an::consumer::AnyImage>>> read =
            readSequenceFrame(engine, execution, *m_source, index);
        if (!read.hasValue())
        {
            return read.status();
        }
        if (!read->has_value())
        {
            if (index.has_value())
            {
                return refusal(an::ErrorCode::OutOfRange,
                               "the sequence holds no frame " + std::to_string(*index + 1u) + " of the selection");
            }
            return std::optional<ProcessSequenceFrame>{};
        }
        ++m_nextSelected;
        const std::uint64_t position = (*read)->first;
        an::consumer::AnyImage image = std::move((*read)->second);
        if (m_autoDebayer && isMosaic(image))
        {
            an::Result<an::consumer::AnyImage> colour =
                demosaicViewFrame(engine, execution, image, m_sequence.demosaic.value_or(an::DemosaicMethod::Rcd));
            if (!colour.hasValue())
            {
                return colour.status();
            }
            image = std::move(*colour);
        }
        an::Result<an::consumer::ImageAsset> asset = an::consumer::ImageAsset::adopt(m_session, std::move(image));
        if (!asset.hasValue())
        {
            return asset.status();
        }
        return std::optional<ProcessSequenceFrame>(ProcessSequenceFrame{.index = position, .asset = std::move(*asset)});
    }

private:
    const an::consumer::EngineSession& m_session;
    DocumentSequence m_sequence;
    bool m_autoDebayer;
    std::optional<an::FrameSource> m_source;
    std::size_t m_nextSelected = 0u;
};

/// The sequence a process run writes, through a writer that publishes it only when the run finishes it.
class SequenceFrameOutput final : public IProcessFrameWriter
{
public:
    SequenceFrameOutput(SequenceWriter writer, DocumentSequence source, QString path)
        : m_writer(std::move(writer)), m_source(std::move(source)), m_path(std::move(path))
    {
    }

    [[nodiscard]] an::Status write(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                   std::uint64_t index, const an::consumer::AnyImage& frame) override
    {
        (void)index;
        return m_writer.append(engine, execution, frame);
    }

    [[nodiscard]] an::Result<DocumentSequence> finish(const an::EngineContext& engine,
                                                      const an::ExecutionContext& execution) override
    {
        (void)engine;
        an::Result<an::SequenceDescriptor> written = m_writer.finish(execution);
        if (!written.hasValue())
        {
            return written.status();
        }
        DocumentSequence sequence;
        sequence.sourcePath = m_path;
        sequence.descriptor = std::move(*written);
        sequence.demosaic = m_source.demosaic;
        return sequence;
    }

private:
    SequenceWriter m_writer;
    DocumentSequence m_source;
    QString m_path;
};

} // namespace

an::Result<ImageDocument> openSequenceDocument(const an::consumer::EngineSession& session, const QString& path,
                                               an::SequenceFormat format, std::stop_token stop)
{
    if (!isReadableSequenceContainer(format))
    {
        return refusal(an::ErrorCode::Unsupported, "the product reads no sequence from '" + path.toStdString() + "'");
    }
    an::Result<an::consumer::OperationScope> scope = admitSequenceWork(session, stop);
    if (!scope.hasValue())
    {
        return scope.status();
    }
    an::Result<OpenedSequence> opened = openSequenceSource(scope->engine(), scope->execution(), path, format);
    if (!opened.hasValue())
    {
        return opened.status();
    }
    an::Result<std::optional<std::pair<std::uint64_t, an::consumer::AnyImage>>> first =
        readSequenceFrame(scope->engine(), scope->execution(), opened->source, std::nullopt);
    // The document reads its frames afresh through the player; the source goes now.
    (void)opened->source.close();
    if (!first.hasValue())
    {
        return first.status();
    }
    if (!first->has_value())
    {
        return refusal(an::ErrorCode::EmptySelection, "the sequence '" + path.toStdString() + "' holds no frame");
    }
    scope->end();
    // What the sequence's summary states of it is gathered here, on the worker that opened it, and never where a report
    // is drawn: when it was recorded, the frame the first file describes for a sequence that states no frame every
    // frame is held to, and for an image list the size of every file it names.
    DocumentSequence sequence;
    sequence.sourcePath = path;
    sequence.descriptor = opened->descriptor;
    sequence.currentFrame = (*first)->first;
    sequence.recorded = opened->created;
    if (!sequence.descriptor.fixedFrame.has_value())
    {
        const std::optional<an::ImageDescriptor> shown =
            std::visit([](const auto& typed) { return typed.descriptor(); }, (*first)->second);
        if (shown.has_value())
        {
            sequence.firstFrame = an::RawFrameDescriptor::fromImageDescriptor(*shown);
        }
    }
    if (format == an::SequenceFormat::ImageList)
    {
        sequence.namedFilesBytes = sizeOfEveryFile(opened->framePaths);
    }
    an::Result<an::consumer::ImageAsset> asset = an::consumer::ImageAsset::adopt(session, std::move((*first)->second));
    if (!asset.hasValue())
    {
        return asset.status();
    }
    an::Result<ImageDocument> document = ImageDocument::create(
        std::move(*asset), DocumentNames{.displayName = QFileInfo(path).completeBaseName(), .sourcePath = path});
    if (!document.hasValue())
    {
        return document.status();
    }
    document->setSequence(std::move(sequence));
    return document;
}

an::Status writeSequenceFile(const an::consumer::EngineSession& session, const DocumentSequence& sequence,
                             const QString& path, std::stop_token stop)
{
    const std::optional<an::SequenceFormat> container = writableSequenceContainerFor(path);
    if (!container.has_value())
    {
        return refusal(an::ErrorCode::Unsupported,
                       "no sequence container the product writes is named by '" + path.toStdString() + "'");
    }
    if (!isReadableSequenceContainer(sequence.descriptor.format))
    {
        return refusal(an::ErrorCode::Unsupported,
                       "the product reads no sequence from '" + sequence.sourcePath.toStdString() + "'");
    }
    an::Result<an::consumer::OperationScope> scope = admitSequenceWork(session, stop);
    if (!scope.hasValue())
    {
        return scope.status();
    }
    an::Result<OpenedSequence> opened =
        openSequenceSource(scope->engine(), scope->execution(), sequence.sourcePath, sequence.descriptor.format);
    if (!opened.hasValue())
    {
        return opened.status();
    }
    SequenceWriteSettings settings;
    settings.container = *container;
    settings.frameRate = opened->descriptor.nominalFrameRate.value_or(
        sequence.descriptor.nominalFrameRate.value_or(an::SequenceRate{1u, 1u}));
    settings.frameCount = opened->descriptor.frameCount;
    settings.createdSeconds = static_cast<std::uint64_t>((std::max)(qint64{0}, QDateTime::currentSecsSinceEpoch()));
    an::Result<SequenceWriter> writer = SequenceWriter::create(path, settings);
    if (!writer.hasValue())
    {
        return writer.status();
    }
    for (;;)
    {
        an::Result<std::optional<std::pair<std::uint64_t, an::consumer::AnyImage>>> frame =
            readSequenceFrame(scope->engine(), scope->execution(), opened->source, std::nullopt);
        if (!frame.hasValue())
        {
            return frame.status();
        }
        if (!frame->has_value())
        {
            break;
        }
        if (an::Status appended = writer->append(scope->engine(), scope->execution(), (*frame)->second);
            appended.failed())
        {
            return appended;
        }
    }
    an::Result<an::SequenceDescriptor> written = writer->finish(scope->execution());
    if (!written.hasValue())
    {
        return written.status();
    }
    LOG_INFO("Sequences: wrote '{}' ({} frames) from '{}'", path.toStdString(), written->frameCount.value_or(0u),
             sequence.sourcePath.toStdString());
    return an::Status::success();
}

an::Result<std::unique_ptr<IProcessFrameReader>> openProcessFrames(const an::consumer::EngineSession& session,
                                                                   const DocumentSequence& sequence, bool autoDebayer)
{
    if (sequence.sourcePath.isEmpty())
    {
        return refusal(an::ErrorCode::InvalidArgument, "the sequence names no file");
    }
    if (!isReadableSequenceContainer(sequence.descriptor.format))
    {
        return refusal(an::ErrorCode::Unsupported,
                       "the product reads no sequence from '" + sequence.sourcePath.toStdString() + "'");
    }
    return std::unique_ptr<IProcessFrameReader>(std::make_unique<SequenceFrameReader>(session, sequence, autoDebayer));
}

an::Result<std::unique_ptr<IProcessFrameWriter>> createProcessFrameOutput(const an::consumer::EngineSession& session,
                                                                          const DocumentSequence& source,
                                                                          const QString& path)
{
    (void)session;
    SequenceWriteSettings settings;
    settings.container = source.descriptor.format;
    settings.frameRate = source.descriptor.nominalFrameRate.value_or(an::SequenceRate{1u, 1u});
    settings.createdSeconds = static_cast<std::uint64_t>((std::max)(qint64{0}, QDateTime::currentSecsSinceEpoch()));
    an::Result<SequenceWriter> writer = SequenceWriter::create(path, settings);
    if (!writer.hasValue())
    {
        return writer.status();
    }
    return std::unique_ptr<IProcessFrameWriter>(
        std::make_unique<SequenceFrameOutput>(std::move(*writer), source, path));
}

} // namespace an::ui
