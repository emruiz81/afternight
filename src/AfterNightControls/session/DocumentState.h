// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The state an image document, its writer and the commit tickets captured against it share: the asset the document
/// holds now, what the user interface knows it by, and whether it is open.
///
/// Private to the session bridge. The document's mutex orders the asset handle, the writers that hold the document, the
/// names, the sequence, the presentation and the open flag; it is never held while a lease is awaited, so a reader or
/// writer waiting for an asset blocks nobody who only asks the document a question.
#pragma once

#include "session/ImageDocument.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace an::ui::detail
{

/// One document's shared state.
class DocumentState
{
public:
    /// A document's state around an asset.
    ///
    /// @param identity the document's identity, unique in the process.
    /// @param asset the asset it holds; not null.
    /// @param names what it is shown and saved under.
    /// @param shared whether the asset is another document's, which this one shows and never writes.
    DocumentState(std::uint64_t identity, std::shared_ptr<an::consumer::ImageAsset> asset, DocumentNames names,
                  bool shared = false)
        : m_identity(identity), m_shared(shared), m_asset(std::move(asset)), m_names(std::move(names))
    {
    }

    DocumentState(const DocumentState&) = delete;
    DocumentState& operator=(const DocumentState&) = delete;

    /// The document's identity.
    [[nodiscard]] std::uint64_t identity() const noexcept
    {
        return m_identity;
    }

    /// Whether the asset is another document's, which this one shows and never writes.
    [[nodiscard]] bool shared() const noexcept
    {
        return m_shared;
    }

    /// The asset the document holds now.
    [[nodiscard]] std::shared_ptr<an::consumer::ImageAsset> currentAsset() const
    {
        std::lock_guard lock(m_mutex);
        return m_asset;
    }

    /// Whether `asset` is the one the document holds now.
    ///
    /// @param asset an asset the document held when a lease on it was asked for.
    [[nodiscard]] bool holds(const an::consumer::ImageAsset* asset) const
    {
        std::lock_guard lock(m_mutex);
        return m_asset.get() == asset;
    }

    /// Whether the document has been closed.
    [[nodiscard]] bool closed() const noexcept
    {
        return m_closed.load(std::memory_order_acquire);
    }

    /// Closes the document, ordered with a replacement by the document's mutex.
    void close() noexcept
    {
        std::lock_guard lock(m_mutex);
        m_closed.store(true, std::memory_order_release);
    }

    /// The published revision of what the document holds now.
    [[nodiscard]] DocumentRevision revision() const
    {
        const std::shared_ptr<an::consumer::ImageAsset> asset = currentAsset();
        return DocumentRevision{.documentIdentity = m_identity,
                                .asset = asset != nullptr ? asset->revision() : an::consumer::AssetRevision{}};
    }

    /// Makes `replacement` the asset the document holds, when `expected` is still the one it holds and it is open.
    ///
    /// @param expected the asset whose write lease the caller holds.
    /// @param replacement the new asset.
    /// @return success; `ErrorCode::InvalidState` once the document is closed or when it no longer holds `expected`.
    [[nodiscard]] an::Status replaceAsset(const an::consumer::ImageAsset* expected,
                                          std::shared_ptr<an::consumer::ImageAsset> replacement)
    {
        std::lock_guard lock(m_mutex);
        if (m_closed.load(std::memory_order_relaxed))
        {
            return an::Status::failure(an::ErrorCode::InvalidState, "the document is closed");
        }
        if (m_asset.get() != expected)
        {
            return an::Status::failure(an::ErrorCode::InvalidState, "the document no longer holds the asset written");
        }
        m_asset = std::move(replacement);
        return an::Status::success();
    }

    /// Claims the document for a writer of `asset` when it is the asset the document holds now, so a replacement alone
    /// is refused while the writer holds the document.
    ///
    /// @param asset the asset whose write lease the writer was granted.
    /// @return whether the document holds it, and the writer holds the document.
    [[nodiscard]] bool claimWriter(const an::consumer::ImageAsset* asset)
    {
        std::lock_guard lock(m_mutex);
        if (m_asset.get() != asset)
        {
            return false;
        }
        ++m_writers;
        return true;
    }

    /// A writer that claimed the document lets it go.
    void releaseWriter() noexcept
    {
        std::lock_guard lock(m_mutex);
        if (m_writers > 0u)
        {
            --m_writers;
        }
    }

    /// Makes `replacement` the asset the document holds when no writer holds the document and it is open. What leases
    /// the asset it held keeps it.
    ///
    /// @param replacement the new asset.
    /// @return success; `ErrorCode::InvalidState` once the document is closed; `ErrorCode::Busy` while it is written.
    [[nodiscard]] an::Status replaceUnwritten(std::shared_ptr<an::consumer::ImageAsset> replacement)
    {
        std::lock_guard lock(m_mutex);
        if (m_closed.load(std::memory_order_relaxed))
        {
            return an::Status::failure(an::ErrorCode::InvalidState, "the document is closed");
        }
        if (m_writers > 0u)
        {
            return an::Status::failure(an::ErrorCode::Busy, "a writer holds the document");
        }
        m_asset = std::move(replacement);
        return an::Status::success();
    }

    /// The names.
    [[nodiscard]] DocumentNames names() const
    {
        std::lock_guard lock(m_mutex);
        return m_names;
    }

    /// Renames.
    void setNames(DocumentNames names)
    {
        std::lock_guard lock(m_mutex);
        m_names = std::move(names);
    }

    /// The sequence.
    [[nodiscard]] std::optional<DocumentSequence> sequence() const
    {
        std::lock_guard lock(m_mutex);
        return m_sequence;
    }

    /// States the sequence.
    void setSequence(std::optional<DocumentSequence> sequence)
    {
        std::lock_guard lock(m_mutex);
        m_sequence = std::move(sequence);
    }

    /// The presentation.
    [[nodiscard]] DocumentPresentation presentation() const
    {
        std::lock_guard lock(m_mutex);
        return m_presentation;
    }

    /// Changes the presentation and moves its generation.
    void setPresentation(DocumentPresentation presentation)
    {
        std::lock_guard lock(m_mutex);
        m_presentation = std::move(presentation);
        m_presentationGeneration.fetch_add(1u, std::memory_order_acq_rel);
    }

    /// The presentation generation.
    [[nodiscard]] std::uint64_t presentationGeneration() const noexcept
    {
        return m_presentationGeneration.load(std::memory_order_acquire);
    }

private:
    std::uint64_t m_identity;
    bool m_shared;
    mutable std::mutex m_mutex;
    std::shared_ptr<an::consumer::ImageAsset> m_asset;
    /// How many writers hold the document: one, or none, but for a writer granted while another lets go.
    std::uint32_t m_writers = 0u;
    DocumentNames m_names;
    std::optional<DocumentSequence> m_sequence;
    DocumentPresentation m_presentation{};
    std::atomic<std::uint64_t> m_presentationGeneration{0u};
    std::atomic<bool> m_closed{false};
};

} // namespace an::ui::detail
