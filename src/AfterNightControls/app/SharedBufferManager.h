// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The extension buffer seam: how an image of the session crosses to an extension host as a planar shared buffer, and
/// how a buffer an extension wrote comes back as an asset of the session.
///
/// An extension host and the application share images through memory-mapped files the protocol library describes -
/// one `SharedBufferDescriptor` per buffer, planar, in one of four representations - and this is the only Controls code
/// that translates between those buffers and the engine's images. It translates and refuses; it never repairs:
///
/// - **An export states what the image is.** The representation is the image's own, or the export is refused by name;
///   a mosaic keeps its Bayer pattern - the phase at the exported origin folded into the pattern the protocol names -
///   and an X-Trans mosaic is refused; the colour space comes from the image's semantics - a grey image is `Grayscale`,
///   a colour image `Linear` while its transfer function is linear and `SRGB` once it is not - and never from the
///   channel count. The samples are copied as they are, validity and all: the protocol carries no validity mask.
/// - **A mapping is a borrow for its own lifetime, never engine storage.** `borrowFrame` describes a mapped buffer as a
///   `RawFrame` whose bytes are released through the mapping's lifetime token, so the frame cannot outlive the mapping
///   and the engine cannot adopt it. Owning a buffer's samples means `importImage`: an explicit copy into an engine
///   allocation of the session, counted and refused like any other, returned as an `ImageAsset`.
/// - **The sidecar keeps its schema.** An export writes the image's FITS and XISF cards, its EXIF cards and its
///   astrometric solution to the JSON sidecar beside the buffer, and an import reads the cards back into the asset's
///   codec metadata, projecting the acquisition record and the solution from them.
/// - **A buffer can be used again.** A caller that keeps a buffer for the next image of its shape exports into it
///   (`exportImageInto`), which states the buffer as a fresh export would and writes its sidecar only when the cards
///   differ from what the file holds; restates it for another use (`restateBuffer`); clears it (`clearBuffer`); and
///   imports what it holds without opening it again (`importMapped`). `exportImage` and `importImage` are those same
///   steps over a buffer of their own.
///
/// Every failure is a returned `Status`: a representation or a channel schema the protocol cannot carry, a buffer whose
/// path is outside the managed root, a descriptor the protocol refuses, or what the engine refuses when it allocates.
/// Nothing here reads a widget or a setting, and nothing here is admitted: a copy is a transfer, not an operation, and
/// runs on the thread that asks for it under the lease that thread holds.
#pragma once

#include "AfterNightControlsExport.h"
#include "app/AfterNightExtensionProtocol.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <QByteArray>
#include <QRect>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace an::ui::extensions
{

class SharedBufferFileMapping;

/// What an allocation of a shared buffer asks for, with no image behind it: an output buffer an extension will fill.
struct SharedImageBufferSpec
{
    /// The extension session the buffer belongs to; its directory under the managed root.
    std::string sessionId;

    /// The buffer's identity; generated when empty.
    std::string bufferId;

    /// The name the extension knows the buffer by - `source`, `selection`, `output_mask`.
    std::string symbolicName;

    /// The buffer's pixels.
    int width = 0;
    int height = 0;
    int channels = 0;

    /// The representation and what the samples mean.
    SharedBufferPixelType pixelType = SharedBufferPixelType::Float32;
    SharedBufferPixelFormat pixelFormat = SharedBufferPixelFormat::Mono;
    SharedBufferColorSpace colorSpace = SharedBufferColorSpace::Linear;

    /// Whether the extension may write the buffer, and what it is for.
    SharedBufferAccessMode accessMode = SharedBufferAccessMode::ReadWrite;
    SharedBufferRole bufferRole = SharedBufferRole::Source;

    /// The rectangle of a larger image the buffer holds, when it holds one: stated together or not at all.
    std::optional<int> offsetX;
    std::optional<int> offsetY;
    std::optional<int> sourceWidth;
    std::optional<int> sourceHeight;

    /// The metadata sidecar beside the buffer, when there is one.
    std::string metadataSidecar;
};

/// A shared buffer the seam allocated or opened: its descriptor and the mapping of its file, borrowed for as long as
/// this object or a frame borrowed from it lives.
///
/// Move-only. Letting the last holder go unmaps the file; the file itself stays under the session's directory until
/// `SharedBufferManager::removeSessionBuffers` removes it.
class AFTERNIGHTCONTROLS_API SharedImageBuffer
{
public:
    /// A buffer of nothing.
    SharedImageBuffer() noexcept;

    /// Lets the mapping go, unless a borrowed frame still holds it.
    ~SharedImageBuffer();

    /// Takes over another buffer.
    ///
    /// @param other the buffer to take over.
    SharedImageBuffer(SharedImageBuffer&& other) noexcept;

    /// Lets this buffer's mapping go and takes over another.
    ///
    /// @param other the buffer to take over.
    /// @return this buffer.
    SharedImageBuffer& operator=(SharedImageBuffer&& other) noexcept;

    SharedImageBuffer(const SharedImageBuffer&) = delete;
    SharedImageBuffer& operator=(const SharedImageBuffer&) = delete;

    /// Whether this holds a mapped buffer.
    [[nodiscard]] bool valid() const noexcept;

    /// What the buffer is, as the protocol describes it - the descriptor an extension is sent.
    [[nodiscard]] const SharedBufferDescriptor& descriptor() const noexcept;

    /// The mapped file.
    [[nodiscard]] const std::filesystem::path& filePath() const noexcept;

    /// How many bytes are mapped: the descriptor's `byte_size`.
    [[nodiscard]] std::uint64_t mappedSize() const noexcept;

    /// The mapped bytes, for writing; null for a buffer of nothing.
    [[nodiscard]] std::byte* bytes() noexcept;

    /// The mapped bytes, for reading; null for a buffer of nothing.
    [[nodiscard]] const std::byte* bytes() const noexcept;

    /// Lets the mapping go now, keeping the descriptor. Releasing twice releases once.
    void release() noexcept;

    /// The mapping's lifetime, for a frame borrowed over it: holding the token keeps the file mapped.
    [[nodiscard]] std::shared_ptr<void> mappingToken() const noexcept;

    /// What the seam last wrote to the buffer's sidecar, as it remembers it; empty when it wrote none.
    [[nodiscard]] const QByteArray& sidecarBytes() const noexcept;

    /// How many times the seam has written the buffer's sidecar.
    [[nodiscard]] std::uint64_t sidecarWrites() const noexcept;

private:
    friend class SharedBufferManager;

    SharedBufferDescriptor m_descriptor;
    std::filesystem::path m_filePath;
    std::shared_ptr<SharedBufferFileMapping> m_mapping;
    QByteArray m_sidecarBytes;
    std::uint64_t m_sidecarWrites = 0u;
};

/// What an image's export to a shared buffer asks for.
struct ImageExportRequest
{
    /// The extension session the buffer belongs to.
    std::string sessionId;

    /// What the buffer is for, and whether the extension may write it.
    SharedBufferRole role = SharedBufferRole::Source;
    SharedBufferAccessMode accessMode = SharedBufferAccessMode::ReadWrite;

    /// The buffer's identity; generated when empty.
    std::string bufferId;

    /// The name the extension knows the buffer by.
    std::string symbolicName;

    /// The rectangle of the image to export, in its pixels; absent exports the whole image. A rectangle is clipped to
    /// the image, and the descriptor then records it and the whole image's size as the buffer's sub-area.
    std::optional<QRect> region;

    /// The file the image came from, written to the sidecar as `FILENAME`; empty writes none.
    std::string fileName;
};

/// The protocol's description of an image's samples: what an export states, derived from the image's description and
/// semantics.
struct SharedBufferImageFormat
{
    SharedBufferPixelType pixelType = SharedBufferPixelType::Float32;
    SharedBufferPixelFormat pixelFormat = SharedBufferPixelFormat::Mono;
    SharedBufferColorSpace colorSpace = SharedBufferColorSpace::Linear;

    /// Field-wise equality.
    [[nodiscard]] friend constexpr bool operator==(const SharedBufferImageFormat& left,
                                                   const SharedBufferImageFormat& right) noexcept = default;
};

/// An image imported from a shared buffer: the asset, and what the buffer and its sidecar called it.
struct ImportedImage
{
    /// The copied samples and their metadata, a new asset of the session.
    an::consumer::ImageAsset asset;

    /// The buffer's symbolic name, or its identity when it has none.
    std::string name;

    /// The `FILENAME` the sidecar stated; empty when it stated none.
    std::string fileName;
};

/// What a cleanup of stale sessions removed.
struct SharedBufferCleanupResult
{
    int removedSessionCount = 0;
    int removedBufferCount = 0;
};

/// The protocol's representation of an engine representation.
///
/// @param type the engine's sample type.
/// @return the protocol's, or `ErrorCode::Unsupported` naming the type for the four the protocol does not carry.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<SharedBufferPixelType> sharedBufferPixelTypeOf(an::SampleType type);

/// The engine representation of a protocol representation.
///
/// @param type the protocol's sample type.
/// @return the engine's.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::SampleType sampleTypeOf(SharedBufferPixelType type) noexcept;

/// What an export of an image, or of a rectangle of it, states about its samples.
///
/// The representation is the image's own; the pixel format is `Mono` for a grey image, `RGB` for a colour one and the
/// Bayer pattern seen from the rectangle's origin for a mosaic; the colour space is `Grayscale` for a grey image and
/// for a colour one `Linear` while its samples are linear and `SRGB` once they are not - read from the transfer
/// function its semantics state, then from the processing-state cards' record of a nonlinear stretch, which is how the
/// product's stretches state one, then from its description.
///
/// @param descriptor what the image is.
/// @param metadata what has been done to it: its semantics and its cards.
/// @param origin the top-left pixel of the rectangle exported; the image's origin for the whole image.
/// @return the format, or `ErrorCode::Unsupported` naming what the protocol cannot carry: a representation outside its
///         four, an X-Trans mosaic, or a channel schema other than mono and RGB.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<SharedBufferImageFormat>
sharedBufferFormatOf(const an::ImageDescriptor& descriptor, const an::ImageMetadata& metadata,
                     QPoint origin = QPoint(0, 0));

/// What the engine makes of a buffer's description: the frame its bytes hold.
///
/// A `Mono` or Bayer buffer is a mono image, a Bayer one with its pattern at the origin; an `RGB` buffer an RGB image.
/// `Linear` reads as linear RGB in normalized samples, `SRGB` as display RGB in encoded samples, `Grayscale` as a grey
/// image; a three-channel `Grayscale` buffer describes no image and is refused.
///
/// @param descriptor the buffer's description.
/// @return the frame description, or `ErrorCode::Unsupported` for a combination that describes no image.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<an::RawFrameDescriptor>
frameDescriptorOf(const SharedBufferDescriptor& descriptor);

/// The transfer function an imported buffer's colour space states.
///
/// @param colorSpace the buffer's colour space.
/// @return linear for `Linear`, sRGB for `SRGB`, unspecified for `Grayscale`.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::TransferFunction
transferFunctionOf(SharedBufferColorSpace colorSpace) noexcept;

/// The seam itself: allocates, exports, opens, borrows and imports shared buffers under one managed root.
///
/// Thread-safe for distinct buffers: nothing here is shared between calls but the root path.
class AFTERNIGHTCONTROLS_API SharedBufferManager
{
public:
    /// A manager over a root directory.
    ///
    /// @param rootPath the directory the sessions' buffers live under.
    explicit SharedBufferManager(std::filesystem::path rootPath = defaultSharedBufferRootPath());

    /// The application's shared-buffer root: the `SharedBuffers` directory of its local data.
    [[nodiscard]] static std::filesystem::path defaultSharedBufferRootPath();

    /// The directory the sessions' buffers live under.
    [[nodiscard]] const std::filesystem::path& rootPath() const noexcept;

    /// Allocates a buffer with nothing in it, mapped for writing.
    ///
    /// @param spec what the buffer is.
    /// @return the buffer, or `ErrorCode::InvalidArgument` for a specification the protocol refuses, or
    ///         `ErrorCode::ExternalFailure` when the file cannot be made or mapped.
    [[nodiscard]] an::Result<SharedImageBuffer> allocateBuffer(const SharedImageBufferSpec& spec) const;

    /// Exports an image, or a rectangle of it, to a new buffer: allocates it in the image's format, copies the samples
    /// plane by plane and writes the metadata sidecar beside it - `allocateBuffer`, then `exportImageInto`.
    ///
    /// The caller reads the image under a lease it holds; the copy runs on the calling thread. A rectangle's export
    /// carries the image's cards with the solution moved to it, as a crop moves one, or without the astrometric ones
    /// when they state none that can be moved.
    ///
    /// @param image the image, under the caller's lease.
    /// @param request what the export is for.
    /// @return the buffer, or what `sharedBufferFormatOf`, `allocateBuffer` or the sidecar's write refused, or
    ///         `ErrorCode::InvalidArgument` for a rectangle that lies outside the image.
    [[nodiscard]] an::Result<SharedImageBuffer> exportImage(const an::consumer::AnyImage& image,
                                                            const ImageExportRequest& request) const;

    /// Exports an image, or a rectangle of it, into a buffer allocated before, of the width, height, channels and
    /// representation the export fills: states the buffer as a fresh export would - its pixel format, colour space,
    /// place in the image, symbolic name, role and access - copies the samples, and renders the sidecar as a fresh
    /// export does, writing it only when what it renders differs from what the buffer's sidecar holds. The buffer
    /// keeps its identity, its file and its sidecar's path; the request's identity is not read.
    ///
    /// @param image the image, under the caller's lease.
    /// @param request what the export is for.
    /// @param buffer the buffer, mapped.
    /// @return success; `ErrorCode::NotInitialized` for a buffer holding no mapping; `ErrorCode::InvalidArgument` for
    ///         a buffer of another shape, which is left as it was, or a rectangle outside the image; or what
    ///         `sharedBufferFormatOf` or the sidecar's write refused.
    [[nodiscard]] an::Status exportImageInto(const an::consumer::AnyImage& image, const ImageExportRequest& request,
                                             SharedImageBuffer& buffer) const;

    /// States a buffer allocated before as an allocation of a specification of its shape would: its pixel format,
    /// colour space, place in an image, symbolic name, role, access and sidecar's path. Its identity, file, strides
    /// and samples stay.
    ///
    /// @param spec what the buffer is now; its session and identity are not read.
    /// @param buffer the buffer, mapped.
    /// @return success; `ErrorCode::NotInitialized` for a buffer holding no mapping; `ErrorCode::InvalidArgument` for
    ///         a specification of another width, height, channel count or representation, or one the protocol
    ///         refuses, the buffer then left as it was.
    [[nodiscard]] an::Status restateBuffer(const SharedImageBufferSpec& spec, SharedImageBuffer& buffer) const;

    /// Writes zeros over every byte a buffer maps, as a new allocation starts.
    ///
    /// @param buffer the buffer, mapped.
    /// @return success, or `ErrorCode::NotInitialized` for a buffer holding no mapping.
    [[nodiscard]] an::Status clearBuffer(SharedImageBuffer& buffer) const;

    /// Opens a buffer an extension or the host described, mapped for reading or for writing as its access mode says.
    ///
    /// @param descriptor the buffer's description.
    /// @return the buffer, or `ErrorCode::InvalidArgument` for a descriptor the protocol refuses or a path outside
    ///         the managed root, or `ErrorCode::ExternalFailure` when the file is missing, too small or cannot be
    ///         mapped.
    [[nodiscard]] an::Result<SharedImageBuffer> openBuffer(const SharedBufferDescriptor& descriptor) const;

    /// Describes a mapped buffer as a frame: a borrow whose bytes are released through the mapping's lifetime, so the
    /// frame cannot outlive the mapping and the engine refuses to adopt it. Writable when the buffer is.
    ///
    /// @param buffer the buffer; it keeps its mapping while the frame lives.
    /// @return the frame, or what `frameDescriptorOf` or the engine's layout check refused.
    [[nodiscard]] static an::Result<an::RawFrame> borrowFrame(SharedImageBuffer& buffer);

    /// Imports a buffer as a new asset of a session: an explicit copy of its samples into an engine allocation, with
    /// the metadata its sidecar states, the acquisition record and the solution projected from the cards -
    /// `openBuffer`, then `importMapped`.
    ///
    /// The copy runs on the calling thread. The mapping is let go when the import returns.
    ///
    /// @param session the session the asset belongs to.
    /// @param descriptor the buffer's description.
    /// @return the imported image, or what `openBuffer`, `frameDescriptorOf`, the sidecar's read or the engine refused.
    [[nodiscard]] an::Result<ImportedImage> importImage(const an::consumer::EngineSession& session,
                                                        const SharedBufferDescriptor& descriptor) const;

    /// Imports a buffer this seam allocated or opened, as `importImage` imports one by its description, without
    /// opening it again: the samples it holds now, the sidecar its description names. The buffer keeps its mapping.
    ///
    /// @param session the session the asset belongs to.
    /// @param buffer the buffer, mapped.
    /// @return the imported image; `ErrorCode::NotInitialized` for a buffer holding no mapping; or what
    ///         `frameDescriptorOf`, the sidecar's read or the engine refused.
    [[nodiscard]] an::Result<ImportedImage> importMapped(const an::consumer::EngineSession& session,
                                                         SharedImageBuffer& buffer) const;

    /// Removes a session's directory and every buffer in it.
    ///
    /// @param sessionId the session.
    /// @return success, also when there was nothing to remove; `ErrorCode::InvalidArgument` for an empty identity;
    ///         `ErrorCode::ExternalFailure` when the directory cannot be removed.
    [[nodiscard]] an::Status removeSessionBuffers(std::string_view sessionId) const;

    /// Removes the directory of every session under the root that is not live.
    ///
    /// @param liveSessionIds the sessions to keep.
    /// @return what was removed, or `ErrorCode::ExternalFailure` when a directory cannot be read or removed, with what
    ///         was removed before it counted.
    [[nodiscard]] an::Result<SharedBufferCleanupResult>
    cleanupStaleSessions(const std::vector<std::string>& liveSessionIds) const;

private:
    std::filesystem::path m_rootPath;
};

} // namespace an::ui::extensions
