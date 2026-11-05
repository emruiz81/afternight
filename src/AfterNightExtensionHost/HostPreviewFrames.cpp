// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The frame seam of a native window's preview: a frame's source, mask and output lent at the frame's size through the
/// window's pool, what the extension wrote taken back, the buffers kept for the next frame and removed when the
/// window's session ends.

#include "HostPreviewFrames.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>

#include <QIODevice>
#include <QString>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <optional>
#include <set>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>

namespace an::ui::extensions
{

namespace
{

using Clock = std::chrono::steady_clock;

/// A span in microseconds, to a tenth, as the trace writes it.
[[nodiscard]] QByteArray microseconds(const std::chrono::nanoseconds span)
{
    return QByteArray::number(std::chrono::duration<double, std::micro>(span).count(), 'f', 1);
}

/// Every file a buffer leaves under the root: the buffer and its sidecar.
void collectFiles(const SharedImageBuffer& buffer, std::set<std::filesystem::path>& files)
{
    if (!buffer.filePath().empty())
    {
        files.insert(buffer.filePath());
    }
    if (!buffer.descriptor().metadata_sidecar.empty())
    {
        files.insert(std::filesystem::path(buffer.descriptor().metadata_sidecar));
    }
}

/// Which of a frame's buffers a kept buffer serves.
enum class PooledRole : std::uint8_t
{
    Source,
    Output,
    Selection,
};

[[nodiscard]] std::string_view pooledRoleName(const PooledRole role) noexcept
{
    switch (role)
    {
    case PooledRole::Source:
        return "source";
    case PooledRole::Output:
        return "output";
    case PooledRole::Selection:
        return "selection";
    }
    return "buffer";
}

/// What a frame asks the pool for: a role, a size and a representation.
struct PooledShape
{
    PooledRole role = PooledRole::Source;
    int width = 0;
    int height = 0;
    int channels = 0;
    SharedBufferPixelType pixelType = SharedBufferPixelType::Float32;

    [[nodiscard]] friend bool operator==(const PooledShape& left, const PooledShape& right) = default;
};

/// The shape an export of an image, or of a part of it, fills.
[[nodiscard]] an::Result<PooledShape> shapeOf(const PooledRole role, const an::consumer::AnyImage& image,
                                              const std::optional<QRect>& part)
{
    const std::optional<an::ImageDescriptor> described =
        std::visit([](const auto& typed) { return typed.descriptor(); }, image);
    if (!described.has_value())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "the frame's image holds nothing to lend");
    }
    an::Result<SharedBufferPixelType> pixelType = sharedBufferPixelTypeOf(described->sampleType());
    if (!pixelType.hasValue())
    {
        return pixelType.status();
    }
    const QRect whole(0, 0, static_cast<int>(described->extent().width()),
                      static_cast<int>(described->extent().height()));
    const QRect lent = part.has_value() ? part->normalized().intersected(whole) : whole;
    if (lent.isEmpty())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument, "the frame's part lies outside the image");
    }
    return PooledShape{.role = role,
                       .width = lent.width(),
                       .height = lent.height(),
                       .channels = static_cast<int>(described->extent().channelCount()),
                       .pixelType = *pixelType};
}

/// Writes a file whole.
[[nodiscard]] an::Status writeFileBytes(const std::string& path, const QByteArray& bytes)
{
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(bytes) != bytes.size())
    {
        return an::Status::failure(an::ErrorCode::ExternalFailure, "the output's sidecar cannot be written: " + path);
    }
    return an::Status::success();
}

/// Lets buffers' mappings go and removes their files.
void removeBuffers(std::vector<SharedImageBuffer>& buffers) noexcept
{
    try
    {
        std::vector<SharedImageBuffer*> removed;
        removed.reserve(buffers.size());
        for (SharedImageBuffer& buffer : buffers)
        {
            removed.push_back(&buffer);
        }
        removeLentBuffers(removed);
    }
    catch (...)
    {
        // Removing what a pool leaves is best effort; the session's directory is the host's temporary tree.
    }
}

} // namespace

/// What a pool and the frames it lent share: the buffers kept, lent or idle, under one lock, and what a frame gives
/// back goes to, however long it outlives the pool.
class PreviewFrameBufferStore
{
public:
    /// A buffer a frame takes, and whether it was made for it.
    struct Taken
    {
        SharedImageBuffer buffer;
        bool fresh = false;
    };

    PreviewFrameBufferStore(SharedBufferManager manager, std::string sessionId,
                            const PreviewFrameBufferPoolBounds& bounds)
        : m_manager(std::move(manager)), m_sessionId(std::move(sessionId)), m_bounds(bounds)
    {
        std::error_code ignored;
        std::filesystem::create_directories(sessionDirectory(), ignored);
    }

    [[nodiscard]] const SharedBufferManager& manager() const noexcept
    {
        return m_manager;
    }

    /// Counts a frame, and lets go the idle buffers no frame has asked for in the bound's frames.
    void beginFrame() noexcept
    {
        std::vector<SharedImageBuffer> removed;
        try
        {
            const std::lock_guard lock(m_mutex);
            ++m_frames;
            for (auto it = m_kept.begin(); it != m_kept.end();)
            {
                if (!it->lent && m_frames - it->lentInFrame >= m_bounds.idleFrames)
                {
                    removed.push_back(std::move(it->buffer));
                    it = m_kept.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }
        catch (...)
        {
            // A buffer that could not be listed stays until a later frame lets it go, or the pool closes.
        }
        removeBuffers(removed);
    }

    /// A buffer of a shape for a frame: the idle one lent longest ago, or a new one, which no other frame is lent until
    /// this one gives it back.
    [[nodiscard]] an::Result<Taken> take(const PooledShape& shape)
    {
        std::string bufferId;
        {
            const std::lock_guard lock(m_mutex);
            // Of the idle buffers of the shape, the one lent longest ago: every kept buffer of a shape frames keep
            // asking for is lent in turn, so none ages out while the others serve.
            auto idle = m_kept.end();
            for (auto it = m_kept.begin(); it != m_kept.end(); ++it)
            {
                if (!it->lent && it->shape == shape && (idle == m_kept.end() || it->lentAt < idle->lentAt))
                {
                    idle = it;
                }
            }
            if (idle != m_kept.end())
            {
                idle->lent = true;
                idle->lentAt = ++m_lends;
                idle->lentInFrame = m_frames;
                return Taken{.buffer = std::move(idle->buffer), .fresh = false};
            }
            bufferId = std::string(pooledRoleName(shape.role)) + "." + std::to_string(m_nextNumber++);
        }

        // Making a file is the slow part, done outside the lock: another frame may take or give back meanwhile.
        SharedImageBufferSpec spec;
        spec.sessionId = m_sessionId;
        spec.bufferId = bufferId;
        spec.width = shape.width;
        spec.height = shape.height;
        spec.channels = shape.channels;
        spec.pixelType = shape.pixelType;
        spec.pixelFormat = shape.channels == 3 ? SharedBufferPixelFormat::RGB : SharedBufferPixelFormat::Mono;
        spec.colorSpace = shape.channels == 3 ? SharedBufferColorSpace::Linear : SharedBufferColorSpace::Grayscale;
        an::Result<SharedImageBuffer> made = m_manager.allocateBuffer(spec);
        if (!made.hasValue())
        {
            return made.status();
        }
        const std::lock_guard lock(m_mutex);
        m_kept.push_back(Kept{.shape = shape,
                              .bufferId = bufferId,
                              .bytes = made->mappedSize(),
                              .buffer = {},
                              .lent = true,
                              .lentAt = ++m_lends,
                              .lentInFrame = m_frames});
        return Taken{.buffer = std::move(*made), .fresh = true};
    }

    /// Takes a frame's buffers back: each kept idle for a frame of its shape, or removed when the pool is closed, when
    /// it is larger than the largest kept, or when the idle ones would pass their bound - the ones lent longest ago
    /// first.
    void giveBack(std::vector<SharedImageBuffer>& buffers) noexcept
    {
        std::vector<SharedImageBuffer> removed;
        bool removeDirectory = false;
        try
        {
            const std::lock_guard lock(m_mutex);
            for (SharedImageBuffer& buffer : buffers)
            {
                if (buffer.filePath().empty())
                {
                    continue;
                }
                const auto kept = std::find_if(m_kept.begin(), m_kept.end(), [&buffer](const Kept& each) {
                    return each.lent && each.bufferId == buffer.descriptor().buffer_id;
                });
                if (kept == m_kept.end())
                {
                    removed.push_back(std::move(buffer));
                    continue;
                }
                if (m_closed || kept->bytes > m_bounds.largestBufferBytes)
                {
                    removed.push_back(std::move(buffer));
                    m_kept.erase(kept);
                    continue;
                }
                kept->buffer = std::move(buffer);
                kept->lent = false;
            }
            std::uint64_t idle = idleBytesLocked();
            while (idle > m_bounds.idleBytes)
            {
                auto oldest = m_kept.end();
                for (auto it = m_kept.begin(); it != m_kept.end(); ++it)
                {
                    if (!it->lent && (oldest == m_kept.end() || it->lentAt < oldest->lentAt))
                    {
                        oldest = it;
                    }
                }
                if (oldest == m_kept.end())
                {
                    break;
                }
                idle -= oldest->bytes;
                removed.push_back(std::move(oldest->buffer));
                m_kept.erase(oldest);
            }
            removeDirectory = m_closed && m_kept.empty();
        }
        catch (...)
        {
            // A buffer that could not be kept is removed below with the rest.
        }
        removeBuffers(removed);
        if (removeDirectory)
        {
            removeEmptyDirectory();
        }
    }

    /// Removes the idle buffers now and keeps nothing from now on.
    void close() noexcept
    {
        std::vector<SharedImageBuffer> removed;
        bool removeDirectory = false;
        try
        {
            const std::lock_guard lock(m_mutex);
            m_closed = true;
            for (auto it = m_kept.begin(); it != m_kept.end();)
            {
                if (!it->lent)
                {
                    removed.push_back(std::move(it->buffer));
                    it = m_kept.erase(it);
                }
                else
                {
                    ++it;
                }
            }
            removeDirectory = m_kept.empty();
        }
        catch (...)
        {
            // What could not be listed is removed when its frame gives it back, or with the host's temporary tree.
        }
        removeBuffers(removed);
        if (removeDirectory)
        {
            removeEmptyDirectory();
        }
    }

    void countSidecarWrites(const std::uint64_t writes)
    {
        const std::lock_guard lock(m_mutex);
        m_sidecarWrites += writes;
    }

    [[nodiscard]] std::uint64_t idleBytes() const
    {
        const std::lock_guard lock(m_mutex);
        return idleBytesLocked();
    }

    [[nodiscard]] std::size_t idleBuffers() const
    {
        const std::lock_guard lock(m_mutex);
        return static_cast<std::size_t>(
            std::count_if(m_kept.begin(), m_kept.end(), [](const Kept& kept) { return !kept.lent; }));
    }

    [[nodiscard]] std::uint64_t sidecarWrites() const
    {
        const std::lock_guard lock(m_mutex);
        return m_sidecarWrites;
    }

private:
    /// A buffer the pool made: idle, holding it, or lent to a frame, which holds it.
    struct Kept
    {
        PooledShape shape;
        std::string bufferId;
        std::uint64_t bytes = 0u;
        SharedImageBuffer buffer;
        bool lent = false;

        /// When it was last lent: which lending, and in which frame.
        std::uint64_t lentAt = 0u;
        std::uint64_t lentInFrame = 0u;
    };

    [[nodiscard]] std::filesystem::path sessionDirectory() const
    {
        return m_manager.rootPath() / m_sessionId;
    }

    [[nodiscard]] std::uint64_t idleBytesLocked() const
    {
        std::uint64_t idle = 0u;
        for (const Kept& kept : m_kept)
        {
            idle += kept.lent ? 0u : kept.bytes;
        }
        return idle;
    }

    void removeEmptyDirectory() const noexcept
    {
        try
        {
            std::error_code ignored;
            std::filesystem::remove(sessionDirectory(), ignored);
        }
        catch (...)
        {
            // An empty directory left in the host's temporary tree holds nothing.
        }
    }

    const SharedBufferManager m_manager;
    const std::string m_sessionId;
    const PreviewFrameBufferPoolBounds m_bounds;
    mutable std::mutex m_mutex;
    std::vector<Kept> m_kept;
    std::uint64_t m_frames = 0u;
    std::uint64_t m_lends = 0u;
    std::uint64_t m_nextNumber = 0u;
    std::uint64_t m_sidecarWrites = 0u;
    bool m_closed = false;
};

QByteArray previewFrameTraceHeader()
{
    return QByteArrayLiteral("width,height,channels,pixel_type,interactive,prepare_us,lend_source_us,lend_output_us,"
                             "lend_mask_us,bind_us,run_us,flush_us,take_us,release_us");
}

QByteArray previewFrameTraceLine(const PreviewFrameSpans& spans)
{
    const std::string_view pixelType = sharedBufferPixelTypeName(spans.pixelType);
    QByteArray line;
    line += QByteArray::number(spans.width) + ',' + QByteArray::number(spans.height) + ',' +
            QByteArray::number(spans.channels) + ',' +
            QByteArray(pixelType.data(), static_cast<qsizetype>(pixelType.size())) + ',' +
            (spans.interactive ? "1" : "0");
    for (const std::chrono::nanoseconds span : {spans.prepare, spans.lendSource, spans.lendOutput, spans.lendMask,
                                                spans.bind, spans.run, spans.flush, spans.take, spans.release})
    {
        line += ',' + microseconds(span);
    }
    return line;
}

PreviewFrameTrace::PreviewFrameTrace(std::filesystem::path path) : m_path(std::move(path)) {}

bool PreviewFrameTrace::enabled() const noexcept
{
    return !m_path.empty();
}

void PreviewFrameTrace::write(const PreviewFrameSpans& spans) noexcept
{
    if (m_path.empty())
    {
        return;
    }
    try
    {
        const std::lock_guard lock(m_mutex);
        if (!m_file.isOpen())
        {
            m_file.setFileName(QString::fromStdWString(m_path.wstring()));
            if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            {
                // A trace that cannot be written is asked for no more.
                m_path.clear();
                return;
            }
            m_file.write(previewFrameTraceHeader() + '\n');
        }
        m_file.write(previewFrameTraceLine(spans) + '\n');
        m_file.flush();
    }
    catch (...)
    {
        // A trace is a measurement: a frame never fails for it.
    }
}

PreviewFrameBuffers::PreviewFrameBuffers(std::shared_ptr<PreviewFrameBufferStore> store) noexcept
    : m_store(std::move(store))
{
}

PreviewFrameBuffers::~PreviewFrameBuffers()
{
    release();
}

PreviewFrameBuffers::PreviewFrameBuffers(PreviewFrameBuffers&& other) noexcept
    : source(std::move(other.source)), output(std::move(other.output)), auxiliary(std::move(other.auxiliary)),
      m_store(std::move(other.m_store))
{
    other.auxiliary.clear();
}

PreviewFrameBuffers& PreviewFrameBuffers::operator=(PreviewFrameBuffers&& other) noexcept
{
    if (this != &other)
    {
        release();
        source = std::move(other.source);
        output = std::move(other.output);
        auxiliary = std::move(other.auxiliary);
        m_store = std::move(other.m_store);
        other.auxiliary.clear();
    }
    return *this;
}

void PreviewFrameBuffers::release(PreviewFrameSpans* spans) noexcept
{
    if (!m_store)
    {
        return;
    }
    const Clock::time_point started = Clock::now();
    const std::shared_ptr<PreviewFrameBufferStore> store = std::move(m_store);
    try
    {
        std::vector<SharedImageBuffer> given;
        given.reserve(2u + auxiliary.size());
        given.push_back(std::move(source));
        given.push_back(std::move(output));
        for (SharedImageBuffer& buffer : auxiliary)
        {
            given.push_back(std::move(buffer));
        }
        auxiliary.clear();
        store->giveBack(given);
    }
    catch (...)
    {
        // A buffer that could not be given back went with the list; its file goes with the host's temporary tree.
    }
    if (spans != nullptr)
    {
        spans->release = Clock::now() - started;
    }
}

PreviewFrameBufferPool::PreviewFrameBufferPool(SharedBufferManager manager, std::string sessionId,
                                               PreviewFrameBufferPoolBounds bounds)
    : m_store(std::make_shared<PreviewFrameBufferStore>(std::move(manager), std::move(sessionId), bounds))
{
}

PreviewFrameBufferPool::~PreviewFrameBufferPool()
{
    close();
}

const SharedBufferManager& PreviewFrameBufferPool::manager() const noexcept
{
    return m_store->manager();
}

void PreviewFrameBufferPool::close() noexcept
{
    m_store->close();
}

std::uint64_t PreviewFrameBufferPool::idleBytes() const
{
    return m_store->idleBytes();
}

std::size_t PreviewFrameBufferPool::idleBuffers() const
{
    return m_store->idleBuffers();
}

std::uint64_t PreviewFrameBufferPool::sidecarWrites() const
{
    return m_store->sidecarWrites();
}

void removeLentBuffers(const std::vector<SharedImageBuffer*>& buffers) noexcept
{
    try
    {
        std::set<std::filesystem::path> files;
        for (const SharedImageBuffer* buffer : buffers)
        {
            if (buffer != nullptr)
            {
                collectFiles(*buffer, files);
            }
        }
        // A mapped file cannot be removed on every platform: the mappings go first.
        for (SharedImageBuffer* buffer : buffers)
        {
            if (buffer != nullptr)
            {
                buffer->release();
            }
        }
        for (const std::filesystem::path& file : files)
        {
            std::error_code ec;
            std::filesystem::remove(file, ec);
        }
    }
    catch (...)
    {
        // Removing what a run leaves is best effort; the session's directory is the host's temporary tree.
    }
}

an::Result<PreviewFrameBuffers> lendPreviewFrame(PreviewFrameBufferPool& pool, const ExtensionPreviewFrame& frame,
                                                 PreviewFrameSpans* spans)
{
    if (frame.source == nullptr)
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "the preview frame has no image to lend");
    }
    // The source: a part cut from the image itself, so its descriptor states where it lies and its sidecar the image's
    // cards moved onto it, as a preview area is; otherwise the frame's source as it is. A frame never waits for
    // a writer, and a part lent as it was cut could not say where it lies - it would claim to be a whole image of its
    // size - so a part of an image a writer holds now is stopped, as a superseded frame is, lending nothing.
    const bool part = !frame.part.isEmpty() && frame.image;
    an::Result<an::consumer::AssetReadLease> whole =
        part ? frame.image->tryRead()
             : an::Result<an::consumer::AssetReadLease>(
                   an::Status::failure(an::ErrorCode::NotInitialized, "the frame is the whole of what it lends"));
    if (part && !whole.hasValue())
    {
        return an::Status::failure(an::ErrorCode::Cancelled,
                                   std::string("the image the preview frame is a part of is being written: ") +
                                       whole.status().message());
    }

    PreviewFrameBufferStore& store = *pool.m_store;
    const SharedBufferManager& manager = store.manager();
    PreviewFrameBuffers lent(pool.m_store);
    store.beginFrame();
    Clock::time_point started = Clock::now();
    const an::consumer::AnyImage& exported = whole.hasValue() ? whole->image() : *frame.source;
    const std::optional<QRect> region = whole.hasValue() ? std::optional<QRect>(frame.part) : std::nullopt;
    an::Result<PooledShape> sourceShape = shapeOf(PooledRole::Source, exported, region);
    if (!sourceShape.hasValue())
    {
        return sourceShape.status();
    }
    an::Result<PreviewFrameBufferStore::Taken> source = store.take(*sourceShape);
    if (!source.hasValue())
    {
        return source.status();
    }
    lent.source = std::move(source->buffer);
    ImageExportRequest request;
    request.role = SharedBufferRole::Source;
    request.accessMode = SharedBufferAccessMode::ReadWrite;
    request.symbolicName = frame.viewName;
    request.fileName = frame.fileName;
    request.region = region;
    const std::uint64_t sourceWrites = lent.source.sidecarWrites();
    const an::Status exportedSource = manager.exportImageInto(exported, request, lent.source);
    if (whole.hasValue())
    {
        whole->release();
    }
    if (exportedSource.failed())
    {
        return exportedSource;
    }
    store.countSidecarWrites(lent.source.sidecarWrites() - sourceWrites);
    const SharedBufferDescriptor& described = lent.source.descriptor();
    if (spans != nullptr)
    {
        spans->lendSource = Clock::now() - started;
        spans->width = described.width;
        spans->height = described.height;
        spans->channels = described.channels;
        spans->pixelType = described.pixel_type;
        spans->interactive = frame.interactive;
    }

    // The output: the source's shape, stated where the source lies, starting as a new file does - every sample zero -
    // with a sidecar of its own reset to the source's cards, which carries what the extension states of its result.
    started = Clock::now();
    an::Result<PreviewFrameBufferStore::Taken> output = store.take(PooledShape{.role = PooledRole::Output,
                                                                               .width = described.width,
                                                                               .height = described.height,
                                                                               .channels = described.channels,
                                                                               .pixelType = described.pixel_type});
    if (!output.hasValue())
    {
        return output.status();
    }
    lent.output = std::move(output->buffer);
    SharedImageBufferSpec outputSpec;
    outputSpec.symbolicName = "destination";
    outputSpec.width = described.width;
    outputSpec.height = described.height;
    outputSpec.channels = described.channels;
    outputSpec.pixelType = described.pixel_type;
    outputSpec.pixelFormat = described.pixel_format;
    outputSpec.colorSpace = described.color_space;
    outputSpec.accessMode = SharedBufferAccessMode::ReadWrite;
    outputSpec.bufferRole = SharedBufferRole::Destination;
    outputSpec.offsetX = described.offset_x;
    outputSpec.offsetY = described.offset_y;
    outputSpec.sourceWidth = described.source_width;
    outputSpec.sourceHeight = described.source_height;
    outputSpec.metadataSidecar = lent.output.descriptor().metadata_sidecar;
    if (outputSpec.metadataSidecar.empty())
    {
        std::filesystem::path sidecar = lent.output.filePath();
        sidecar += ".metadata.json";
        outputSpec.metadataSidecar = sidecar.lexically_normal().generic_string();
    }
    if (const an::Status restated = manager.restateBuffer(outputSpec, lent.output); restated.failed())
    {
        return restated;
    }
    if (!output->fresh)
    {
        if (const an::Status cleared = manager.clearBuffer(lent.output); cleared.failed())
        {
            return cleared;
        }
    }
    if (const an::Status reset = writeFileBytes(outputSpec.metadataSidecar, lent.source.sidecarBytes()); reset.failed())
    {
        return reset;
    }
    if (spans != nullptr)
    {
        spans->lendOutput = Clock::now() - started;
    }

    // The manual mask, laid over the source by the window: the frame's size, read only, known as the selection.
    if (frame.mask != nullptr)
    {
        started = Clock::now();
        an::Result<PooledShape> maskShape = shapeOf(PooledRole::Selection, *frame.mask, std::nullopt);
        if (!maskShape.hasValue())
        {
            return maskShape.status();
        }
        an::Result<PreviewFrameBufferStore::Taken> selection = store.take(*maskShape);
        if (!selection.hasValue())
        {
            return selection.status();
        }
        lent.auxiliary.push_back(std::move(selection->buffer));
        SharedImageBuffer& mask = lent.auxiliary.back();
        ImageExportRequest maskRequest;
        maskRequest.role = SharedBufferRole::Mask;
        maskRequest.accessMode = SharedBufferAccessMode::ReadOnly;
        maskRequest.symbolicName = "selection";
        const std::uint64_t maskWrites = mask.sidecarWrites();
        if (const an::Status exportedMask = manager.exportImageInto(*frame.mask, maskRequest, mask);
            exportedMask.failed())
        {
            return exportedMask;
        }
        store.countSidecarWrites(mask.sidecarWrites() - maskWrites);
        if (spans != nullptr)
        {
            spans->lendMask = Clock::now() - started;
        }
    }
    return lent;
}

an::Result<an::consumer::ImageAsset> takePreviewFrameResult(const SharedBufferManager& manager,
                                                            const an::consumer::EngineSession& session,
                                                            PreviewFrameBuffers& lent, PreviewFrameSpans* spans)
{
    const Clock::time_point started = Clock::now();
    an::Result<ImportedImage> imported = manager.importMapped(session, lent.output);
    if (spans != nullptr)
    {
        spans->take = Clock::now() - started;
    }
    if (!imported.hasValue())
    {
        return imported.status();
    }
    return std::move(imported->asset);
}

} // namespace an::ui::extensions
