// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The SDK calls over sequences of files: calibration masters stacked from their frames, lights calibrated by them,
/// lights registered by their stars and warped onto the reference, stored registrations applied, and frames stacked or
/// drizzled - each frame read in its own admitted operation, every output written whole or not at all.
///
/// The keywords, defaults, refusals and result dictionaries are the SDK's. Two conventions are kept at this boundary:
/// a registration matrix is reported and accepted with the first pixel's centre at zero, as the SDK always stated it,
/// and converted to the engine's convention here (`RuntimeEngine.h`); and a setting a stack cannot honour - a weighting
/// a median cannot take, a rejection two frames cannot run - is left out with a Python warning naming it rather than
/// dropped silently.

#include "RuntimeBindings.h"

#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>
#include <AfterNightEngine/Algorithms/Calibration/ImageCalibration.h>
#include <AfterNightEngine/Algorithms/Demosaic/Demosaic.h>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzleCanvas.h>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzleExecutor.h>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzleMapDescriptor.h>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzleModel.h>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzleSource.h>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzleStackSource.h>
#include <AfterNightEngine/Algorithms/Kernel/KernelContract.h>
#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/SamplingKernel.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationFit.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationPlan.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>
#include <AfterNightEngine/Algorithms/Stacking/StackExecutor.h>
#include <AfterNightEngine/Algorithms/Stacking/StackFrameSource.h>
#include <AfterNightEngine/Algorithms/Stacking/StackFrameWeighting.h>
#include <AfterNightEngine/Algorithms/Stacking/StackModelError.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRequest.h>
#include <AfterNightEngine/Algorithms/Stacking/StackSource.h>
#include <AfterNightEngine/Algorithms/Stacking/StackWorkingSet.h>
#include <AfterNightEngine/Algorithms/Stacking/StackingModel.h>
#include <AfterNightEngine/Algorithms/Stars/StarAlignment.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ImageStorage.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityMask.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace an::sdk
{

namespace
{

// ---- Small helpers.

[[nodiscard]] std::vector<std::string> pathsFromPythonSequence(const py::sequence& paths,
                                                               const std::string_view function)
{
    std::vector<std::string> result;
    result.reserve(static_cast<std::size_t>(py::len(paths)));
    for (const auto item : paths)
    {
        std::string path = py::str(item).cast<std::string>();
        if (path.empty())
        {
            throw std::runtime_error(std::string(function) + " paths must not contain empty entries");
        }
        result.push_back(std::move(path));
    }
    return result;
}

/// Raises a Python warning; a warning a filter turned into an error is raised as that error.
void warn(const std::string& text)
{
    if (PyErr_WarnEx(PyExc_UserWarning, text.c_str(), 1) != 0)
    {
        throw py::error_already_set();
    }
}

/// A frame read in its own admitted operation.
[[nodiscard]] ReadImage readFrame(const an::consumer::EngineSession& session, const std::string& path,
                                  const std::string_view context)
{
    an::consumer::OperationScope scope = admitOperation(session, an::consumer::OperationPurpose::Batch, context);
    return require(readImageFile(scope, path, true), std::string(context) + " could not read '" + path + "'");
}

/// The SDK's cards of a frame without the cards of its solution - the world coordinate cards, and an XISF solution's
/// properties as the engine's `withoutWcsCards` removes them - which no longer describe a frame moved onto another
/// plane.
[[nodiscard]] SdkMetadata withoutWorldCoordinates(SdkMetadata metadata)
{
    const an::Result<an::CodecMetadata> codec = an::CodecMetadata::create(an::SourceEncoding{}, metadata.cards);
    const an::Result<an::CodecMetadata> stripped = codec.hasValue() ? an::withoutWcsCards(*codec) : codec;
    if (stripped.hasValue())
    {
        metadata.cards.assign(stripped->cards().begin(), stripped->cards().end());
        return metadata;
    }
    // More cards than one image's metadata holds still lose their world coordinate cards.
    std::erase_if(metadata.cards, [](const an::CodecCard& card) {
        return card.origin() == an::CodecNamespace::Fits && an::isWcsKeyword(card.keyword());
    });
    return metadata;
}

/// A demosaiced copy of a Float32 mosaic, by the fast method for its shape - `Fast` for a two-by-two Bayer block,
/// `XTransFast` for an X-Trans sensor's six-by-six table - in compact planar storage.
[[nodiscard]] an::Image<float> demosaicedFrame(const an::consumer::OperationScope& scope,
                                               const an::Image<float>& mosaic, const std::string_view context)
{
    const std::optional<an::ImageDescriptor> stored = mosaic.descriptor();
    const auto view = mosaic.view();
    if (!stored.has_value() || !view.has_value())
    {
        throw std::runtime_error(std::string(context) + ": a frame holds nothing");
    }
    const an::DemosaicMethod method =
        an::cfaPatternIsTabulated(stored->cfa().pattern()) ? an::DemosaicMethod::XTransFast : an::DemosaicMethod::Fast;
    const an::DemosaicPlan plan = require(
        an::planDemosaic(an::DemosaicRequest{.method = method, .domain = {.whiteLevel = 1.0}}, stored->cfa()), context);
    const an::ImageDescriptor colour = require(describeSdkImage(scope.engine().limits(), stored->extent().width(),
                                                                stored->extent().height(), 3u, an::SampleType::Float32),
                                               context);
    an::Image<float> result =
        require(createCompactImage<float>(scope.engine(), colour, an::StorageInitialization::Uninitialized), context);
    {
        std::optional<an::ImageWriteAccess<float>> access = result.write();
        if (!access.has_value())
        {
            throw std::runtime_error(std::string(context) + ": the demosaiced frame holds nothing");
        }
        (void)require(an::demosaicImage(scope.execution(), an::SampleType::Float32, view->data(), view->layout(),
                                        access->view().data(), access->view().layout(), plan),
                      context);
    }
    return result;
}

/// A frame as Float32 fractions of full scale, demosaiced when it is a mosaic: what registration and drizzle read.
[[nodiscard]] an::Image<float> registrationFrameOf(const an::consumer::OperationScope& scope,
                                                   const an::consumer::AnyImage& image, const std::string_view context)
{
    an::consumer::AnyImage widened = require(convertedImageOf(scope, image, an::SampleType::Float32), context);
    an::Image<float> frame = std::move(std::get<an::Image<float>>(widened));
    const std::optional<an::ImageDescriptor> descriptor = frame.descriptor();
    if (descriptor.has_value() && descriptor->isMosaiced())
    {
        return demosaicedFrame(scope, frame, context);
    }
    return frame;
}

// ---- Output naming.

/// How a calibration or an alignment names and writes its outputs.
struct OutputNaming
{
    std::string format = "fits";
    bool use32BitFloat = true;
    bool preserveOriginalName = false;
    std::string filenameSuffix{};
    WriteOptions save{};
};

[[nodiscard]] OutputNaming parseOutputNaming(const py::object& value)
{
    OutputNaming naming;
    if (value.is_none())
    {
        return naming;
    }
    const py::dict values = value.cast<py::dict>();
    assignIfPresent(values, "format", naming.format);
    assignIfPresent(values, "use_32bit_float", naming.use32BitFloat);
    assignIfPresent(values, "preserve_original_name", naming.preserveOriginalName);
    assignIfPresent(values, "filename_suffix", naming.filenameSuffix);
    const py::object saveOptions = dictValueOrNone(values, "save_options");
    if (!saveOptions.is_none())
    {
        naming.save = parseSaveOptions(saveOptions);
    }
    else
    {
        py::dict saveKeys;
        for (const auto item : values)
        {
            const std::string key = py::str(item.first).cast<std::string>();
            if (key != "format" && key != "use_32bit_float" && key != "preserve_original_name" &&
                key != "filename_suffix")
            {
                saveKeys[item.first] = item.second;
            }
        }
        naming.save = parseSaveOptions(saveKeys);
    }
    const std::string format = lowerName(naming.format);
    if (format != "fits" && format != "tiff" && format != "original")
    {
        throw std::runtime_error("Unsupported output format: " + naming.format + " (fits, tiff or original)");
    }
    naming.format = format;
    return naming;
}

[[nodiscard]] bool isFitsExtension(const std::string& extension)
{
    const std::string lower = lowerName(extension);
    return lower == ".fits" || lower == ".fit" || lower == ".fts";
}

/// An output's path: the source's stem or `image_<index>`, the suffix, and the format's extension. A calibration keeps
/// a FITS source's own FITS extension.
[[nodiscard]] std::string outputPathFor(const std::string& source, const std::string& directory,
                                        const OutputNaming& naming, const std::size_t index,
                                        const bool keepFitsExtension)
{
    const std::filesystem::path sourcePath(source);
    std::string base;
    if (naming.preserveOriginalName)
    {
        base = sourcePath.stem().string();
    }
    else
    {
        char numbered[32];
        std::snprintf(numbered, sizeof(numbered), "image_%04zu", index);
        base = numbered;
    }
    const std::string sourceExtension = sourcePath.extension().string();
    std::string extension = ".fits";
    if (naming.format == "original")
    {
        extension = sourceExtension.empty() ? ".fits" : sourceExtension;
    }
    else if (naming.format == "tiff")
    {
        extension = ".tiff";
    }
    else if (keepFitsExtension && isFitsExtension(sourceExtension))
    {
        extension = sourceExtension;
    }
    return (std::filesystem::path(directory) / (base + naming.filenameSuffix + extension)).string();
}

/// The representation an output is written in.
[[nodiscard]] an::SampleType outputTypeFor(const OutputNaming& naming, const an::SampleType source)
{
    if (naming.format == "original")
    {
        return source;
    }
    return naming.use32BitFloat ? an::SampleType::Float32 : an::SampleType::Float64;
}

// ---- Registration choices.

[[nodiscard]] std::pair<an::StarTransformModel, an::RegistrationTransformKind>
transformKindFromSdkName(const std::string& name)
{
    const std::string lower = lowerName(name);
    if (lower == "similarity" || lower == "sim" || lower == "rigid")
    {
        return {an::StarTransformModel::Similarity, an::RegistrationTransformKind::Similarity};
    }
    if (lower == "affine")
    {
        return {an::StarTransformModel::Affine, an::RegistrationTransformKind::Affine};
    }
    if (lower == "homography" || lower == "projective")
    {
        return {an::StarTransformModel::Projective, an::RegistrationTransformKind::Projective};
    }
    throw std::runtime_error("Unsupported registration transform type: " + name);
}

[[nodiscard]] const char* transformKindSdkName(const an::RegistrationTransformKind kind) noexcept
{
    switch (kind)
    {
    case an::RegistrationTransformKind::Similarity:
        return "similarity";
    case an::RegistrationTransformKind::Affine:
        return "affine";
    case an::RegistrationTransformKind::Projective:
        return "homography";
    default:
        return "unknown";
    }
}

[[nodiscard]] an::KernelInterpolation interpolationFromSdkName(const std::string& name)
{
    const std::string lower = lowerName(name);
    if (lower == "nearest" || lower == "nearest_neighbor")
    {
        return an::KernelInterpolation::Nearest;
    }
    if (lower == "bilinear" || lower == "linear")
    {
        return an::KernelInterpolation::Linear;
    }
    if (lower == "bicubic" || lower == "cubic")
    {
        return an::KernelInterpolation::Cubic;
    }
    if (lower == "area")
    {
        return an::KernelInterpolation::Area;
    }
    if (lower == "lanczos" || lower == "lanczos4")
    {
        return an::KernelInterpolation::Lanczos4;
    }
    throw std::runtime_error("Unsupported registration interpolation type: " + name);
}

[[nodiscard]] an::RegistrationCanvasMode alignmentModeFromSdkName(const std::string& name)
{
    const std::string lower = lowerName(name);
    if (lower == "standard" || lower == "reference")
    {
        return an::RegistrationCanvasMode::Reference;
    }
    if (lower == "mosaic" || lower == "union")
    {
        return an::RegistrationCanvasMode::Union;
    }
    if (lower == "intersection" || lower == "common")
    {
        return an::RegistrationCanvasMode::Intersection;
    }
    throw std::runtime_error("Unsupported registration alignment mode: " + name);
}

// ---- Registration records.

/// A registration as the SDK states it: the transform, how it was found and what its stars measured.
struct FrameRegistration
{
    an::RegistrationTransform transform{};
    std::string method = "star_matching";
    bool onError = false;
    std::string error{};
    int matchedStars = 0;
    double rmsPixels = 0.0;
    std::size_t starCount = 0u;
    an::StarFieldSummary summary{};
};

[[nodiscard]] py::list matrixToPythonList(const std::array<double, 9>& matrix)
{
    py::list rows;
    for (std::size_t row = 0u; row < 3u; ++row)
    {
        py::list values;
        for (std::size_t column = 0u; column < 3u; ++column)
        {
            values.append(matrix[row * 3u + column]);
        }
        rows.append(values);
    }
    return rows;
}

[[nodiscard]] py::dict registrationToPythonDict(const FrameRegistration& registration, const bool reference)
{
    const std::array<double, 9> matrix = sdkMatrixOf(registration.transform);
    py::dict result;
    result[py::str("method")] =
        py::str(registration.onError ? "failed" : (reference ? std::string("reference") : registration.method));
    result[py::str("transform_type")] = py::str(transformKindSdkName(registration.transform.linearKind()));
    result[py::str("matrix")] = matrixToPythonList(matrix);
    result[py::str("dx")] = matrix[2];
    result[py::str("dy")] = matrix[5];
    result[py::str("rotation_degrees")] = registration.transform.rotationDegrees();
    result[py::str("scale")] = registration.transform.scale();
    result[py::str("rms_pixels")] = std::isfinite(registration.rmsPixels) ? registration.rmsPixels : 0.0;
    result[py::str("star_count")] = static_cast<int>(registration.starCount);
    result[py::str("matched_stars")] = registration.matchedStars;
    result[py::str("fwhm")] = registration.summary.medianFullWidth.value_or(0.0);
    result[py::str("roundness")] = registration.summary.medianRoundness.value_or(0.0);
    result[py::str("background")] = registration.summary.channel.background;
    result[py::str("background_noise")] = registration.summary.channel.backgroundNoise;
    result[py::str("snr")] = registration.summary.medianSignalToNoise.value_or(0.0);
    result[py::str("quality")] = registration.summary.quality.value_or(0.0);
    result[py::str("on_error")] = registration.onError;
    const an::RegistrationPolynomial polynomial = sdkPolynomialOf(registration.transform);
    if (polynomial.order != 0u)
    {
        py::dict terms;
        terms[py::str("order")] = static_cast<int>(polynomial.order);
        py::list x;
        py::list y;
        for (std::uint32_t index = 0u; index < an::registrationPolynomialTermCount(polynomial.order); ++index)
        {
            x.append(polynomial.x[index]);
            y.append(polynomial.y[index]);
        }
        terms[py::str("a_coeffs")] = x;
        terms[py::str("b_coeffs")] = y;
        py::list origin;
        origin.append(polynomial.normalizationOrigin.x);
        origin.append(polynomial.normalizationOrigin.y);
        terms[py::str("normalization_origin")] = origin;
        py::list scale;
        scale.append(polynomial.normalizationScale.x);
        scale.append(polynomial.normalizationScale.y);
        terms[py::str("normalization_scale")] = scale;
        result[py::str("polynomial")] = terms;
    }
    return result;
}

[[nodiscard]] an::PixelCoordinate pointFromSequence(const py::handle value, const char* name)
{
    const py::sequence values = py::reinterpret_borrow<py::sequence>(value);
    if (py::len(values) != 2)
    {
        throw std::runtime_error(std::string(name) + " must contain 2 values");
    }
    return an::PixelCoordinate{values[0].cast<double>(), values[1].cast<double>()};
}

/// A registration the SDK states as a dictionary - a matrix or a translation, a linear kind and an optional
/// polynomial in the SDK's convention - or the identity for None.
[[nodiscard]] FrameRegistration registrationFromPythonDict(const py::handle value)
{
    FrameRegistration registration;
    if (value.is_none())
    {
        return registration;
    }
    if (!py::isinstance<py::dict>(value))
    {
        throw std::runtime_error("registration transform must be a dictionary");
    }
    const py::dict values = py::reinterpret_borrow<py::dict>(value);
    an::RegistrationTransformKind kind = an::RegistrationTransformKind::Similarity;
    const py::object type = dictValueOrNone(values, "transform_type");
    if (!type.is_none())
    {
        kind = transformKindFromSdkName(type.cast<std::string>()).second;
    }
    std::array<double, 9> matrix{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
    const py::object matrixValue = dictValueOrNone(values, "matrix");
    if (!matrixValue.is_none())
    {
        const py::sequence rows = matrixValue.cast<py::sequence>();
        if (py::len(rows) != 3)
        {
            throw std::runtime_error("registration transform matrix must contain 3 rows");
        }
        for (std::size_t row = 0u; row < 3u; ++row)
        {
            const py::sequence columns = py::reinterpret_borrow<py::sequence>(rows[row]);
            if (py::len(columns) != 3)
            {
                throw std::runtime_error("registration transform matrix rows must contain 3 columns");
            }
            for (std::size_t column = 0u; column < 3u; ++column)
            {
                matrix[row * 3u + column] = columns[column].cast<double>();
            }
        }
    }
    else
    {
        const py::object dx = dictValueOrNone(values, "dx");
        const py::object dy = dictValueOrNone(values, "dy");
        if (!dx.is_none())
        {
            matrix[2] = dx.cast<double>();
        }
        if (!dy.is_none())
        {
            matrix[5] = dy.cast<double>();
        }
    }
    an::RegistrationPolynomial polynomial;
    const py::object polynomialValue = dictValueOrNone(values, "polynomial");
    if (!polynomialValue.is_none())
    {
        const py::dict terms = polynomialValue.cast<py::dict>();
        int order = 0;
        assignIfPresent(terms, "order", order);
        if (order < 2 || order > 4)
        {
            throw std::runtime_error("registration polynomial transform order must be between 2 and 4");
        }
        polynomial.order = static_cast<std::uint32_t>(order);
        const std::uint32_t count = an::registrationPolynomialTermCount(polynomial.order);
        const py::sequence x = dictValueOrNone(terms, "a_coeffs").cast<py::sequence>();
        const py::sequence y = dictValueOrNone(terms, "b_coeffs").cast<py::sequence>();
        if (py::len(x) < count || py::len(y) < count)
        {
            throw std::runtime_error("registration polynomial transform coefficients are incomplete");
        }
        for (std::uint32_t index = 0u; index < count; ++index)
        {
            polynomial.x[index] = x[index].cast<double>();
            polynomial.y[index] = y[index].cast<double>();
        }
        const py::object origin = dictValueOrNone(terms, "normalization_origin");
        if (!origin.is_none())
        {
            polynomial.normalizationOrigin = pointFromSequence(origin, "registration polynomial normalization_origin");
        }
        const py::object scale = dictValueOrNone(terms, "normalization_scale");
        if (!scale.is_none())
        {
            polynomial.normalizationScale = pointFromSequence(scale, "registration polynomial normalization_scale");
        }
    }
    registration.transform = require(engineTransformOfSdk(kind, matrix, polynomial), "registration transform");
    const py::object method = dictValueOrNone(values, "method");
    if (!method.is_none())
    {
        // A stated method only says whether the registration failed; an applied registration reports the method the
        // SDK always reported for a registered frame.
        registration.onError = lowerName(method.cast<std::string>()) == "failed";
    }
    const py::object onError = dictValueOrNone(values, "on_error");
    if (!onError.is_none())
    {
        registration.onError = onError.cast<bool>();
    }
    const py::object matched = dictValueOrNone(values, "matched_stars");
    if (!matched.is_none())
    {
        registration.matchedStars = matched.cast<int>();
    }
    const py::object rms = dictValueOrNone(values, "rms_pixels");
    if (!rms.is_none())
    {
        registration.rmsPixels = rms.cast<double>();
    }
    return registration;
}

/// Each frame's registration of a call, checked against its frames.
[[nodiscard]] std::vector<FrameRegistration> registrationsFromPython(const std::vector<std::string>& paths,
                                                                     const py::sequence& registrations,
                                                                     const int referenceIndex,
                                                                     const std::string_view function)
{
    if (static_cast<std::size_t>(py::len(registrations)) != paths.size())
    {
        throw std::runtime_error(std::string(function) + " paths and registrations must have the same length");
    }
    if (referenceIndex < 0 || referenceIndex >= static_cast<int>(paths.size()))
    {
        throw std::runtime_error(std::string(function) + " reference_index is out of range");
    }
    std::vector<FrameRegistration> result;
    result.reserve(paths.size());
    for (std::size_t index = 0u; index < paths.size(); ++index)
    {
        result.push_back(registrationFromPythonDict(registrations[static_cast<py::ssize_t>(index)]));
    }
    return result;
}

// ---- Alignment.

/// What aligning one frame did.
struct AlignedFrame
{
    std::string inputPath;
    std::string outputPath;
    bool succeeded = false;
    bool skipped = false;
    std::string error;
};

/// The plane the aligned frames are warped onto: each frame's bounds under its transform, planned in the mode asked.
[[nodiscard]] an::RegistrationCanvas alignmentCanvas(const std::vector<FrameRegistration>& registrations,
                                                     const std::vector<std::pair<std::uint32_t, std::uint32_t>>& sizes,
                                                     const std::size_t referenceIndex,
                                                     const an::RegistrationCanvasMode mode,
                                                     const std::string_view context)
{
    std::vector<an::RegistrationBounds> bounds;
    std::uint32_t reference = 0u;
    for (std::size_t index = 0u; index < registrations.size(); ++index)
    {
        if (registrations[index].onError)
        {
            continue;
        }
        if (index == referenceIndex)
        {
            reference = static_cast<std::uint32_t>(bounds.size());
        }
        bounds.push_back(require(
            an::computeRegistrationBounds(registrations[index].transform, sizes[index].first, sizes[index].second),
            context));
    }
    if (bounds.empty())
    {
        throw std::runtime_error(std::string(context) + ": no frame has a registration to align");
    }
    return require(an::planRegistrationCanvas(bounds, reference, mode), context);
}

/// Warps one frame onto a canvas and writes it.
[[nodiscard]] an::Status alignFrame(const an::consumer::EngineSession& session, const std::string& path,
                                    const an::RegistrationTransform& transform, const an::RegistrationCanvas& canvas,
                                    const an::KernelInterpolation interpolation, const std::string& outputPath,
                                    const OutputNaming& naming, const std::string_view context)
{
    ReadImage read = readFrame(session, path, context);
    an::consumer::OperationScope scope = admitOperation(session, an::consumer::OperationPurpose::Batch, context);
    const an::SampleType sourceType = an::consumer::sampleTypeOf(read.image);
    const an::Image<float> frame = registrationFrameOf(scope, read.image, context);
    const std::optional<an::ImageDescriptor> descriptor = frame.descriptor();
    const auto view = frame.view();
    if (!descriptor.has_value() || !view.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the frame holds nothing");
    }
    an::RegistrationWarpRequest request;
    request.width = canvas.width;
    request.height = canvas.height;
    request.canvasOrigin = canvas.origin;
    // The warp writes only where the frame reaches, so the zeroed frame holds zero wherever it does not; the clamp
    // border continues the frame's edge sample past its edge, where a step to zero would ring.
    request.sampling = an::SamplingKernelParameters{.interpolation = interpolation,
                                                    .border = an::KernelBorderMode::Clamp,
                                                    .invalid = an::SamplingInvalidPolicy::RequireAll};
    an::Result<an::RegistrationWarpPlan> plan =
        an::planRegistrationWarp(*descriptor, transform, an::SipDistortion{}, request);
    if (!plan.hasValue())
    {
        return plan.status();
    }
    an::Result<an::ImageDescriptor> destination =
        describeSdkImage(scope.engine().limits(), canvas.width, canvas.height, descriptor->extent().channelCount(),
                         an::SampleType::Float32);
    if (!destination.hasValue())
    {
        return destination.status();
    }
    an::Result<an::Image<float>> warped =
        createCompactImage<float>(scope.engine(), *destination, an::StorageInitialization::Zeroed);
    if (!warped.hasValue())
    {
        return warped.status();
    }
    an::Result<an::ValidityMask> mask = an::ValidityMask::create(scope.engine(), destination->extent());
    if (!mask.hasValue())
    {
        return mask.status();
    }
    {
        std::optional<an::ImageWriteAccess<float>> access = warped->write();
        std::optional<an::ValidityMaskView<std::uint8_t>> maskView = mask->view();
        if (!access.has_value() || !maskView.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the aligned frame holds nothing");
        }
        const an::Result<an::RegistrationWarpResult> ran = an::warpRegisteredImage(
            scope.execution(), an::SampleType::Float32, view->data(), view->layout(), access->view().data(),
            access->view().layout(), *plan, an::KernelValidityView{}, *maskView);
        if (!ran.hasValue())
        {
            return ran.status();
        }
    }
    WriteOptions options = naming.save;
    options.sampleType = outputTypeFor(naming, sourceType);
    return writeImageFile(scope, an::consumer::AnyImage(std::move(*warped)),
                          withoutWorldCoordinates(std::move(read.metadata)), outputPath, options);
}

/// Aligns every frame with a registration onto one canvas, and the SDK's result dictionary of it.
[[nodiscard]] py::dict alignFrames(const an::consumer::EngineSession& session, const std::vector<std::string>& paths,
                                   const std::vector<FrameRegistration>& registrations,
                                   const std::vector<std::pair<std::uint32_t, std::uint32_t>>& sizes,
                                   const int referenceIndex, const std::string& outputDir,
                                   const an::KernelInterpolation interpolation, const an::RegistrationCanvasMode mode,
                                   const OutputNaming& naming, const std::string_view context)
{
    const an::RegistrationCanvas canvas =
        alignmentCanvas(registrations, sizes, static_cast<std::size_t>(referenceIndex), mode, context);
    std::error_code ignored;
    std::filesystem::create_directories(outputDir, ignored);
    py::list frames;
    int succeeded = 0;
    int failed = 0;
    int skipped = 0;
    for (std::size_t index = 0u; index < paths.size(); ++index)
    {
        py::dict frame;
        frame[py::str("input_path")] = paths[index];
        frame[py::str("index")] = static_cast<int>(index);
        frame[py::str("sip_correction_applied")] = false;
        std::string outputPath;
        std::string error;
        bool ok = false;
        if (registrations[index].onError)
        {
            error = registrations[index].error.empty() ? "the frame has no registration" : registrations[index].error;
            ++skipped;
        }
        else
        {
            outputPath = outputPathFor(paths[index], outputDir, naming, index, false);
            const an::Status aligned = alignFrame(session, paths[index], registrations[index].transform, canvas,
                                                  interpolation, outputPath, naming, context);
            if (aligned.ok())
            {
                ok = true;
                ++succeeded;
            }
            else
            {
                error = describeFailure(aligned);
                outputPath.clear();
                ++failed;
            }
        }
        frame[py::str("output_path")] = outputPath;
        frame[py::str("succeeded")] = ok;
        frame[py::str("error_message")] = error;
        frame[py::str("registration")] =
            registrationToPythonDict(registrations[index], index == static_cast<std::size_t>(referenceIndex));
        frames.append(frame);
    }
    py::dict result;
    result[py::str("reference_index")] = referenceIndex;
    result[py::str("reference_path")] = paths[static_cast<std::size_t>(referenceIndex)];
    result[py::str("total_count")] = static_cast<int>(paths.size());
    result[py::str("succeeded_count")] = succeeded;
    result[py::str("failed_count")] = failed;
    result[py::str("skipped_count")] = skipped;
    result[py::str("canvas_width")] = static_cast<int>(canvas.width);
    result[py::str("canvas_height")] = static_cast<int>(canvas.height);
    result[py::str("frames")] = frames;
    return result;
}

// ---- Stacking.

/// A stack the SDK asks for, before the frames' description is known.
struct StackSettings
{
    std::string methodName = "average";
    an::StackCombination combination = an::StackCombination::Average;
    an::StackRejection rejection = an::StackRejection::None;
    std::optional<double> low{};
    std::optional<double> high{};
    an::StackNormalizationScope normalizationScope = an::StackNormalizationScope::None;
    std::optional<an::NormalizationMethod> normalizationMethod{};
    an::ScaleEstimator scaleEstimator = an::ScaleEstimator::IterativeKSigma;
    an::StackWeighting weighting = an::StackWeighting::Uniform;
    an::StarCountWeightingParameters starCount{};
    std::optional<double> minimumWeight{};
    /// Whether the call asked for the per-channel width weighting, which is not offered: it is said and left out.
    bool perChannelFwhmAsked = false;
    an::LocalWeightingRequest localWeighting{};
    bool output64 = false;
    std::uint32_t referenceIndex = 0u;
    std::vector<double> imageWeights{};
    std::vector<std::vector<double>> channelWeights{};
    std::uint64_t workingSetLimitBytes = 0u;
    bool rejectionMaps = false;
    bool mergeRejectionMaps = false;
};

[[nodiscard]] std::pair<an::StackCombination, an::StackRejection> methodFromSdkName(const std::string& name,
                                                                                    std::string& canonical)
{
    const std::string lower = lowerName(name);
    const auto average = [&canonical](const char* named, const an::StackRejection rejection) {
        canonical = named;
        return std::pair{an::StackCombination::Average, rejection};
    };
    if (lower == "average" || lower == "avg" || lower == "mean")
    {
        return average("average", an::StackRejection::None);
    }
    if (lower == "median")
    {
        canonical = "median";
        return {an::StackCombination::Median, an::StackRejection::None};
    }
    if (lower == "maximum" || lower == "max")
    {
        canonical = "maximum";
        return {an::StackCombination::Maximum, an::StackRejection::None};
    }
    if (lower == "minimum" || lower == "min")
    {
        canonical = "minimum";
        return {an::StackCombination::Minimum, an::StackRejection::None};
    }
    if (lower == "sum")
    {
        canonical = "sum";
        return {an::StackCombination::Sum, an::StackRejection::None};
    }
    if (lower == "auto_adaptive_weighted_average" || lower == "auto_adaptive" || lower == "adaptive_weighted_average")
    {
        canonical = "auto_adaptive_weighted_average";
        return {an::StackCombination::AutoAdaptiveWeightedAverage, an::StackRejection::None};
    }
    if (lower == "sigma_clipping" || lower == "sigma" || lower == "ksigma")
    {
        return average("sigma_clipping", an::StackRejection::SigmaClipping);
    }
    if (lower == "mad_sigma_clipping" || lower == "mad_sigma" || lower == "madsigma")
    {
        return average("mad_sigma_clipping", an::StackRejection::MadSigmaClipping);
    }
    if (lower == "median_sigma")
    {
        return average("median_sigma", an::StackRejection::MedianSigmaReplacement);
    }
    if (lower == "winsorized_sigma" || lower == "winsorized")
    {
        return average("winsorized_sigma", an::StackRejection::WinsorizedSigmaClipping);
    }
    if (lower == "linear_fit" || lower == "linear")
    {
        return average("linear_fit", an::StackRejection::LinearFitClipping);
    }
    if (lower == "percentile" || lower == "percentile_clipping")
    {
        return average("percentile", an::StackRejection::PercentileClipping);
    }
    if (lower == "gesdt" || lower == "esd")
    {
        return average("gesdt", an::StackRejection::GeneralizedEsdTest);
    }
    throw std::runtime_error("Unsupported stacking method: " + name);
}

[[nodiscard]] std::vector<double> doublesFromSequence(const py::handle value, const std::string_view field)
{
    std::vector<double> result;
    for (const auto item : py::reinterpret_borrow<py::sequence>(value))
    {
        result.push_back(py::reinterpret_borrow<py::object>(item).cast<double>());
    }
    if (result.empty())
    {
        throw std::runtime_error(std::string(field) + " must not be empty");
    }
    return result;
}

[[nodiscard]] StackSettings parseStackSettings(const std::string& method, const py::object& value,
                                               const std::size_t frameCount)
{
    StackSettings settings;
    std::tie(settings.combination, settings.rejection) = methodFromSdkName(method, settings.methodName);
    if (value.is_none())
    {
        return settings;
    }
    const py::dict values = value.cast<py::dict>();
    const py::object named = dictValueOrNone(values, "method");
    if (!named.is_none())
    {
        std::tie(settings.combination, settings.rejection) =
            methodFromSdkName(named.cast<std::string>(), settings.methodName);
    }
    const py::object normalization = dictValueOrNone(values, "normalization");
    if (!normalization.is_none())
    {
        const std::string name = lowerName(normalization.cast<std::string>());
        settings.normalizationScope = an::StackNormalizationScope::Global;
        if (name == "none" || name == "off")
        {
            settings.normalizationScope = an::StackNormalizationScope::None;
        }
        else if (name == "additive")
        {
            settings.normalizationMethod = an::NormalizationMethod::Additive;
        }
        else if (name == "multiplicative")
        {
            settings.normalizationMethod = an::NormalizationMethod::Multiplicative;
        }
        else if (name == "additive_scaling" || name == "additive_scale")
        {
            settings.normalizationMethod = an::NormalizationMethod::AdditiveScaling;
        }
        else if (name == "multiplicative_scaling" || name == "multiplicative_scale")
        {
            settings.normalizationMethod = an::NormalizationMethod::MultiplicativeScaling;
        }
        else
        {
            throw std::runtime_error("Unsupported stacking normalization method: " + normalization.cast<std::string>());
        }
    }
    const py::object estimator = dictValueOrNone(values, "scale_estimator");
    if (!estimator.is_none())
    {
        const std::string name = lowerName(estimator.cast<std::string>());
        if (name == "mad")
        {
            settings.scaleEstimator = an::ScaleEstimator::MedianAbsoluteDeviation;
        }
        else if (name == "ikss" || name == "iterative_ksigma")
        {
            settings.scaleEstimator = an::ScaleEstimator::IterativeKSigma;
        }
        else
        {
            throw std::runtime_error("Unsupported stacking scale estimator: " + estimator.cast<std::string>());
        }
    }
    const py::object weighting = dictValueOrNone(values, "weighting");
    if (!weighting.is_none())
    {
        const std::string name = lowerName(weighting.cast<std::string>());
        if (name == "none" || name == "off")
        {
            settings.weighting = an::StackWeighting::Uniform;
        }
        else if (name == "noise" || name == "noise_based")
        {
            settings.weighting = an::StackWeighting::Noise;
        }
        else if (name == "star_count" || name == "stars")
        {
            settings.weighting = an::StackWeighting::StarCount;
        }
        else if (name == "weighted_fwhm" || name == "fwhm")
        {
            settings.weighting = an::StackWeighting::Fwhm;
        }
        else if (name == "image_count" || name == "stack_count")
        {
            settings.weighting = an::StackWeighting::ImageCount;
        }
        else if (name == "photometric")
        {
            settings.weighting = an::StackWeighting::Photometric;
        }
        else
        {
            throw std::runtime_error("Unsupported stacking weighting method: " + weighting.cast<std::string>());
        }
    }
    const py::object scaling = dictValueOrNone(values, "star_count_scaling");
    if (!scaling.is_none())
    {
        const std::string name = lowerName(scaling.cast<std::string>());
        if (name == "linear")
        {
            settings.starCount.scaling = an::StarCountScaling::Linear;
        }
        else if (name == "quadratic" || name == "siril")
        {
            settings.starCount.scaling = an::StarCountScaling::Quadratic;
        }
        else if (name == "exponential" || name == "exp")
        {
            settings.starCount.scaling = an::StarCountScaling::Exponential;
        }
        else if (name == "roundness_weighted" || name == "roundness")
        {
            settings.starCount.scaling = an::StarCountScaling::RoundnessWeighted;
        }
        else
        {
            throw std::runtime_error("Unsupported star-count weighting scale: " + scaling.cast<std::string>());
        }
    }
    for (const char* key : {"sigma_low", "sigma_or_linear_low"})
    {
        const py::object low = dictValueOrNone(values, key);
        if (!low.is_none())
        {
            settings.low = low.cast<double>();
        }
    }
    for (const char* key : {"sigma_high", "sigma_or_linear_high"})
    {
        const py::object high = dictValueOrNone(values, key);
        if (!high.is_none())
        {
            settings.high = high.cast<double>();
        }
    }
    assignIfPresent(values, "out_64_bits", settings.output64);
    double memoryMegabytes = 0.0;
    assignIfPresent(values, "max_memory_usage_mb", memoryMegabytes);
    if (memoryMegabytes > 0.0)
    {
        settings.workingSetLimitBytes = static_cast<std::uint64_t>(memoryMegabytes * 1024.0 * 1024.0);
    }
    assignIfPresent(values, "star_count_exponential_alpha", settings.starCount.exponentialAlpha);
    const py::object floor = dictValueOrNone(values, "min_weight_floor");
    if (!floor.is_none())
    {
        settings.minimumWeight = floor.cast<double>();
    }
    assignIfPresent(values, "enable_per_channel_fwhm_weighting", settings.perChannelFwhmAsked);
    assignIfPresent(values, "create_rejection_maps", settings.rejectionMaps);
    assignIfPresent(values, "merge_rejection_maps", settings.mergeRejectionMaps);

    const py::object reference = dictValueOrNone(values, "reference_index");
    if (!reference.is_none())
    {
        const int index = reference.cast<int>();
        if (index < 0 || static_cast<std::size_t>(index) >= frameCount)
        {
            throw std::runtime_error("stack_sequence_paths() reference_index is out of range");
        }
        settings.referenceIndex = static_cast<std::uint32_t>(index);
    }
    const py::object imageWeights = dictValueOrNone(values, "image_weights");
    if (!imageWeights.is_none())
    {
        settings.imageWeights = doublesFromSequence(imageWeights, "stacking image_weights");
        if (settings.imageWeights.size() != frameCount)
        {
            throw std::runtime_error("stack_sequence_paths() image_weights must match the frame count");
        }
    }
    const py::object channelWeights = dictValueOrNone(values, "channel_weights");
    if (!channelWeights.is_none())
    {
        for (const auto item : channelWeights.cast<py::sequence>())
        {
            settings.channelWeights.push_back(doublesFromSequence(item, "stacking channel_weights"));
            if (settings.channelWeights.back().size() != frameCount)
            {
                throw std::runtime_error("stack_sequence_paths() channel_weights must match the frame count");
            }
        }
        if (settings.channelWeights.empty())
        {
            throw std::runtime_error("stacking channel_weights must not be empty");
        }
    }
    const py::object local = dictValueOrNone(values, "local_weighting");
    if (!local.is_none())
    {
        if (py::isinstance<py::bool_>(local))
        {
            settings.localWeighting.enabled = local.cast<bool>();
        }
        else
        {
            const py::dict localValues = local.cast<py::dict>();
            settings.localWeighting.enabled = true;
            assignIfPresent(localValues, "enabled", settings.localWeighting.enabled);
            assignIfPresent(localValues, "window_size", settings.localWeighting.windowSize);
            assignIfPresent(localValues, "weight_grid_spacing", settings.localWeighting.gridSpacing);
            assignIfPresent(localValues, "snr_weight_exponent", settings.localWeighting.snrExponent);
        }
    }
    return settings;
}

/// Which quality outputs a stack call asks for, and where each is written.
struct QualityOutputs
{
    bool report = false;
    std::array<std::optional<std::string>, an::kStackOutputMapCount> maps{};
};

[[nodiscard]] std::string auxiliaryMapPath(const std::string& outputPath, const std::string_view suffix)
{
    const std::filesystem::path path(outputPath);
    const std::filesystem::path extension = path.extension().empty() ? std::filesystem::path(".fit") : path.extension();
    return (path.parent_path() / (path.stem().string() + "_" + std::string(suffix) + extension.string())).string();
}

void requestMap(const py::object& value, const std::string& outputPath, const std::string_view suffix,
                std::optional<std::string>& target)
{
    if (value.is_none())
    {
        return;
    }
    if (py::isinstance<py::str>(value))
    {
        const std::string path = value.cast<std::string>();
        if (!path.empty())
        {
            target = path;
        }
        return;
    }
    if (value.cast<bool>())
    {
        target = auxiliaryMapPath(outputPath, suffix);
    }
}

[[nodiscard]] QualityOutputs parseQualityOutputs(const py::object& params, const std::string& outputPath,
                                                 StackSettings& settings)
{
    QualityOutputs outputs;
    if (params.is_none())
    {
        return outputs;
    }
    const py::dict values = params.cast<py::dict>();
    const auto map = [&outputs](const an::StackOutputMap which) -> std::optional<std::string>& {
        return outputs.maps[static_cast<std::size_t>(which)];
    };
    const py::object report = dictValueOrNone(values, "quality_report");
    outputs.report = !report.is_none() && report.cast<bool>();
    const py::object qualityMaps = dictValueOrNone(values, "quality_maps");
    if (!qualityMaps.is_none())
    {
        if (py::isinstance<py::bool_>(qualityMaps))
        {
            if (qualityMaps.cast<bool>())
            {
                map(an::StackOutputMap::Coverage) = auxiliaryMapPath(outputPath, "coverage");
                map(an::StackOutputMap::Variance) = auxiliaryMapPath(outputPath, "variance");
                map(an::StackOutputMap::SignalToNoiseGain) = auxiliaryMapPath(outputPath, "snr_gain");
                map(an::StackOutputMap::WeightSum) = auxiliaryMapPath(outputPath, "weight_sum");
            }
        }
        else
        {
            const py::dict maps = qualityMaps.cast<py::dict>();
            requestMap(dictValueOrNone(maps, "coverage"), outputPath, "coverage", map(an::StackOutputMap::Coverage));
            requestMap(dictValueOrNone(maps, "variance"), outputPath, "variance", map(an::StackOutputMap::Variance));
            requestMap(dictValueOrNone(maps, "snr_gain"), outputPath, "snr_gain",
                       map(an::StackOutputMap::SignalToNoiseGain));
            requestMap(dictValueOrNone(maps, "weight_sum"), outputPath, "weight_sum",
                       map(an::StackOutputMap::WeightSum));
            requestMap(dictValueOrNone(maps, "rejection_low"), outputPath, "rejection_low",
                       map(an::StackOutputMap::RejectionLow));
            requestMap(dictValueOrNone(maps, "rejection_high"), outputPath, "rejection_high",
                       map(an::StackOutputMap::RejectionHigh));
        }
    }
    const py::object rejectionMaps = dictValueOrNone(values, "rejection_maps");
    if (!rejectionMaps.is_none())
    {
        if (py::isinstance<py::bool_>(rejectionMaps))
        {
            if (rejectionMaps.cast<bool>())
            {
                map(an::StackOutputMap::RejectionLow) = auxiliaryMapPath(outputPath, "rejection_low");
                map(an::StackOutputMap::RejectionHigh) = auxiliaryMapPath(outputPath, "rejection_high");
            }
        }
        else
        {
            const py::dict maps = rejectionMaps.cast<py::dict>();
            requestMap(dictValueOrNone(maps, "low"), outputPath, "rejection_low",
                       map(an::StackOutputMap::RejectionLow));
            requestMap(dictValueOrNone(maps, "high"), outputPath, "rejection_high",
                       map(an::StackOutputMap::RejectionHigh));
            requestMap(dictValueOrNone(maps, "rejection_low"), outputPath, "rejection_low",
                       map(an::StackOutputMap::RejectionLow));
            requestMap(dictValueOrNone(maps, "rejection_high"), outputPath, "rejection_high",
                       map(an::StackOutputMap::RejectionHigh));
        }
    }
    requestMap(dictValueOrNone(values, "coverage_map"), outputPath, "coverage", map(an::StackOutputMap::Coverage));
    requestMap(dictValueOrNone(values, "variance_map"), outputPath, "variance", map(an::StackOutputMap::Variance));
    requestMap(dictValueOrNone(values, "snr_gain_map"), outputPath, "snr_gain",
               map(an::StackOutputMap::SignalToNoiseGain));
    requestMap(dictValueOrNone(values, "weight_sum_map"), outputPath, "weight_sum", map(an::StackOutputMap::WeightSum));
    if (map(an::StackOutputMap::RejectionLow).has_value() || map(an::StackOutputMap::RejectionHigh).has_value())
    {
        settings.rejectionMaps = true;
        settings.mergeRejectionMaps = false;
    }
    return outputs;
}

/// The description of a stack's frames: the first frame's, as fractions of full scale.
[[nodiscard]] an::ImageDescriptor stackInputDescriptor(const an::consumer::EngineSession& session,
                                                       const std::string& path, const std::string_view context)
{
    an::consumer::OperationScope scope = admitOperation(session, an::consumer::OperationPurpose::Batch, context);
    const an::CodecImageInfo info =
        require(an::probeImage(scope.engine(), scope.execution(),
                               an::CodecReadRequest{.path = path, .readMetadata = false, .validatePayload = false}),
                std::string(context) + " could not read '" + path + "'");
    const std::optional<an::ImageDescriptor> stored = info.descriptor.toImageDescriptor();
    if (!stored.has_value())
    {
        throw std::runtime_error(std::string(context) + ": '" + path +
                                 "' holds samples outside the eight representations the SDK reads");
    }
    an::ImageDescriptorRequest request = stored->toRequest();
    request.numericDomain = an::NumericDomain::NormalizedLinear;
    const std::optional<an::ImageDescriptor> normalized = an::ImageDescriptor::create(request);
    if (!normalized.has_value())
    {
        throw std::runtime_error(std::string(context) + ": '" + path +
                                 "' has no description as fractions of full scale");
    }
    return *normalized;
}

/// A stack's frames, each decoded whole when the stack reads it and packed planar into the stack's read buffer.
class FileStackSource final : public an::StackFrameSource
{
public:
    FileStackSource(const an::EngineContext& engine, const std::vector<std::string>& paths,
                    const an::ImageDescriptor& descriptor)
        : m_engine(engine), m_paths(paths), m_descriptor(descriptor), m_layouts(paths.size())
    {
    }

    [[nodiscard]] an::StackSourceAccess access() const noexcept override
    {
        return an::StackSourceAccess::WholeFrame;
    }

    [[nodiscard]] an::Result<an::StackSourceBand> read(const an::ExecutionContext& execution,
                                                       const std::uint32_t frameIndex, const std::uint32_t firstRow,
                                                       const std::uint32_t rows,
                                                       const std::span<std::byte> buffer) override
    {
        if (frameIndex >= m_paths.size() || firstRow != 0u || rows != m_descriptor.extent().height())
        {
            return refusal(an::ErrorCode::OutOfRange, "the stack asked for a frame the sequence does not hold");
        }
        const std::string& path = m_paths[frameIndex];
        an::Result<an::CodecDecodeResult> decoded =
            an::decodeImage(m_engine, execution, an::CodecReadRequest{.path = path, .readMetadata = false});
        if (!decoded.hasValue())
        {
            return decoded.status();
        }
        const std::optional<an::RawFrameDescriptor> frame = decoded->frame.descriptor();
        const std::optional<an::ImageLayout> layout = decoded->frame.layout();
        const std::optional<an::SampleType> stored = frame.has_value() ? frame->residentSampleType() : std::nullopt;
        const std::optional<an::ImageDescriptor> described =
            frame.has_value() ? frame->toImageDescriptor() : std::nullopt;
        if (!layout.has_value() || !stored.has_value() || !described.has_value() ||
            *stored != m_descriptor.sampleType() || described->extent() != m_descriptor.extent() ||
            described->cfa() != m_descriptor.cfa())
        {
            return refusal(an::ErrorCode::IncompatibleArguments,
                           "'" + path +
                               "' does not have the size, channels, depth and filter array of the first frame");
        }
        const an::Result<an::ImageLayout> packed = compactLayoutOf(m_descriptor, m_engine.limits());
        if (!packed.hasValue() || packed->totalBytes() > buffer.size())
        {
            return refusal(an::ErrorCode::IncompatibleArguments, "a decoded frame does not fit the stack's read slot");
        }
        if (an::Status copied = an::repackImageSamples(execution, *stored, decoded->frame.bytes().data(), *layout,
                                                       buffer.data(), *packed);
            copied.failed())
        {
            return copied;
        }
        const bool foreignOrder = frame->byteOrder() != an::ByteOrder::Native &&
                                  frame->byteOrder() != an::nativeByteOrder() && an::isByteOrderMeaningful(*stored);
        if (foreignOrder)
        {
            if (an::Status swapped = an::swapImageByteOrder(execution, *stored, buffer.data(), *packed);
                swapped.failed())
            {
                return swapped;
            }
        }
        m_layouts[frameIndex] = *packed;
        return an::StackSourceBand{.sampleType = *stored,
                                   .samples = buffer.data(),
                                   .layout = &*m_layouts[frameIndex],
                                   .frameIndex = frameIndex,
                                   .firstRow = 0u,
                                   .validity = an::StackBandValidity{}};
    }

private:
    const an::EngineContext& m_engine;
    const std::vector<std::string>& m_paths;
    an::ImageDescriptor m_descriptor;
    std::vector<std::optional<an::ImageLayout>> m_layouts;
};

/// A drizzle's frames, each read as Float32 fractions of full scale when the drizzle asks for it.
class FileDrizzleSource final : public an::DrizzleFrameSource
{
public:
    FileDrizzleSource(const an::EngineContext& engine, const std::vector<std::string>& paths,
                      const an::ImageDescriptor& descriptor)
        : m_engine(engine), m_paths(paths), m_descriptor(descriptor)
    {
    }

    [[nodiscard]] an::DrizzleSourceAccess access() const noexcept override
    {
        return an::DrizzleSourceAccess::WholeFrame;
    }

    [[nodiscard]] an::Result<an::DrizzleFrameData> read(const an::ExecutionContext& execution,
                                                        const std::uint32_t frameIndex, const std::uint32_t firstRow,
                                                        const std::uint32_t rows,
                                                        const an::DrizzleReadBuffers& buffers) override
    {
        (void)buffers;
        if (an::Status stopped = execution.checkCancellation(); stopped.failed())
        {
            return stopped;
        }
        if (frameIndex >= m_paths.size() || firstRow != 0u || rows != m_descriptor.extent().height())
        {
            return refusal(an::ErrorCode::OutOfRange, "the drizzle asked for a frame the sequence does not hold");
        }
        // The frame is decoded and brought into Float32 fractions in the drizzle's own execution: the drizzle reads
        // its frames inside the stack's operation, which a second admission would wait for.
        an::Result<an::CodecDecodeResult> decoded = an::decodeImage(
            m_engine, execution, an::CodecReadRequest{.path = m_paths[frameIndex], .readMetadata = false});
        if (!decoded.hasValue())
        {
            return decoded.status();
        }
        const std::optional<an::RawFrameDescriptor> frame = decoded->frame.descriptor();
        const std::optional<an::ImageLayout> layout = decoded->frame.layout();
        const std::optional<an::SampleType> stored = frame.has_value() ? frame->residentSampleType() : std::nullopt;
        const std::optional<an::ImageDescriptor> described =
            frame.has_value() ? frame->toImageDescriptor() : std::nullopt;
        if (!layout.has_value() || !stored.has_value() || !described.has_value() ||
            described->extent() != m_descriptor.extent())
        {
            return refusal(an::ErrorCode::IncompatibleArguments,
                           "'" + m_paths[frameIndex] + "' does not have the size and channels of the first frame");
        }
        const an::Result<an::ImageLayout> nativeLayout = compactLayoutOf(*described, m_engine.limits());
        const an::Result<an::ImageLayout> packed = compactLayoutOf(m_descriptor, m_engine.limits());
        if (!nativeLayout.hasValue() || !packed.hasValue())
        {
            return refusal(an::ErrorCode::LimitExceeded, "a drizzled frame has no layout within the limits");
        }
        std::vector<std::byte> native(static_cast<std::size_t>(nativeLayout->totalBytes()));
        if (an::Status copied = an::repackImageSamples(execution, *stored, decoded->frame.bytes().data(), *layout,
                                                       native.data(), *nativeLayout);
            copied.failed())
        {
            return copied;
        }
        if (frame->byteOrder() != an::ByteOrder::Native && frame->byteOrder() != an::nativeByteOrder() &&
            an::isByteOrderMeaningful(*stored))
        {
            if (an::Status swapped = an::swapImageByteOrder(execution, *stored, native.data(), *nativeLayout);
                swapped.failed())
            {
                return swapped;
            }
        }
        m_current.assign(static_cast<std::size_t>(packed->totalBytes()), std::byte{0});
        an::ConversionCounters counters;
        if (an::Status converted =
                an::convertImageSamples(execution, *stored, native.data(), *nativeLayout, an::SampleType::Float32,
                                        m_current.data(), *packed, an::ConversionPolicy{}, counters);
            converted.failed())
        {
            return converted;
        }
        m_currentLayout = *packed;
        return an::DrizzleFrameData{
            .pixels = an::DrizzleSampleBand{.layout = *m_currentLayout,
                                            .samples = std::span<const std::byte>(m_current.data(), m_current.size()),
                                            .sampleType = an::SampleType::Float32}};
    }

private:
    const an::EngineContext& m_engine;
    const std::vector<std::string>& m_paths;
    an::ImageDescriptor m_descriptor;
    std::vector<std::byte> m_current;
    std::optional<an::ImageLayout> m_currentLayout;
};

/// A drizzled stack's frames restated at their lights' level. The drizzle keeps the flux each light's pixel carried,
/// so a frame drizzled onto a canvas `scale` times finer holds every sample divided by the square of the scale, and a
/// stack of such frames - the master the call writes - would lie that far below the lights it was made from. Each
/// finite sample of the band the drizzled source copies into the slot the stack lends is multiplied by the square of
/// the scale; a sample no drop reached, which the drizzle leaves not a number, stays as it is. The drizzled source is
/// borrowed where its `create` left it.
class LightsLevelDrizzleSource final : public an::StackFrameSource
{
public:
    LightsLevelDrizzleSource(an::DrizzledStackFrameSource& drizzled, const double scale) noexcept
        : m_drizzled(drizzled), m_factor(scale * scale)
    {
    }

    [[nodiscard]] an::StackSourceAccess access() const noexcept override
    {
        return m_drizzled.access();
    }

    [[nodiscard]] an::Result<an::StackSourceBand> read(const an::ExecutionContext& execution,
                                                       const std::uint32_t frameIndex, const std::uint32_t firstRow,
                                                       const std::uint32_t rows,
                                                       const std::span<std::byte> buffer) override
    {
        an::Result<an::StackSourceBand> band = m_drizzled.read(execution, frameIndex, firstRow, rows, buffer);
        if (!band.hasValue())
        {
            return band;
        }
        if (an::Status restated = restate(*band, buffer); restated.failed())
        {
            return restated;
        }
        return band;
    }

private:
    /// Multiplies every finite sample of a band in the lent slot by the square of the scale.
    [[nodiscard]] an::Status restate(const an::StackSourceBand& band, const std::span<std::byte> buffer) const
    {
        if (band.layout == nullptr || band.samples == nullptr || buffer.data() == nullptr)
        {
            return refusal(an::ErrorCode::Internal, "a drizzled band holds nothing");
        }
        const an::ImageLayout& layout = *band.layout;
        // The drizzled source copies each frame into the slot it is lent; a band anywhere else is not the stack's to
        // write.
        const std::uintptr_t first = reinterpret_cast<std::uintptr_t>(band.samples);
        const std::uintptr_t slot = reinterpret_cast<std::uintptr_t>(buffer.data());
        if (first < slot || first - slot > buffer.size() || layout.totalBytes() > buffer.size() - (first - slot))
        {
            return refusal(an::ErrorCode::Internal, "a drizzled band lies outside the slot the stack lent it");
        }
        std::byte* const samples = buffer.data() + (first - slot);
        const std::size_t perRow = static_cast<std::size_t>(layout.extent().width()) * layout.samplesPerPixel();
        const auto scaled = [&]<typename SampleT>() {
            for (std::uint32_t plane = 0u; plane < layout.planeCount(); ++plane)
            {
                for (std::uint32_t y = 0u; y < layout.extent().height(); ++y)
                {
                    auto* row = reinterpret_cast<SampleT*>(samples + plane * layout.planeStrideBytes() +
                                                           y * layout.rowStrideBytes());
                    for (std::size_t x = 0u; x < perRow; ++x)
                    {
                        if (std::isfinite(row[x]))
                        {
                            row[x] = static_cast<SampleT>(static_cast<double>(row[x]) * m_factor);
                        }
                    }
                }
            }
        };
        switch (band.sampleType)
        {
        case an::SampleType::Float32:
            scaled.template operator()<float>();
            return an::Status::success();
        case an::SampleType::Float64:
            scaled.template operator()<double>();
            return an::Status::success();
        default:
            return refusal(an::ErrorCode::Internal, "a drizzled band is not floating point");
        }
    }

    an::DrizzledStackFrameSource& m_drizzled;
    double m_factor;
};

/// What the measurement of a sequence's frames gives each weighting that needs one.
[[nodiscard]] bool weightingNeedsMeasurements(const an::StackWeighting weighting) noexcept
{
    return weighting != an::StackWeighting::Uniform && weighting != an::StackWeighting::Explicit;
}

[[nodiscard]] std::optional<double> numericCard(const SdkMetadata& metadata, const std::string_view keyword)
{
    for (const an::CodecCard& card : metadata.cards)
    {
        if (card.keyword() == keyword)
        {
            try
            {
                std::size_t used = 0u;
                const double value = std::stod(cardText(card), &used);
                if (used > 0u && std::isfinite(value))
                {
                    return value;
                }
            }
            catch (const std::exception&)
            {
                return std::nullopt;
            }
        }
    }
    return std::nullopt;
}

/// A measured figure a weighting can rank frames by: present only when it is finite and positive.
[[nodiscard]] std::optional<double> positiveFigure(const double value) noexcept
{
    return std::isfinite(value) && value > 0.0 ? std::optional<double>(value) : std::nullopt;
}

/// Each frame's figures a weighting reads: its stars, their width and roundness, its noise and signal, from the
/// engine's detector; how many frames it integrates and how long it was exposed, from its cards.
[[nodiscard]] std::vector<an::StackFrameMeasurement> measureFrames(const an::consumer::EngineSession& session,
                                                                   const std::vector<std::string>& paths,
                                                                   const std::string_view context)
{
    std::vector<an::StackFrameMeasurement> measurements;
    measurements.reserve(paths.size());
    for (const std::string& path : paths)
    {
        ReadImage read = readFrame(session, path, context);
        an::consumer::OperationScope scope = admitOperation(session, an::consumer::OperationPurpose::Batch, context);
        an::StackFrameMeasurement measurement;
        an::Result<an::StarDetectionResult> found =
            detectImageStars(scope, read.image, an::StarDetectionRequest{}, std::nullopt);
        if (found.hasValue())
        {
            measurement.starCount = found->summary.starCount;
            measurement.medianRoundness = found->summary.medianRoundness;
            measurement.fullWidth = found->summary.medianFullWidth;
            // A sky with no spread - a flat or a synthetic frame - measures a noise of zero, which no weighting can
            // rank by and the stack refuses as a figure whatever the weighting reads, so the figure is left out.
            measurement.backgroundNoise = positiveFigure(found->summary.channel.backgroundNoise);
            if (found->summary.medianSignalToNoise.has_value())
            {
                measurement.signalToNoise = positiveFigure(*found->summary.medianSignalToNoise);
            }
        }
        for (const char* keyword : {"STACKCNT", "NCOMBINE"})
        {
            if (const std::optional<double> count = numericCard(read.metadata, keyword);
                count.has_value() && *count >= 1.0)
            {
                measurement.integratedFrameCount = static_cast<std::uint32_t>(*count);
                break;
            }
        }
        if (!measurement.integratedFrameCount.has_value())
        {
            measurement.integratedFrameCount = 1u;
        }
        for (const char* keyword : {"EXPTIME", "EXPOSURE"})
        {
            if (const std::optional<double> seconds = numericCard(read.metadata, keyword); seconds.has_value())
            {
                measurement.exposureSeconds = *seconds;
                break;
            }
        }
        measurements.push_back(measurement);
    }
    return measurements;
}

/// The engine's stack request for a sequence of frames of one description, every setting it cannot honour left out
/// with a warning.
[[nodiscard]] an::StackModel stackModelFor(StackSettings& settings, const an::ImageDescriptor& input,
                                           const std::uint32_t frameCount, const QualityOutputs& quality,
                                           const std::string_view function)
{
    const an::StackCombinationCapabilities capabilities = an::stackCombinationCapabilities(settings.combination);
    if (settings.rejection != an::StackRejection::None && frameCount < an::kMinimumRejectionFrames)
    {
        warn(std::string(function) + ": " + settings.methodName + " rejects samples among at least three frames; " +
             std::to_string(frameCount) + " are averaged with no rejection");
        settings.rejection = an::StackRejection::None;
    }
    const bool explicitWeights = !settings.imageWeights.empty() || !settings.channelWeights.empty();
    if (explicitWeights && settings.weighting != an::StackWeighting::Uniform)
    {
        throw std::runtime_error(std::string(function) +
                                 " takes image_weights or channel_weights, or a weighting method, not both");
    }
    an::StackWeighting weighting = explicitWeights ? an::StackWeighting::Explicit : settings.weighting;
    if (weighting != an::StackWeighting::Uniform && !capabilities.admitsWeighting)
    {
        warn(std::string(function) + ": the " + settings.methodName + " combination takes no frame weights; " +
             "the weighting is left out");
        weighting = an::StackWeighting::Uniform;
        settings.weighting = an::StackWeighting::Uniform;
        settings.imageWeights.clear();
        settings.channelWeights.clear();
    }
    an::LocalWeightingRequest local = settings.localWeighting;
    if (local.enabled && !capabilities.admitsWeighting)
    {
        warn(std::string(function) + ": the " + settings.methodName + " combination takes no local weights; " +
             "local weighting is left out");
        local.enabled = false;
    }
    if (settings.perChannelFwhmAsked)
    {
        // Each frame's stars are measured in one channel, and its one width weighs every channel under any weighting.
        warn(std::string(function) + ": enable_per_channel_fwhm_weighting is not offered: every channel is weighed " +
             "by its frame's star width; it is left out");
    }

    an::StackRejectionRequest rejection{.method = settings.rejection};
    if (an::stackRejectionUsesSigmaThresholds(settings.rejection))
    {
        rejection.sigma.low = settings.low.value_or(rejection.sigma.low);
        rejection.sigma.high = settings.high.value_or(rejection.sigma.high);
    }
    else if (an::stackRejectionUsesMedianFractions(settings.rejection))
    {
        rejection.medianFractions.low = settings.low.value_or(rejection.medianFractions.low);
        rejection.medianFractions.high = settings.high.value_or(rejection.medianFractions.high);
    }
    else if (an::stackRejectionUsesGesdtParameters(settings.rejection))
    {
        rejection.gesdt.maximumOutlierFraction = settings.low.value_or(rejection.gesdt.maximumOutlierFraction);
        rejection.gesdt.significance = settings.high.value_or(rejection.gesdt.significance);
    }

    an::StackWeightingRequest weightingRequest{.method = weighting, .starCount = settings.starCount};
    weightingRequest.minimumWeight = settings.minimumWeight.value_or(weightingRequest.minimumWeight);

    const bool usesRejection = settings.rejection != an::StackRejection::None;
    an::StackOutputRequest output{.sampleType = settings.output64 ? an::SampleType::Float64 : an::SampleType::Float32};
    if (settings.rejectionMaps && usesRejection)
    {
        output.rejectionMaps =
            settings.mergeRejectionMaps ? an::RejectionMapForm::Merged : an::RejectionMapForm::LowAndHigh;
    }
    output.coverageMap =
        quality.maps[static_cast<std::size_t>(an::StackOutputMap::Coverage)].has_value() || quality.report;
    output.varianceMap = quality.maps[static_cast<std::size_t>(an::StackOutputMap::Variance)].has_value();
    output.signalToNoiseGainMap =
        quality.maps[static_cast<std::size_t>(an::StackOutputMap::SignalToNoiseGain)].has_value();
    output.weightSumMap = quality.maps[static_cast<std::size_t>(an::StackOutputMap::WeightSum)].has_value();

    an::StackNormalizationRequest normalization{.scope = settings.normalizationScope,
                                                .globalMethod = settings.normalizationMethod,
                                                .scaleEstimator = settings.scaleEstimator,
                                                .referenceFrame = settings.referenceIndex};
    if (normalization.scope == an::StackNormalizationScope::None)
    {
        normalization.globalMethod.reset();
    }

    an::StackRequest request{.input = an::StackInputDescription{.frameCount = frameCount, .descriptor = input},
                             .combination = settings.combination,
                             .rejection = rejection,
                             .weighting = weightingRequest,
                             .localWeighting = local,
                             .normalization = normalization,
                             .output = output};
    // A map a combination cannot produce is reported missing rather than refused.
    if (!an::stackCombinationCapabilities(settings.combination).admitsWeighting)
    {
        request.output.weightSumMap = false;
    }
    if (const an::StackModelError error = an::StackModel::check(request); error != an::StackModelError::None)
    {
        throw std::runtime_error(std::string(function) +
                                 " cannot stack these frames: " + an::stackModelErrorName(error));
    }
    return *an::StackModel::create(request);
}

[[nodiscard]] py::dict qualityReportToPythonDict(const an::StackExecutionReport& report, const std::uint32_t frameCount,
                                                 const std::span<const an::FrameRejectionRate> rates)
{
    py::dict result;
    result[py::str("valid")] = true;
    result[py::str("input_frame_count")] = static_cast<int>(frameCount);
    result[py::str("effective_frame_count")] = report.effectiveFrameCount;
    result[py::str("theoretical_snr_gain")] = report.theoreticalSnrGain;
    result[py::str("actual_snr_gain")] = report.actualSnrGain;
    result[py::str("integration_efficiency")] = report.integrationEfficiency;
    result[py::str("min_coverage")] = static_cast<double>(report.coverage.minimum);
    result[py::str("median_coverage")] = report.coverage.median;
    result[py::str("max_coverage")] = static_cast<double>(report.coverage.maximum);
    result[py::str("coverage_stddev")] = report.coverage.standardDeviation;
    result[py::str("has_rejection_stats")] = report.rejectionSummarized;
    result[py::str("global_rejection_rate")] = report.rejection.globalRate;
    result[py::str("min_frame_rejection_rate")] = report.rejection.minimumRate;
    result[py::str("mean_frame_rejection_rate")] = report.rejection.meanRate;
    result[py::str("median_frame_rejection_rate")] = report.rejection.medianRate;
    result[py::str("max_frame_rejection_rate")] = report.rejection.maximumRate;
    const bool weighted = report.weighting.disposition == an::StackFrameWeightingDisposition::Computed ||
                          report.weighting.disposition == an::StackFrameWeightingDisposition::Supplied;
    result[py::str("has_weighting_stats")] = weighted;
    result[py::str("weight_min")] = report.weighting.summary.minimum;
    result[py::str("weight_max")] = report.weighting.summary.maximum;
    result[py::str("weight_stddev")] = report.weighting.summary.standardDeviation;
    result[py::str("weight_efficiency")] = report.weighting.summary.efficiency;
    result[py::str("has_local_weighting_stats")] = report.localWeightsMeasured;
    result[py::str("local_weight_window_size")] = static_cast<int>(report.localWeighting.windowSize);
    result[py::str("local_weight_grid_spacing")] = static_cast<int>(report.localWeighting.gridSpacing);
    result[py::str("local_weight_snr_exponent")] = report.localWeighting.snrExponent;
    result[py::str("local_weight_grid_count")] = static_cast<int>(report.localWeighting.nodeCount);
    result[py::str("local_weight_mean")] = report.localWeighting.meanWeight;
    result[py::str("local_weight_min")] = report.localWeighting.minimumWeight;
    result[py::str("local_weight_max")] = report.localWeighting.maximumWeight;
    result[py::str("local_weight_stddev")] = report.localWeighting.weightStandardDeviation;
    result[py::str("has_spatial_rejection_clusters")] = report.clustersAnalyzed;
    result[py::str("spatial_cluster_count")] = static_cast<int>(report.clusters.clustersFound);
    result[py::str("spatial_cluster_pixels")] = static_cast<int>(report.clusters.reportedClusterPixels);
    result[py::str("spatial_largest_cluster_pixels")] = static_cast<int>(report.clusters.largestClusterPixels);
    result[py::str("spatial_cluster_fraction")] = report.clusters.clusteredPixelFraction;
    result[py::str("spatial_largest_cluster_fraction")] = report.clusters.largestClusterFraction;
    // The stack is not repeated without the frames its rejection flagged: no re-stack is asked for or made.
    result[py::str("spatial_restack_requested")] = false;
    result[py::str("spatial_restack_applied")] = false;
    result[py::str("spatial_restack_initial_cluster_count")] = 0;
    result[py::str("spatial_restack_flagged_frame_threshold")] = 0.0;
    py::list frameRates;
    for (const an::FrameRejectionRate& rate : rates)
    {
        frameRates.append(rate.rated ? rate.rate : 0.0);
    }
    result[py::str("frame_rejection_rates")] = frameRates;
    result[py::str("spatial_restack_excluded_frame_indices")] = py::list();
    result[py::str("spatial_restack_excluded_frame_rates")] = py::list();
    return result;
}

/// A stack's result image, or one of its maps, as an engine image of its storage.
template <an::ResidentSample SampleT>
[[nodiscard]] an::consumer::AnyImage
imageOfStorage(an::ImageStorageBytes&& bytes, const an::ImageDescriptor& descriptor, const std::string_view context)
{
    an::ImageStorage<SampleT> storage = require(an::ImageStorage<SampleT>::fromBytes(std::move(bytes)), context);
    return an::consumer::AnyImage(
        require(an::Image<SampleT>::fromStorage(std::move(storage), descriptor, an::ImageMetadata{}), context));
}

/// A rejection map's counts as Float32 fractions of the frames, held at one, as the SDK writes them.
[[nodiscard]] an::consumer::AnyImage rejectionFractions(const an::consumer::OperationScope& scope,
                                                        const an::consumer::AnyImage& counts,
                                                        const std::uint32_t frames, const std::string_view context)
{
    const auto* typed = std::get_if<an::Image<std::uint32_t>>(&counts);
    const auto view = typed != nullptr ? typed->view() : std::nullopt;
    if (!view.has_value())
    {
        throw std::runtime_error(std::string(context) + ": a rejection map holds nothing");
    }
    const an::ImageDescriptor descriptor =
        require(describeSdkImage(scope.engine().limits(), view->width(), view->height(), view->channelCount(),
                                 an::SampleType::Float32),
                context);
    an::Image<float> map = require(
        createCompactImage<float>(scope.engine(), descriptor, an::StorageInitialization::Uninitialized), context);
    std::optional<an::ImageWriteAccess<float>> access = map.write();
    const double count = static_cast<double>((std::max)(frames, 1u));
    for (std::uint32_t plane = 0u; plane < view->planeCount(); ++plane)
    {
        for (std::uint32_t y = 0u; y < view->height(); ++y)
        {
            const auto in = view->row(plane, y);
            const auto out = access->view().row(plane, y);
            for (std::size_t x = 0u; x < out->size(); ++x)
            {
                (*out)[x] = static_cast<float>((std::min)(static_cast<double>((*in)[x]) / count, 1.0));
            }
        }
    }
    return an::consumer::AnyImage(std::move(map));
}

/// A map of counts as Float32 counts: a count is a count, not a fraction of a 32-bit code's full scale.
[[nodiscard]] an::consumer::AnyImage countsAsFloat(const an::consumer::OperationScope& scope,
                                                   const an::consumer::AnyImage& counts, const std::string_view context)
{
    const auto* typed = std::get_if<an::Image<std::uint32_t>>(&counts);
    const auto view = typed != nullptr ? typed->view() : std::nullopt;
    if (!view.has_value())
    {
        throw std::runtime_error(std::string(context) + ": a map of counts holds nothing");
    }
    const an::ImageDescriptor descriptor =
        require(describeSdkImage(scope.engine().limits(), view->width(), view->height(), view->channelCount(),
                                 an::SampleType::Float32),
                context);
    an::Image<float> map = require(
        createCompactImage<float>(scope.engine(), descriptor, an::StorageInitialization::Uninitialized), context);
    {
        std::optional<an::ImageWriteAccess<float>> access = map.write();
        an::ConversionCounters counters;
        require(an::convertImageSamples(scope.execution(), an::SampleType::UInt32, view->data(), view->layout(),
                                        an::SampleType::Float32, access->view().data(), access->view().layout(),
                                        an::ConversionPolicy{.domain = an::DomainMapping::CodeValue}, counters),
                context);
    }
    return an::consumer::AnyImage(std::move(map));
}

/// A crop of an image in compact planar storage.
[[nodiscard]] an::consumer::AnyImage croppedImage(const an::consumer::OperationScope& scope,
                                                  const an::consumer::AnyImage& image, const std::uint32_t originX,
                                                  const std::uint32_t originY, const std::uint32_t width,
                                                  const std::uint32_t height, const std::string_view context)
{
    return std::visit(
        [&](const auto& typed) -> an::consumer::AnyImage {
            using SampleT = typename std::remove_cvref_t<decltype(typed)>::Sample;
            const auto view = typed.view();
            const auto region = view.has_value() ? view->subview(originX, originY, width, height) : std::nullopt;
            if (!region.has_value())
            {
                throw std::runtime_error(std::string(context) + ": the drizzle's output is outside its canvas");
            }
            const an::ImageDescriptor descriptor =
                require(describeSdkImage(scope.engine().limits(), width, height, view->channelCount(),
                                         an::Image<SampleT>::kSampleType),
                        context);
            an::Image<SampleT> cropped = require(
                createCompactImage<SampleT>(scope.engine(), descriptor, an::StorageInitialization::Uninitialized),
                context);
            {
                std::optional<an::ImageWriteAccess<SampleT>> access = cropped.write();
                require(an::repackImageSamples(scope.execution(), an::Image<SampleT>::kSampleType, region->data(),
                                               region->layout(), access->view().data(), access->view().layout()),
                        context);
            }
            return an::consumer::AnyImage(std::move(cropped));
        },
        image);
}

/// The drizzle a stack call asks for.
struct DrizzleSettings
{
    an::DrizzleKernel kernel = an::DrizzleKernel::Square;
    double scale = 2.0;
    double pixelFraction = 0.9;
    double weightScale = 1.0;
    bool bayer = false;
    std::optional<an::CfaPattern> pattern{};
    std::uint32_t outputWidth = 0u;
    std::uint32_t outputHeight = 0u;
    std::optional<an::RegistrationBounds> canvasBounds{};
};

[[nodiscard]] const char* drizzleKernelSdkName(const an::DrizzleKernel kernel) noexcept
{
    switch (kernel)
    {
    case an::DrizzleKernel::Square:
        return "square";
    case an::DrizzleKernel::Point:
        return "point";
    case an::DrizzleKernel::Turbo:
        return "turbo";
    case an::DrizzleKernel::Gaussian:
        return "gaussian";
    case an::DrizzleKernel::Lanczos2:
        return "lanczos2";
    case an::DrizzleKernel::Lanczos3:
        return "lanczos3";
    }
    return "unknown";
}

[[nodiscard]] DrizzleSettings parseDrizzleSettings(const py::object& params)
{
    DrizzleSettings settings;
    if (params.is_none())
    {
        return settings;
    }
    const py::object value = dictValueOrNone(params.cast<py::dict>(), "drizzle");
    if (value.is_none() || py::isinstance<py::bool_>(value))
    {
        return settings;
    }
    const py::dict values = value.cast<py::dict>();
    const py::object kernel = dictValueOrNone(values, "kernel");
    if (!kernel.is_none())
    {
        const std::string name = lowerName(kernel.cast<std::string>());
        if (name == "square")
        {
            settings.kernel = an::DrizzleKernel::Square;
        }
        else if (name == "point")
        {
            settings.kernel = an::DrizzleKernel::Point;
        }
        else if (name == "turbo")
        {
            settings.kernel = an::DrizzleKernel::Turbo;
        }
        else if (name == "gaussian")
        {
            settings.kernel = an::DrizzleKernel::Gaussian;
        }
        else if (name == "lanczos2" || name == "lanczos_2")
        {
            settings.kernel = an::DrizzleKernel::Lanczos2;
        }
        else if (name == "lanczos3" || name == "lanczos_3" || name == "lanczos")
        {
            settings.kernel = an::DrizzleKernel::Lanczos3;
        }
        else
        {
            throw std::runtime_error("Unsupported drizzle kernel: " + kernel.cast<std::string>());
        }
    }
    const py::object pattern = dictValueOrNone(values, "cfa_pattern");
    if (!pattern.is_none())
    {
        std::string name = pattern.cast<std::string>();
        std::transform(name.begin(), name.end(), name.begin(),
                       [](const unsigned char character) { return static_cast<char>(std::toupper(character)); });
        if (name == "RGGB")
        {
            settings.pattern = an::CfaPattern::Rggb;
        }
        else if (name == "BGGR")
        {
            settings.pattern = an::CfaPattern::Bggr;
        }
        else if (name == "GBRG")
        {
            settings.pattern = an::CfaPattern::Gbrg;
        }
        else if (name == "GRBG")
        {
            settings.pattern = an::CfaPattern::Grbg;
        }
        else if (name != "AUTO")
        {
            throw std::runtime_error("Unsupported Bayer pattern: " + pattern.cast<std::string>());
        }
    }
    assignIfPresent(values, "scale", settings.scale);
    assignIfPresent(values, "pixel_fraction", settings.pixelFraction);
    assignIfPresent(values, "pixfrac", settings.pixelFraction);
    assignIfPresent(values, "weight_scale", settings.weightScale);
    assignIfPresent(values, "is_bayer", settings.bayer);
    assignIfPresent(values, "cfa", settings.bayer);
    int outputWidth = 0;
    int outputHeight = 0;
    assignIfPresent(values, "output_width", outputWidth);
    assignIfPresent(values, "output_height", outputHeight);
    settings.outputWidth = static_cast<std::uint32_t>((std::max)(outputWidth, 0));
    settings.outputHeight = static_cast<std::uint32_t>((std::max)(outputHeight, 0));
    double canvasX = 0.0;
    double canvasY = 0.0;
    double canvasWidth = 0.0;
    double canvasHeight = 0.0;
    assignIfPresent(values, "canvas_x", canvasX);
    assignIfPresent(values, "canvas_y", canvasY);
    assignIfPresent(values, "canvas_width", canvasWidth);
    assignIfPresent(values, "canvas_height", canvasHeight);
    bool useCanvasBounds = canvasWidth > 0.0 && canvasHeight > 0.0;
    assignIfPresent(values, "use_canvas_bounds", useCanvasBounds);
    if (useCanvasBounds)
    {
        settings.canvasBounds = an::RegistrationBounds{.minimumX = canvasX,
                                                       .minimumY = canvasY,
                                                       .maximumX = canvasX + canvasWidth,
                                                       .maximumY = canvasY + canvasHeight};
    }
    return settings;
}

/// Writes a stack's master, its maps and its report, and the SDK's result dictionary of them.
[[nodiscard]] py::dict publishStack(const an::consumer::EngineSession& session, an::StackResult& stacked,
                                    const an::StackModel& model, const StackSettings& settings,
                                    const QualityOutputs& quality, const std::uint32_t frameCount,
                                    std::span<const an::FrameRejectionRate> rates, SdkMetadata metadata,
                                    const std::string& outputPath, const WriteOptions& saveOptions,
                                    const std::optional<std::array<std::uint32_t, 4u>>& crop,
                                    const std::string_view context)
{
    an::consumer::OperationScope scope = admitOperation(session, an::consumer::OperationPurpose::Batch, context);
    const an::ImageDescriptor descriptor = stacked.descriptor;
    an::consumer::AnyImage master = descriptor.sampleType() == an::SampleType::Float64
                                        ? imageOfStorage<double>(std::move(stacked.image), descriptor, context)
                                        : imageOfStorage<float>(std::move(stacked.image), descriptor, context);
    const auto cropped = [&](an::consumer::AnyImage&& image) {
        return crop.has_value() ? croppedImage(scope, image, (*crop)[0], (*crop)[1], (*crop)[2], (*crop)[3], context)
                                : std::move(image);
    };
    master = cropped(std::move(master));
    require(writeImageFile(scope, master, metadata, outputPath, saveOptions),
            std::string(context) + " could not write '" + outputPath + "'");

    py::dict result;
    result[py::str("path")] = py::str(outputPath);
    result[py::str("frame_count")] = static_cast<int>(frameCount);
    result[py::str("method")] = py::str(settings.methodName);
    result[py::str("native")] = true;
    if (quality.report)
    {
        result[py::str("quality_report")] = qualityReportToPythonDict(stacked.report, frameCount, rates);
    }

    py::dict written;
    py::list missing;
    const std::array<std::pair<an::StackOutputMap, const char*>, 6u> named{
        std::pair{an::StackOutputMap::Coverage, "coverage"},
        std::pair{an::StackOutputMap::Variance, "variance"},
        std::pair{an::StackOutputMap::SignalToNoiseGain, "snr_gain"},
        std::pair{an::StackOutputMap::WeightSum, "weight_sum"},
        std::pair{an::StackOutputMap::RejectionLow, "rejection_low"},
        std::pair{an::StackOutputMap::RejectionHigh, "rejection_high"}};
    for (const auto& [which, key] : named)
    {
        const std::optional<std::string>& path = quality.maps[static_cast<std::size_t>(which)];
        if (!path.has_value())
        {
            continue;
        }
        an::ImageStorageBytes& bytes = stacked.maps[static_cast<std::size_t>(which)];
        if (!bytes.valid())
        {
            missing.append(py::str(key));
            continue;
        }
        an::ImageDescriptorRequest request = descriptor.toRequest();
        request.sampleType = an::stackOutputMapSampleType(which);
        const std::optional<an::ImageDescriptor> mapDescriptor = an::ImageDescriptor::create(request);
        if (!mapDescriptor.has_value())
        {
            missing.append(py::str(key));
            continue;
        }
        an::consumer::AnyImage map = request.sampleType == an::SampleType::UInt32
                                         ? imageOfStorage<std::uint32_t>(std::move(bytes), *mapDescriptor, context)
                                         : imageOfStorage<float>(std::move(bytes), *mapDescriptor, context);
        if (which == an::StackOutputMap::RejectionLow || which == an::StackOutputMap::RejectionHigh)
        {
            map = rejectionFractions(scope, map, frameCount, context);
        }
        else if (request.sampleType == an::SampleType::UInt32)
        {
            map = countsAsFloat(scope, map, context);
        }
        map = cropped(std::move(map));
        require(writeImageFile(scope, map, SdkMetadata{.fileName = *path}, *path, saveOptions),
                std::string(context) + " could not write '" + *path + "'");
        written[py::str(key)] = py::str(*path);
    }
    if (py::len(written) > 0)
    {
        result[py::str("quality_maps")] = written;
    }
    if (py::len(missing) > 0)
    {
        result[py::str("missing_quality_maps")] = missing;
    }
    (void)model;
    return result;
}

/// The SDK's cards of a stacked image: the reference frame's, then what the stack states, then the caller's.
[[nodiscard]] SdkMetadata stackedMetadata(const an::consumer::EngineSession& session, const std::string& reference,
                                          const py::dict& stated, const py::object& callerMetadata,
                                          const std::string& outputPath, const std::string_view context)
{
    SdkMetadata metadata = readFrame(session, reference, context).metadata;
    applyMetadataDict(stated, metadata);
    if (!callerMetadata.is_none())
    {
        applyMetadataDict(callerMetadata.cast<py::dict>(), metadata);
    }
    metadata.fileName = outputPath;
    return metadata;
}

/// Runs a stack over a source of frames of one description.
[[nodiscard]] an::StackResult runStack(const an::consumer::EngineSession& session, const an::StackModel& model,
                                       an::StackFrameSource& source, const StackSettings& settings,
                                       std::span<const an::StackFrameMeasurement> measurements,
                                       std::span<an::FrameRejectionRate> rates, const std::string_view context)
{
    std::vector<double> explicitWeights;
    const std::uint32_t channels = model.request().input.descriptor.extent().channelCount();
    an::StackExecutionRequest request;
    request.workingSetLimitBytes = settings.workingSetLimitBytes;
    request.measurements = measurements;
    if (!settings.imageWeights.empty())
    {
        request.explicitWeights.perFrame = settings.imageWeights;
    }
    else if (!settings.channelWeights.empty())
    {
        if (settings.channelWeights.size() != channels)
        {
            throw std::runtime_error(std::string(context) + ": channel_weights must give one list per channel");
        }
        const std::size_t frames = settings.channelWeights.front().size();
        explicitWeights.resize(frames * channels);
        for (std::size_t frame = 0u; frame < frames; ++frame)
        {
            for (std::size_t channel = 0u; channel < channels; ++channel)
            {
                explicitWeights[frame * channels + channel] = settings.channelWeights[channel][frame];
            }
        }
        request.explicitWeights.perFrameChannel = explicitWeights;
    }
    an::StackExecutionOutputs outputs;
    if (model.usesRejection())
    {
        outputs.frameRejectionRates = rates;
    }
    an::consumer::OperationScope scope = admitOperation(session, an::consumer::OperationPurpose::Batch, context);
    return require(an::executeStack(scope.engine(), scope.execution(), model, source,
                                    an::StackSourceContract{.state = an::StackFrameState::Calibrated}, request,
                                    outputs),
                   context);
}

/// Each frame's pixel size, read from its header.
[[nodiscard]] std::vector<std::pair<std::uint32_t, std::uint32_t>>
frameSizes(const an::consumer::EngineSession& session, const std::vector<std::string>& paths,
           const std::string_view context)
{
    std::vector<std::pair<std::uint32_t, std::uint32_t>> sizes;
    for (const std::string& path : paths)
    {
        const an::ImageDescriptor descriptor = stackInputDescriptor(session, path, context);
        sizes.emplace_back(descriptor.extent().width(), descriptor.extent().height());
    }
    return sizes;
}

} // namespace

// ---- Calibration masters.

py::dict createCalibrationMastersPathsCall(const py::dict& rolePaths, const std::string& outputDir,
                                           const std::string& filterName, const std::string& method,
                                           const py::object& params, const py::object& saveOptions)
{
    constexpr std::string_view kContext = "create_calibration_masters_paths()";
    if (outputDir.empty())
    {
        throw std::runtime_error("create_calibration_masters_paths() output_dir must not be empty");
    }
    std::error_code ignored;
    std::filesystem::create_directories(outputDir, ignored);
    const WriteOptions parsedSave = parseSaveOptions(saveOptions);
    const std::shared_ptr<an::consumer::EngineSession> session = requireSession();

    py::dict result;
    for (const std::string role : {"bias", "dark", "flat"})
    {
        const py::str roleKey(role);
        if (!rolePaths.contains(roleKey))
        {
            continue;
        }
        const std::vector<std::string> paths =
            pathsFromPythonSequence(rolePaths[roleKey].cast<py::sequence>(), kContext);
        if (paths.empty())
        {
            continue;
        }
        StackSettings settings = parseStackSettings(method, params, paths.size());
        QualityOutputs quality;
        const an::ImageDescriptor input = stackInputDescriptor(*session, paths.front(), kContext);
        const an::StackModel model =
            stackModelFor(settings, input, static_cast<std::uint32_t>(paths.size()), quality, kContext);
        std::vector<an::StackFrameMeasurement> measurements;
        if (weightingNeedsMeasurements(model.request().weighting.method))
        {
            measurements = measureFrames(*session, paths, kContext);
        }
        const an::consumer::SessionHold hold = require(session->hold(), kContext);
        FileStackSource source(hold.engine(), paths, input);
        std::vector<an::FrameRejectionRate> rates(paths.size());
        an::StackResult stacked = runStack(*session, model, source, settings, measurements, rates, kContext);

        std::string upperRole = role;
        std::transform(upperRole.begin(), upperRole.end(), upperRole.begin(),
                       [](const unsigned char character) { return static_cast<char>(std::toupper(character)); });
        py::dict masterMetadata;
        masterMetadata[py::str("IMAGETYP")] = py::str("MASTER_" + upperRole);
        masterMetadata[py::str("FILTER")] = py::str(filterName);
        masterMetadata[py::str("FRAMECNT")] = static_cast<int>(paths.size());
        masterMetadata[py::str("STACKMTH")] = py::str(settings.methodName);
        const std::string outputPath =
            (std::filesystem::path(outputDir) / ("master_" + role + "_" + filterName + ".xisf")).string();
        SdkMetadata metadata =
            stackedMetadata(*session, paths[settings.referenceIndex], masterMetadata, py::none(), outputPath, kContext);
        py::dict published =
            publishStack(*session, stacked, model, settings, quality, static_cast<std::uint32_t>(paths.size()), rates,
                         std::move(metadata), outputPath, parsedSave, std::nullopt, kContext);
        (void)published;

        py::dict record;
        record[py::str("path")] = py::str(outputPath);
        record[py::str("frame_count")] = static_cast<int>(paths.size());
        record[py::str("method")] = py::str(settings.methodName);
        record[py::str("metadata")] = masterMetadata;
        record[py::str("native")] = true;
        result[roleKey] = record;
    }
    return result;
}

// ---- Calibration of lights.

py::dict calibrateSequencePathsCall(const py::sequence& paths, const py::dict& masters, const std::string& outputDir,
                                    const py::object& params, const py::object& outputOptions)
{
    constexpr std::string_view kContext = "calibrate_sequence_paths()";
    const std::vector<std::string> sourcePaths = pathsFromPythonSequence(paths, kContext);
    if (sourcePaths.empty())
    {
        throw std::runtime_error("calibrate_sequence_paths() requires at least one source path");
    }
    if (outputDir.empty())
    {
        throw std::runtime_error("calibrate_sequence_paths() output_dir must not be empty");
    }
    bool flatAutolevel = true;
    bool darkOptimization = false;
    double darkFactor = 1.0;
    double pedestal = 0.0;
    if (!params.is_none())
    {
        const py::dict values = params.cast<py::dict>();
        assignIfPresent(values, "flat_autolevel", flatAutolevel);
        assignIfPresent(values, "dark_optimization", darkOptimization);
        assignIfPresent(values, "dark_multiplication_factor", darkFactor);
        assignIfPresent(values, "pedestal", pedestal);
    }
    const OutputNaming naming = parseOutputNaming(outputOptions);
    const std::shared_ptr<an::consumer::EngineSession> session = requireSession();

    // The masters, each read once.
    std::array<std::optional<an::consumer::AnyImage>, 3u> masterImages;
    const std::array<const char*, 3u> roles{"bias", "dark", "flat"};
    for (std::size_t index = 0u; index < roles.size(); ++index)
    {
        const py::str key(roles[index]);
        if (!masters.contains(key))
        {
            continue;
        }
        const py::handle value = masters[key];
        std::string path;
        if (py::isinstance<py::dict>(value))
        {
            const py::object recorded = dictValueOrNone(py::reinterpret_borrow<py::dict>(value), "path");
            path = recorded.is_none() ? std::string{} : py::str(recorded).cast<std::string>();
        }
        else if (!value.is_none())
        {
            path = py::str(value).cast<std::string>();
        }
        if (path.empty())
        {
            continue;
        }
        an::consumer::OperationScope scope = admitOperation(*session, an::consumer::OperationPurpose::Batch, kContext);
        an::Result<ReadImage> read = readImageFile(scope, path, false);
        if (!read.hasValue())
        {
            throw std::runtime_error("calibrate_sequence_paths() failed to load master " + std::string(roles[index]) +
                                     ": " + path);
        }
        masterImages[index] = std::move(read->image);
    }

    std::error_code ignored;
    std::filesystem::create_directories(outputDir, ignored);
    py::list frames;
    py::list outputPaths;
    int succeeded = 0;
    int failed = 0;
    for (std::size_t index = 0u; index < sourcePaths.size(); ++index)
    {
        const std::string& path = sourcePaths[index];
        std::string outputPath = outputPathFor(path, outputDir, naming, index, true);
        std::string error;
        try
        {
            ReadImage read = readFrame(*session, path, kContext);
            an::consumer::OperationScope scope =
                admitOperation(*session, an::consumer::OperationPurpose::Batch, kContext);
            const an::SampleType lightType = an::consumer::sampleTypeOf(read.image);
            const an::SampleType workType = lightType == an::SampleType::Float64 ||
                                                    lightType == an::SampleType::UInt32 ||
                                                    lightType == an::SampleType::Int32
                                                ? an::SampleType::Float64
                                                : an::SampleType::Float32;
            an::consumer::AnyImage light = require(convertedImageOf(scope, read.image, workType), kContext);
            std::array<std::optional<an::consumer::AnyImage>, 3u> working;
            for (std::size_t role = 0u; role < 3u; ++role)
            {
                if (masterImages[role].has_value())
                {
                    working[role] = require(convertedImageOf(scope, *masterImages[role], workType), kContext);
                }
            }
            an::consumer::AnyImage calibrated = std::visit(
                [&](const auto& typed) -> an::consumer::AnyImage {
                    using SampleT = typename std::remove_cvref_t<decltype(typed)>::Sample;
                    if constexpr (!std::is_floating_point_v<SampleT>)
                    {
                        throw std::runtime_error("calibration works in floating point");
                    }
                    else
                    {
                        // The views and their layouts outlive the calibration, which borrows both.
                        const auto viewOf = [](const an::Image<SampleT>& image) { return image.view(); };
                        const auto lightView = viewOf(typed);
                        const std::optional<an::ImageDescriptor> lightDescriptor = typed.descriptor();
                        const an::ImageLayout lightLayout = lightView->layout();
                        const an::CalibrationFrameView lightFrame =
                            require(an::CalibrationFrameView::create(lightView->data(), lightLayout.totalBytes(),
                                                                     lightLayout, *lightDescriptor),
                                    kContext);
                        std::array<std::optional<an::ImageView<const SampleT>>, 3u> masterViews;
                        std::array<std::optional<an::ImageDescriptor>, 3u> masterDescriptors;
                        std::array<std::optional<an::ImageLayout>, 3u> masterLayouts;
                        an::CalibrationMasters applied;
                        std::array<std::optional<an::CalibrationFrameView>*, 3u> slots{&applied.bias, &applied.dark,
                                                                                       &applied.flat};
                        for (std::size_t role = 0u; role < 3u; ++role)
                        {
                            if (!working[role].has_value())
                            {
                                continue;
                            }
                            const auto& master = std::get<an::Image<SampleT>>(*working[role]);
                            masterViews[role] = master.view();
                            masterDescriptors[role] = master.descriptor();
                            masterLayouts[role] = masterViews[role]->layout();
                            *slots[role] = require(an::CalibrationFrameView::create(
                                                       masterViews[role]->data(), masterLayouts[role]->totalBytes(),
                                                       *masterLayouts[role], *masterDescriptors[role]),
                                                   kContext);
                        }
                        an::CalibrationRequest request;
                        request.pedestal = pedestal / 65535.0;
                        request.darkScaling = darkOptimization && applied.dark.has_value()
                                                  ? an::DarkScalingMode::OptimizeCentralRegion
                                                  : an::DarkScalingMode::Fixed;
                        request.darkScale = darkFactor;
                        request.flatNormalization =
                            flatAutolevel ? an::FlatNormalizationMode::CentralMean : an::FlatNormalizationMode::None;
                        const bool nothingApplies = !applied.bias.has_value() && !applied.dark.has_value() &&
                                                    !applied.flat.has_value() && request.pedestal == 0.0;
                        if (nothingApplies)
                        {
                            return an::consumer::AnyImage(require(typed.clone(), kContext));
                        }
                        an::Result<an::CalibrationPlan> plan =
                            an::prepareImageCalibration(scope.execution(), lightFrame, applied, request);
                        if (!plan.hasValue() && request.darkScaling == an::DarkScalingMode::OptimizeCentralRegion &&
                            plan.status().code() == an::ErrorCode::OutOfRange)
                        {
                            // A colour light, or one smaller than the optimization's central square, is calibrated
                            // by the stated dark factor, as the SDK always fell back.
                            request.darkScaling = an::DarkScalingMode::Fixed;
                            plan = an::prepareImageCalibration(scope.execution(), lightFrame, applied, request);
                        }
                        const an::CalibrationPlan decided = require(std::move(plan), kContext);
                        an::Image<SampleT> result =
                            require(createCompactImage<SampleT>(scope.engine(), *lightDescriptor,
                                                                an::StorageInitialization::Uninitialized),
                                    kContext);
                        {
                            std::optional<an::ImageWriteAccess<SampleT>> access = result.write();
                            (void)require(an::calibrateImage(
                                              scope.execution(), decided, lightFrame, applied, access->view().data(),
                                              access->view().layout().totalBytes(), access->view().layout()),
                                          kContext);
                        }
                        return an::consumer::AnyImage(std::move(result));
                    }
                },
                light);
            WriteOptions options = naming.save;
            options.sampleType = outputTypeFor(naming, lightType);
            const an::Status written = writeImageFile(scope, calibrated, read.metadata, outputPath, options);
            if (written.failed())
            {
                error = describeFailure(written);
            }
        }
        catch (const std::exception& failure)
        {
            error = failure.what();
        }
        py::dict frame;
        frame[py::str("index")] = static_cast<int>(index);
        frame[py::str("input_path")] = py::str(path);
        frame[py::str("output_path")] = py::str(error.empty() ? outputPath : std::string{});
        frame[py::str("succeeded")] = error.empty();
        frame[py::str("error_message")] = py::str(error);
        frames.append(frame);
        if (error.empty())
        {
            outputPaths.append(py::str(outputPath));
            ++succeeded;
        }
        else
        {
            ++failed;
        }
    }
    py::dict result;
    result[py::str("total_count")] = static_cast<int>(sourcePaths.size());
    result[py::str("succeeded_count")] = succeeded;
    result[py::str("failed_count")] = failed;
    result[py::str("skipped_count")] = 0;
    result[py::str("was_cancelled")] = false;
    result[py::str("frames")] = frames;
    result[py::str("output_paths")] = outputPaths;
    return result;
}

// ---- Registration.

py::dict registerSequencePathsCall(const py::sequence& paths, const std::string& outputDir, const int referenceIndex,
                                   const int maxStars, const int minStars, const py::object& params,
                                   const std::string& transformType, const int polynomialOrder,
                                   const bool autoSelectPolynomialOrder, const std::string& interpolation)
{
    constexpr std::string_view kContext = "register_sequence_paths()";
    const std::vector<std::string> sourcePaths = pathsFromPythonSequence(paths, kContext);
    if (sourcePaths.empty())
    {
        throw std::runtime_error("register_sequence_paths() requires at least one source path");
    }
    if (outputDir.empty())
    {
        throw std::runtime_error("register_sequence_paths() output_dir must not be empty");
    }
    if (referenceIndex < 0 || referenceIndex >= static_cast<int>(sourcePaths.size()))
    {
        throw std::runtime_error("register_sequence_paths() reference_index is out of range");
    }
    if (maxStars <= 0)
    {
        throw std::runtime_error("register_sequence_paths() max_stars must be greater than zero");
    }
    if (minStars <= 0)
    {
        throw std::runtime_error("register_sequence_paths() min_stars must be greater than zero");
    }
    const auto [starModel, linearKind] = transformKindFromSdkName(transformType);
    const an::KernelInterpolation sampling = interpolationFromSdkName(interpolation);
    an::StarDetectionRequest finder = parseStarFinderParams(params);
    finder.maximumStars = static_cast<std::uint32_t>(maxStars);
    const std::shared_ptr<an::consumer::EngineSession> session = requireSession();

    // Each frame's stars, in its own operation.
    std::vector<FrameRegistration> registrations(sourcePaths.size());
    std::vector<std::vector<an::StarAlignmentPoint>> points(sourcePaths.size());
    std::vector<std::pair<std::uint32_t, std::uint32_t>> sizes(sourcePaths.size());
    for (std::size_t index = 0u; index < sourcePaths.size(); ++index)
    {
        FrameRegistration& registration = registrations[index];
        try
        {
            ReadImage read = readFrame(*session, sourcePaths[index], kContext);
            an::consumer::OperationScope scope =
                admitOperation(*session, an::consumer::OperationPurpose::Batch, kContext);
            const an::Image<float> frame = registrationFrameOf(scope, read.image, kContext);
            const std::optional<an::ImageDescriptor> descriptor = frame.descriptor();
            sizes[index] = {descriptor->extent().width(), descriptor->extent().height()};
            const an::StarDetectionResult found = require(
                detectImageStars(scope, an::consumer::AnyImage(require(frame.clone(), kContext)), finder, std::nullopt),
                kContext);
            registration.summary = found.summary;
            registration.starCount = found.stars.size();
            for (const an::DetectedStar& star : found.stars)
            {
                points[index].push_back(an::StarAlignmentPoint{.x = star.positionX, .y = star.positionY});
            }
        }
        catch (const std::exception& failure)
        {
            registration.onError = true;
            registration.error = failure.what();
        }
    }

    // Every frame matched against the reference's stars.
    const std::size_t reference = static_cast<std::size_t>(referenceIndex);
    py::list failedPaths;
    if (registrations[reference].onError)
    {
        throw std::runtime_error("register_sequence_paths() could not read the reference frame: " +
                                 registrations[reference].error);
    }
    {
        an::consumer::OperationScope scope = admitOperation(*session, an::consumer::OperationPurpose::Batch, kContext);
        an::StarAlignmentRequest alignment;
        alignment.model = starModel;
        const std::uint32_t floor = starModel == an::StarTransformModel::Projective ? 4u : 3u;
        alignment.minimumMatches = (std::max)(static_cast<std::uint32_t>(minStars), floor);
        an::Result<an::PreparedStarAlignmentReference> prepared =
            an::prepareStarAlignmentReference(scope.execution(), points[reference], alignment);
        for (std::size_t index = 0u; index < sourcePaths.size(); ++index)
        {
            FrameRegistration& registration = registrations[index];
            registration.transform =
                require(an::RegistrationTransform::create(an::RegistrationTransformRequest{.linearKind = linearKind}),
                        kContext);
            if (index == reference)
            {
                registration.method = "reference";
                continue;
            }
            if (registration.onError)
            {
                continue;
            }
            registration.method = "star_matching";
            if (!prepared.hasValue())
            {
                registration.onError = true;
                registration.error = describeFailure(prepared.status());
                continue;
            }
            an::Result<an::StarAlignmentResult> matched =
                an::alignStarFields(scope.execution(), points[index], *prepared);
            if (!matched.hasValue() || matched->quality.inlierMatches < static_cast<std::uint32_t>(minStars))
            {
                registration.onError = true;
                registration.error = matched.hasValue() ? "only " + std::to_string(matched->quality.inlierMatches) +
                                                              " stars matched the reference's"
                                                        : describeFailure(matched.status());
                continue;
            }
            registration.matchedStars = static_cast<int>(matched->quality.inlierMatches);
            if (polynomialOrder >= 2 || autoSelectPolynomialOrder)
            {
                std::vector<an::PixelCoordinate> source;
                std::vector<an::PixelCoordinate> target;
                for (const an::StarMatch& match : matched->matches)
                {
                    const an::StarAlignmentPoint& from = points[index][match.sourceIndex];
                    const an::StarAlignmentPoint& to = points[reference][match.referenceIndex];
                    source.push_back(an::PixelCoordinate{from.x, from.y});
                    target.push_back(an::PixelCoordinate{to.x, to.y});
                }
                an::RegistrationFitRequest fit;
                fit.linearKind = linearKind;
                fit.polynomialPolicy = autoSelectPolynomialOrder ? an::RegistrationPolynomialPolicy::AutomaticOrder
                                                                 : an::RegistrationPolynomialPolicy::StatedOrder;
                fit.polynomialOrder = static_cast<std::uint32_t>(std::clamp(polynomialOrder, 2, 4));
                const double halfWidth = static_cast<double>(sizes[index].first) * 0.5;
                const double halfHeight = static_cast<double>(sizes[index].second) * 0.5;
                fit.normalizationOrigin = an::PixelCoordinate{halfWidth, halfHeight};
                fit.normalizationScale = an::PixelCoordinate{(std::max)(halfWidth, 1.0), (std::max)(halfHeight, 1.0)};
                an::Result<an::RegistrationFitResult> fitted =
                    an::fitRegistrationTransform(scope.execution(), source, target, fit);
                if (!fitted.hasValue())
                {
                    registration.onError = true;
                    registration.error = describeFailure(fitted.status());
                    continue;
                }
                registration.transform = fitted->transform;
                registration.rmsPixels = fitted->rootMeanSquareResidual;
            }
            else
            {
                an::Result<an::RegistrationTransform> transform = an::registrationTransformOf(matched->transform);
                if (!transform.hasValue())
                {
                    registration.onError = true;
                    registration.error = describeFailure(transform.status());
                    continue;
                }
                // The matcher states the model it fitted; the SDK reports the kind it was asked for.
                an::RegistrationTransformRequest stated = transform->toRequest();
                stated.linearKind = linearKind;
                registration.transform = require(an::RegistrationTransform::create(stated), kContext);
                registration.rmsPixels = matched->quality.rootMeanSquareResidual;
            }
        }
    }
    for (std::size_t index = 0u; index < sourcePaths.size(); ++index)
    {
        if (registrations[index].onError)
        {
            failedPaths.append(py::str(sourcePaths[index]));
        }
    }

    py::dict result = alignFrames(*session, sourcePaths, registrations, sizes, referenceIndex, outputDir, sampling,
                                  an::RegistrationCanvasMode::Reference, OutputNaming{}, kContext);
    result[py::str("failed_registration_paths")] = failedPaths;
    return result;
}

py::dict applyRegistrationPathsCall(const py::sequence& paths, const py::sequence& registrations,
                                    const std::string& outputDir, const int referenceIndex,
                                    const std::string& interpolation, const std::string& alignmentMode,
                                    const py::object& outputOptions)
{
    constexpr std::string_view kContext = "apply_registration_paths()";
    const std::vector<std::string> sourcePaths = pathsFromPythonSequence(paths, kContext);
    if (sourcePaths.empty())
    {
        throw std::runtime_error("apply_registration_paths() requires at least one source path");
    }
    if (outputDir.empty())
    {
        throw std::runtime_error("apply_registration_paths() output_dir must not be empty");
    }
    const std::vector<FrameRegistration> parsed =
        registrationsFromPython(sourcePaths, registrations, referenceIndex, kContext);
    const an::KernelInterpolation sampling = interpolationFromSdkName(interpolation);
    const an::RegistrationCanvasMode mode = alignmentModeFromSdkName(alignmentMode);
    const OutputNaming naming = parseOutputNaming(outputOptions);
    const std::shared_ptr<an::consumer::EngineSession> session = requireSession();
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> sizes = frameSizes(*session, sourcePaths, kContext);
    py::dict result =
        alignFrames(*session, sourcePaths, parsed, sizes, referenceIndex, outputDir, sampling, mode, naming, kContext);
    result[py::str("was_cancelled")] = false;
    return result;
}

// ---- Stacking.

py::dict stackSequencePathsCall(const py::sequence& paths, const std::string& outputPath, const std::string& method,
                                const py::object& params, const py::object& metadata, const py::object& saveOptions)
{
    constexpr std::string_view kContext = "stack_sequence_paths()";
    const std::vector<std::string> sourcePaths = pathsFromPythonSequence(paths, kContext);
    if (sourcePaths.empty())
    {
        throw std::runtime_error("stack_sequence_paths() requires at least one source path");
    }
    if (outputPath.empty())
    {
        throw std::runtime_error("stack_sequence_paths() output_path must not be empty");
    }
    StackSettings settings = parseStackSettings(method, params, sourcePaths.size());
    const QualityOutputs quality = parseQualityOutputs(params, outputPath, settings);
    const WriteOptions parsedSave = parseSaveOptions(saveOptions);
    const std::shared_ptr<an::consumer::EngineSession> session = requireSession();
    const an::ImageDescriptor input = stackInputDescriptor(*session, sourcePaths.front(), kContext);
    const an::StackModel model =
        stackModelFor(settings, input, static_cast<std::uint32_t>(sourcePaths.size()), quality, kContext);
    std::vector<an::StackFrameMeasurement> measurements;
    if (weightingNeedsMeasurements(model.request().weighting.method))
    {
        measurements = measureFrames(*session, sourcePaths, kContext);
    }
    const an::consumer::SessionHold hold = require(session->hold(), kContext);
    FileStackSource source(hold.engine(), sourcePaths, input);
    std::vector<an::FrameRejectionRate> rates(sourcePaths.size());
    an::StackResult stacked = runStack(*session, model, source, settings, measurements, rates, kContext);

    py::dict stated;
    stated[py::str("IMAGETYP")] = py::str("STACKED_LIGHT");
    stated[py::str("STACKCNT")] = static_cast<int>(sourcePaths.size());
    stated[py::str("STACKMTH")] = py::str(settings.methodName);
    SdkMetadata stackedCards =
        stackedMetadata(*session, sourcePaths[settings.referenceIndex], stated, metadata, outputPath, kContext);
    return publishStack(*session, stacked, model, settings, quality, static_cast<std::uint32_t>(sourcePaths.size()),
                        rates, std::move(stackedCards), outputPath, parsedSave, std::nullopt, kContext);
}

py::dict drizzleStackSequencePathsCall(const py::sequence& paths, const py::sequence& registrations,
                                       const std::string& outputPath, const std::string& method,
                                       const py::object& params, const py::object& metadata,
                                       const py::object& saveOptions, const int referenceIndex,
                                       const std::string& interpolation, const std::string& alignmentMode)
{
    constexpr std::string_view kContext = "drizzle_stack_sequence_paths()";
    const std::vector<std::string> sourcePaths = pathsFromPythonSequence(paths, kContext);
    if (sourcePaths.empty())
    {
        throw std::runtime_error("drizzle_stack_sequence_paths() requires at least one source path");
    }
    if (outputPath.empty())
    {
        throw std::runtime_error("drizzle_stack_sequence_paths() output_path must not be empty");
    }
    StackSettings settings = parseStackSettings(method, params, sourcePaths.size());
    const QualityOutputs quality = parseQualityOutputs(params, outputPath, settings);
    const DrizzleSettings drizzle = parseDrizzleSettings(params);
    const std::vector<FrameRegistration> parsed =
        registrationsFromPython(sourcePaths, registrations, referenceIndex, "drizzle_stack_sequence_paths()");
    // The drizzle reads its frames through their registrations and resamples nothing itself, so the interpolation is
    // only checked; it names the warp an alignment would use.
    (void)interpolationFromSdkName(interpolation);
    const an::RegistrationCanvasMode mode = alignmentModeFromSdkName(alignmentMode);
    const WriteOptions parsedSave = parseSaveOptions(saveOptions);
    const std::shared_ptr<an::consumer::EngineSession> session = requireSession();

    an::ImageDescriptor input = stackInputDescriptor(*session, sourcePaths.front(), kContext);
    {
        // The drizzle reads every frame as Float32 fractions of full scale.
        an::ImageDescriptorRequest request = input.toRequest();
        request.sampleType = an::SampleType::Float32;
        if (drizzle.bayer)
        {
            if (drizzle.pattern.has_value())
            {
                request.cfa = *an::CfaDescriptor::create(*drizzle.pattern, 0u, 0u);
            }
            else if (!input.isMosaiced())
            {
                throw std::runtime_error("drizzle_stack_sequence_paths(): a CFA drizzle needs frames that state their "
                                         "Bayer pattern, or a cfa_pattern");
            }
        }
        const std::optional<an::ImageDescriptor> described = an::ImageDescriptor::create(request);
        if (!described.has_value())
        {
            throw std::runtime_error("drizzle_stack_sequence_paths(): the frames cannot be drizzled as described");
        }
        input = *described;
    }
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> sizes = frameSizes(*session, sourcePaths, kContext);

    an::DrizzleCanvas canvas;
    if (drizzle.canvasBounds.has_value())
    {
        canvas = require(an::planDrizzleCanvasOnPlane(*drizzle.canvasBounds, drizzle.scale), kContext);
    }
    else
    {
        std::vector<an::RegistrationBounds> bounds;
        for (std::size_t index = 0u; index < sourcePaths.size(); ++index)
        {
            bounds.push_back(
                require(an::computeRegistrationBounds(parsed[index].transform, sizes[index].first, sizes[index].second),
                        kContext));
        }
        canvas = require(
            an::planDrizzleCanvas(
                bounds, static_cast<std::uint32_t>(referenceIndex),
                an::DrizzleCanvasRequest{.mode = mode, .scale = drizzle.scale, .pixelFraction = drizzle.pixelFraction}),
            kContext);
    }
    std::uint32_t renderWidth = canvas.width;
    std::uint32_t renderHeight = canvas.height;
    std::optional<std::array<std::uint32_t, 4u>> crop =
        std::array<std::uint32_t, 4u>{canvas.edgeGuard, canvas.edgeGuard, canvas.outputWidth, canvas.outputHeight};
    if (drizzle.outputWidth > 0u && drizzle.outputHeight > 0u)
    {
        // A stated output size is the canvas the drizzle renders, from the planned origin, with nothing trimmed.
        renderWidth = drizzle.outputWidth;
        renderHeight = drizzle.outputHeight;
        crop.reset();
    }
    else if (canvas.edgeGuard == 0u && canvas.outputWidth == canvas.width && canvas.outputHeight == canvas.height)
    {
        crop.reset();
    }

    an::DrizzleRequest request{.input = input, .frameCount = static_cast<std::uint32_t>(sourcePaths.size())};
    request.parameters.kernel = drizzle.kernel;
    request.parameters.scale = drizzle.scale;
    request.parameters.pixelFraction = drizzle.pixelFraction;
    request.parameters.weightScale = drizzle.weightScale;
    request.parameters.nonFinite = an::DrizzleNonFinitePolicy::Exclude;
    request.output.extent = an::ImageExtent::create(
        renderWidth, renderHeight, input.isMosaiced() ? 3u : input.extent().channelCount(), session->limits());
    const an::DrizzleModel drizzleModel = require(an::DrizzleModel::create(request, session->limits()), kContext);
    std::vector<an::DrizzleFrameParameters> frames;
    for (const FrameRegistration& registration : parsed)
    {
        frames.push_back(an::DrizzleFrameParameters{
            .geometry = require(an::DrizzleMapDescriptor::create(
                                    drizzleModel, an::DrizzleMappingRequest{.transform = registration.transform,
                                                                            .postTranslation = canvas.origin}),
                                kContext)});
    }
    const an::consumer::SessionHold hold = require(session->hold(), kContext);
    FileDrizzleSource drizzleSource(hold.engine(), sourcePaths, input);
    // The source is used where create left it, and the stack reads it through a decorator that borrows it: the engine
    // exports the class's members but not its virtual table, which a copy or move outside the engine would need.
    an::Result<an::DrizzledStackFrameSource> stackSource =
        an::DrizzledStackFrameSource::create(hold.engine(), drizzleModel, drizzleSource, frames);
    if (!stackSource.hasValue())
    {
        raiseFailure(kContext, stackSource.status());
    }
    LightsLevelDrizzleSource atLightsLevel(*stackSource, drizzle.scale);
    const an::StackModel model = stackModelFor(settings, drizzleModel.outputDescriptor(),
                                               static_cast<std::uint32_t>(sourcePaths.size()), quality, kContext);
    std::vector<an::StackFrameMeasurement> measurements;
    if (weightingNeedsMeasurements(model.request().weighting.method))
    {
        measurements = measureFrames(*session, sourcePaths, kContext);
    }
    std::vector<an::FrameRejectionRate> rates(sourcePaths.size());
    an::StackResult stacked = runStack(*session, model, atLightsLevel, settings, measurements, rates, kContext);
    const std::uint32_t trimX = crop.has_value() ? (*crop)[0] : 0u;
    const std::uint32_t trimY = crop.has_value() ? (*crop)[1] : 0u;
    const std::uint32_t outputWidth = crop.has_value() ? (*crop)[2] : renderWidth;
    const std::uint32_t outputHeight = crop.has_value() ? (*crop)[3] : renderHeight;

    py::dict stated;
    stated[py::str("IMAGETYP")] = py::str("STACKED_LIGHT");
    stated[py::str("STACKCNT")] = static_cast<int>(sourcePaths.size());
    stated[py::str("STACKMTH")] = py::str(settings.methodName);
    stated[py::str("DRIZZLE")] = py::str("1");
    stated[py::str("DRZSCALE")] = drizzle.scale;
    stated[py::str("DRZPIXFR")] = drizzle.pixelFraction;
    stated[py::str("DRZKERNL")] = py::str(drizzleKernelSdkName(drizzle.kernel));
    SdkMetadata cards = withoutWorldCoordinates(stackedMetadata(
        *session, sourcePaths[static_cast<std::size_t>(referenceIndex)], stated, metadata, outputPath, kContext));
    py::dict result =
        publishStack(*session, stacked, model, settings, quality, static_cast<std::uint32_t>(sourcePaths.size()), rates,
                     std::move(cards), outputPath, parsedSave, crop, kContext);
    py::dict info;
    info[py::str("enabled")] = true;
    info[py::str("scale")] = drizzle.scale;
    info[py::str("pixel_fraction")] = drizzle.pixelFraction;
    info[py::str("kernel")] = py::str(drizzleKernelSdkName(drizzle.kernel));
    info[py::str("trim_offset_x")] = static_cast<int>(trimX);
    info[py::str("trim_offset_y")] = static_cast<int>(trimY);
    info[py::str("output_width")] = static_cast<int>(outputWidth);
    info[py::str("output_height")] = static_cast<int>(outputHeight);
    result[py::str("drizzle")] = info;
    return result;
}

} // namespace an::sdk
