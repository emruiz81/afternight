// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/// @file
/// A light as the integration's registration and alignment stages read it: its header probed for what registering it
/// needs - its extent, its filter, when it was taken and for how long, and the solution and distortion its own cards
/// state - and its samples read through the run's codec registry as Float32 fractions of full scale, calibrated by the
/// masters it names when the run calibrates in memory, and demosaiced by the registration's method when it is a mosaic;
/// and a registered light warped onto the plane its group is aligned onto.
///
/// Each runs inside an operation the caller holds, so a stage reads, calibrates, demosaics and warps a frame in the
/// same admitted operation.

#include "processes/core/RegistrationRun.h"

#include <AfterNightEngine/Algorithms/Astrometry/SipDistortion.h>
#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Algorithms/Kernel/SamplingKernel.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationPlan.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/Image.h>

#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

namespace an
{
class CodecRegistry;
} // namespace an

namespace an::consumer
{
class OperationScope;
} // namespace an::consumer

namespace core
{
struct LightCalibration;
} // namespace core

namespace stage_utils
{

/// What a light's header states that registering it reads.
struct RegistrationFrameFacts
{
    /// The frame's extent.
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;

    /// Its channels, before any demosaic.
    std::uint32_t channels = 0u;

    /// Whether its samples are a filter-array mosaic.
    bool mosaic = false;

    /// The filter its header names, trimmed; empty when it names none.
    QString filterName;

    /// When the exposure started, as a FITS date; empty when the header does not say.
    QString dateObs;

    /// How long the exposure lasted, in seconds, when the header says.
    std::optional<double> exposureSeconds;

    /// The epoch a catalog's stars are carried to for this frame, from when it was taken.
    std::optional<double> observationEpochYear;

    /// The frame's own world-coordinate solution, when its cards state one.
    std::optional<an::WcsSolution> solution;

    /// The distortion that solution carries, when it carries one.
    std::optional<an::SipDistortion> distortion;

    /// The frame's world-coordinate cards as its header states them.
    std::vector<an::CodecCard> wcsCards;
};

/// Probes a light's header through the run's registry; no sample is decoded.
///
/// @param scope the caller's admitted operation.
/// @param registry the run's registry, or null for the engine context's.
/// @param path the light.
/// @return what the header states; or what the codec refused, the file named.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<RegistrationFrameFacts>
probeRegistrationFrame(const an::consumer::OperationScope& scope, const an::CodecRegistry* registry,
                       const QString& path);

/// Reads a light as the registration reads it: Float32 fractions of full scale, calibrated by `calibration` when one is
/// given and it changes anything, and a mosaic demosaiced by the registration's method and arrangement.
///
/// @param scope the caller's admitted operation.
/// @param registry the run's registry, or null for the engine context's.
/// @param path the light.
/// @param calibration the light's calibration, or null when the light is read as it is.
/// @param demosaic how a mosaic is demosaiced.
/// @return the frame, an image of the operation's engine; or what reading, calibrating or demosaicing refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::Image<float>>
readRegistrationImage(const an::consumer::OperationScope& scope, const an::CodecRegistry* registry, const QString& path,
                      const core::LightCalibration* calibration,
                      const an::ui::processes::RegistrationDemosaic& demosaic);

/// A registered frame warped onto a plane: sampled through its transform with its source distortion undone first, zero
/// wherever its position on the frame lies off the frame's pixels, its edge read from the frame's own edge sample past
/// it rather than from a step to zero an interpolation would ring on, its description restated onto the plane, and its
/// cards the source's but its world coordinates - the plane's take their place when the plane is solved, and the frame
/// carries none otherwise.
///
/// @param scope the caller's admitted operation.
/// @param source the frame, as `readRegistrationImage` read it.
/// @param transform its transform onto the reference.
/// @param distortion the source distortion the transform was solved over, or nothing.
/// @param canvas the plane.
/// @param interpolation the sampling.
/// @param planeSolution the plane's solution, or nothing for a plane the sky does not describe.
/// @return the warped frame, an image of the operation's engine; or what planning, warping or describing refused.
[[nodiscard]] an::Result<an::Image<float>>
warpRegistrationFrame(const an::consumer::OperationScope& scope, const an::Image<float>& source,
                      const an::RegistrationTransform& transform, const std::optional<an::SipDistortion>& distortion,
                      const an::RegistrationCanvas& canvas, an::KernelInterpolation interpolation,
                      const std::optional<an::WcsSolution>& planeSolution);

} // namespace stage_utils
