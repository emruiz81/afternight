// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The image document and its writer: a document's state shared by everything it hands out, leases taken on the asset
/// the document holds when the lease is granted, a writer that claims the document, and a replacement that swaps the
/// asset under the document's mutex - a writer's, or one made with no writer while none holds the document.

#include "session/ImageDocument.h"

#include "session/DocumentState.h"

#include <AfterNightEngine/Core/ErrorCode.h>

#include <atomic>
#include <utility>

namespace an::ui
{

namespace
{

/// A document's identity: unique in the process, so a result derived from one document can never be taken for
/// another's, whatever the lifetime of the cache that holds it.
[[nodiscard]] std::uint64_t nextDocumentIdentity() noexcept
{
    static std::atomic<std::uint64_t> counter{0u};
    return counter.fetch_add(1u, std::memory_order_relaxed) + 1u;
}

[[nodiscard]] an::Status emptyDocument()
{
    return an::Status::failure(an::ErrorCode::NotInitialized, "the document holds nothing");
}

[[nodiscard]] an::Status closedDocument()
{
    return an::Status::failure(an::ErrorCode::InvalidState, "the document is closed");
}

[[nodiscard]] an::Status sharedDocument()
{
    return an::Status::failure(an::ErrorCode::InvalidState,
                               "the document shows an image another document holds, which it never writes");
}

/// Takes a lease on the asset a document holds when the lease is granted. A lease granted on an asset the document has
/// since let go is released and asked for again on the one it holds now, so a holder never reads or writes an image
/// the document no longer shows.
///
/// @param state the document's state.
/// @param take asks one asset for the lease: `an::Result<LeaseT>(an::consumer::ImageAsset&)`.
/// @param refuse decides, with the document's lock not held, whether the document refuses the lease now.
/// @param keep decides, under the document's lock, whether the lease granted on an asset is kept: whether the document
///        still holds the asset, and for a writer that it now holds the document.
/// @return the asset and its lease, or the first refusal.
template <typename LeaseT, typename TakeT, typename RefuseT, typename KeepT>
[[nodiscard]] an::Result<std::pair<std::shared_ptr<an::consumer::ImageAsset>, LeaseT>>
leaseCurrent(const detail::DocumentState& state, TakeT&& take, RefuseT&& refuse, KeepT&& keep)
{
    while (true)
    {
        if (an::Status refused = refuse(); !refused.ok())
        {
            return refused;
        }
        std::shared_ptr<an::consumer::ImageAsset> asset = state.currentAsset();
        an::Result<LeaseT> lease = take(*asset);
        if (!lease.hasValue())
        {
            return lease.status();
        }
        if (keep(asset.get()))
        {
            return std::make_pair(std::move(asset), std::move(*lease));
        }
    }
}

} // namespace

ImageDocument::ImageDocument() noexcept = default;

ImageDocument::~ImageDocument() = default;

ImageDocument::ImageDocument(ImageDocument&& other) noexcept = default;

ImageDocument& ImageDocument::operator=(ImageDocument&& other) noexcept = default;

ImageDocument::ImageDocument(std::shared_ptr<detail::DocumentState> state) noexcept : m_state(std::move(state)) {}

an::Result<ImageDocument> ImageDocument::create(an::consumer::ImageAsset&& asset, DocumentNames names)
{
    if (!asset.valid())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "a document needs an asset that holds an image");
    }
    return ImageDocument(std::make_shared<detail::DocumentState>(
        nextDocumentIdentity(), std::make_shared<an::consumer::ImageAsset>(std::move(asset)), std::move(names)));
}

an::Result<ImageDocument> ImageDocument::share(std::shared_ptr<const an::consumer::ImageAsset> asset,
                                               DocumentNames names)
{
    if (asset == nullptr || !asset->valid())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "a document needs an asset that holds an image");
    }
    // The state holds the asset as every document does, and a shared one refuses every writer, so nothing writes the
    // asset through it.
    return ImageDocument(std::make_shared<detail::DocumentState>(
        nextDocumentIdentity(), std::const_pointer_cast<an::consumer::ImageAsset>(std::move(asset)), std::move(names),
        true));
}

bool ImageDocument::valid() const noexcept
{
    return m_state != nullptr;
}

bool ImageDocument::isShared() const noexcept
{
    return m_state != nullptr && m_state->shared();
}

std::uint64_t ImageDocument::identity() const noexcept
{
    return m_state != nullptr ? m_state->identity() : 0u;
}

bool ImageDocument::isOpen() const noexcept
{
    return m_state != nullptr && !m_state->closed();
}

void ImageDocument::close() noexcept
{
    if (m_state != nullptr)
    {
        m_state->close();
    }
}

DocumentNames ImageDocument::names() const
{
    return m_state != nullptr ? m_state->names() : DocumentNames{};
}

void ImageDocument::setNames(DocumentNames names)
{
    if (m_state != nullptr)
    {
        m_state->setNames(std::move(names));
    }
}

std::optional<DocumentSequence> ImageDocument::sequence() const
{
    return m_state != nullptr ? m_state->sequence() : std::nullopt;
}

void ImageDocument::setSequence(std::optional<DocumentSequence> sequence)
{
    if (m_state != nullptr)
    {
        m_state->setSequence(std::move(sequence));
    }
}

DocumentPresentation ImageDocument::presentation() const
{
    return m_state != nullptr ? m_state->presentation() : DocumentPresentation{};
}

void ImageDocument::setPresentation(DocumentPresentation presentation)
{
    if (m_state != nullptr)
    {
        m_state->setPresentation(std::move(presentation));
    }
}

std::uint64_t ImageDocument::presentationGeneration() const noexcept
{
    return m_state != nullptr ? m_state->presentationGeneration() : 0u;
}

std::shared_ptr<const an::consumer::ImageAsset> ImageDocument::asset() const
{
    return m_state != nullptr ? m_state->currentAsset() : nullptr;
}

DocumentRevision ImageDocument::revision() const
{
    return m_state != nullptr ? m_state->revision() : DocumentRevision{};
}

std::optional<an::ImageDescriptor> ImageDocument::descriptor() const
{
    if (m_state == nullptr)
    {
        return std::nullopt;
    }
    return m_state->currentAsset()->descriptor();
}

an::Result<an::consumer::AssetReadLease> ImageDocument::read(std::stop_token stop) const
{
    if (m_state == nullptr)
    {
        return emptyDocument();
    }
    const detail::DocumentState& state = *m_state;
    auto leased = leaseCurrent<an::consumer::AssetReadLease>(
        state, [&stop](const an::consumer::ImageAsset& asset) { return asset.read(stop); },
        [] { return an::Status::success(); },
        [&state](const an::consumer::ImageAsset* asset) { return state.holds(asset); });
    if (!leased.hasValue())
    {
        return leased.status();
    }
    return std::move(leased->second);
}

an::Result<an::consumer::AssetReadLease> ImageDocument::tryRead() const
{
    if (m_state == nullptr)
    {
        return emptyDocument();
    }
    const detail::DocumentState& state = *m_state;
    auto leased = leaseCurrent<an::consumer::AssetReadLease>(
        state, [](const an::consumer::ImageAsset& asset) { return asset.tryRead(); },
        [] { return an::Status::success(); },
        [&state](const an::consumer::ImageAsset* asset) { return state.holds(asset); });
    if (!leased.hasValue())
    {
        return leased.status();
    }
    return std::move(leased->second);
}

an::Result<ImageDocumentWriter> ImageDocument::write(std::stop_token stop)
{
    if (m_state == nullptr)
    {
        return emptyDocument();
    }
    if (m_state->shared())
    {
        return sharedDocument();
    }
    detail::DocumentState& state = *m_state;
    auto leased = leaseCurrent<an::consumer::AssetWriteLease>(
        state, [&stop](an::consumer::ImageAsset& asset) { return asset.write(stop); },
        [&state] { return state.closed() ? closedDocument() : an::Status::success(); },
        [&state](const an::consumer::ImageAsset* asset) { return state.claimWriter(asset); });
    if (!leased.hasValue())
    {
        return leased.status();
    }
    if (state.closed())
    {
        state.releaseWriter();
        return closedDocument();
    }
    return ImageDocumentWriter(m_state, std::move(leased->first), std::move(leased->second));
}

an::Result<ImageDocumentWriter> ImageDocument::tryWrite()
{
    if (m_state == nullptr)
    {
        return emptyDocument();
    }
    if (m_state->shared())
    {
        return sharedDocument();
    }
    detail::DocumentState& state = *m_state;
    auto leased = leaseCurrent<an::consumer::AssetWriteLease>(
        state, [](an::consumer::ImageAsset& asset) { return asset.tryWrite(); },
        [&state] { return state.closed() ? closedDocument() : an::Status::success(); },
        [&state](const an::consumer::ImageAsset* asset) { return state.claimWriter(asset); });
    if (!leased.hasValue())
    {
        return leased.status();
    }
    if (state.closed())
    {
        state.releaseWriter();
        return closedDocument();
    }
    return ImageDocumentWriter(m_state, std::move(leased->first), std::move(leased->second));
}

an::Status ImageDocument::tryReplace(an::consumer::ImageAsset&& replacement)
{
    if (m_state == nullptr)
    {
        return emptyDocument();
    }
    if (m_state->shared())
    {
        return sharedDocument();
    }
    if (!replacement.valid())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "a replacement must hold an image");
    }
    // Nothing else may lease the replacement, and its write lease is held until the document holds it, so no lease of
    // it is handed out before.
    an::Result<an::consumer::AssetWriteLease> lease = replacement.tryWrite();
    if (!lease.hasValue())
    {
        return an::Status::failure(an::ErrorCode::Busy, "the replacement is leased by someone else");
    }
    auto held = std::make_shared<an::consumer::ImageAsset>(std::move(replacement));
    if (an::Status swapped = m_state->replaceUnwritten(held); !swapped.ok())
    {
        lease->release();
        replacement = std::move(*held);
        return swapped;
    }
    lease->release();
    return an::Status::success();
}

an::Result<an::consumer::ImageAsset> ImageDocument::snapshot(std::stop_token stop) const
{
    an::Result<an::consumer::AssetReadLease> lease = read(std::move(stop));
    if (!lease.hasValue())
    {
        return lease.status();
    }
    return lease->snapshot();
}

ImageDocumentWriter::ImageDocumentWriter() noexcept = default;

ImageDocumentWriter::~ImageDocumentWriter()
{
    release();
}

ImageDocumentWriter::ImageDocumentWriter(ImageDocumentWriter&& other) noexcept = default;

ImageDocumentWriter& ImageDocumentWriter::operator=(ImageDocumentWriter&& other) noexcept
{
    if (this != &other)
    {
        release();
        m_document = std::move(other.m_document);
        m_asset = std::move(other.m_asset);
        m_lease = std::move(other.m_lease);
    }
    return *this;
}

ImageDocumentWriter::ImageDocumentWriter(std::shared_ptr<detail::DocumentState> document,
                                         std::shared_ptr<an::consumer::ImageAsset> asset,
                                         an::consumer::AssetWriteLease lease) noexcept
    : m_document(std::move(document)), m_asset(std::move(asset)), m_lease(std::move(lease))
{
}

bool ImageDocumentWriter::valid() const noexcept
{
    return m_document != nullptr && m_lease.valid();
}

std::uint64_t ImageDocumentWriter::documentIdentity() const noexcept
{
    return m_document != nullptr ? m_document->identity() : 0u;
}

an::consumer::AssetWriteLease& ImageDocumentWriter::asset() noexcept
{
    return m_lease;
}

const an::consumer::AssetWriteLease& ImageDocumentWriter::asset() const noexcept
{
    return m_lease;
}

DocumentRevision ImageDocumentWriter::revision() const noexcept
{
    if (m_document == nullptr)
    {
        return DocumentRevision{};
    }
    return DocumentRevision{.documentIdentity = m_document->identity(), .asset = m_lease.revision()};
}

an::Status ImageDocumentWriter::replace(an::consumer::ImageAsset&& replacement)
{
    if (!valid())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "the writer writes no document");
    }
    if (!replacement.valid())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "a replacement must hold an image");
    }
    // The replacement's write lease is taken before the document can hand it to anyone, so the document's readers wait
    // for this writer on the replacement as they did on the asset it replaces. A lease keeps the asset's state rather
    // than the asset object, so it survives the asset moving to where the document holds it.
    an::Result<an::consumer::AssetWriteLease> lease = replacement.tryWrite();
    if (!lease.hasValue())
    {
        return an::Status::failure(an::ErrorCode::Busy, "the replacement is leased by someone else");
    }
    auto held = std::make_shared<an::consumer::ImageAsset>(std::move(replacement));
    if (an::Status swapped = m_document->replaceAsset(m_asset.get(), held); !swapped.ok())
    {
        lease->release();
        replacement = std::move(*held);
        return swapped;
    }
    // Taking the replacement's lease over releases the replaced asset's, publishing what was written to it.
    m_lease = std::move(*lease);
    m_asset = std::move(held);
    return an::Status::success();
}

void ImageDocumentWriter::release() noexcept
{
    m_lease.release();
    m_asset.reset();
    if (m_document != nullptr)
    {
        // The document is let go once the revision is published.
        m_document->releaseWriter();
    }
    m_document.reset();
}

} // namespace an::ui
