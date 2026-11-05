// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Image/ImageStorage.h>

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/GeometryError.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <utility>

// Four things in this file are decisions rather than mechanics, and each of them is invisible from
// the header.
//
// **The engine context is held by a shared lease.** A storage cannot let its context shut down -
// the memory has to go back to that context's resource - so it takes a lease and holds it. A clone
// shares the lease rather than taking a second one, which is correct for what a lease means here:
// it counts pieces of work outstanding, and a copy of an image buffer is not a second piece of
// work. The property that matters is that the context refuses to shut down while any of them
// exists, and one shared hold gives that.
//
// **A geometry refusal becomes a code by rule, not by taste.** The mapping below is exhaustive and
// has no `default` label, so a reason added to `Core/GeometryError.h` fails this build rather than
// arriving at a caller as whatever the last case happened to be. The rule's own name travels in the
// message because that is the part a person reads; the code is the part a caller branches on.
//
// **Reshape never reaches the allocator.** It compares against what is already held and refuses.
// Growing through a reshape would fail for a budget reason a caller answers by tiling, which is a
// different answer from "this does not fit what you are holding", and a single entry point that can
// return either leaves the caller unable to tell which it got without re-deriving the sizes itself.
//
// **The typed shell is not in this file, and that is the point of the split.** Everything here is
// compiled once. What `ImageStorage<SampleT>` adds is a static cast and a compile-time element-size
// check, so the eight instantiations share this code rather than each carrying a copy of the
// allocation path, the accounting and the strings.

namespace an
{
namespace
{

/// Which code a geometry refusal is reported under.
///
/// The three groups are the three answers a caller has. An argument it can correct, a pair of
/// arguments that cannot both be true, and a maximum somebody configured are different problems,
/// and the two representability reasons are a fourth: no configuration admits them and no argument
/// short of a smaller image avoids them.
///
/// @param error the geometry rule that refused; never `GeometryError::None` on this path.
/// @return the code to report it under.
[[nodiscard]] constexpr ErrorCode codeForGeometryError(GeometryError error) noexcept
{
    switch (error)
    {
    case GeometryError::None:
        return ErrorCode::Ok;
    case GeometryError::ZeroDimension:
    case GeometryError::AlignmentNotPowerOfTwo:
    case GeometryError::ElementSizeNotPowerOfTwo:
        return ErrorCode::InvalidArgument;
    case GeometryError::PlaneCountMismatch:
    case GeometryError::StrideTooSmall:
    case GeometryError::StrideMisaligned:
    case GeometryError::ElementSizeMismatch:
    case GeometryError::BufferTooSmall:
    case GeometryError::BaseAddressMisaligned:
        return ErrorCode::IncompatibleArguments;
    case GeometryError::DimensionTooLarge:
    case GeometryError::ChannelCountTooLarge:
    case GeometryError::ImageTooLarge:
        return ErrorCode::LimitExceeded;
    case GeometryError::ArithmeticOverflow:
    case GeometryError::SizeNotRepresentable:
    case GeometryError::RegionOutOfBounds:
        return ErrorCode::OutOfRange;
    }
    return ErrorCode::Internal;
}

/// The refusal for a request that does not describe an image.
///
/// @param error the geometry rule that refused.
/// @param operation the engine operation to name in the failure.
/// @return the failure, carrying the rule's own name in its message and `"request"` as the
///         parameter at fault.
[[nodiscard]] Status refuseGeometry(GeometryError error, const char* operation)
{
    return Status::failure(codeForGeometryError(error),
                           StatusDetail{.message = std::string("the requested geometry does not describe an image: ") +
                                                   geometryErrorName(error),
                                        .operation = operation,
                                        .domain = ErrorDomain::Image,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, "request")}});
}

/// The refusal for an argument the caller cannot have meant.
///
/// @param message what is wrong, for a person.
/// @param parameter which argument, spelled as the caller wrote it.
/// @param operation the engine operation to name in the failure.
/// @return an `ErrorCode::InvalidArgument` failure naming the argument.
[[nodiscard]] Status refuseArgument(const char* message, const char* parameter, const char* operation)
{
    return Status::failure(ErrorCode::InvalidArgument,
                           StatusDetail{.message = message,
                                        .operation = operation,
                                        .domain = ErrorDomain::Image,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

/// The refusal for a context that owns nothing.
///
/// @param operation the engine operation to name in the failure.
/// @return an `ErrorCode::NotInitialized` failure.
[[nodiscard]] Status refuseEmptyContext(const char* operation)
{
    return Status::failure(ErrorCode::NotInitialized,
                           StatusDetail{.message = "image storage needs an engine context that holds state",
                                        .operation = operation,
                                        .domain = ErrorDomain::Image});
}

} // namespace

/// The allocation, its accounting, and the hold that keeps the owning context open.
///
/// Defined here rather than in the header so that the published surface says nothing about how a
/// storage is laid out: a caller sees a handle, and the pointer, the lease and the counters are
/// this file's business.
class ImageStorageBytes::State
{
public:
    /// Takes ownership of an allocation that has already been made.
    ///
    /// @param contextHold the type-erased hold that keeps the owning context open, shared with
    ///        every storage cloned from this one.
    /// @param resource the owning context's memory resource.
    /// @param contextId the owning context's identity.
    /// @param data the allocation.
    /// @param capacityBytes how many bytes it is.
    /// @param alignmentBytes the boundary it begins on.
    /// @param layout what those bytes currently mean.
    State(std::shared_ptr<void> contextHold, MemoryResource& resource, std::uint64_t contextId, void* data,
          std::uint64_t capacityBytes, std::uint64_t alignmentBytes, const ImageLayout& layout) noexcept
        : m_contextHold(std::move(contextHold)), m_resource(&resource), m_data(data), m_capacityBytes(capacityBytes),
          m_alignmentBytes(alignmentBytes), m_contextId(contextId), m_layout(layout)
    {
    }

    /// Gives the memory back to the resource it came from.
    ~State()
    {
        m_resource->deallocate(m_data, m_capacityBytes, m_alignmentBytes);
    }

    /// Neither copied nor moved: the handle moves, and this stays where it is.
    State(const State&) = delete;

    /// Neither copied nor moved. See the copy constructor.
    State& operator=(const State&) = delete;

    /// Neither copied nor moved. See the copy constructor.
    State(State&&) = delete;

    /// Neither copied nor moved. See the copy constructor.
    State& operator=(State&&) = delete;

    /// The hold on the owning context, for a clone that shares it.
    ///
    /// @return the type-erased lease.
    [[nodiscard]] const std::shared_ptr<void>& contextHold() const noexcept
    {
        return m_contextHold;
    }

    /// Where the samples are.
    ///
    /// @return the allocation, never null.
    [[nodiscard]] void* data() const noexcept
    {
        return m_data;
    }

    /// How many bytes were taken from the resource.
    ///
    /// @return the capacity.
    [[nodiscard]] std::uint64_t capacityBytes() const noexcept
    {
        return m_capacityBytes;
    }

    /// The boundary the allocation begins on.
    ///
    /// @return the alignment.
    [[nodiscard]] std::uint64_t alignmentBytes() const noexcept
    {
        return m_alignmentBytes;
    }

    /// Which context's budget the memory belongs to.
    ///
    /// @return the identity.
    [[nodiscard]] std::uint64_t contextId() const noexcept
    {
        return m_contextId;
    }

    /// Where the memory came from and will go back to.
    ///
    /// @return the resource, never null.
    [[nodiscard]] MemoryResource& resource() const noexcept
    {
        return *m_resource;
    }

    /// What the memory currently means.
    ///
    /// @return the layout.
    [[nodiscard]] const ImageLayout& layout() const noexcept
    {
        return m_layout;
    }

    /// Adopts a new meaning for memory that is already held.
    ///
    /// @param layout the geometry to adopt, already proved to fit the capacity.
    void adopt(const ImageLayout& layout) noexcept
    {
        m_layout = layout;
    }

private:
    /// The hold that stops the owning context shutting down.
    std::shared_ptr<void> m_contextHold;

    /// Where the memory came from. Never null.
    MemoryResource* m_resource;

    /// The allocation. Never null.
    void* m_data;

    /// How many bytes it is, which is what the budget sees.
    std::uint64_t m_capacityBytes;

    /// The boundary it begins on.
    std::uint64_t m_alignmentBytes;

    /// Which context's budget it belongs to.
    std::uint64_t m_contextId;

    /// What the bytes currently mean, which a reshape replaces.
    ImageLayout m_layout;
};

const char* storageInitializationName(StorageInitialization initialization) noexcept
{
    switch (initialization)
    {
    case StorageInitialization::Uninitialized:
        return "Uninitialized";
    case StorageInitialization::Zeroed:
        return "Zeroed";
    }
    return "UnknownStorageInitialization";
}

Status checkStorageSampleSize(std::uint32_t elementSizeBytes, SampleType sampleType, const char* operation)
{
    const std::uint32_t expected = sampleTypeSizeBytes(sampleType);
    if (expected != 0u && elementSizeBytes == expected)
    {
        return Status::success();
    }
    return Status::failure(
        codeForGeometryError(GeometryError::ElementSizeMismatch),
        StatusDetail{.message = std::string("the layout describes samples of a different size than ") +
                                sampleTypeName(sampleType) + ": " +
                                geometryErrorName(GeometryError::ElementSizeMismatch),
                     .operation = operation,
                     .domain = ErrorDomain::Image,
                     .context = {StatusContextField::ofCount(ContextKey::Requested, elementSizeBytes),
                                 StatusContextField::ofCount(ContextKey::Available, expected)}});
}

Status checkStorageSampleAlignment(const void* address, SampleType sampleType)
{
    // The size of a sample is its alignment for all eight: every one of them is a scalar whose
    // alignment requirement is its width on every platform this engine is built for. Deriving it
    // from the size rather than from a second table is what keeps the two from disagreeing about a
    // representation somebody adds.
    const std::uint64_t required = sampleTypeSizeBytes(sampleType);
    if (required == 0u)
    {
        return refuseArgument("the representation is not one this engine stores", "sampleType",
                              "ImageStorage::fromBytes");
    }
    if (address == nullptr || isPointerAligned(address, required))
    {
        return Status::success();
    }
    return Status::failure(
        codeForGeometryError(GeometryError::BaseAddressMisaligned),
        StatusDetail{.message = std::string("the memory does not begin on a boundary a ") + sampleTypeName(sampleType) +
                                " may be read from: " + geometryErrorName(GeometryError::BaseAddressMisaligned),
                     .operation = "ImageStorage::fromBytes",
                     .domain = ErrorDomain::Image,
                     .context = {StatusContextField::ofText(ContextKey::Parameter, "bytes"),
                                 StatusContextField::ofCount(ContextKey::Requested, required)}});
}

ImageStorageBytes::ImageStorageBytes() noexcept = default;

ImageStorageBytes::~ImageStorageBytes() = default;

ImageStorageBytes::ImageStorageBytes(ImageStorageBytes&& other) noexcept = default;

ImageStorageBytes& ImageStorageBytes::operator=(ImageStorageBytes&& other) noexcept
{
    if (this != &other)
    {
        m_state = std::move(other.m_state);
    }
    return *this;
}

ImageStorageBytes::ImageStorageBytes(std::unique_ptr<State> state) noexcept : m_state(std::move(state)) {}

Result<ImageStorageBytes> ImageStorageBytes::create(const EngineContext& engine, const ImageLayout& layout,
                                                    StorageInitialization initialization, std::uint64_t alignmentBytes)
{
    if (!engine.valid())
    {
        return refuseEmptyContext("ImageStorageBytes::create");
    }
    if (!isStorageInitialization(initialization))
    {
        return refuseArgument("an initialization policy this build does not know cannot be applied", "initialization",
                              "ImageStorageBytes::create");
    }
    if (!isPowerOfTwo(alignmentBytes))
    {
        return refuseArgument("an alignment that is not a power of two is not an alignment", "alignmentBytes",
                              "ImageStorageBytes::create");
    }

    MemoryResource* const resource = engine.memoryResource();
    if (resource == nullptr)
    {
        return refuseEmptyContext("ImageStorageBytes::create");
    }

    Result<EngineContextLease> lease = engine.acquireLease();
    if (!lease.hasValue())
    {
        return lease.status();
    }

    const std::uint64_t capacityBytes = layout.totalBytes();
    Result<void*> block = initialization == StorageInitialization::Zeroed
                              ? resource->allocateZeroed(capacityBytes, alignmentBytes)
                              : resource->allocate(capacityBytes, alignmentBytes);
    if (!block.hasValue())
    {
        return block.status();
    }

    try
    {
        auto contextHold = std::make_shared<EngineContextLease>(std::move(*lease));
        const std::uint64_t contextId = contextHold->contextId();
        return ImageStorageBytes{std::make_unique<State>(std::move(contextHold), *resource, contextId, *block,
                                                         capacityBytes, alignmentBytes, layout)};
    }
    catch (const std::bad_alloc&)
    {
        resource->deallocate(*block, capacityBytes, alignmentBytes);
        return Status::failure(ErrorCode::AllocationFailed);
    }
}

Result<ImageStorageBytes> ImageStorageBytes::create(const EngineContext& engine, const ImageLayoutRequest& request,
                                                    StorageInitialization initialization)
{
    if (!engine.valid())
    {
        return refuseEmptyContext("ImageStorageBytes::create");
    }

    const EngineLimits limits = engine.limits();
    const GeometryError geometry = ImageLayout::check(request, limits);
    if (geometry != GeometryError::None)
    {
        return refuseGeometry(geometry, "ImageStorageBytes::create");
    }
    const std::optional<ImageLayout> layout = ImageLayout::create(request, limits);
    if (!layout.has_value())
    {
        return refuseGeometry(GeometryError::ArithmeticOverflow, "ImageStorageBytes::create");
    }

    // The rows were padded to the request's own alignment, so the first row has to begin on it as
    // well - otherwise every row is aligned relative to a base that is not, which is the one case
    // where a layout's padding buys nothing at all.
    const std::uint64_t alignmentBytes = request.rowAlignmentBytes > kDefaultBufferAlignmentBytes
                                             ? request.rowAlignmentBytes
                                             : kDefaultBufferAlignmentBytes;
    return create(engine, *layout, initialization, alignmentBytes);
}

bool ImageStorageBytes::valid() const noexcept
{
    return m_state != nullptr;
}

void* ImageStorageBytes::data() noexcept
{
    return m_state != nullptr ? m_state->data() : nullptr;
}

const void* ImageStorageBytes::data() const noexcept
{
    return m_state != nullptr ? m_state->data() : nullptr;
}

std::optional<ImageLayout> ImageStorageBytes::layout() const noexcept
{
    if (m_state == nullptr)
    {
        return std::nullopt;
    }
    return m_state->layout();
}

std::uint64_t ImageStorageBytes::capacityBytes() const noexcept
{
    return m_state != nullptr ? m_state->capacityBytes() : 0u;
}

std::uint64_t ImageStorageBytes::logicalBytes() const noexcept
{
    return m_state != nullptr ? m_state->layout().totalBytes() : 0u;
}

std::uint64_t ImageStorageBytes::alignmentBytes() const noexcept
{
    return m_state != nullptr ? m_state->alignmentBytes() : 0u;
}

std::uint64_t ImageStorageBytes::contextId() const noexcept
{
    return m_state != nullptr ? m_state->contextId() : 0u;
}

MemoryResource* ImageStorageBytes::memoryResource() const noexcept
{
    return m_state != nullptr ? &m_state->resource() : nullptr;
}

Result<ImageStorageBytes> ImageStorageBytes::clone() const
{
    if (m_state == nullptr)
    {
        return Status::failure(ErrorCode::NotInitialized,
                               StatusDetail{.message = "a storage that holds nothing has nothing to copy",
                                            .operation = "ImageStorageBytes::clone",
                                            .domain = ErrorDomain::Image});
    }

    const std::uint64_t capacityBytes = m_state->layout().totalBytes();
    const std::uint64_t alignmentBytes = m_state->alignmentBytes();
    MemoryResource& resource = m_state->resource();

    Result<void*> block = resource.allocate(capacityBytes, alignmentBytes);
    if (!block.hasValue())
    {
        return block.status();
    }
    std::memcpy(*block, m_state->data(), static_cast<std::size_t>(capacityBytes));

    try
    {
        return ImageStorageBytes{std::make_unique<State>(m_state->contextHold(), resource, m_state->contextId(), *block,
                                                         capacityBytes, alignmentBytes, m_state->layout())};
    }
    catch (const std::bad_alloc&)
    {
        resource.deallocate(*block, capacityBytes, alignmentBytes);
        return Status::failure(ErrorCode::AllocationFailed);
    }
}

Status ImageStorageBytes::reshape(const ImageLayout& layout)
{
    if (m_state == nullptr)
    {
        return Status::failure(ErrorCode::NotInitialized,
                               StatusDetail{.message = "a storage that holds nothing has no memory to re-describe",
                                            .operation = "ImageStorageBytes::reshape",
                                            .domain = ErrorDomain::Image});
    }
    const std::uint64_t required = layout.totalBytes();
    if (required > m_state->capacityBytes())
    {
        return Status::failure(
            ErrorCode::OutOfRange,
            StatusDetail{.message = "the layout describes more memory than this storage holds, and a reshape never "
                                    "allocates",
                         .operation = "ImageStorageBytes::reshape",
                         .domain = ErrorDomain::Image,
                         .context = {StatusContextField::ofCount(ContextKey::Requested, required),
                                     StatusContextField::ofCount(ContextKey::Available, m_state->capacityBytes())}});
    }
    m_state->adopt(layout);
    return Status::success();
}

void ImageStorageBytes::release() noexcept
{
    m_state.reset();
}

} // namespace an
