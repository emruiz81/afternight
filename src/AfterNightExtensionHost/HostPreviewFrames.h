// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// How one frame of a native window's preview crosses to the extension and back: the frame's source and the manual
/// mask laid over it lent as buffers of the frame's own size, the output the extension writes in the source's shape,
/// what it wrote taken back as an asset of the session, and the buffers kept for the window's next frame
/// (`PreviewFrameBufferPool`) until its session ends, when every file goes.
///
/// A frame runs over what the framework chose for it - the whole image, the image reduced by the preview view's factor,
/// or a part of it - so what is copied into the buffers is the frame's size and not the image's. A part is lent as a
/// preview area is: cut from the image by the export, so the buffer's descriptor records where it lies and the image's
/// size, and its sidecar states the image's cards moved onto it.
///
/// What each step of a frame costs is measured when asked for (`PreviewFrameSpans`), and the host writes it, a line a
/// frame, to the file `AFTERNIGHT_PREVIEW_FRAME_TRACE` names (`PreviewFrameTrace`).
#pragma once

#include "app/SharedBufferManager.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Core/Result.h>

#include <QByteArray>
#include <QCborMap>
#include <QFile>
#include <QRect>
#include <QSize>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace an::ui::extensions
{

/// One frame of an extension's real-time preview, as its window hands it to the extension: on the frame's worker, and
/// borrowed for the call.
struct ExtensionPreviewFrame
{
    /// What the frame runs the extension over - the whole image, the image reduced by the preview view's factor, or a
    /// part of it - read under the lease the frame holds.
    const an::consumer::AnyImage* source = nullptr;

    /// The whole image the source was made from, when it is a part of it, so the part can be lent as cut from the image
    /// itself; null otherwise.
    std::shared_ptr<const an::consumer::ImageAsset> image;

    /// The rectangle of the image the source is, in the image's pixels, when it is a part of it - the preview area, or
    /// the part of the image the preview view shows - and empty otherwise.
    QRect part;

    /// The image's width and height.
    QSize imageSize;

    /// How many of the image's pixels each way one pixel of the source stands for: more than one for a reduction.
    std::uint32_t factor = 1u;

    /// The parameters the window held when the frame was issued.
    QCborMap params;

    /// The manual mask the person drew and uses, laid over the source as the source lies over the image - reduced with
    /// it, or cut to its part - or null.
    const an::consumer::AnyImage* mask = nullptr;

    /// What the extension knows the image's view as: its name and the file it came from.
    std::string viewName;
    std::string fileName;

    /// Whether the frame shows a drag - an interactive frame - rather than parameters that have settled.
    bool interactive = false;

    /// Stops when the frame is no longer wanted: superseded by a newer one, refused for an Apply, or its window
    /// closing.
    std::stop_token stop;
};

/// Runs the extension's preview over one frame, on the frame's worker, and answers what it made: a new asset of the
/// session, of the source's width and height.
using ExtensionPreviewBody = std::function<an::Result<an::consumer::ImageAsset>(const ExtensionPreviewFrame& frame)>;

/// How long one frame spent in each step of its way to the extension and back, on the frame's worker, and what it lent:
/// what the host's trace and the drag benchmark report. A step the frame did not take stays zero.
struct PreviewFrameSpans
{
    /// Before anything is lent: the extension's word on the parameters, the window's pool, the request.
    std::chrono::nanoseconds prepare{0};

    /// Lending the source, the output and the manual mask.
    std::chrono::nanoseconds lendSource{0};
    std::chrono::nanoseconds lendOutput{0};
    std::chrono::nanoseconds lendMask{0};

    /// Binding the buffers on the bridge, each binding reading its sidecar, until the extension is called.
    std::chrono::nanoseconds bind{0};

    /// The extension's own `execute_preview`.
    std::chrono::nanoseconds run{0};

    /// Writing what the extension set on its output back into the output's sidecar.
    std::chrono::nanoseconds flush{0};

    /// Taking what the extension wrote back as an asset of the session.
    std::chrono::nanoseconds take{0};

    /// Letting the frame's buffers go.
    std::chrono::nanoseconds release{0};

    /// What the frame lent: the source's width, height, channels and representation.
    int width = 0;
    int height = 0;
    int channels = 0;
    SharedBufferPixelType pixelType = SharedBufferPixelType::Float32;

    /// Whether the frame showed a drag.
    bool interactive = false;
};

/// The first line of a frame trace: the names of the columns `previewFrameTraceLine` writes, comma-separated.
[[nodiscard]] QByteArray previewFrameTraceHeader();

/// One frame's line of a trace: what it lent, whether it showed a drag, then each span in microseconds,
/// comma-separated.
[[nodiscard]] QByteArray previewFrameTraceLine(const PreviewFrameSpans& spans);

/// A trace of a window's frames: a header and then one line a frame, written to a file as each frame ends, or nothing
/// when no file is named. Safe to write from several frames' workers.
class PreviewFrameTrace
{
public:
    /// @param path the file to write; empty writes nothing. The file is replaced when the first frame is written.
    explicit PreviewFrameTrace(std::filesystem::path path = {});

    PreviewFrameTrace(const PreviewFrameTrace&) = delete;
    PreviewFrameTrace& operator=(const PreviewFrameTrace&) = delete;

    /// Whether a file is named.
    [[nodiscard]] bool enabled() const noexcept;

    /// Writes one frame's line, after the header when it is the first; a file that cannot be written is passed over.
    ///
    /// @param spans the frame's spans.
    void write(const PreviewFrameSpans& spans) noexcept;

private:
    std::filesystem::path m_path;
    std::mutex m_mutex;
    QFile m_file;
};

/// What a pool keeps of the buffers its frames give back. A frame's buffers are as large as what it runs over: a
/// reduction's or a part's are small, a whole colour Float32 image's hundreds of megabytes each - and those are where
/// keeping a buffer gains most, since a new file is written page by page for the first time in every frame.
struct PreviewFrameBufferPoolBounds
{
    /// The largest buffer kept once its frame is done, in bytes; a larger one is removed then. None by default: a drag
    /// over a whole 6016 x 4016 colour Float32 image showed four times the previews a second with its buffers kept
    /// than with a bound of 64 MiB, which keeps none of them.
    std::uint64_t largestBufferBytes = (std::numeric_limits<std::uint64_t>::max)();

    /// The most bytes kept idle in all; past it the buffers lent longest ago go first. A gibibyte holds one frame of
    /// that image, its source and its output.
    std::uint64_t idleBytes = 1ull << 30u;

    /// How many frames an idle buffer waits to be lent again before it goes.
    std::uint64_t idleFrames = 16u;
};

class PreviewFrameBufferPool;
class PreviewFrameBufferStore;
class PreviewFrameBuffers;

/// Lends one frame to the extension through a window's pool: the source exported as it is - or, for a part of the
/// image, cut from the image itself, so it states where it lies - and the mask exported as the selection, each the
/// frame's size; and an output in the source's shape, cleared, whose sidecar starts as the source's cards. Each goes
/// into a buffer of its shape the pool keeps idle, or a new one; a sidecar that states what its file already holds is
/// not written again. A frame never waits for a writer: a part of an image a writer holds now lends nothing.
///
/// @param pool the window's pool.
/// @param frame the frame.
/// @param spans where to state how long each lending took and what was lent; may be null.
/// @return the buffers; `ErrorCode::Cancelled` for a part of an image being written, as for a superseded frame; or
///         what the export or an allocation refused, the buffers taken so far given back.
[[nodiscard]] an::Result<PreviewFrameBuffers>
lendPreviewFrame(PreviewFrameBufferPool& pool, const ExtensionPreviewFrame& frame, PreviewFrameSpans* spans = nullptr);

/// The buffers one frame lends the extension: the source, the output it writes and, when the person uses one, the
/// manual mask as the selection. Move-only; letting it go gives the buffers back to the pool that lent them, which
/// keeps them for a later frame of their shape or removes their files.
class PreviewFrameBuffers
{
public:
    PreviewFrameBuffers() noexcept = default;

    /// Gives the buffers back.
    ~PreviewFrameBuffers();

    /// Takes the buffers over; the other is left holding nothing.
    PreviewFrameBuffers(PreviewFrameBuffers&& other) noexcept;
    PreviewFrameBuffers& operator=(PreviewFrameBuffers&& other) noexcept;
    PreviewFrameBuffers(const PreviewFrameBuffers&) = delete;
    PreviewFrameBuffers& operator=(const PreviewFrameBuffers&) = delete;

    /// The frame's source, as the extension reads it.
    SharedImageBuffer source;

    /// What the extension writes, in the source's shape.
    SharedImageBuffer output;

    /// The selection, when the frame carries the manual mask.
    std::vector<SharedImageBuffer> auxiliary;

    /// Gives the buffers back to the pool that lent them now; once is enough.
    ///
    /// @param spans where to state how long it took; may be null.
    void release(PreviewFrameSpans* spans = nullptr) noexcept;

private:
    friend an::Result<PreviewFrameBuffers>
    lendPreviewFrame(PreviewFrameBufferPool& pool, const ExtensionPreviewFrame& frame, PreviewFrameSpans* spans);

    explicit PreviewFrameBuffers(std::shared_ptr<PreviewFrameBufferStore> store) noexcept;

    /// What the buffers go back to; null once they have.
    std::shared_ptr<PreviewFrameBufferStore> m_store;
};

/// The buffers a native window's frames are lent, kept from one frame to the next: the next frame of a shape is lent
/// the files the last one gave back, so a frame pays for making, mapping and removing files - and for touching every
/// page of a new one - only when no buffer of its shape waits. One per window session; its frames may run on several
/// workers at once, and a buffer lent to a frame is lent to no other until that frame gives it back.
///
/// What the extension reads is what a new file would hold: an output is cleared and its sidecar reset to the source's
/// cards at every frame, since the bridge writes what the extension set on its result into that sidecar. An array the
/// extension takes of a buffer is the call's alone, as the SDK states; a later frame writes the same file.
class PreviewFrameBufferPool
{
public:
    /// @param manager the seam, over the root the buffers go under.
    /// @param sessionId the directory of the root the buffers go in, made now.
    /// @param bounds what is kept of the buffers frames give back.
    PreviewFrameBufferPool(SharedBufferManager manager, std::string sessionId,
                           PreviewFrameBufferPoolBounds bounds = {});

    /// Closes the pool.
    ~PreviewFrameBufferPool();

    PreviewFrameBufferPool(const PreviewFrameBufferPool&) = delete;
    PreviewFrameBufferPool& operator=(const PreviewFrameBufferPool&) = delete;

    /// The seam the pool lends through.
    [[nodiscard]] const SharedBufferManager& manager() const noexcept;

    /// Removes every idle buffer's files - and the session's directory, once nothing is lent - and keeps nothing from
    /// now on: a buffer still lent is removed when its frame gives it back, and a frame lent afterwards is lent new
    /// buffers, removed when it ends. The window's session ends with it.
    void close() noexcept;

    /// How many bytes, and buffers, wait idle for a frame of their shape.
    [[nodiscard]] std::uint64_t idleBytes() const;
    [[nodiscard]] std::size_t idleBuffers() const;

    /// How many times a frame's source or selection sidecar was written: once for a new buffer, and again only when
    /// the cards it states changed.
    [[nodiscard]] std::uint64_t sidecarWrites() const;

private:
    friend an::Result<PreviewFrameBuffers>
    lendPreviewFrame(PreviewFrameBufferPool& pool, const ExtensionPreviewFrame& frame, PreviewFrameSpans* spans);

    std::shared_ptr<PreviewFrameBufferStore> m_store;
};

/// Lets buffers' mappings go and removes their files and their sidecars: what a run lent the extension is read by
/// nothing once the run has returned and its result is taken back. A buffer of nothing, or one already removed, is
/// passed over.
///
/// @param buffers the buffers; null entries are passed over.
void removeLentBuffers(const std::vector<SharedImageBuffer*>& buffers) noexcept;

/// Removes what a run lent the extension when it goes, however the run ends: the buffers it names, and every buffer
/// the auxiliary list holds by then.
class LentBuffersRemoval
{
public:
    /// @param buffers the run's own buffers, which outlive this object.
    /// @param auxiliary the run's auxiliary buffers, filled as the run lends them; may be null.
    LentBuffersRemoval(std::vector<SharedImageBuffer*> buffers, std::vector<SharedImageBuffer>* auxiliary) noexcept
        : m_buffers(std::move(buffers)), m_auxiliary(auxiliary)
    {
    }

    ~LentBuffersRemoval()
    {
        try
        {
            if (m_auxiliary != nullptr)
            {
                for (SharedImageBuffer& buffer : *m_auxiliary)
                {
                    m_buffers.push_back(&buffer);
                }
            }
        }
        catch (...)
        {
            // The run's own buffers still go; the session's directory is the host's temporary tree.
        }
        removeLentBuffers(m_buffers);
    }

    LentBuffersRemoval(const LentBuffersRemoval&) = delete;
    LentBuffersRemoval& operator=(const LentBuffersRemoval&) = delete;

private:
    std::vector<SharedImageBuffer*> m_buffers;
    std::vector<SharedImageBuffer>* m_auxiliary = nullptr;
};

/// What the extension wrote into a frame's output, imported as a new asset of the session, with the cards its sidecar
/// states, from the output's mapping as it stands.
///
/// @param manager the seam the frame was lent through.
/// @param session the session the asset belongs to.
/// @param lent the frame's buffers.
/// @param spans where to state how long it took; may be null.
/// @return the asset, or what the import refused.
[[nodiscard]] an::Result<an::consumer::ImageAsset> takePreviewFrameResult(const SharedBufferManager& manager,
                                                                          const an::consumer::EngineSession& session,
                                                                          PreviewFrameBuffers& lent,
                                                                          PreviewFrameSpans* spans = nullptr);

} // namespace an::ui::extensions
