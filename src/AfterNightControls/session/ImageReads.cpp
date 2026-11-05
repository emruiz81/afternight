// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The images a run reads beside the one it writes or presents: named once each, leased in order of identity around
/// the run's own writer, and read through shares of those leases.

#include "session/ImageReads.h"

#include "session/ExecutionScope.h"

#include <AfterNightEngine/Core/ErrorCode.h>

#include <algorithm>
#include <utility>

namespace an::ui
{

/// One image the run named, and the lease it holds on it once taken.
struct ImageReads::Named
{
    std::shared_ptr<const an::consumer::ImageAsset> image;
    std::uint64_t identity = 0u;
    an::consumer::AssetReadLease lease;
};

ImageReads::ImageReads() noexcept = default;

ImageReads::~ImageReads() = default;

ImageReads::ImageReads(ImageReads&& other) noexcept = default;

ImageReads& ImageReads::operator=(ImageReads&& other) noexcept = default;

void ImageReads::name(std::shared_ptr<const an::consumer::ImageAsset> image)
{
    if (image == nullptr || !image->valid())
    {
        return;
    }
    const std::uint64_t identity = image->identity();
    const auto ordered =
        std::lower_bound(m_named.begin(), m_named.end(), identity,
                         [](const Named& named, std::uint64_t value) { return named.identity < value; });
    if (ordered != m_named.end() && ordered->identity == identity)
    {
        return;
    }
    m_named.insert(ordered, Named{.image = std::move(image), .identity = identity, .lease = {}});
}

void ImageReads::hold(an::consumer::AssetReadLease lease)
{
    if (!lease.valid())
    {
        return;
    }
    const std::uint64_t identity = lease.assetIdentity();
    const auto ordered =
        std::lower_bound(m_named.begin(), m_named.end(), identity,
                         [](const Named& named, std::uint64_t value) { return named.identity < value; });
    if (ordered != m_named.end() && ordered->identity == identity)
    {
        if (!ordered->lease.valid())
        {
            ordered->lease = std::move(lease);
        }
        return;
    }
    m_named.insert(ordered, Named{.image = nullptr, .identity = identity, .lease = std::move(lease)});
}

std::vector<std::uint64_t> ImageReads::identities() const
{
    std::vector<std::uint64_t> named;
    named.reserve(m_named.size());
    for (const Named& image : m_named)
    {
        named.push_back(image.identity);
    }
    return named;
}

an::Status ImageReads::take(std::stop_token stop, std::uint64_t written, const std::function<an::Status()>& takeWriter)
{
    m_written = written;
    bool writerTaken = !takeWriter;
    const auto takeWriterNow = [&]() -> an::Status {
        writerTaken = true;
        return takeWriter();
    };
    for (Named& named : m_named)
    {
        if (!writerTaken && named.identity > written)
        {
            if (an::Status taken = takeWriterNow(); taken.failed())
            {
                release();
                return taken;
            }
        }
        // The run reads its own image through its writer; a lease on it here would wait for that writer.
        if (written != 0u && named.identity == written)
        {
            continue;
        }
        // A lease held already is read through; one held and let go has no image to lease again.
        if (named.lease.valid() || named.image == nullptr)
        {
            continue;
        }
        an::Result<an::consumer::AssetReadLease> lease = named.image->read(stop);
        if (!lease.hasValue())
        {
            release();
            return lease.status();
        }
        named.lease = std::move(*lease);
    }
    if (!writerTaken)
    {
        if (an::Status taken = takeWriterNow(); taken.failed())
        {
            release();
            return taken;
        }
    }
    return an::Status::success();
}

const an::consumer::AssetReadLease* ImageReads::find(std::uint64_t identity) const noexcept
{
    const auto found = std::lower_bound(m_named.begin(), m_named.end(), identity,
                                        [](const Named& named, std::uint64_t value) { return named.identity < value; });
    if (found == m_named.end() || found->identity != identity || !found->lease.valid())
    {
        return nullptr;
    }
    return &found->lease;
}

std::uint64_t ImageReads::written() const noexcept
{
    return m_written;
}

void ImageReads::release() noexcept
{
    for (Named& named : m_named)
    {
        named.lease.release();
    }
}

an::Result<an::consumer::AssetReadLease> readBeside(const ExecutionScope& scope, const an::consumer::ImageAsset& image,
                                                    std::stop_token stop)
{
    if (const ImageReads* reads = scope.reads(); reads != nullptr && image.valid())
    {
        if (const an::consumer::AssetReadLease* held = reads->find(image.identity()); held != nullptr)
        {
            return held->share();
        }
        if (reads->written() != 0u && reads->written() == image.identity())
        {
            return an::Status::failure(an::ErrorCode::InvalidState,
                                       "the run writes this image, and reads it through its writer");
        }
    }
    return image.read(std::move(stop));
}

} // namespace an::ui
