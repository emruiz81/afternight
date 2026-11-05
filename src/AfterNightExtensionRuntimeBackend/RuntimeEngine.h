// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the runtime backend's bindings share on the engine side, with no Python in it: the session's codecs, how an
/// operation is admitted, how a failure becomes the text a Python exception carries, how an image is held, read and
/// written, and how a registration matrix crosses between the SDK's pixel convention and the engine's.
///
/// **An SDK image is compact planar.** `_RuntimeImage.to_numpy()` lends NumPy the image's own samples, and the SDK has
/// always described them as rows of `width` samples one after another, each channel's plane after the previous one. The
/// engine pads every row to its buffer alignment, so every image the backend hands to Python is held in storage laid
/// out without row padding (`createCompactImage`) and every image an engine call returns is brought into that layout
/// before it is (`compactImageOf`).
///
/// **An SDK image is read as fractions of full scale.** The SDK reads an integer sample as a fraction of its type's
/// largest value and a floating sample as the fraction itself, so every image it holds is described in the normalized
/// linear domain whatever a codec called it.
///
/// **What the SDK says about an image beside its samples is the SDK's.** Its header and EXIF cards and the file it came
/// from travel as an `SdkMetadata` beside the asset, because an engine image's codec cards are fixed when the image is
/// made and `set_metadata` changes them on an image NumPy may be writing into. A write states them to the encoder.
#pragma once

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>
#include <AfterNightEngine/Algorithms/Stars/StarDetection.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageStorage.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Runtime/EngineContext.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace an::sdk
{

// ---- Failures.

/// A failure as one line for a person: the code's name, the message, and the root cause's message when it differs.
///
/// @param status the failure.
/// @return the text. Never parsed.
[[nodiscard]] std::string describeFailure(const an::Status& status);

/// What a binding raises for an engine failure: `RuntimeError` in Python, its text the context and the failure.
///
/// @param context what the binding was doing, naming the binding.
/// @param status the failure.
[[noreturn]] void raiseFailure(std::string_view context, const an::Status& status);

/// Raises a failure's text when a status failed.
///
/// @param status the status.
/// @param context what the binding was doing.
inline void require(const an::Status& status, std::string_view context)
{
    if (status.failed())
    {
        raiseFailure(context, status);
    }
}

/// The value of a result, or its failure raised.
///
/// @param result the result.
/// @param context what the binding was doing.
/// @return the value.
template <typename ValueT> [[nodiscard]] ValueT require(an::Result<ValueT>&& result, std::string_view context)
{
    if (!result.hasValue())
    {
        raiseFailure(context, result.status());
    }
    return std::move(*result);
}

/// A refusal the backend decides, as a status.
///
/// @param code the code.
/// @param message the text.
/// @return the failure.
[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message);

// ---- The session.

/// The codecs the backend's session reads and writes: FITS, XISF, TIFF, PNG, JPEG and camera RAW.
///
/// @return the registry, or the first codec's failure to build.
[[nodiscard]] an::Result<std::shared_ptr<const an::CodecRegistry>> createRuntimeCodecRegistry();

/// Whether a path names a file the backend's codecs read, judged by its extension alone, as the SDK judges it.
///
/// @param path the path.
/// @return true for a FITS, XISF, TIFF, PNG, JPEG or camera RAW extension.
[[nodiscard]] bool isSupportedImagePath(std::string_view path) noexcept;

/// The session the bindings run in, or a raised failure while the backend holds none.
///
/// @return the session.
[[nodiscard]] std::shared_ptr<an::consumer::EngineSession> requireSession();

/// Admits one operation of the session, waiting for its threads.
///
/// @param session the session.
/// @param purpose why it runs.
/// @param context what the binding is doing, for the raised failure.
/// @return the scope.
[[nodiscard]] an::consumer::OperationScope admitOperation(const an::consumer::EngineSession& session,
                                                          an::consumer::OperationPurpose purpose,
                                                          std::string_view context);

// ---- Images.

/// What the SDK says about an image beside its samples.
struct SdkMetadata
{
    /// The header cards, in the FITS namespace, and the EXIF cards in the namespace their file gave them, in order.
    std::vector<an::CodecCard> cards{};

    /// The file the image came from or was last written to; empty for an image made in memory.
    std::string fileName{};
};

/// The SDK's channel schema for a channel count: grey, colour, colour with alpha.
///
/// @param channels the count.
/// @return the schema, or nothing for another count.
[[nodiscard]] std::optional<an::ChannelSchema> sdkChannelSchema(std::uint32_t channels) noexcept;

/// The SDK's name of a representation: int8, uint8, int16, uint16, int32, uint32, float32, float64.
[[nodiscard]] const char* sdkSampleTypeName(an::SampleType type) noexcept;

/// The representation an SDK name gives, accepting the aliases sint8, sint16, sint32, float and double; nothing for
/// another name.
[[nodiscard]] std::optional<an::SampleType> sampleTypeFromSdkName(std::string_view name);

/// The SDK's name of a channel schema: gray, rgb, rgba, ciexyz, cielab, cielch, hsl, hsv, hsi, ycbcr, or unknown.
[[nodiscard]] const char* sdkPixelFormatName(an::ChannelSchema schema) noexcept;

/// The description of an SDK image: compact planar storage of the given extent and representation, in the normalized
/// linear domain, grey or colour by its channel count.
///
/// @param limits the context's limits.
/// @param width the width.
/// @param height the height.
/// @param channels 1, 3 or 4.
/// @param type the representation.
/// @return the description, or the refusal naming what cannot be described.
[[nodiscard]] an::Result<an::ImageDescriptor> describeSdkImage(const an::EngineLimits& limits, std::uint32_t width,
                                                               std::uint32_t height, std::uint32_t channels,
                                                               an::SampleType type);

/// The layout of compact planar storage for a description: no row padding, each plane after the previous one.
///
/// @param descriptor the description.
/// @param limits the context's limits.
/// @return the layout, or the refusal.
[[nodiscard]] an::Result<an::ImageLayout> compactLayoutOf(const an::ImageDescriptor& descriptor,
                                                          const an::EngineLimits& limits);

/// Whether a layout is compact planar.
[[nodiscard]] bool isCompactPlanar(const an::ImageLayout& layout) noexcept;

/// An image of an SDK description in compact planar storage of the engine context.
///
/// @param engine the context.
/// @param descriptor the description.
/// @param initialization what the samples start as.
/// @return the image, or the engine's refusal.
template <an::ResidentSample SampleT>
[[nodiscard]] an::Result<an::Image<SampleT>> createCompactImage(const an::EngineContext& engine,
                                                                const an::ImageDescriptor& descriptor,
                                                                an::StorageInitialization initialization)
{
    an::Result<an::ImageLayout> layout = compactLayoutOf(descriptor, engine.limits());
    if (!layout.hasValue())
    {
        return layout.status();
    }
    an::Result<an::ImageStorage<SampleT>> storage = an::ImageStorage<SampleT>::create(engine, *layout, initialization);
    if (!storage.hasValue())
    {
        return storage.status();
    }
    return an::Image<SampleT>::fromStorage(std::move(*storage), descriptor, an::ImageMetadata{});
}

/// An image in an SDK image's form: in compact planar storage, described in the normalized linear domain. An image
/// already in that form is returned as it is; any other is copied once.
///
/// @param scope the operation.
/// @param image the image; taken.
/// @return the image in that form, or the engine's refusal.
[[nodiscard]] an::Result<an::consumer::AnyImage> compactImageOf(const an::consumer::OperationScope& scope,
                                                                an::consumer::AnyImage&& image);

/// A copy of an image converted to another representation, each sample mapped between the two types' full scales,
/// rounded to the nearest value and held to the destination's range.
///
/// @param scope the operation.
/// @param image the source.
/// @param type the destination representation.
/// @return the converted image in an SDK image's form, or the engine's refusal.
[[nodiscard]] an::Result<an::consumer::AnyImage>
convertedImageOf(const an::consumer::OperationScope& scope, const an::consumer::AnyImage& image, an::SampleType type);

/// An image adopted as an asset of the session.
///
/// @param session the session.
/// @param image the image; taken.
/// @param context what the binding is doing, for the raised failure.
/// @return the asset.
[[nodiscard]] std::shared_ptr<an::consumer::ImageAsset>
adoptAsset(const an::consumer::EngineSession& session, an::consumer::AnyImage&& image, std::string_view context);

/// What a file read gives the SDK.
struct ReadImage
{
    /// The samples, in an SDK image's form.
    an::consumer::AnyImage image;

    /// The file's cards, or none when the caller asked for none.
    SdkMetadata metadata;
};

/// Reads an image file through the session's codecs into an SDK image.
///
/// @param scope the operation.
/// @param path the file.
/// @param readMetadata whether the file's cards are read.
/// @return the image and its metadata, or the codec's refusal.
[[nodiscard]] an::Result<ReadImage> readImageFile(const an::consumer::OperationScope& scope, const std::string& path,
                                                  bool readMetadata);

/// How a write encodes, beside the image's own representation.
struct WriteOptions
{
    /// TIFF compression.
    an::OutputCompression tiffCompression = an::OutputCompression::None;

    /// JPEG quality, 1 to 100.
    std::uint32_t jpegQuality = 95u;

    /// PNG deflate level, 0 to 9.
    std::uint32_t pngCompressionLevel = 6u;

    /// Whether the image's EXIF cards are written to a raster format that carries them.
    bool embedExif = true;

    /// Whether `processingHistory` is written as FITS history cards.
    bool embedProcessingHistory = false;

    /// The history lines.
    std::vector<std::string> processingHistory{};

    /// The representation written, when the caller decides it; otherwise the image's own, or the nearest one the
    /// format holds.
    std::optional<an::SampleType> sampleType{};
};

/// Writes an image to a file through the session's codecs, whole or not at all, creating its directory.
///
/// @param scope the operation.
/// @param image the image.
/// @param metadata the cards written beside it.
/// @param path the destination; its extension names the format.
/// @param options how it is encoded.
/// @return success, or the refusal: an extension no codec writes, or the encoder's failure.
[[nodiscard]] an::Status writeImageFile(const an::consumer::OperationScope& scope, const an::consumer::AnyImage& image,
                                        const SdkMetadata& metadata, const std::string& path,
                                        const WriteOptions& options);

/// The value of a FITS card as text: a quoted string's content without its quotes, its doubled quotes undone and its
/// trailing blanks dropped; any other value as it is.
[[nodiscard]] std::string cardText(const an::CodecCard& card);

/// Whether a key is one the SDK never lets a caller set: the structural cards a writer derives from the samples.
[[nodiscard]] bool isReservedMetadataKey(std::string_view key);

/// Sets a header card of the SDK's metadata, replacing a card of the same keyword, or names the file for "FILENAME".
///
/// @param metadata the metadata.
/// @param key the keyword; never empty or reserved.
/// @param lexicalValue the value as a FITS card spells it: a string quoted, a number or a logical as it is.
/// @return success, or the refusal of a card the engine cannot hold.
[[nodiscard]] an::Status setHeaderCard(SdkMetadata& metadata, const std::string& key, const std::string& lexicalValue);

// ---- Stars.

/// Stars detected in one channel of an image read as Float32 fractions of full scale, a non-finite sample counted as
/// no data rather than refused.
///
/// @param scope the operation.
/// @param image the image.
/// @param request what the detector looks for; its full scale is one.
/// @param channel the channel, or nothing for the engine's choice: the green of a colour image, the only one of a grey.
/// @return the stars, brightest first, with the field's summary; or the engine's refusal.
[[nodiscard]] an::Result<an::StarDetectionResult> detectImageStars(const an::consumer::OperationScope& scope,
                                                                   const an::consumer::AnyImage& image,
                                                                   const an::StarDetectionRequest& request,
                                                                   std::optional<std::uint32_t> channel);

// ---- Registration matrices.

/// The engine's transform of a registration the SDK states in its own convention - the first pixel's centre at zero -
/// so that a matrix `register_sequence_paths` answered and one a caller wrote by hand move the same pixels. The matrix
/// is conjugated by the half-pixel shift and a polynomial's normalization origin moves with it.
///
/// @param kind the linear kind.
/// @param sdkMatrix the row-major source-to-reference matrix in the SDK's convention.
/// @param polynomial the polynomial in the SDK's convention, or order zero.
/// @return the engine's transform, or the refusal of a matrix no transform holds.
[[nodiscard]] an::Result<an::RegistrationTransform> engineTransformOfSdk(an::RegistrationTransformKind kind,
                                                                         const std::array<double, 9>& sdkMatrix,
                                                                         const an::RegistrationPolynomial& polynomial);

/// The SDK's matrix of an engine transform: the inverse of `engineTransformOfSdk`.
///
/// @param transform the engine's transform.
/// @return the row-major matrix in the SDK's convention.
[[nodiscard]] std::array<double, 9> sdkMatrixOf(const an::RegistrationTransform& transform);

/// The SDK's polynomial of an engine transform: the same coefficients about the normalization origin in the SDK's
/// convention.
[[nodiscard]] an::RegistrationPolynomial sdkPolynomialOf(const an::RegistrationTransform& transform);

} // namespace an::sdk
