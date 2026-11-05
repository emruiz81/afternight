// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the image view computes over the session: a file read into a new asset, a mosaic demosaiced by the one demosaic
/// setting, a preview light calibrated by its masters, an image's statistics, an image written to a file whole or not
/// at all, the right-angle turns and mirrors of its image menu, and the stars its overlay draws.
///
/// Every read, demosaic, preview calibration and save is a function a worker calls under the session, step by
/// admitted step, so a read holds a share of the session's threads only while it computes, and observes the request's
/// stop between steps. The view decides on the GUI thread what to read and with which settings, and publishes on the
/// GUI thread.
///
/// **One demosaic setting, honoured everywhere a mosaic is opened.** `demosaicMethodForSetting` reads the
/// product's persisted choice; the three choices the engine does not offer read as RCD and say so.
#pragma once

#include "AfterNightControlsExport.h"
#include "app/DemosaicTextMaps.h"
#include "controls/CalibrationPreviewConfig.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Algorithms/Demosaic/CfaWhiteBalance.h>
#include <AfterNightEngine/Algorithms/Demosaic/Demosaic.h>
#include <AfterNightEngine/Algorithms/Geometry/Geometry.h>
#include <AfterNightEngine/Algorithms/Stars/StarDetectionChannels.h>
#include <AfterNightEngine/Core/ExceptionBoundary.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <QString>

#include <array>
#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <unordered_map>
#include <utility>

namespace an
{
class CodecRegistry;
class EngineContext;
class ExecutionContext;
class ImageMetadata;
class OutputIntent;
class RawFrame;
class ValidityMask;
} // namespace an

namespace an::ui
{

/// The demosaic method the product's persisted demosaic setting names, through `demosaicMethodFromSettingsKey`:
/// `bilinear`, `vng4`, `ahd`, `amaze`, `lmmse` and `rcd`, case aside; `dcb`, `hphd` and `igv`, which the engine does
/// not offer, and anything unknown read as RCD.
///
/// @param key the setting's value.
/// @return the method, and whether the setting names a method the engine does not offer.
[[nodiscard]] AFTERNIGHTCONTROLS_API DemosaicChoice demosaicMethodForSetting(const QString& key) noexcept;

/// The masters a static preview calibrates its light with, and how.
struct ViewCalibration
{
    /// The bias master, or empty for none.
    QString biasPath;

    /// The dark master, or empty for none.
    QString darkPath;

    /// The flat master, or empty for none.
    QString flatPath;

    /// Whether the dark master holds its bias, which the preview subtracts from it first.
    bool subtractBiasFromDark = false;

    /// The calibration's settings.
    CalibrationPreviewSettings settings{};
};

/// The canonical text of a calibration, for a cache key: equal calibrations give equal text.
///
/// @param calibration the calibration.
/// @return the text.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString viewCalibrationKey(const ViewCalibration& calibration);

/// The masters a view's previews read, each once, shared by every preview of the view until it is cleared.
///
/// Thread-safe: preview reads run on workers.
class ViewMasterCache
{
public:
    /// A cache of at most `capacity` masters.
    ///
    /// @param capacity the most masters kept.
    AFTERNIGHTCONTROLS_API explicit ViewMasterCache(std::size_t capacity = 3u);

    /// The master read from a path, if it is kept.
    ///
    /// @param path the master's file.
    /// @return the master, or null.
    [[nodiscard]] AFTERNIGHTCONTROLS_API std::shared_ptr<const an::consumer::ImageAsset> find(const QString& path);

    /// Keeps a master read from a path, letting the least recently used go.
    ///
    /// @param path the master's file.
    /// @param master the master.
    AFTERNIGHTCONTROLS_API void keep(const QString& path, std::shared_ptr<const an::consumer::ImageAsset> master);

    /// Lets every master go.
    AFTERNIGHTCONTROLS_API void clear();

    /// How many masters are kept.
    [[nodiscard]] AFTERNIGHTCONTROLS_API std::size_t size() const;

private:
    using Entry = std::pair<QString, std::shared_ptr<const an::consumer::ImageAsset>>;

    std::size_t m_capacity;
    mutable std::mutex m_mutex;
    std::list<Entry> m_order;
};

/// What a read does to a camera RAW frame beyond decoding it: the white balance a mosaic's filter-array step applies,
/// and whether the frame is turned by the orientation the camera recorded. A full-colour frame's balance is the camera
/// RAW adapter's, carried by the registry the read names.
struct ViewRawTreatment
{
    /// Where a mosaic's gains come from. Every mode also stretches a black-subtracted code at the white level to 65535,
    /// `CfaWhiteBalanceMode::None` included.
    an::CfaWhiteBalanceMode mosaicBalance = an::CfaWhiteBalanceMode::Daylight;

    /// The red, green and blue gains `CfaWhiteBalanceMode::Custom` applies; each positive and finite.
    std::array<double, 3u> customGains{1.0, 1.0, 1.0};

    /// Whether a camera RAW frame is turned by the orientation its camera recorded.
    bool autoRotate = false;
};

/// What a view's read asks for, decided on the GUI thread.
struct ViewReadRequest
{
    /// The file.
    QString path;

    /// The method chosen for a mosaic, or nothing to keep it as the sensor recorded it; over a mosaic of the other
    /// shape, an X-Trans table, it runs as `demosaicMethodForMosaic` gives it.
    std::optional<an::DemosaicMethod> demosaic{};

    /// The masters a preview light is calibrated with, or nothing.
    std::optional<ViewCalibration> calibration{};

    /// A codec registry carrying the read's RAW policy, or null for the session's own. Borrowed for the read.
    const an::CodecRegistry* registry = nullptr;

    /// What is done to a camera RAW frame after it is decoded, or nothing to keep it as decoded. A frame that states
    /// no camera record - every file but a camera RAW - is never treated.
    std::optional<ViewRawTreatment> raw{};
};

/// What a view's read produced.
struct ViewReadResult
{
    /// The image, a new asset of the session.
    an::consumer::ImageAsset asset;

    /// Whether the file held a mosaic the read left as recorded.
    bool mosaic = false;

    /// Whether the read demosaiced a mosaic, and the method that ran.
    std::optional<an::DemosaicMethod> demosaiced{};

    /// Whether the light was calibrated by its masters.
    bool calibrated = false;
};

/// Reads an image file into a new asset of the session: decoded, calibrated by its masters when asked, and a mosaic
/// demosaiced when asked. Each step is admitted on its own and the stop is observed between steps and inside each.
///
/// @param session the process's session.
/// @param request what to read.
/// @param stop the request's stop.
/// @param masters the view's master cache, or null to read every master afresh.
/// @return the image; `ErrorCode::Cancelled` when stopped; or what a step refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<ViewReadResult>
readViewImage(const an::consumer::EngineSession& session, const ViewReadRequest& request, std::stop_token stop,
              ViewMasterCache* masters);

/// Runs a view's work on its worker and turns an exception it throws into the failure the work would have returned, as
/// the engine's boundaries do: `ErrorCode::AllocationFailed` for `std::bad_alloc`, `ErrorCode::ExternalFailure` for
/// anything else, with the exception's message. So the completion the GUI thread waits for always lands - a read that
/// threw ends as a read that failed, its spinner stopped and its listeners told - instead of being dropped with a wait
/// never ended.
///
/// @param work what runs; returns a `Result` or a `Status`.
/// @return what `work` returned, or the failure its exception became.
template <typename WorkT> [[nodiscard]] std::invoke_result_t<WorkT&> viewWorkOutcome(WorkT&& work) noexcept
{
    static constexpr an::ExternalBoundary kBoundary{
        .library = "AfterNightControls", .operation = "view work", .domain = an::ErrorDomain::Interop};
    try
    {
        return work();
    }
    catch (const std::exception& error)
    {
        return an::translateException(kBoundary, error);
    }
    catch (...)
    {
        return an::translateUnknownException(kBoundary);
    }
}

/// Demosaics a mosaic into a new colour asset of the session, with the pattern the mosaic states. The mosaic's validity
/// reaches the demosaic: a mosaic that states which of its samples mean something makes a colour asset whose mask
/// clears every pixel estimated from a sample that means nothing. A floating mosaic in a physical or sensor-code domain
/// is demosaiced against its own brightest finite sample, never below one, as its full exposure.
///
/// @param session the process's session.
/// @param mosaic the mosaic.
/// @param method the method chosen; over a mosaic of the other shape it runs as `demosaicMethodForMosaic` gives it.
/// @param stop the request's stop.
/// @return the colour image, or what the demosaic refused - an image that is not a mosaic among it.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<an::consumer::ImageAsset>
demosaicViewImage(const an::consumer::EngineSession& session, const an::consumer::ImageAsset& mosaic,
                  an::DemosaicMethod method, std::stop_token stop);

/// Takes a decoded frame over as an image of its own representation, under an execution the caller holds: with no copy
/// when its memory is the engine's and in this host's byte order, and through one planar copy otherwise - a frame
/// whose samples a decoder or a mapping owns, or whose bytes are in the other order, among them.
///
/// @param engine the engine context the execution runs in, which owns the image.
/// @param execution the caller's admitted execution.
/// @param frame the decoded frame, taken over.
/// @param metadata the frame's metadata, taken over.
/// @param planar whether the image must keep each channel in a plane of its own, so an interleaved frame of several
///        channels is copied into planes rather than adopted.
/// @return the image; `ErrorCode::Unsupported` for samples outside the eight this engine processes; or what the copy
///         refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<an::consumer::AnyImage>
importViewFrame(const an::EngineContext& engine, const an::ExecutionContext& execution, an::RawFrame&& frame,
                an::ImageMetadata&& metadata, bool planar = false);

/// Demosaics a mosaic every sample of which means something, or whose non-finite samples alone mean nothing, under an
/// execution the caller holds, into a colour image of its representation: what `demosaicViewImage` runs, for a caller
/// that is already admitted. A mosaic of the second kind - a light calibrated in memory - makes a colour image under
/// the same rule, each colour pixel estimated from a non-finite sample written non-finite in every colour.
///
/// @param engine the engine context the execution runs in, which owns the colour image.
/// @param execution the caller's admitted execution.
/// @param mosaic the mosaic: one channel of samples.
/// @param method the method chosen; over a mosaic of the other shape it runs as `demosaicMethodForMosaic` gives it.
/// @param pattern the filter array the samples were measured through, stated by the caller; nothing demosaics under
///        the one the image's description states.
/// @return the colour image, or what the demosaic refused - an image with no filter array to demosaic under among it,
///         and `ErrorCode::IncompatibleArguments` for a mosaic stating any other validity rule, whose colour image
///         needs the mask `demosaicViewFrameWithValidity` returns beside it.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<an::consumer::AnyImage>
demosaicViewFrame(const an::EngineContext& engine, const an::ExecutionContext& execution,
                  const an::consumer::AnyImage& mosaic, an::DemosaicMethod method,
                  const std::optional<an::CfaDescriptor>& pattern = std::nullopt);

/// A colour image a demosaic made, and the mask that answers for it.
struct ViewDemosaic
{
    /// The colour image.
    an::consumer::AnyImage image;

    /// Which of its pixels mean something - one plane the three colours share - when the mosaic stated a validity rule
    /// other than every sample meaning something; nothing when every sample of both does.
    std::optional<an::ValidityMask> validity{};
};

/// Demosaics a mosaic under an execution the caller holds, honouring which of its samples mean something: its rule,
/// and its mask when the rule names one. A colour pixel estimated from a sample that means nothing is cleared in the
/// mask the result carries, and the colour image states that the mask decides.
///
/// @param engine the engine context the execution runs in, which owns the colour image and its mask.
/// @param execution the caller's admitted execution.
/// @param mosaic the mosaic: one channel of samples.
/// @param validity the mosaic's mask, or null when its rule names none.
/// @param method the method chosen; over a mosaic of the other shape it runs as `demosaicMethodForMosaic` gives it.
/// @param pattern the filter array the samples were measured through, stated by the caller; nothing demosaics under
///        the one the image's description states.
/// @return the colour image and its mask; `ErrorCode::IncompatibleArguments` for a mosaic whose rule names a mask it
///         was not given; or what the demosaic refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<ViewDemosaic>
demosaicViewFrameWithValidity(const an::EngineContext& engine, const an::ExecutionContext& execution,
                              const an::consumer::AnyImage& mosaic, const an::ValidityMask* validity,
                              an::DemosaicMethod method,
                              const std::optional<an::CfaDescriptor>& pattern = std::nullopt);

/// A copy of an image in another representation, under an execution the caller holds: converted at corresponding full
/// scales to the nearest value, a floating image in a physical or sensor-code domain read as a fraction of full scale,
/// and the conversion recorded in the copy's lineage - the conversion `writeViewImage` makes before it writes.
///
/// @param engine the engine context the execution runs in, which owns the copy.
/// @param execution the caller's admitted execution.
/// @param image the image.
/// @param target the representation the copy has.
/// @return the copy, or what the conversion refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<an::consumer::AnyImage>
convertViewFrame(const an::EngineContext& engine, const an::ExecutionContext& execution,
                 const an::consumer::AnyImage& image, an::SampleType target);

/// One channel's statistics, as the static preview's overlay shows them.
struct ViewChannelStatistics
{
    /// The smallest valid sample.
    double minimum = 0.0;

    /// The largest valid sample.
    double maximum = 0.0;

    /// The mean.
    double mean = 0.0;

    /// The exact median.
    double median = 0.0;

    /// The population standard deviation.
    double standardDeviation = 0.0;
};

/// An image's statistics, channel by channel, alpha aside.
struct ViewImageStatistics
{
    /// How many channels were measured.
    std::uint32_t channels = 0u;

    /// Each channel's statistics.
    std::array<ViewChannelStatistics, 3u> channel{};
};

/// Measures an image for the static preview's overlay, in the image's own units.
///
/// @param session the process's session.
/// @param image the image.
/// @param stop the request's stop.
/// @return the statistics, or what the engine refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<ViewImageStatistics>
measureViewImage(const an::consumer::EngineSession& session, const an::consumer::ImageAsset& image,
                 std::stop_token stop);

/// Whether a container can state a mosaic's colour filter array, and so hold the mosaic as the mosaic it is: XISF and
/// TIFF state a two-by-two pattern and an X-Trans sensor's six-by-six table alike; FITS states a two-by-two pattern
/// only, in its one pattern card, and its codec refuses a tabulated mosaic; PNG and JPEG state none, and their codecs
/// refuse such a frame.
///
/// @param format the container.
/// @param pattern the mosaic's filter array.
/// @return true when the container holds the mosaic.
[[nodiscard]] AFTERNIGHTCONTROLS_API bool containerHoldsFilterArray(an::CodecFormat format,
                                                                    an::CfaPattern pattern) noexcept;

/// Writes an image to a file through the output transaction: whole, or absent with nothing beside it.
///
/// The image is written in the representation the intent states. When that is not the image's own, a copy is converted
/// to it first at corresponding full scales, to the nearest value, and the conversion is recorded in the copy's
/// lineage; a floating image in a physical or sensor-code domain is converted as a fraction of full scale, as the view
/// shows it. A mosaic is refused before any work for a container that cannot state its filter array - an X-Trans mosaic
/// for FITS among them - naming what can.
///
/// @param session the process's session.
/// @param image the image, read under a lease held for the write.
/// @param path the file; its extension names the container.
/// @param intent how the image is written: representation, compression and cards.
/// @param stop the request's stop.
/// @return success; `ErrorCode::Unsupported` for a container nothing writes or a mosaic it cannot hold; or what the
///         conversion or the write refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Status writeViewImage(const an::consumer::EngineSession& session,
                                                               const an::consumer::ImageAsset& image,
                                                               const QString& path, const an::OutputIntent& intent,
                                                               std::stop_token stop);

/// Turns or mirrors an image exactly into a new asset of the session: every sample is one of the source's, a mosaic's
/// filters stay under the samples they measured, and an explicit validity mask turns with the samples.
///
/// The result's description is restated, its astrometric solution invalidated, its header cards dropped and any output
/// decision withdrawn, as for every geometry operation of the engine; its validity rule is kept.
///
/// @param session the process's session.
/// @param image the image, read for the call - under the writer that will replace it, when the view edits.
/// @param validity the image's explicit validity mask, or null.
/// @param orientation the turn or mirror.
/// @param stop the edit's stop.
/// @return the new image; `ErrorCode::Cancelled` when stopped; or what the engine refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<an::consumer::ImageAsset>
orientViewImage(const an::consumer::EngineSession& session, const an::consumer::AnyImage& image,
                const an::ValidityMask* validity, an::ImageOrientation orientation, std::stop_token stop);

/// The world-coordinate solution an image's own header cards state, for a frame of its size.
///
/// @param metadata the image's metadata.
/// @param width the image's width.
/// @param height the image's height.
/// @return the solution, or nothing when the image states none or one the engine refuses to read.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::optional<an::WcsSolution>
viewImageSolution(const an::ImageMetadata& metadata, std::uint32_t width, std::uint32_t height);

/// The most stars the overlay's detection keeps.
inline constexpr std::uint32_t kViewOverlayMaximumStars = 5000u;

/// Detects the stars of a solved image for the view's overlay: the overlay's green-then-red channel policy, counting
/// the stars the solution places on the sky, with a search radius of five pixels, a noise sigma of one, a minimum
/// roundness of 0.4 and a Gaussian profile.
///
/// @param session the process's session.
/// @param image the image.
/// @param solution the image's solution.
/// @param stop the detection's stop.
/// @return the detection; `ErrorCode::Cancelled` when stopped; `ErrorCode::Unsupported` for a mosaic; or what the
///         engine refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<an::StarDetectionChannelResult>
detectViewOverlayStars(const an::consumer::EngineSession& session, const an::consumer::ImageAsset& image,
                       const an::WcsSolution& solution, std::stop_token stop);

} // namespace an::ui
