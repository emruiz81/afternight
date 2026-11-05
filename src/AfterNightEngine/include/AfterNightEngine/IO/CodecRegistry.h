// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The immutable set of codec adapters an engine context reads and writes images through.
///
/// A registry is built before any engine context exists, from at most one adapter per named format,
/// and given to a context through `EngineContextOptions::codecRegistry` as a shared const handle.
/// There is no global catalogue and no default registry: a context given none has no codecs, and two
/// contexts share adapters only when they are given the same registry. The registry's own storage and
/// the adapters' configuration are setup costs the caller owns, like the engine context's control
/// block; every pixel and workspace allocation a codec makes belongs to the calling execution's engine
/// context.
///
/// Adapters are reached only through the facade in `IO/CodecIO.h`, which checks each request against
/// the capability record copied here, runs the call inside the execution's task arena and translates
/// what the adapter throws.

#pragma once

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/IO/CodecCapabilities.h>

#include <memory>
#include <optional>
#include <span>

namespace an
{
class ImageCodec;
class CodecRegistryState;

/// An immutable set of codec adapters, at most one per named format; its const members are thread-safe.
class CodecRegistry
{
public:
    /// Releases this registry's references to its adapters; an adapter is destroyed when its last
    /// owner releases it.
    AFTERNIGHTENGINE_API ~CodecRegistry();
    /// Moves the registration state; nothing is copied, so the adapters' addresses do not change.
    /// @param other the registry to move from; left holding no adapter.
    AFTERNIGHTENGINE_API CodecRegistry(CodecRegistry&& other) noexcept;
    /// Not copyable; a registry is shared through the `std::shared_ptr` that `create` returns.
    CodecRegistry(const CodecRegistry&) = delete;
    /// Not assignable; a registry does not change once built.
    CodecRegistry& operator=(const CodecRegistry&) = delete;

    /// Builds a registry, reading and validating each adapter's capability record once.
    ///
    /// An empty span is valid and builds a registry with no codecs. Registration order does not affect
    /// lookup. The adapters are retained only when the registry is built; a failure keeps none.
    ///
    /// @param codecs the adapters, borrowed for the call; each a nonnull shared const adapter.
    /// @return the registry; `ErrorCode::LimitExceeded` for more than `kCodecFormatCount` adapters;
    ///         `ErrorCode::InvalidArgument` for a null adapter, with `CodecFailure::InvalidCapabilities`
    ///         for a record `validCodecCapabilities` refuses, or with `CodecFailure::DuplicateFormat`
    ///         for a second adapter of one format; `ErrorCode::AllocationFailed` when the registry's
    ///         storage cannot be allocated; or, when an adapter's `capabilities` throws,
    ///         `ErrorCode::AllocationFailed` for `std::bad_alloc` and `ErrorCode::ExternalFailure` for
    ///         anything else.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<std::shared_ptr<const CodecRegistry>>
    create(std::span<const std::shared_ptr<const ImageCodec>> codecs);

    /// The capability record of the adapter installed for a format.
    ///
    /// Needs no engine context and makes no callback, allocation or file access.
    ///
    /// @param format the format to look up.
    /// @return a copy of the record; or no value for `Unknown`, a value outside the enumeration, or a
    ///         format with no adapter.
    [[nodiscard]] AFTERNIGHTENGINE_API std::optional<CodecCapabilities> capabilities(CodecFormat format) const noexcept;

    /// The number of formats with an adapter. Allocation-free.
    /// @return from zero through `kCodecFormatCount`.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t size() const noexcept;

private:
    friend class CodecRegistryAccess;

    /// The adapter for a format, reached only through the engine's dispatch boundary so that every call
    /// to it passes the facade's checks.
    /// @param format the format to look up.
    /// @return the adapter, valid as long as this registry; or null when none is installed.
    [[nodiscard]] const ImageCodec* find(CodecFormat format) const noexcept;

    /// Builds from already validated private state.
    /// @param state immutable snapshot, moved into the handle.
    explicit CodecRegistry(std::shared_ptr<const CodecRegistryState> state) noexcept;
    std::shared_ptr<const CodecRegistryState> m_state; ///< Immutable registration state.
};
} // namespace an
