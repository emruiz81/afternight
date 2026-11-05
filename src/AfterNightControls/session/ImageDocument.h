// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Controls record around one image asset: what a viewer, a process dialog or the history knows an image by.
///
/// The consumer runtime's `ImageAsset` owns the samples, the validity mask and the revision identity, and decides who
/// may read and write them. A document adds what only the user interface knows: the names the image is shown and saved
/// under, an identity that survives the asset being replaced, the sequence it is a frame of, how it is presented, and
/// whether it is still open. It is what every Controls interface passes: a process reads a `const ImageDocument&` and
/// writes an `ImageDocumentWriter&`.
///
/// **A document holds one asset at a time, and the asset changes rather than the samples being swapped.** The writer
/// writes the current asset in place through its write lease, or replaces it with another asset - which has a new
/// identity, so nothing derived from the old one can be taken for the new. A reader that asked for the document while a
/// replacement was made reads the new asset, never the one that was let go. A replacement that writes nothing in place
/// - a preview's frame, a result shown - needs no writer at all (`tryReplace`): it waits for no reader, and what reads
/// the image the document held keeps reading it.
///
/// **Holding a document grants nothing.** Reading takes the current asset's read lease, writing takes the document's
/// writer, and the revision is published when the writer lets go, as the asset's is.
///
/// **A document may show an asset another document holds.** One made by `share` reads that asset and hands out no
/// writer, so a view shows another document's image without a copy of it, and nothing can write that image through it.
///
/// **Closing comes before draining.** `close` takes effect at once: every commit ticket captured against the document
/// is refused from then on and no writer is handed out, while work already reading it finishes and lets go.
#pragma once

#include "AfterNightControlsExport.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Color/ChannelScope.h>
#include <AfterNightEngine/Algorithms/Demosaic/Demosaic.h>
#include <AfterNightEngine/Algorithms/Presentation/DisplayPresentation.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/IO/SequenceDescriptor.h>

#include <QString>

#include <cstdint>
#include <memory>
#include <optional>
#include <stop_token>
#include <vector>

namespace an::ui
{

class ImageDocumentWriter;

namespace detail
{
class DocumentState;
} // namespace detail

/// The names a document is shown and saved under.
struct DocumentNames
{
    /// What the document's window and lists call it.
    QString displayName;

    /// The file it was read from or last saved to; empty for an image that has never been a file.
    QString sourcePath;
};

/// The sequence a document shows one frame of.
struct DocumentSequence
{
    /// The file the frames are read from - a SER, AVI or image-list file - as it was opened.
    QString sourcePath;

    /// What the sequence is, as its source described it when it was opened.
    an::SequenceDescriptor descriptor{};

    /// The frames selected for work over the sequence, ascending and distinct; absent selects every frame.
    std::optional<std::vector<std::uint64_t>> selectedFrames;

    /// The zero-based index of the frame the document shows.
    std::uint64_t currentFrame = 0u;

    /// The method chosen for a mosaic frame when the sequence is shown or processed demosaiced: the product's one
    /// demosaic setting, stated on the GUI thread when the sequence is shown, so a worker reading a frame never reads a
    /// setting. Absent reads as RCD, the setting's default. A frame of a tabulated mosaic - an X-Trans sensor's
    /// six-by-six table - is demosaiced by the chosen method's counterpart for its shape, as `demosaicMethodForMosaic`
    /// gives it.
    std::optional<an::DemosaicMethod> demosaic;

    /// When the sequence was recorded or made, as its file stated it when it was opened: a SER header's UTC start time,
    /// or its first frame's timestamp when the header states none; an image list's creation time. Absent when the file
    /// states none.
    std::optional<an::UtcInstant> recorded;

    /// The first frame's description, read when the sequence was opened, for a sequence whose frames describe
    /// themselves - an image list - and so state no frame every frame is held to: what the summary shows of its frames.
    /// No frame is held to it.
    std::optional<an::RawFrameDescriptor> firstFrame;

    /// For an image list, the size in bytes of every file it names, measured when it was opened; absent for another
    /// container, or when a file it names could not be measured then.
    std::optional<std::uint64_t> namedFilesBytes;
};

/// How a document's viewer presents it.
struct DocumentPresentation
{
    /// The presentation operation's request: promotion, neutralization and calibration, the stretch, saturation,
    /// brightness and contrast, the output and how an invalid pixel is shown.
    an::DisplayPresentationRequest request{};

    /// The channel or colour component the viewer shows.
    an::ChannelScope channel = an::ChannelScope::Whole;
};

/// What a result derived from a document depends on in it: which document, and the revision of the asset it held.
struct DocumentRevision
{
    /// The document's identity, unique in the process.
    std::uint64_t documentIdentity = 0u;

    /// The revision of the asset the document held.
    an::consumer::AssetRevision asset{};

    /// Field-wise equality.
    [[nodiscard]] friend constexpr bool operator==(const DocumentRevision& left,
                                                   const DocumentRevision& right) noexcept = default;
};

/// The Controls record around one image asset.
///
/// Move-only: one owner, and shared ownership is a shared pointer to it. Thread-safe: every member may be called from
/// any thread, though the names, the sequence and the presentation are the GUI thread's to change. The leases and the
/// writer it hands out keep what they lease alive, so they may outlive the document object.
class ImageDocument
{
public:
    /// A document that holds nothing, as a moved-from one does.
    AFTERNIGHTCONTROLS_API ImageDocument() noexcept;

    /// Lets the document's state go; the asset is destroyed with its last owner and its last lease.
    AFTERNIGHTCONTROLS_API ~ImageDocument();

    /// Takes over another document.
    ///
    /// @param other the document to take over.
    AFTERNIGHTCONTROLS_API ImageDocument(ImageDocument&& other) noexcept;

    /// Lets this document's state go and takes over another's.
    ///
    /// @param other the document to take over.
    /// @return this document.
    AFTERNIGHTCONTROLS_API ImageDocument& operator=(ImageDocument&& other) noexcept;

    ImageDocument(const ImageDocument&) = delete;
    ImageDocument& operator=(const ImageDocument&) = delete;

    /// Makes a document around an asset, with a new identity.
    ///
    /// @param asset the asset; it must hold an image.
    /// @param names what the document is shown and saved under.
    /// @return the document, or `ErrorCode::NotInitialized` for an asset that holds nothing.
    [[nodiscard]] static AFTERNIGHTCONTROLS_API an::Result<ImageDocument> create(an::consumer::ImageAsset&& asset,
                                                                                 DocumentNames names = {});

    /// Makes a document, with a new identity, that shows an asset another document holds without owning it: it reads
    /// the asset as that document does, and refuses every writer, so nothing writes through it an image another
    /// document shows - in place or by replacing it. What a view shows of another image costs no copy; a view that is
    /// to show something else takes another document.
    ///
    /// @param asset the asset, held by another document; it must hold an image.
    /// @param names what the document is shown and saved under.
    /// @return the document, or `ErrorCode::NotInitialized` for no asset or one that holds nothing.
    [[nodiscard]] static AFTERNIGHTCONTROLS_API an::Result<ImageDocument>
    share(std::shared_ptr<const an::consumer::ImageAsset> asset, DocumentNames names = {});

    /// Whether this is a document at all; false for a default-constructed or moved-from one.
    [[nodiscard]] AFTERNIGHTCONTROLS_API bool valid() const noexcept;

    /// Whether the document shows an asset it does not own (`share`), and so hands out no writer.
    [[nodiscard]] AFTERNIGHTCONTROLS_API bool isShared() const noexcept;

    /// The document's identity, unique in the process and kept when its asset is replaced; zero for a document that
    /// holds nothing.
    [[nodiscard]] AFTERNIGHTCONTROLS_API std::uint64_t identity() const noexcept;

    /// Whether the document is open: false once it has been closed, and for a document that holds nothing.
    [[nodiscard]] AFTERNIGHTCONTROLS_API bool isOpen() const noexcept;

    /// Closes the document: every commit ticket captured against it is refused from now on and no writer is handed
    /// out. Leases and a writer already held are kept until their holders let go, so work in flight drains.
    /// Closing twice closes once.
    AFTERNIGHTCONTROLS_API void close() noexcept;

    /// The names the document is shown and saved under.
    [[nodiscard]] AFTERNIGHTCONTROLS_API DocumentNames names() const;

    /// Renames the document.
    ///
    /// @param names the new names.
    AFTERNIGHTCONTROLS_API void setNames(DocumentNames names);

    /// The sequence the document shows a frame of, if it is one.
    [[nodiscard]] AFTERNIGHTCONTROLS_API std::optional<DocumentSequence> sequence() const;

    /// States the sequence the document shows a frame of, or that it is a single image.
    ///
    /// @param sequence the sequence, or nothing.
    AFTERNIGHTCONTROLS_API void setSequence(std::optional<DocumentSequence> sequence);

    /// How the document's viewer presents it.
    [[nodiscard]] AFTERNIGHTCONTROLS_API DocumentPresentation presentation() const;

    /// Changes how the document's viewer presents it, moving the presentation generation.
    ///
    /// @param presentation the new presentation.
    AFTERNIGHTCONTROLS_API void setPresentation(DocumentPresentation presentation);

    /// Moves every time the presentation is changed, whether or not a field differs; starts at zero.
    [[nodiscard]] AFTERNIGHTCONTROLS_API std::uint64_t presentationGeneration() const noexcept;

    /// The asset the document holds now, for ownership: holding it keeps the image alive and grants nothing else, and
    /// it is not the document's image once the document's writer replaces it.
    [[nodiscard]] AFTERNIGHTCONTROLS_API std::shared_ptr<const an::consumer::ImageAsset> asset() const;

    /// The last published revision of what the document holds, read without a lease.
    [[nodiscard]] AFTERNIGHTCONTROLS_API DocumentRevision revision() const;

    /// The last published description of the image the document holds, read without a lease.
    [[nodiscard]] AFTERNIGHTCONTROLS_API std::optional<an::ImageDescriptor> descriptor() const;

    /// Takes a read lease on the asset the document holds, waiting while a writer holds or waits for it. When the
    /// asset is replaced while the lease is awaited, the lease is taken on the replacement instead.
    ///
    /// @param stop stops the wait.
    /// @return the lease, `ErrorCode::Cancelled` when stopped while waiting, or `ErrorCode::NotInitialized` for a
    ///         document that holds nothing.
    [[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<an::consumer::AssetReadLease> read(std::stop_token stop = {}) const;

    /// Takes a read lease on the asset the document holds only if no writer holds or waits for it. Never blocks, so
    /// a GUI thread may call it.
    ///
    /// @return the lease, `ErrorCode::Busy`, or `ErrorCode::NotInitialized` for a document that holds nothing.
    [[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<an::consumer::AssetReadLease> tryRead() const;

    /// Takes the document's writer, waiting until nothing reads or writes the asset it holds. When the asset is
    /// replaced while the writer is awaited, the writer is taken on the replacement instead.
    ///
    /// @param stop stops the wait.
    /// @return the writer; `ErrorCode::Cancelled` when stopped while waiting; `ErrorCode::InvalidState` for a closed
    ///         document and for one that shows an asset it does not own; `ErrorCode::NotInitialized` for a document
    ///         that holds nothing.
    [[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<ImageDocumentWriter> write(std::stop_token stop = {});

    /// Takes the document's writer only if nothing reads or writes the asset it holds. Never blocks.
    ///
    /// @return the writer, `ErrorCode::Busy`, or what `write` refuses otherwise.
    [[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<ImageDocumentWriter> tryWrite();

    /// Makes another asset the document's image without waiting for its readers, as a writer's `replace` does but with
    /// no writer: what reads the image the document holds now keeps reading that image, and every lease asked for from
    /// now on is taken on the replacement. A writer holding the document is never cut short - the replacement is
    /// refused while one does. Never blocks.
    ///
    /// @param replacement the new image, from the same session, written whole; nothing else may lease it.
    /// @return success; `ErrorCode::Busy` while a writer holds the document or when the replacement is leased;
    ///         `ErrorCode::InvalidState` for a closed document and for one that shows an asset it does not own;
    ///         `ErrorCode::NotInitialized` for a document or a replacement that holds nothing. On a failure the
    ///         document is unchanged and `replacement` is untouched.
    [[nodiscard]] AFTERNIGHTCONTROLS_API an::Status tryReplace(an::consumer::ImageAsset&& replacement);

    /// A copy of the image the document holds, as a new asset with a new identity, taken under a read lease.
    ///
    /// @param stop stops the wait for the lease.
    /// @return the copy, or what `read` or the copy refuses.
    [[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<an::consumer::ImageAsset> snapshot(std::stop_token stop = {}) const;

private:
    friend class RequestTracker;

    explicit ImageDocument(std::shared_ptr<detail::DocumentState> state) noexcept;

    std::shared_ptr<detail::DocumentState> m_state;
};

/// Permission to change a document: its writer lease.
///
/// Holds the write lease on the asset the document holds, so it excludes every reader and every other writer of the
/// document, and a replacement made without a writer (`ImageDocument::tryReplace`). The samples, the description and
/// metadata through a stated transition, and the validity mask are written through `asset()`; `replace` makes another
/// asset the document's image. The revision is published when the writer lets go.
///
/// Move-only. Keeps what it leases alive, and may be released on any thread.
class ImageDocumentWriter
{
public:
    /// A writer of nothing.
    AFTERNIGHTCONTROLS_API ImageDocumentWriter() noexcept;

    /// Publishes the revision and lets the document go.
    AFTERNIGHTCONTROLS_API ~ImageDocumentWriter();

    /// Takes over another writer.
    ///
    /// @param other the writer to take over.
    AFTERNIGHTCONTROLS_API ImageDocumentWriter(ImageDocumentWriter&& other) noexcept;

    /// Releases this writer and takes over another.
    ///
    /// @param other the writer to take over.
    /// @return this writer.
    AFTERNIGHTCONTROLS_API ImageDocumentWriter& operator=(ImageDocumentWriter&& other) noexcept;

    ImageDocumentWriter(const ImageDocumentWriter&) = delete;
    ImageDocumentWriter& operator=(const ImageDocumentWriter&) = delete;

    /// Whether this is a writer.
    [[nodiscard]] AFTERNIGHTCONTROLS_API bool valid() const noexcept;

    /// The identity of the document written, or zero for a writer of nothing.
    [[nodiscard]] AFTERNIGHTCONTROLS_API std::uint64_t documentIdentity() const noexcept;

    /// The write lease on the asset the document holds now. Calling it on a writer of nothing is a defect.
    [[nodiscard]] AFTERNIGHTCONTROLS_API an::consumer::AssetWriteLease& asset() noexcept;

    /// The write lease on the asset the document holds now, for reading. Calling it on a writer of nothing is a
    /// defect.
    [[nodiscard]] AFTERNIGHTCONTROLS_API const an::consumer::AssetWriteLease& asset() const noexcept;

    /// The revision of what the document holds as this writer leaves it now.
    [[nodiscard]] AFTERNIGHTCONTROLS_API DocumentRevision revision() const noexcept;

    /// Makes another asset the document's image, keeping the document's identity, names, sequence and presentation.
    ///
    /// The replacement keeps its own identity, so every result derived from the asset it replaces is stale. The writer
    /// goes on to hold the replacement's write lease; the replaced asset's lease is let go, and anything still holding
    /// the replaced asset keeps reading it, not the document. This is the one way a document's image changes shape or
    /// representation, and how a copy, a conversion or a restored snapshot becomes the document's image.
    ///
    /// @param replacement the new image, from the same session; nothing else may lease it.
    /// @return success; `ErrorCode::NotInitialized` for an asset that holds nothing or a writer of nothing;
    ///         `ErrorCode::InvalidState` once the document is closed; `ErrorCode::Busy` when the replacement is leased.
    ///         On a failure the document is unchanged and `replacement` is untouched.
    [[nodiscard]] AFTERNIGHTCONTROLS_API an::Status replace(an::consumer::ImageAsset&& replacement);

    /// Publishes the revision and lets the document go now. Releasing twice releases once.
    AFTERNIGHTCONTROLS_API void release() noexcept;

private:
    friend class ImageDocument;

    ImageDocumentWriter(std::shared_ptr<detail::DocumentState> document,
                        std::shared_ptr<an::consumer::ImageAsset> asset, an::consumer::AssetWriteLease lease) noexcept;

    std::shared_ptr<detail::DocumentState> m_document;
    std::shared_ptr<an::consumer::ImageAsset> m_asset;
    an::consumer::AssetWriteLease m_lease;
};

} // namespace an::ui
