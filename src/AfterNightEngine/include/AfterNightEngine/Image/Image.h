// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// An image: owned samples, what they are, what has been done to them.
///
/// An image holds four things and does nothing else: it decodes no file, converts no representation,
/// resizes nothing, computes no statistic, reads no global setting and dispatches on no pixel type.
/// **There are no algorithm methods on it at all**, which is the rule that keeps it that way: an
/// operation is a function that takes views and descriptors, and adding a method here is how a class
/// starts growing a file that never stops.
///
/// **Storage type and arithmetic type are separate.** An image may hold any of the eight resident
/// representations, which is what lets a sixteen-bit frame stay sixteen-bit in memory and be read
/// directly - the measured alternative, materialising a float buffer per operation, costs three to
/// five times the memory traffic at frame scale and buys nothing. Which width an operation *computes*
/// in is `Image/ComputePrecision.h`, and it is a separate decision made per operation.
///
/// **A working image is an ordinary image that somebody chose.** `Image<float>` and `Image<double>`
/// are the explicit working forms for an operation that widens range or a chain that must not
/// requantize between stages. `createWorkingImage` is where that choice is written down, because the
/// alternative - inheriting a working representation from whatever the previous stage happened to
/// produce - is how a process chain acquires a requantization nobody planned.
///
/// **There is no implicit conversion anywhere.** Not between two images of different sample types,
/// not from an image to a view, not from an image to its storage. Every one of those is a named call,
/// because each of them is either a copy, a reinterpretation or a borrow, and a language feature that
/// makes all three look like an assignment is how a full-frame conversion ends up inside a loop.
///
/// **Writing is asked for, and asking bumps the pixel generation.** Read access is free and changes
/// nothing; write access is a handle, and obtaining one moves the generation whether or not a byte
/// is written. That is coarse on purpose: an unnecessary recomputation costs time, and a derived
/// result that survives a write it should not have costs a wrong answer with no symptom. A revision
/// that a mutator is *supposed* to increment is a stale hit waiting for the one call site that
/// forgets.
///
/// **Two generations, and neither is a cache key.** The pixel generation moves when the samples may
/// have changed; the semantic generation moves when what they *mean* changed - the description, the
/// processing meaning, or the astrometric solution. A cache combines them with the things an image
/// cannot know: which secondary inputs a result used, which region it covers, which parameters
/// produced it. That composite key belongs to whatever owns the cache, and pushing it in here is how
/// an image class becomes responsible for selections and parameters it should know nothing about.
/// A generation is only ever compared with another generation of the same image; two images say
/// nothing to each other by their numbers.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ComputePrecision.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageStorage.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/StorageArrangement.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace an
{

/// An image of one of the eight resident representations. Defined below.
template <ResidentSample SampleT> class Image;

/// The same description with a different representation, when one exists.
///
/// What a working image and a conversion target are built from: everything about the picture stays,
/// and only what a sample is changes. It can fail - a physically scaled image has no integer
/// description, because nothing in a descriptor says what physical quantity an integer stands for -
/// and it says so rather than producing a description that would be refused later.
///
/// @param descriptor the description to re-represent.
/// @param sampleType what the samples are to become.
/// @return the new description, or `ErrorCode::InvalidArgument` for a representation outside the
///         eight, or `ErrorCode::IncompatibleArguments` naming the descriptor rule that refused.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ImageDescriptor> describeSamplesAs(const ImageDescriptor& descriptor,
                                                                             SampleType sampleType);

/// Whether a description and a memory geometry are about one image, with the reason when they are
/// not.
///
/// The pairing rule, stated once so that every entry point that accepts both reports it the same
/// way. It is deliberately more than `layoutDescribes`: that answers whether the two agree, and
/// this says which of the two facts they disagree about.
///
/// @param descriptor what the image is.
/// @param layout where its samples sit.
/// @param operation the engine operation to name in the failure.
/// @return success when the extents match and the layout's element size is the description's;
///         otherwise `ErrorCode::IncompatibleArguments` saying which.
[[nodiscard]] AFTERNIGHTENGINE_API Status checkImageDescribesLayout(const ImageDescriptor& descriptor,
                                                                    const ImageLayout& layout, const char* operation);

/// Permission to change an image's samples, and proof that the change was recorded.
///
/// Obtaining one moves the image's pixel generation, so a derived result computed before it is
/// invalidated whether or not a sample is actually written. Holding one does not lock anything:
/// this is a record that a write was asked for, not a mutual exclusion, and two threads writing one
/// image through two handles race exactly as they would through two pointers.
///
/// Move-only, and it borrows the image. It must not outlive the image it came from, and any change
/// to that image that could move its samples - a clone assigned over it, a release - invalidates
/// the view this holds.
///
/// @tparam SampleT the image's sample type.
template <ResidentSample SampleT> class ImageWriteAccess
{
public:
    /// Copying is deleted: a second copy would be a second write nobody recorded.
    ImageWriteAccess(const ImageWriteAccess&) = delete;

    /// Copying is deleted. See the copy constructor.
    ImageWriteAccess& operator=(const ImageWriteAccess&) = delete;

    /// Takes over another handle.
    ///
    /// @param other the handle to take over.
    ImageWriteAccess(ImageWriteAccess&& other) noexcept = default;

    /// Assignment is deleted: a handle names one write of one image, and replacing it in place
    /// would leave the generation it recorded belonging to nothing.
    ImageWriteAccess& operator=(ImageWriteAccess&&) = delete;

    /// The samples, through which they may be changed.
    ///
    /// @return the view. It borrows the image and must not outlive it.
    [[nodiscard]] constexpr const ImageView<SampleT>& view() const noexcept
    {
        return m_view;
    }

    /// The generation this write recorded.
    ///
    /// @return the image's pixel generation as it stood after the handle was obtained.
    [[nodiscard]] constexpr std::uint64_t pixelGeneration() const noexcept
    {
        return m_pixelGeneration;
    }

private:
    /// Only an image hands one of these out.
    friend class Image<SampleT>;

    /// Records a write of these samples.
    ///
    /// @param view the samples.
    /// @param pixelGeneration what the image's generation became.
    constexpr ImageWriteAccess(ImageView<SampleT> view, std::uint64_t pixelGeneration) noexcept
        : m_view(view), m_pixelGeneration(pixelGeneration)
    {
    }

    /// The samples.
    ImageView<SampleT> m_view;

    /// What the image's generation became when this handle was obtained.
    std::uint64_t m_pixelGeneration;
};

/// An image: owned samples of one of the eight resident representations, what they are, what has
/// been done to them, and how much of that has changed.
///
/// Move-only, because the storage underneath is. A default-constructed image holds nothing, which is
/// also what a move and a `release` leave behind; `valid` is how to ask, and every accessor answers
/// for that state rather than requiring the question to have been asked.
///
/// Not thread-safe. Two threads may read one image concurrently, and anything that changes it -
/// obtaining write access, applying a transition, releasing it - is a write to the image object and
/// must be synchronised like any other.
///
/// @tparam SampleT one of the eight resident sample types. Const-qualified types are deliberately
///         not accepted: an image owns memory that something has to be able to write.
template <ResidentSample SampleT> class Image
{
public:
    /// The type of one sample.
    using Sample = SampleT;

    /// Which representation this image's samples are, as a value.
    static constexpr SampleType kSampleType = kSampleTypeOf<SampleT>;

    /// An image that holds nothing.
    Image() noexcept = default;

    /// Takes over another image, leaving it holding nothing.
    ///
    /// @param other the image to take over.
    Image(Image&& other) noexcept = default;

    /// Releases this image and takes over another.
    ///
    /// @param other the image to take over.
    /// @return this image.
    Image& operator=(Image&& other) noexcept = default;

    /// Copying is deleted: a frame is expensive, and sharing one is a decision a call site should
    /// have to write. `clone` is the copy that says so.
    Image(const Image&) = delete;

    /// Copying is deleted. See the copy constructor.
    Image& operator=(const Image&) = delete;

    /// An image of this description, with memory from this engine context.
    ///
    /// @param engine the context whose memory resource and budget own the samples.
    /// @param descriptor what the image is. Its sample type must be this image's.
    /// @param arrangement how the channels are spread across planes.
    /// @param initialization what the samples hold before anything writes to them.
    /// @param metadata what is known about the image. Nothing by default, which is the honest
    ///        description of a buffer the engine has just allocated.
    /// @return the image; `ErrorCode::IncompatibleArguments` when the description names another
    ///         representation; `ErrorCode::InvalidArgument` for `StorageArrangement::SemiPlanar` or
    ///         a value outside the enumeration, which `layoutRequestFor` derives no layout for; or
    ///         whatever `ImageStorage::create` reported for the geometry or the allocation.
    [[nodiscard]] static Result<Image> create(const EngineContext& engine, const ImageDescriptor& descriptor,
                                              StorageArrangement arrangement, StorageInitialization initialization,
                                              ImageMetadata metadata = ImageMetadata{})
    {
        Result<ImageStorage<SampleT>> storage = createStorage(engine, descriptor, arrangement, initialization);
        if (!storage.hasValue())
        {
            return storage.status();
        }
        return Image{std::move(*storage), descriptor, std::move(metadata)};
    }

    /// An image of this description, with every sample set to one value.
    ///
    /// @param engine the context whose memory resource and budget own the samples.
    /// @param descriptor what the image is.
    /// @param arrangement how the channels are spread across planes.
    /// @param value what every sample holds.
    /// @param metadata what is known about the image.
    /// @return the image, or whatever `create` reported.
    [[nodiscard]] static Result<Image> createFilled(const EngineContext& engine, const ImageDescriptor& descriptor,
                                                    StorageArrangement arrangement, SampleT value,
                                                    ImageMetadata metadata = ImageMetadata{})
    {
        Result<ImageStorage<SampleT>> storage =
            createStorage(engine, descriptor, arrangement, StorageInitialization::Uninitialized);
        if (!storage.hasValue())
        {
            return storage.status();
        }
        const Status filled = storage->fill(value);
        if (filled.failed())
        {
            return filled;
        }
        return Image{std::move(*storage), descriptor, std::move(metadata)};
    }

    /// A destination for an operation that reads `source`, carrying what is known about `source`.
    ///
    /// `create` is right to leave a fresh image with no metadata: the engine is handed a
    /// destination and writes samples into it, and deciding that a destination inherits its input's
    /// history is a policy rather than a mechanic. But a workflow that allocates its destinations
    /// with `create` and forgets the last argument gets a correct picture with no acquisition
    /// record, no lineage and no solution, and nothing fails - which is the one failure mode a
    /// name can prevent. This is that name.
    ///
    /// What is carried is everything `source` knows, unchanged. What the operation then did to it
    /// is the transition's to state: `Image/OperationEffect.h` declares the effect, `transition`
    /// applies it, and a solution a geometric change invalidates is withdrawn there rather than
    /// here. So the order is allocate from the input, run, then state the change.
    ///
    /// @tparam SourceT the source's representation, which need not be this image's - a working
    ///         image widened from a decoded frame is the usual case.
    /// @param engine the context whose memory resource and budget own the samples.
    /// @param descriptor what the destination is, which is not read from `source`: an operation
    ///        that changes the extent, the representation or the channels states that here.
    /// @param arrangement how the channels are spread across planes.
    /// @param initialization what the samples hold before anything writes to them.
    /// @param source the image the operation reads, whose metadata the destination carries. An
    ///        image that holds nothing carries metadata that states nothing, which is copied as
    ///        faithfully as any other.
    /// @return the image, or whatever `create` reported.
    template <ResidentSample SourceT>
    [[nodiscard]] static Result<Image>
    createDestination(const EngineContext& engine, const ImageDescriptor& descriptor, StorageArrangement arrangement,
                      StorageInitialization initialization, const Image<SourceT>& source)
    {
        return create(engine, descriptor, arrangement, initialization, source.metadata());
    }

    /// An image over memory that already holds its samples.
    ///
    /// For a caller that filled engine storage before it had a description to put on it - a
    /// transfer target, a kernel output written into a buffer taken in advance.
    ///
    /// @param storage the memory, which is taken over. Must hold memory and must be laid out as the
    ///        description says.
    /// @param descriptor what those samples are. Must name this image's exact sample representation.
    /// @param metadata what is known about the image.
    /// @return the image; `ErrorCode::NotInitialized` when the storage holds nothing;
    ///         `ErrorCode::IncompatibleArguments` when the description's sample type or extent is
    ///         not the storage's. The storage is left untouched on refusal.
    [[nodiscard]] static Result<Image> fromStorage(ImageStorage<SampleT>&& storage, const ImageDescriptor& descriptor,
                                                   ImageMetadata metadata = ImageMetadata{})
    {
        const std::optional<ImageLayout> layout = storage.layout();
        if (!layout.has_value())
        {
            return Status::failure(ErrorCode::NotInitialized,
                                   StatusDetail{.message = "an image needs samples, and this storage holds none",
                                                .operation = "Image::fromStorage",
                                                .domain = ErrorDomain::Image});
        }
        if (descriptor.sampleType() != kSampleType)
        {
            return Status::failure(
                ErrorCode::IncompatibleArguments,
                StatusDetail{.message = "the description names a different sample representation",
                             .operation = "Image::fromStorage",
                             .domain = ErrorDomain::Image,
                             .context = {StatusContextField::ofText(ContextKey::Parameter, "descriptor")}});
        }
        const Status describes = checkImageDescribesLayout(descriptor, *layout, "Image::fromStorage");
        if (describes.failed())
        {
            return describes;
        }
        return Image{std::move(storage), descriptor, std::move(metadata)};
    }

    /// An image that takes a decoded frame over, with no copy and no conversion.
    ///
    /// The zero-cost path from a boundary into the processing model, and the reason a sample buffer
    /// records who owns it. It succeeds only for a frame whose representation is one of the eight,
    /// whose bytes are in host order, and whose memory came from an engine context; anything else is
    /// refused by name, so a caller knows whether it needs a conversion, a swap or a copy.
    ///
    /// @param frame the decoded frame, which is taken over. Left holding nothing on success, and
    ///        untouched by every refusal made before its bytes are released.
    /// @param metadata what is known about the image - typically the acquisition facts and
    ///        preserved cards the same decode produced.
    /// @return the image; `ErrorCode::IncompatibleArguments` when the frame holds another
    ///         representation than this image's; whatever `RawFrame::checkAdoptable` reported -
    ///         `ErrorCode::NotInitialized`, `ErrorCode::Unsupported`,
    ///         `ErrorCode::IncompatibleArguments` or `ErrorCode::InvalidState`; or, with the frame
    ///         already released, whatever `ImageStorage::fromBytes` reported about the adopted
    ///         memory.
    [[nodiscard]] static Result<Image> adopt(RawFrame&& frame, ImageMetadata metadata = ImageMetadata{})
    {
        const std::optional<RawFrameDescriptor> described = frame.descriptor();
        if (described.has_value() && described->residentSampleType().has_value() &&
            *described->residentSampleType() != kSampleType)
        {
            return Status::failure(
                ErrorCode::IncompatibleArguments,
                StatusDetail{.message = std::string("the frame holds ") +
                                        sampleTypeName(*described->residentSampleType()) +
                                        " samples and this image holds " + sampleTypeName(kSampleType),
                             .operation = "Image::adopt",
                             .domain = ErrorDomain::Image,
                             .context = {StatusContextField::ofText(ContextKey::Parameter, "frame")}});
        }

        Result<AdoptedFrame> adopted = frame.releaseAdoptable();
        if (!adopted.hasValue())
        {
            return adopted.status();
        }
        Result<ImageStorage<SampleT>> storage = ImageStorage<SampleT>::fromBytes(std::move(adopted->storage));
        if (!storage.hasValue())
        {
            return storage.status();
        }
        return Image{std::move(*storage), adopted->descriptor, std::move(metadata)};
    }

    /// Whether this image holds samples.
    ///
    /// @return false for a default-constructed image, one that has been moved from, and one that
    ///         has been released.
    [[nodiscard]] bool valid() const noexcept
    {
        return m_storage.valid() && m_descriptor.has_value();
    }

    /// What the image is.
    ///
    /// @return the description, or an empty optional when this image holds nothing. An optional
    ///         rather than a reference because there is no description of no image.
    [[nodiscard]] std::optional<ImageDescriptor> descriptor() const noexcept
    {
        if (!valid())
        {
            return std::nullopt;
        }
        return m_descriptor;
    }

    /// Where the samples sit.
    ///
    /// @return the layout, or an empty optional when this image holds nothing.
    [[nodiscard]] std::optional<ImageLayout> layout() const noexcept
    {
        return m_storage.layout();
    }

    /// What is known about the image beyond its samples and its geometry.
    ///
    /// @return the six metadata objects. An image that holds nothing carries metadata that states
    ///         nothing, which is the same value a freshly created one has.
    [[nodiscard]] const ImageMetadata& metadata() const noexcept
    {
        return m_metadata;
    }

    /// The memory the samples live in.
    ///
    /// @return the storage, borrowed for as long as this image lives.
    [[nodiscard]] const ImageStorage<SampleT>& storage() const noexcept
    {
        return m_storage;
    }

    /// How many times write access to these samples has been taken.
    ///
    /// The pixel-level input to a derived result's identity, and nothing more. It is compared only
    /// with another generation of the same image.
    ///
    /// @return the count, starting at zero for a newly created image.
    [[nodiscard]] std::uint64_t pixelGeneration() const noexcept
    {
        return m_pixelGeneration;
    }

    /// How many times what these samples mean has changed.
    ///
    /// Moves when the description, the processing meaning or the astrometric solution changes -
    /// the three things a result computed under one and read under another gets wrong. It does not
    /// move for a corrected object name, a preserved card or a decision about how the image is to
    /// be written, which is what keeps a plate solution from being discarded by a typo fix.
    ///
    /// @return the count, starting at zero for a newly created image.
    [[nodiscard]] std::uint64_t semanticGeneration() const noexcept
    {
        return m_semanticGeneration;
    }

    /// The samples, through which none of them may be changed.
    ///
    /// Free: reading changes no generation and records nothing.
    ///
    /// @return the view, or an empty optional when this image holds nothing. It borrows this
    ///         image's memory and must not outlive it.
    [[nodiscard]] std::optional<ImageView<const SampleT>> view() const noexcept
    {
        if (!valid())
        {
            return std::nullopt;
        }
        return m_storage.view();
    }

    /// Permission to change the samples, which moves the pixel generation.
    ///
    /// @return the handle, or an empty optional when this image holds nothing - in which case
    ///         nothing is recorded, because there was nothing to write.
    [[nodiscard]] std::optional<ImageWriteAccess<SampleT>> write() noexcept
    {
        if (!valid())
        {
            return std::nullopt;
        }
        const std::optional<ImageView<SampleT>> writable = m_storage.view();
        if (!writable.has_value())
        {
            return std::nullopt;
        }
        ++m_pixelGeneration;
        return ImageWriteAccess<SampleT>{*writable, m_pixelGeneration};
    }

    /// Applies what an operation states it changed about this image's metadata.
    ///
    /// @param change what the operation changed, and what changed it.
    /// @return success; `ErrorCode::NotInitialized` when this image holds nothing; or whatever
    ///         applying the transition reported. Nothing is changed on refusal.
    [[nodiscard]] Status transition(const MetadataTransition& change)
    {
        if (!valid())
        {
            return Status::failure(ErrorCode::NotInitialized,
                                   StatusDetail{.message = "this image holds nothing to describe",
                                                .operation = "Image::transition",
                                                .domain = ErrorDomain::Image});
        }
        Result<ImageMetadata> updated = applyMetadataTransition(m_metadata, change);
        if (!updated.hasValue())
        {
            return updated.status();
        }
        m_metadata = std::move(*updated);
        if (changesMeaning(change))
        {
            ++m_semanticGeneration;
        }
        return Status::success();
    }

    /// Re-describes the image and applies what an operation states it changed.
    ///
    /// The one way a description changes. There is no setter for the reason `ImageDescriptor` has
    /// none - a change is stated in full and refused where it is made - and the transition is
    /// required beside it so that a change of meaning cannot happen without a record of what did it.
    ///
    /// The samples are not touched and the memory is not re-laid-out: the new description has to
    /// describe the image that is already there, which is what makes this a re-description rather
    /// than a conversion.
    ///
    /// @param described what the image now is. Must name this image's representation and its
    ///        extent. A different sample type is refused even when it has the same width.
    /// @param change what else the operation changed, carrying the record. A description that
    ///        differs from the current one is itself a change, so the record is required for it.
    /// @return success; `ErrorCode::NotInitialized` when this image holds nothing;
    ///         `ErrorCode::IncompatibleArguments` when the description does not describe these
    ///         samples; `ErrorCode::InvalidArgument` when the description changed and nothing
    ///         records what changed it; or whatever applying the transition reported. Nothing is
    ///         changed on refusal.
    [[nodiscard]] Status transition(const ImageDescriptor& described, const MetadataTransition& change)
    {
        if (!valid())
        {
            return Status::failure(ErrorCode::NotInitialized,
                                   StatusDetail{.message = "this image holds nothing to describe",
                                                .operation = "Image::transition",
                                                .domain = ErrorDomain::Image});
        }
        if (described.sampleType() != kSampleType)
        {
            return Status::failure(
                ErrorCode::IncompatibleArguments,
                StatusDetail{.message = "the description names a different sample representation",
                             .operation = "Image::transition",
                             .domain = ErrorDomain::Image,
                             .context = {StatusContextField::ofText(ContextKey::Parameter, "descriptor")}});
        }
        const std::optional<ImageLayout> current = m_storage.layout();
        const Status describes = checkImageDescribesLayout(described, *current, "Image::transition");
        if (describes.failed())
        {
            return describes;
        }

        const bool redescribed = !(described == *m_descriptor);
        if (redescribed && !change.record.has_value())
        {
            return Status::failure(
                ErrorCode::InvalidArgument,
                StatusDetail{.message = "a change of description records what changed it; a lineage with a gap in it "
                                        "cannot answer the question it exists for",
                             .operation = "Image::transition",
                             .domain = ErrorDomain::Image,
                             .context = {StatusContextField::ofText(ContextKey::Parameter, "record")}});
        }

        Result<ImageMetadata> updated = applyMetadataTransition(m_metadata, change);
        if (!updated.hasValue())
        {
            return updated.status();
        }
        m_metadata = std::move(*updated);
        m_descriptor = described;
        if (redescribed || changesMeaning(change))
        {
            ++m_semanticGeneration;
        }
        return Status::success();
    }

    /// A second image holding a copy of these samples, this description and this metadata.
    ///
    /// The copy comes from the same engine context, so it is counted against the same budget and
    /// may be refused by it. Its generations start at zero: it is a new image whose history is
    /// empty, and a generation is only ever compared with another generation of the same image.
    ///
    /// @return the copy, or `ErrorCode::NotInitialized` when this image holds nothing, or whatever
    ///         the allocation reported.
    [[nodiscard]] Result<Image> clone() const
    {
        if (!valid())
        {
            return Status::failure(ErrorCode::NotInitialized,
                                   StatusDetail{.message = "this image holds nothing to copy",
                                                .operation = "Image::clone",
                                                .domain = ErrorDomain::Image});
        }
        Result<ImageStorage<SampleT>> copied = m_storage.clone();
        if (!copied.hasValue())
        {
            return copied.status();
        }
        return Image{std::move(*copied), *m_descriptor, m_metadata};
    }

    /// Gives the memory back now rather than at the end of the scope.
    ///
    /// Idempotent, and leaves the image exactly as a default-constructed one - description,
    /// metadata and both generations included.
    void release() noexcept
    {
        m_storage.release();
        m_descriptor.reset();
        m_metadata = ImageMetadata{};
        m_pixelGeneration = 0u;
        m_semanticGeneration = 0u;
    }

private:
    /// Takes the memory a description of this arrangement needs.
    ///
    /// @param engine the context whose memory resource and budget own the samples.
    /// @param descriptor what the image is.
    /// @param arrangement how the channels are spread across planes.
    /// @param initialization what the samples hold before anything writes to them.
    /// @return the storage, or the refusal to report.
    [[nodiscard]] static Result<ImageStorage<SampleT>> createStorage(const EngineContext& engine,
                                                                     const ImageDescriptor& descriptor,
                                                                     StorageArrangement arrangement,
                                                                     StorageInitialization initialization)
    {
        if (descriptor.sampleType() != kSampleType)
        {
            return Status::failure(
                ErrorCode::IncompatibleArguments,
                StatusDetail{.message = std::string("the description says ") + sampleTypeName(descriptor.sampleType()) +
                                        " and this image holds " + sampleTypeName(kSampleType),
                             .operation = "Image::create",
                             .domain = ErrorDomain::Image,
                             .context = {StatusContextField::ofText(ContextKey::Parameter, "descriptor")}});
        }
        const std::optional<ImageLayoutRequest> request = layoutRequestFor(descriptor, arrangement);
        if (!request.has_value())
        {
            return Status::failure(
                ErrorCode::InvalidArgument,
                StatusDetail{.message = "a description cannot be laid out in this arrangement; a layout stated in "
                                        "full is what such an image is built from",
                             .operation = "Image::create",
                             .domain = ErrorDomain::Image,
                             .context = {StatusContextField::ofText(ContextKey::Parameter, "arrangement")}});
        }
        return ImageStorage<SampleT>::create(engine, *request, initialization);
    }

    /// Builds from parts that have already been checked against each other.
    ///
    /// @param storage the memory.
    /// @param descriptor what the samples are.
    /// @param metadata what is known about the image.
    Image(ImageStorage<SampleT>&& storage, const ImageDescriptor& descriptor, ImageMetadata metadata) noexcept
        : m_storage(std::move(storage)), m_descriptor(descriptor), m_metadata(std::move(metadata))
    {
    }

    /// The memory the samples live in, and its hold on the engine context.
    ImageStorage<SampleT> m_storage;

    /// What the samples are, or nothing when this image holds nothing.
    std::optional<ImageDescriptor> m_descriptor;

    /// What is known about the image beyond its samples and its geometry.
    ImageMetadata m_metadata;

    /// How many times write access has been taken.
    std::uint64_t m_pixelGeneration = 0u;

    /// How many times what the samples mean has changed.
    std::uint64_t m_semanticGeneration = 0u;
};

/// The image an operation computes in when it has chosen a width.
///
/// `Image<float>` and `Image<double>` under the name that says why they were chosen. An operation
/// that widens dynamic range, or a chain that must not requantize between its stages, names one of
/// these in its plan; the alternative is inheriting a representation from whatever ran before it,
/// which is how a requantization nobody planned enters a process chain.
///
/// @tparam PrecisionV the arithmetic width the operation computes at.
template <ComputePrecision PrecisionV> using WorkingImage = Image<ComputeSampleOf<PrecisionV>>;

/// A working image of this width, describing the same picture as a source image.
///
/// Everything about the description survives except what a sample is: the same pixels, the same
/// channels, the same basis, the same domain, the same filter array and phase. The metadata comes
/// across unchanged, because an intermediate is not a product - what it becomes is stated by the
/// transition whoever writes the result applies.
///
/// The samples are not copied. This allocates a working buffer of the right shape; filling it is a
/// conversion, which is a separate operation with its own declared numeric contract.
///
/// @tparam PrecisionV the arithmetic width to work at.
/// @tparam SourceT the source image's sample type.
/// @param engine the context whose memory resource and budget own the working buffer.
/// @param source the image whose description the working image copies.
/// @param arrangement how the working image's channels are spread across planes.
/// @param initialization what the working samples hold before anything writes to them.
/// @return the working image; `ErrorCode::NotInitialized` when the source holds nothing; whatever
///         `describeSamplesAs` reported; or whatever `Image::create` reported.
template <ComputePrecision PrecisionV, ResidentSample SourceT>
[[nodiscard]] Result<WorkingImage<PrecisionV>>
createWorkingImage(const EngineContext& engine, const Image<SourceT>& source, StorageArrangement arrangement,
                   StorageInitialization initialization)
{
    const std::optional<ImageDescriptor> described = source.descriptor();
    if (!described.has_value())
    {
        return Status::failure(ErrorCode::NotInitialized,
                               StatusDetail{.message = "the source image holds nothing to describe a working image "
                                                       "from",
                                            .operation = "createWorkingImage",
                                            .domain = ErrorDomain::Image});
    }
    Result<ImageDescriptor> working = describeSamplesAs(*described, workingSampleTypeOf(PrecisionV));
    if (!working.hasValue())
    {
        return working.status();
    }
    ImageMetadata metadata = source.metadata();
    return WorkingImage<PrecisionV>::create(engine, *working, arrangement, initialization, std::move(metadata));
}

/// An image's samples lent to a writer as the frame a codec encodes.
///
/// The counterpart of `Image::adopt`. What an operation computed reaches a file through
/// `IO/OutputTransaction.h`, which takes a `RawFrame`; this describes the image with
/// `RawFrameDescriptor::fromImageDescriptor` and borrows its bytes without copying one. Borrowed bytes
/// release nothing and are never adoptable, so the frame cannot become a second owner of the image's
/// memory.
///
/// The frame borrows the image: it must not outlive it. A release of the image or an assignment over
/// it leaves the frame describing memory it no longer has, and a write through a handle obtained
/// afterwards changes the bytes the frame reads.
///
/// @tparam SampleT the image's sample type.
/// @param image the image to lend.
/// @return the frame; `ErrorCode::NotInitialized` when the image holds nothing; otherwise
///         `ErrorCode::IncompatibleArguments` when the description has no frame form, or whatever
///         `RawFrame::create` reported - neither of which an image's own description, layout and
///         bytes produce.
template <ResidentSample SampleT> [[nodiscard]] Result<RawFrame> borrowImageFrame(const Image<SampleT>& image)
{
    const std::optional<ImageDescriptor> described = image.descriptor();
    const std::optional<ImageLayout> layout = image.layout();
    if (!described.has_value() || !layout.has_value())
    {
        return Status::failure(ErrorCode::NotInitialized,
                               StatusDetail{.message = "an image that holds nothing has no samples to lend",
                                            .operation = "borrowImageFrame",
                                            .domain = ErrorDomain::Image});
    }
    const std::optional<RawFrameDescriptor> frame = RawFrameDescriptor::fromImageDescriptor(*described);
    if (!frame.has_value())
    {
        return Status::failure(ErrorCode::IncompatibleArguments,
                               StatusDetail{.message = "the image's description has no frame description",
                                            .operation = "borrowImageFrame",
                                            .domain = ErrorDomain::Image});
    }
    // A layout's total is a whole number of samples: its strides are multiples of the element size.
    Result<SampleBuffer> bytes = SampleBuffer::borrowConst(std::as_bytes(std::span<const SampleT>(
        image.storage().data(), static_cast<std::size_t>(layout->totalBytes() / sizeof(SampleT)))));
    if (!bytes.hasValue())
    {
        return bytes.status();
    }
    return RawFrame::create(*frame, *layout, std::move(*bytes));
}

} // namespace an
