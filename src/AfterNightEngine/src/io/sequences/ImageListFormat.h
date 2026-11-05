// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the image-list manifest's format: its XML read and written without an XML
// library - pugixml's formatting of the document, and its rules for the attributes a manifest
// carries - the names a list's manifest and frames take, and the refusals a manifest reports.

#pragma once

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/SequenceError.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "io/sequences/AccountedBlock.h"
#include "io/sequences/SequenceFile.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// The image-list source and sink's private machinery: the manifest's reader and writer, the path rules,
/// and the failure builders.
namespace an::image_list_detail
{

/// How deeply a manifest document's elements may nest. A manifest nests two deep.
inline constexpr std::size_t kMaxDocumentDepth = 4096u;

/// How many bytes of the engine context's memory a manifest writer buffers.
inline constexpr std::uint64_t kManifestBufferBytes = 65536u;

/// Why a document holds no manifest.
enum class DocumentProblem : std::uint8_t
{
    None, ///< The document holds a manifest.
    NotWellFormed, ///< The text is not a well-formed document of the grammar a manifest is read with.
    NoManifest, ///< No top-level element is named `sequence`.
    TooManyFrames ///< The manifest has more `frame` children than the bound.
};

/// What a manifest's `sequence` element states, each attribute decoded and none interpreted.
struct ManifestElement
{
    std::optional<std::string> version; ///< The first `version` attribute; absent when there is none.
    std::optional<std::string> timestamp; ///< The first `timestamp` attribute; absent when there is none.
    std::optional<std::string> width; ///< The first `width` attribute; absent when there is none.
    std::optional<std::string> height; ///< The first `height` attribute; absent when there is none.
    std::optional<std::string> channels; ///< The first `channels` attribute; absent when there is none.
    std::optional<std::string> mosaiced; ///< The first `isCFA` attribute; absent when there is none.
    std::optional<std::string> framesPerSecond; ///< The first `fps` attribute; absent when there is none.

    /// Each `frame` child's decoded `path`, in document order; absent for a frame that carries none.
    std::vector<std::optional<std::string>> framePaths;
};

/// A parsed document: its manifest, or why it holds none.
struct ParsedDocument
{
    DocumentProblem problem = DocumentProblem::None; ///< Why the document holds no manifest, or `None`.
    ManifestElement manifest; ///< The manifest when `problem` is `None`; empty otherwise.
};

/// Parses a document. Allocates the decoded attributes; throws only `std::bad_alloc`.
///
/// As pugixml reads one, the text ends at its first NUL and a leading UTF-8 byte-order
/// mark is skipped. Character data, comments, processing instructions and a document type declaration
/// are passed over, and the manifest is the first top-level `sequence` element.
///
/// @param text the document.
/// @param maxFrames the most `frame` children a manifest may have; one more is
///        `DocumentProblem::TooManyFrames`.
[[nodiscard]] ParsedDocument parseManifestDocument(std::string_view text, std::uint64_t maxFrames);

/// An attribute's value as pugixml decodes it: a tab, a line feed or a carriage return becomes a space,
/// a carriage return and line feed one space, and the five named entities and numeric character
/// references become what they name; a reference it does not recognise stays as it is spelled.
[[nodiscard]] std::string decodeAttribute(std::string_view raw);

/// An unsigned 64-bit integer as pugixml reads one.
///
/// Leading white space is skipped, then an optional sign and decimal or `0x` hexadecimal digits;
/// reading stops at the first other character, and text with no digits reads as zero. A negative value
/// reads as zero and one past the largest as the largest.
[[nodiscard]] std::uint64_t readUnsigned(std::string_view text) noexcept;

/// A signed 32-bit integer as pugixml reads one: the grammar of `readUnsigned`, with a
/// value outside the 32-bit range read as the nearer bound.
[[nodiscard]] std::int32_t readInteger(std::string_view text) noexcept;

/// A real number as `strtod` reads one in the C locale.
[[nodiscard]] double readReal(std::string_view text) noexcept;

/// A boolean as pugixml reads one: true when the first character is `1`, `t`, `T`, `y`
/// or `Y`, and false otherwise.
[[nodiscard]] bool readBoolean(std::string_view text) noexcept;

/// What a manifest's `sequence` element states beside its frames.
struct ManifestFacts
{
    std::uint64_t timestampSeconds = 0u; ///< Written as `timestamp`, in seconds.
    std::uint64_t width = 0u; ///< Written as `width`, in pixels.
    std::uint64_t height = 0u; ///< Written as `height`, in pixels.
    std::uint64_t channels = 0u; ///< Written as `channels`.
    bool mosaiced = false; ///< Written as `isCFA`, `true` or `false`.
    double framesPerSecond = 0.0; ///< Written as `fps`, with at most four significant digits.
};

/// Writes a manifest's document into a file a bounded buffer at a time, as pugixml formats it.
///
/// Nothing reaches the file until the buffer fills or `flush` is called. Each write that empties the
/// buffer first checks the execution's cancellation, and a refused write is `ErrorCode::ExternalFailure`
/// with `SequenceFailure::IoFailure`.
class ManifestWriter
{
public:
    /// Writes into a file opened for writing.
    ///
    /// @param file the file, borrowed for the writer's life.
    /// @param path the file's path for refusals, borrowed for the writer's life.
    /// @param buffer the buffer, which must hold at least eight bytes.
    ManifestWriter(SequenceFile& file, std::string_view path, sequence_detail::AccountedBlock buffer) noexcept;

    /// The declaration and the `sequence` start tag, self-closed for a list with no frame.
    [[nodiscard]] Status head(const ExecutionContext& execution, const ManifestFacts& facts, bool empty);

    /// One indented, self-closed `frame` element; the path holds no NUL.
    [[nodiscard]] Status frame(const ExecutionContext& execution, std::string_view path);

    /// The `sequence` end tag of a list with frames.
    [[nodiscard]] Status tail(const ExecutionContext& execution);

    /// Writes what is buffered, checking cancellation first.
    [[nodiscard]] Status flush(const ExecutionContext& execution);

    /// How many bytes reached the file.
    [[nodiscard]] std::uint64_t bytes() const noexcept
    {
        return m_bytes;
    }

private:
    /// Appends text to the buffer, flushing each time it fills.
    [[nodiscard]] Status put(const ExecutionContext& execution, std::string_view text);

    /// Appends text as an attribute value: `&`, `<` and `"` as named entities and every byte below 32 as
    /// a two-digit decimal character reference.
    [[nodiscard]] Status putEscaped(const ExecutionContext& execution, std::string_view text);

    SequenceFile& m_file;
    std::string_view m_path;
    sequence_detail::AccountedBlock m_buffer;
    std::uint64_t m_filled = 0u;
    std::uint64_t m_bytes = 0u;
};

/// Whether a path's last component is a nonempty stem followed by `.anseq` in any ASCII case.
[[nodiscard]] bool namesManifest(std::string_view path) noexcept;

/// A path's directory: everything up to and including its last separator of either kind; empty for none.
[[nodiscard]] std::string_view directoryOf(std::string_view path) noexcept;

/// A manifest's stem: its last component without `.anseq`. The path must name a manifest.
[[nodiscard]] std::string_view stemOf(std::string_view path) noexcept;

/// A UTF-8 path as the platform's path. Throws only `std::bad_alloc`.
[[nodiscard]] std::filesystem::path nativePath(std::string_view path);

/// A refusal carrying its sequence reason, the parameter and path when given, and further context.
[[nodiscard]] Status manifestFailure(ErrorCode code, SequenceFailure reason, const char* message,
                                     std::string_view parameter, std::string_view path,
                                     std::vector<StatusContextField> extra = {});

/// An `ErrorCode::InvalidArgument` refusal naming its parameter, with no sequence reason.
[[nodiscard]] Status argumentFailure(const char* message, std::string_view parameter,
                                     std::vector<StatusContextField> extra = {});

/// An operating system's refusal: `ErrorCode::ExternalFailure` with `SequenceFailure::IoFailure`,
/// carrying the platform's error code and the path it refused.
[[nodiscard]] Status ioFailure(int platformError, const char* message, std::string_view path);

} // namespace an::image_list_detail
