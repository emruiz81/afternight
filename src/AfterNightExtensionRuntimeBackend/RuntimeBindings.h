// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The functions the embedded `_afternight_runtime` module binds, one per SDK call, declared for the module
/// definition in `RuntimeModule.cpp` and defined beside the engine area each one reaches:
/// `RuntimeImageCalls.cpp` for images, files, conversions and channels, `RuntimeStarCalls.cpp` for stars and
/// deconvolution, `RuntimeWorkflowCalls.cpp` for calibration, registration and stacking over files.
///
/// Each keeps the keyword defaults, the refusals and the result dictionaries the SDK states; an engine failure is
/// raised as `RuntimeError` carrying the binding's name and the engine's reason. Every call runs synchronously with
/// the interpreter's lock held, in operations the backend's session admits.
#pragma once

#include "RuntimePython.h"

#include "RuntimeImage.h"

#include <memory>
#include <string>

namespace an::sdk
{

// ---- Images, files, conversions and channels.

[[nodiscard]] py::list availablePixelTypes();
[[nodiscard]] std::shared_ptr<RuntimeImage> imageFromNumpyCall(const py::array& array, const py::object& metadata);
[[nodiscard]] std::shared_ptr<RuntimeImage> loadImageCall(const std::string& path, bool loadMetadata);
void saveImageCall(const std::shared_ptr<RuntimeImage>& image, const std::string& path, const py::object& options);
[[nodiscard]] std::shared_ptr<RuntimeImage> convertPixelTypeCall(const std::shared_ptr<RuntimeImage>& image,
                                                                 const std::string& pixelType);
void applyLinearTransformCall(const std::shared_ptr<RuntimeImage>& source,
                              const std::shared_ptr<RuntimeImage>& destination, double scale, double offset);
[[nodiscard]] std::shared_ptr<RuntimeImage> extractChannelCall(const std::shared_ptr<RuntimeImage>& image, int channel);
[[nodiscard]] py::list extractChannelsCall(const std::shared_ptr<RuntimeImage>& image);
[[nodiscard]] std::shared_ptr<RuntimeImage> composeRgbCall(const std::shared_ptr<RuntimeImage>& red,
                                                           const std::shared_ptr<RuntimeImage>& green,
                                                           const std::shared_ptr<RuntimeImage>& blue);

/// The session capsule of the backend's session, for the full host (`RuntimeSessionCapsule.h`).
[[nodiscard]] py::capsule sessionCapsuleCall();

// ---- Stars and deconvolution.

[[nodiscard]] double detectFwhmCall(const std::shared_ptr<RuntimeImage>& image, int channel);
[[nodiscard]] py::list findStarsCall(const std::shared_ptr<RuntimeImage>& image, int channel, int maxStars,
                                     const py::object& params);
[[nodiscard]] std::shared_ptr<RuntimeImage> richardsonLucyDeconvolutionCall(
    const std::shared_ptr<RuntimeImage>& image, double fwhmX, double fwhmY, double angleDegrees, int iterations,
    double tvRegularization, double damping, double stoppingDelta, int psfSize, const std::string& outputPixelType,
    bool preserveFlux, bool clampToUnitRange, const std::string& psfProfile, double beta, double relativeStoppingDelta,
    double edgeTaperRadius, const std::string& convolutionBackend, const std::string& colorMode,
    const std::string& edgeTaperMode, int edgeTaperIterations, const std::string& regularizationMode,
    const std::string& updateMethod, double gradientDescentStep);
[[nodiscard]] std::shared_ptr<RuntimeImage>
wienerDeconvolutionCall(const std::shared_ptr<RuntimeImage>& image, double fwhmX, double fwhmY, double angleDegrees,
                        double regularization, int psfSize, const std::string& outputPixelType, bool preserveFlux,
                        bool clampToUnitRange, const std::string& psfProfile, double beta, double edgeTaperRadius,
                        const std::string& colorMode, const std::string& edgeTaperMode, int edgeTaperIterations);
[[nodiscard]] std::shared_ptr<RuntimeImage> richardsonLucyStarDeconvolutionCall(
    const std::shared_ptr<RuntimeImage>& image, int channel, int maxStars, const py::object& starParams, int iterations,
    double tvRegularization, double damping, double stoppingDelta, int psfSize, const std::string& outputPixelType,
    bool preserveFlux, bool clampToUnitRange, const std::string& psfProfile, int minimumStarCount, bool ignoreSaturated,
    bool circularize, double relativeStoppingDelta, double edgeTaperRadius, const std::string& convolutionBackend,
    const std::string& colorMode, const std::string& edgeTaperMode, int edgeTaperIterations,
    const std::string& regularizationMode, const std::string& updateMethod, double gradientDescentStep);

// ---- Calibration, registration and stacking over files.

[[nodiscard]] py::dict createCalibrationMastersPathsCall(const py::dict& rolePaths, const std::string& outputDir,
                                                         const std::string& filterName, const std::string& method,
                                                         const py::object& params, const py::object& saveOptions);
[[nodiscard]] py::dict calibrateSequencePathsCall(const py::sequence& paths, const py::dict& masters,
                                                  const std::string& outputDir, const py::object& params,
                                                  const py::object& outputOptions);
[[nodiscard]] py::dict registerSequencePathsCall(const py::sequence& paths, const std::string& outputDir,
                                                 int referenceIndex, int maxStars, int minStars,
                                                 const py::object& params, const std::string& transformType,
                                                 int polynomialOrder, bool autoSelectPolynomialOrder,
                                                 const std::string& interpolation);
[[nodiscard]] py::dict applyRegistrationPathsCall(const py::sequence& paths, const py::sequence& registrations,
                                                  const std::string& outputDir, int referenceIndex,
                                                  const std::string& interpolation, const std::string& alignmentMode,
                                                  const py::object& outputOptions);
[[nodiscard]] py::dict stackSequencePathsCall(const py::sequence& paths, const std::string& outputPath,
                                              const std::string& method, const py::object& params,
                                              const py::object& metadata, const py::object& saveOptions);
[[nodiscard]] py::dict drizzleStackSequencePathsCall(const py::sequence& paths, const py::sequence& registrations,
                                                     const std::string& outputPath, const std::string& method,
                                                     const py::object& params, const py::object& metadata,
                                                     const py::object& saveOptions, int referenceIndex,
                                                     const std::string& interpolation,
                                                     const std::string& alignmentMode);

// ---- Parsing shared by the calls.

/// The save options a dictionary states, warning about a key the SDK does not know.
[[nodiscard]] WriteOptions parseSaveOptions(const py::object& value);

/// The star detector's request a finder dictionary states - radius, sigma, roundness, focal length and pixel size,
/// convergence, relaxed checks, the Moffat beta floor, the amplitude window, the largest roundness and the profile -
/// over the engine's defaults, which are the finder's own.
[[nodiscard]] an::StarDetectionRequest parseStarFinderParams(const py::object& value);

/// A Python dictionary's value, or None.
[[nodiscard]] py::object dictValueOrNone(const py::dict& values, const char* key);

/// Copies a dictionary's value into a target when the key is present.
template <typename ValueT> void assignIfPresent(const py::dict& values, const char* key, ValueT& target)
{
    const py::str pyKey(key);
    if (values.contains(pyKey))
    {
        target = values[pyKey].cast<ValueT>();
    }
}

/// A lowercase copy of a name, for the case-insensitive names the SDK accepts.
[[nodiscard]] std::string lowerName(std::string_view name);

} // namespace an::sdk
