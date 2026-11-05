// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - holding a whole file for a metadata parser: a copy-on-write mapping, on Windows and elsewhere.
//
// The size is measured before anything is held, and a file longer than the context's largest image is refused before
// it is mapped. A mapping is checked against the size measured, so a file that changed length in between is refused
// rather than parsed at the wrong length.
//
// A mapping takes nothing from the context's budget: its pages are the operating system's cache of the file, and a
// parser touches only the ones it reads. A camera RAW decode reads its Exif this way beside LibRaw's own read of the
// file, so a decode under a tight budget states the Exif the file carries as one under a loose budget does.

#include "io/metadata/MetadataFile.h"

#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/CodecError.h>
#include <AfterNightEngine/Runtime/EngineContext.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// File I/O needs no GUI declarations from the Windows SDK.
#ifndef NOGDI
#define NOGDI
#endif
#ifndef NOUSER
#define NOUSER
#endif
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace an
{
namespace
{

/// The path a literal UTF-8 string names.
///
/// @param path the boundary path.
/// @return the platform path, decoded through `char8_t` rather than the process code page.
[[nodiscard]] std::filesystem::path filePath(std::string_view path)
{
    return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(path.data()), path.size()));
}

/// A codec refusal of the file, naming the operation as `<verb> <container> metadata`.
///
/// @param code the classification.
/// @param format the codec reading the file.
/// @param verb what was being done, as `map`.
/// @param container the container's name, as `TIFF`.
/// @param path the file.
/// @param message a sentence for a person.
/// @return the refusal, with `CodecFailure::MetadataUnreadable`.
[[nodiscard]] Status unreadable(ErrorCode code, CodecFormat format, std::string_view verb, std::string_view container,
                                std::string_view path, std::string_view message)
{
    const std::string operation = std::string(verb) + " " + std::string(container) + " metadata";
    return codecFailure(
        code, CodecFailure::MetadataUnreadable, format, operation, path,
        Status::failure(code, {.message = std::string(message), .operation = operation, .domain = ErrorDomain::Io}));
}

} // namespace

struct MetadataFile::State
{
    State() = default;
    State(const State&) = delete;
    State& operator=(const State&) = delete;

    ~State()
    {
#if defined(_WIN32)
        if (mapped != nullptr)
            UnmapViewOfFile(mapped);
        if (mapping != nullptr)
            CloseHandle(mapping);
        if (file != INVALID_HANDLE_VALUE)
            CloseHandle(file);
#else
        if (mapped != nullptr)
            ::munmap(mapped, bytes.size());
#endif
    }

#if defined(_WIN32)
    /// The file, opened so that nothing writes it in place while it is mapped.
    HANDLE file = INVALID_HANDLE_VALUE;

    /// The copy-on-write mapping of the whole file.
    HANDLE mapping = nullptr;

    /// The mapped view's first byte.
    std::byte* mapped = nullptr;
#else
    /// The private mapping's first byte; the mapping outlives the descriptor it was made from.
    std::byte* mapped = nullptr;
#endif

    /// The whole file, in the mapping.
    std::span<std::byte> bytes;
};

MetadataFile::MetadataFile(std::unique_ptr<State> state) noexcept : m_state(std::move(state)) {}

MetadataFile::MetadataFile(MetadataFile&& other) noexcept = default;

MetadataFile& MetadataFile::operator=(MetadataFile&& other) noexcept = default;

MetadataFile::~MetadataFile() = default;

std::span<std::byte> MetadataFile::bytes() const noexcept
{
    return m_state != nullptr ? m_state->bytes : std::span<std::byte>{};
}

Result<MetadataFile> MetadataFile::open(const EngineContext& engine, std::string_view path, CodecFormat format,
                                        std::string_view container)
{
    std::error_code error;
    const auto size = std::filesystem::file_size(filePath(path), error);
    if (error)
    {
        return Status::failure(ErrorCode::ExternalFailure);
    }
    if (size == 0u)
    {
        return MetadataFile(nullptr);
    }
    if (size > engine.limits().maxImageBytes)
    {
        const std::string operation = "read " + std::string(container) + " metadata";
        return Status::failure(
            ErrorCode::LimitExceeded,
            {.message = "this container is longer than the bytes one image of this context may hold",
             .operation = operation,
             .domain = ErrorDomain::Io,
             .context = {StatusContextField::ofCount(ContextKey::Requested, size),
                         StatusContextField::ofCount(ContextKey::Limit, engine.limits().maxImageBytes)}});
    }
    auto state = std::make_unique<State>();
#if defined(_WIN32)
    state->file = CreateFileW(filePath(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (state->file != INVALID_HANDLE_VALUE)
    {
        // Copy-on-write: a page the parser writes becomes this view's own, and the file is never written.
        state->mapping = CreateFileMappingW(state->file, nullptr, PAGE_WRITECOPY, 0u, 0u, nullptr);
        if (state->mapping != nullptr)
            state->mapped = static_cast<std::byte*>(MapViewOfFile(state->mapping, FILE_MAP_COPY, 0u, 0u, 0u));
    }
    // The handle refuses in-place writes while it is open, and a file whose length changed since it was measured is
    // refused rather than parsed at the wrong length.
    LARGE_INTEGER mappedSize{};
    if (state->mapped == nullptr || !GetFileSizeEx(state->file, &mappedSize) || mappedSize.QuadPart < 0 ||
        static_cast<std::uint64_t>(mappedSize.QuadPart) != size)
    {
        return unreadable(ErrorCode::ExternalFailure, format, "map", container, path,
                          "the container could not be mapped at its reported size");
    }
    state->bytes = std::span<std::byte>{state->mapped, static_cast<std::size_t>(size)};
#else
    const int file = ::open(filePath(path).c_str(), O_RDONLY | O_CLOEXEC);
    if (file < 0)
    {
        return Status::failure(ErrorCode::ExternalFailure);
    }
    // Private and writable: a page the parser writes becomes this mapping's own, and the file is never written. Nothing
    // here stops another program shortening the file while it is mapped, as the Windows handle does; the mapping lives
    // only while a parser reads it, and a file that changed length before it was mapped is refused below.
    struct stat measured{};
    if (::fstat(file, &measured) == 0 && measured.st_size >= 0 && static_cast<std::uint64_t>(measured.st_size) == size)
    {
        void* const mapped =
            ::mmap(nullptr, static_cast<std::size_t>(size), PROT_READ | PROT_WRITE, MAP_PRIVATE, file, 0);
        if (mapped != MAP_FAILED)
        {
            state->mapped = static_cast<std::byte*>(mapped);
            state->bytes = std::span<std::byte>{state->mapped, static_cast<std::size_t>(size)};
        }
    }
    ::close(file);
    if (state->mapped == nullptr)
    {
        return unreadable(ErrorCode::ExternalFailure, format, "map", container, path,
                          "the container could not be mapped at its reported size");
    }
#endif
    return MetadataFile(std::move(state));
}

} // namespace an
