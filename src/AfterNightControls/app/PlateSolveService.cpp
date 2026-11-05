// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/PlateSolveService.h"

#include "app/AppContextHelpers.h"
#include "app/GaiaCatalogSettings.h"
#include "app/GaiaCatalogSource.h"
#include "app/ObservationEpochUtils.h"
#include "app/PlateSolveConfiguration.h"
#include "app/PlateSolveUtils.h"
#include "controls/ImageViewFiles.h"
#include "session/IEngineSessionProvider.h"

#include <AfterNightPlatform/Logging.h>

#include <AfterNightEngine/Algorithms/Astrometry/AstrometryHints.h>
#include <AfterNightEngine/Algorithms/Astrometry/PlateSolver.h>
#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>
#include <AfterNightEngine/Algorithms/Catalog/CatalogRecord.h>
#include <AfterNightEngine/Algorithms/Catalog/Healpix.h>
#include <AfterNightEngine/Algorithms/Stars/StarDetection.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QMetaType>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QThreadPool>
#include <QTimer>
#include <verdigris/wobjectimpl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace
{

using an::ui::CancellationToken;

enum class SolverKind
{
    InternalGaia,
    Astap,
    SolveField,
    AstrometryWeb
};

/// The stars the internal solver detects in one plane, brightest first, and how many it fits at most.
constexpr std::uint32_t kInternalGaiaDetectionStars = 512u;

/// How many image stars and catalog stars the internal solver matches.
constexpr std::uint32_t kInternalGaiaMatchedImageStars = 256u;
constexpr std::uint32_t kInternalGaiaMatchedCatalogStars = 512u;

/// The fewest stars a plane must yield before the internal solver tries to match it.
constexpr std::size_t kInternalGaiaMinimumStars = 3u;

/// How far a hinted solve's catalog reaches past its search radius, in catalog query cones: the solve refines its
/// centre toward the field it finds, and its last cone must still lie inside the catalog it was given.
constexpr double kHintedCatalogMarginCones = 1.5;

/// How far a blind trial centre's catalog reaches, in catalog query cones.
constexpr double kBlindCatalogCones = 2.0;

/// The trial a blind search accepts at once: as many matches and as small a residual as the engine's own search
/// stops at.
constexpr std::uint32_t kBlindAcceptMatches = 12u;
constexpr double kBlindAcceptRmsPixels = 1.0;

constexpr int kAstrometryWebMaxPollAttempts = 120;

/// A failure the plate solve reports in its own words.
[[nodiscard]] an::Status refusal(const an::ErrorCode code, const QString& message)
{
    return an::Status::failure(code, an::StatusDetail{.message = message.toStdString()});
}

/// An engine failure as a person reads it.
[[nodiscard]] QString describeStatus(const an::Status& status)
{
    return QString::fromUtf8(status.message());
}

/// A path as the engine reads it.
[[nodiscard]] std::string enginePath(const QString& path)
{
    return path.toUtf8().toStdString();
}

/// The process's session, or null when no context lends one.
[[nodiscard]] const an::consumer::EngineSession* currentSession()
{
    if (auto* provider = process_utils::currentEngineSessionProvider())
    {
        return &provider->engineSession();
    }
    return nullptr;
}

/// A stop source that follows a cancellation token, for engine work that observes a `std::stop_token`: the token is
/// polled every few milliseconds until the bridge goes.
class CancellationBridge
{
public:
    explicit CancellationBridge(const CancellationToken& token)
        : m_watcher([this, &token](const std::stop_token own) {
              while (!own.stop_requested())
              {
                  if (token.isCancellationRequested())
                  {
                      m_source.request_stop();
                      return;
                  }
                  std::this_thread::sleep_for(std::chrono::milliseconds(20));
              }
          })
    {
    }

    [[nodiscard]] std::stop_token token() const noexcept
    {
        return m_source.get_token();
    }

private:
    std::stop_source m_source;
    std::jthread m_watcher;
};

/// The image a solve is about: its metadata and the frame's size.
struct SolveSubject
{
    an::ImageMetadata metadata;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
};

/// The image's cards.
[[nodiscard]] std::span<const an::CodecCard> cardsOf(const SolveSubject& subject)
{
    return subject.metadata.codec().cards();
}

/// The position a solve is hinted with: the request's own, else - when the request allows it - the first pointing the
/// image's cards state, in the order the engine reads a plate solve's hint in.
[[nodiscard]] std::optional<an::SkyCoordinate> positionHint(const an::ui::PlateSolveRequest& request,
                                                            const SolveSubject& subject)
{
    if (request.hintRaDeg.has_value() && request.hintDecDeg.has_value())
    {
        const an::SkyCoordinate hint{.rightAscensionDegrees = an::normalizeRightAscensionDegrees(*request.hintRaDeg),
                                     .declinationDegrees = *request.hintDecDeg};
        if (an::isSkyCoordinate(hint))
        {
            return hint;
        }
        return std::nullopt;
    }
    if (!request.allowMetadataPositionHints)
    {
        return std::nullopt;
    }
    const an::Result<an::PointingHint> pointing = an::readPointingHint(cardsOf(subject), an::plateSolveHintCards());
    if (!pointing.hasValue())
    {
        return std::nullopt;
    }
    return pointing->direction;
}

/// Whether a stated optical figure says something.
[[nodiscard]] bool positiveFinite(const std::optional<double>& value)
{
    return value.has_value() && std::isfinite(*value) && *value > 0.0;
}

/// What a solve knows about the optics: the image's cards, with the request's and then the settings' focal length and
/// pixel size written over them.
[[nodiscard]] an::OpticalHints opticsOf(const an::ui::PlateSolveRequest& request,
                                        const an::ui::PlateSolveSettings& settings, const SolveSubject& subject)
{
    an::OpticalHints optics = an::readOpticalHints(cardsOf(subject));
    if (positiveFinite(request.focalLengthMm))
    {
        optics.focalLengthMillimetres = request.focalLengthMm;
    }
    else if (positiveFinite(settings.focalLengthMm))
    {
        optics.focalLengthMillimetres = settings.focalLengthMm;
    }
    if (positiveFinite(request.pixelSizeUm))
    {
        optics.pixelSizeMicrometres = request.pixelSizeUm;
    }
    else if (positiveFinite(settings.pixelSizeUm))
    {
        optics.pixelSizeMicrometres = settings.pixelSizeUm;
    }
    return optics;
}

/// The pixel scale the optics imply, which a solve takes as its scale hint.
[[nodiscard]] std::optional<double> scaleHint(const an::OpticalHints& optics)
{
    const an::Result<double> scale = an::resolvePlateScaleHint(optics);
    if (!scale.hasValue())
    {
        return std::nullopt;
    }
    return *scale;
}

[[nodiscard]] QString sha1Hex(const QString& value)
{
    return QString::fromLatin1(QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha1).toHex());
}

[[nodiscard]] QString sanitizeWorkDirComponent(QString value)
{
    static const QString invalid = QStringLiteral("<>:\"/\\|?*");
    for (const auto& ch : invalid)
    {
        value.replace(ch, QLatin1Char('_'));
    }

    value = value.trimmed();
    value.replace(QLatin1Char(' '), QLatin1Char('_'));
    while (value.contains(QStringLiteral("__")))
    {
        value.replace(QStringLiteral("__"), QStringLiteral("_"));
    }

    return value.isEmpty() ? QStringLiteral("request") : value;
}

[[nodiscard]] QString solverWorkDir(const an::ui::PlateSolveRequest& request, const QString& input_path,
                                    const QString& solver_name)
{
    const QString root = request.workDirectoryRoot.trimmed().isEmpty() ? QDir::tempPath() : request.workDirectoryRoot;
    const QString request_tag =
        request.requestTag.trimmed().isEmpty() ? QFileInfo(input_path).completeBaseName() : request.requestTag;
    const QString safe_request_tag = sanitizeWorkDirComponent(request_tag);
    const QString suffix = sha1Hex(request_tag + QStringLiteral("|") + input_path);
    return QDir(root).filePath(
        QStringLiteral(".afternight/plate_solve/%1/%2_%3").arg(safe_request_tag, solver_name, suffix.left(12)));
}

void cleanupWorkDir(const QString& work_dir)
{
    QDir dir(work_dir);
    if (dir.exists())
    {
        dir.removeRecursively();
    }
}

[[nodiscard]] QString writeSolveFieldConfig(const QString& work_dir, const QString& index_path)
{
    const QString trimmed = index_path.trimmed();
    if (trimmed.isEmpty())
    {
        return QString();
    }

    const QString config_path = QDir(work_dir).filePath(QStringLiteral("astrometry.cfg"));
    QFile cfg(config_path);
    if (!cfg.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return QString();
    }

    QByteArray content("inparallel\n");
    content += "cpulimit 0\n";
    content += "add_path ";
    content += trimmed.toUtf8();
    content += "\nautoindex\n";
    cfg.write(content);
    cfg.close();
    return config_path;
}

[[nodiscard]] std::vector<SolverKind> solverOrder(const an::ui::PlateSolveSettings& settings)
{
    std::vector<SolverKind> order = {
        SolverKind::InternalGaia,
        SolverKind::Astap,
        SolverKind::SolveField,
        SolverKind::AstrometryWeb,
    };

    const QString preferred = an::ui::preferredConfiguredPlateSolveSolver(settings);
    if (preferred == QStringLiteral("astap"))
    {
        std::rotate(order.begin(), order.begin() + 1, order.begin() + 2);
    }
    else if (preferred == QStringLiteral("astrometry_local"))
    {
        std::rotate(order.begin(), order.begin() + 2, order.begin() + 3);
    }
    else if (preferred == QStringLiteral("astrometry_web"))
    {
        std::rotate(order.begin(), order.begin() + 3, order.end());
    }

    return order;
}

/// The figures a window shows of a solution: the square-pixel scale, the field's centre, the orientation and the
/// field of view.
void populateDerivedFields(an::ui::PlateSolveResult& result)
{
    if (!result.solution.has_value())
    {
        return;
    }
    const an::WcsSolution& solution = *result.solution;
    result.imageScaleArcsecPerPixel = solution.meanPixelScaleArcsecPerPixel();
    if (const an::Result<an::SkyCoordinate> center = solution.fieldCenter(); center.hasValue())
    {
        result.fieldCenterRaDeg = center->rightAscensionDegrees;
        result.fieldCenterDecDeg = center->declinationDegrees;
    }
    result.rotationDeg = solution.orientationDegrees();
    const an::WcsFieldOfView field = solution.fieldOfViewArcmin();
    result.fovWidthArcmin = field.widthArcmin;
    result.fovHeightArcmin = field.heightArcmin;
}

/// The focal length the solved scale implies for the pixel the optics state, after any drizzle.
void populateDerivedFocalLength(an::ui::PlateSolveResult& result, const an::OpticalHints& optics)
{
    if (!std::isfinite(result.imageScaleArcsecPerPixel) || result.imageScaleArcsecPerPixel <= 0.0)
    {
        return;
    }
    const an::Result<double> pixel = an::resolveEffectivePixelSizeMicrometres(optics);
    if (!pixel.hasValue())
    {
        return;
    }
    const an::Result<double> focal = an::computeFocalLengthMillimetres(*pixel, result.imageScaleArcsecPerPixel);
    if (focal.hasValue())
    {
        result.derivedFocalLengthMm = *focal;
    }
}

[[nodiscard]] an::ui::PlateSolveResult embeddedWcsResult(an::WcsSolution solution, const an::OpticalHints& optics)
{
    an::ui::PlateSolveResult result;
    result.succeeded = true;
    result.solution = std::move(solution);
    result.solverUsed = QStringLiteral("embedded WCS");
    populateDerivedFields(result);
    populateDerivedFocalLength(result, optics);
    return result;
}

[[nodiscard]] an::ui::PlateSolveResult cancelledResult(const QString& message)
{
    an::ui::PlateSolveResult result;
    result.cancelled = true;
    result.errorMessage = message;
    return result;
}

[[nodiscard]] bool isFitsFile(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QStringLiteral("fits") || suffix == QStringLiteral("fit") || suffix == QStringLiteral("fts");
}

// ASTAP has trouble solving certain TIFF/non-FITS formats even when star detection
// works, because the pixel data representation differs from FITS float32.
// When the input is not FITS, convert it to a temporary FITS in the work directory.
[[nodiscard]] QString ensureFitsForAstap(const an::consumer::EngineSession& session, const QString& original_path,
                                         const QString& work_dir, const std::stop_token& stop)
{
    if (isFitsFile(original_path))
    {
        return QString();
    }

    an::Result<an::ui::ViewReadResult> read =
        an::ui::readViewImage(session, an::ui::ViewReadRequest{.path = original_path}, stop, nullptr);
    if (!read.hasValue())
    {
        LOG_WARNING("Failed to convert '{}' to FITS for ASTAP: {}", original_path.toStdString(),
                    describeStatus(read.status()).toStdString());
        return QString();
    }
    const an::Result<an::OutputIntent> intent = an::ui::plateSolveInputIntent();
    if (!intent.hasValue())
    {
        return QString();
    }
    const QString fits_path =
        QDir(work_dir).filePath(QFileInfo(original_path).completeBaseName() + QStringLiteral(".fits"));
    const an::Status written = an::ui::writeViewImage(session, read->asset, fits_path, *intent, stop);
    if (written.failed())
    {
        LOG_WARNING("Failed to convert '{}' to FITS for ASTAP: {}", original_path.toStdString(),
                    describeStatus(written).toStdString());
        return QString();
    }
    LOG_INFO("Converted '{}' to temporary FITS for ASTAP: '{}'", original_path.toStdString(), fits_path.toStdString());
    return fits_path;
}

/// The stars of one plane of a decoded image, brightest first, as the internal solver detects them: a search radius of
/// five pixels, a noise sigma of 2.5, a minimum roundness of 0.45, relaxed checks and one unit of fit iteration, at
/// most 512 stars.
[[nodiscard]] an::Result<std::vector<an::PlateSolveStar>> detectSolverStars(const an::consumer::OperationScope& scope,
                                                                            const an::consumer::AssetReadLease& lease,
                                                                            const std::uint32_t channel)
{
    std::optional<an::ValidityMaskView<const std::uint8_t>> maskView;
    if (const an::ValidityMask* mask = lease.validity(); mask != nullptr)
    {
        maskView = mask->view();
    }
    return lease.visit([&](const auto& typed) -> an::Result<std::vector<an::PlateSolveStar>> {
        const std::optional<an::ImageDescriptor> descriptor = typed.descriptor();
        const auto view = typed.view();
        if (!descriptor.has_value() || !view.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, QStringLiteral("the image holds nothing"));
        }
        // The frame view borrows its layout, so the layout it is given outlives the detection.
        const an::ImageLayout layout = view->layout();
        an::KernelValidityView validity;
        validity.descriptor = typed.metadata().semantics().validity();
        validity.mask = maskView.has_value() ? &*maskView : nullptr;
        an::Result<an::StarFrameView> frame =
            an::StarFrameView::create(view->data(), layout.totalBytes(), layout, *descriptor, validity);
        if (!frame.hasValue())
        {
            return frame.status();
        }
        an::StarDetectionRequest request;
        request.searchRadius = 5u;
        request.noiseSigma = 2.5;
        request.minimumRoundness = 0.45;
        request.relaxedChecks = true;
        request.convergence = 1u;
        request.model = an::PsfModel::Gaussian;
        request.maximumStars = kInternalGaiaDetectionStars;
        an::Result<an::StarDetectionPlan> plan = an::planStarDetection(request, *descriptor, layout, channel);
        if (!plan.hasValue())
        {
            return plan.status();
        }
        an::StarDetectionResult detected;
        if (an::Status ran = an::detectStars(scope.execution(), *frame, *plan, detected); ran.failed())
        {
            return ran;
        }
        std::vector<an::PlateSolveStar> stars;
        stars.reserve(detected.stars.size());
        for (const an::DetectedStar& star : detected.stars)
        {
            stars.push_back(an::PlateSolveStar{.position = {.x = star.positionX, .y = star.positionY}});
        }
        return stars;
    });
}

/// The root-mean-square residual of a solve's final correspondences, in pixels; infinite for a solve without any.
[[nodiscard]] double finalRmsResidualPixels(const an::PlateSolveResult& solved)
{
    if (solved.matches.empty())
    {
        return std::numeric_limits<double>::infinity();
    }
    double sumSquares = 0.0;
    for (const an::PlateSolveMatch& match : solved.matches)
    {
        sumSquares += match.residualPixels * match.residualPixels;
    }
    return std::sqrt(sumSquares / static_cast<double>(solved.matches.size()));
}

/// Whether one blind trial is a better answer than the best before it, as the engine's own search ranks its trials:
/// more matches, then a smaller final residual; an equal answer keeps the earlier trial.
[[nodiscard]] bool betterSolve(const an::PlateSolveResult& candidate, const an::PlateSolveResult& best)
{
    if (candidate.diagnostics.matchedStars != best.diagnostics.matchedStars)
    {
        return candidate.diagnostics.matchedStars > best.diagnostics.matchedStars;
    }
    return finalRmsResidualPixels(candidate) < finalRmsResidualPixels(best);
}

/// Whether an engine failure means this trial found nothing rather than that the solve cannot go on.
[[nodiscard]] bool trialFoundNothing(const an::Status& status)
{
    const an::ErrorCode code = status.code();
    return code == an::ErrorCode::EmptySelection || code == an::ErrorCode::InvalidData ||
           code == an::ErrorCode::OutOfRange;
}

/// The internal solver against one plane's stars: one solve over a regional catalog about the hint, or - with no
/// hint - a solve at each centre of the blind grid the engine plans, each over the catalog about that centre.
[[nodiscard]] an::Result<an::PlateSolveResult> solveAgainstGaia(const an::consumer::OperationScope& scope,
                                                                const an::ui::GaiaCatalogSource& catalog,
                                                                std::span<const an::PlateSolveStar> stars,
                                                                an::PlateSolveRequest request,
                                                                const std::optional<double> epochYear)
{
    const an::Result<an::PlateSolvePlan> plan = an::planPlateSolve(request);
    if (!plan.hasValue())
    {
        return plan.status();
    }
    const double cone = plan->catalogQueryRadiusDegrees();
    if (request.initialCenter.has_value())
    {
        const double radius = (std::min)(180.0, request.searchRadiusDegrees + cone * kHintedCatalogMarginCones);
        an::Result<an::SkyCatalog> region =
            catalog.regionCatalog(scope.execution(), *request.initialCenter, radius, epochYear, request.magnitudeLimit);
        if (!region.hasValue())
        {
            return region.status();
        }
        return an::solvePlate(scope.execution(), *region, stars, request);
    }

    const an::HealpixGrid grid{.order = plan->blindHealpixOrder(), .ordering = an::HealpixOrdering::Ring};
    const std::uint32_t centers = an::healpixPixelCount(grid);
    std::optional<an::PlateSolveResult> best;
    request.searchRadiusDegrees = 0.0;
    for (std::uint32_t index = 0u; index < centers; ++index)
    {
        if (an::Status stopped = scope.execution().checkCancellation(); stopped.failed())
        {
            return stopped;
        }
        const an::Result<an::SkyCoordinate> center =
            an::healpixPixelCenter(an::HealpixPixelIndex{.grid = grid, .value = index});
        if (!center.hasValue())
        {
            continue;
        }
        an::Result<an::SkyCatalog> region =
            catalog.regionCatalog(scope.execution(), *center, (std::min)(180.0, cone * kBlindCatalogCones), epochYear,
                                  request.magnitudeLimit);
        if (!region.hasValue())
        {
            if (trialFoundNothing(region.status()))
            {
                continue;
            }
            return region.status();
        }
        request.initialCenter = *center;
        an::Result<an::PlateSolveResult> trial = an::solvePlate(scope.execution(), *region, stars, request);
        if (!trial.hasValue())
        {
            if (trialFoundNothing(trial.status()))
            {
                continue;
            }
            return trial.status();
        }
        if (!best.has_value() || betterSolve(*trial, *best))
        {
            best = std::move(*trial);
        }
        if (best->diagnostics.matchedStars >= kBlindAcceptMatches &&
            finalRmsResidualPixels(*best) <= kBlindAcceptRmsPixels)
        {
            break;
        }
    }
    if (!best.has_value())
    {
        return refusal(an::ErrorCode::EmptySelection,
                       QStringLiteral("no centre of the blind search matched the field"));
    }
    return std::move(*best);
}

[[nodiscard]] an::ui::PlateSolveResult runInternalGaiaSolver(const an::consumer::EngineSession& session,
                                                             const an::ui::PlateSolveRequest& request,
                                                             const an::ui::PlateSolveSettings& settings,
                                                             const SolveSubject& subject,
                                                             const CancellationToken& token)
{
    an::ui::PlateSolveResult result;
    const int timeout_seconds = (std::max)(1, settings.timeoutSeconds);
    an::ui::PlateSolveRunGuard run_guard{std::chrono::seconds(timeout_seconds)};
    const auto timeoutResult = [&]() {
        an::ui::PlateSolveResult timed_out;
        timed_out.errorMessage =
            QStringLiteral("Internal Gaia solver timed out after %1 seconds.").arg(timeout_seconds);
        return timed_out;
    };
    const auto stoppedResult = [&]() {
        return token.isCancellationRequested() ? cancelledResult(QStringLiteral("Internal Gaia solver cancelled."))
                                               : timeoutResult();
    };

    if (token.isCancellationRequested())
    {
        return cancelledResult(QStringLiteral("Internal Gaia solver cancelled."));
    }

    if (request.imagePath.trimmed().isEmpty())
    {
        result.errorMessage = QStringLiteral("Internal Gaia solver requires an image file on disk.");
        return result;
    }

    const QString catalog_dir = an::ui::resolveGaiaCatalogDirectory(settings);
    LOG_INFO("[Internal Gaia] Starting plate solve for '{}' with catalog '{}'", request.imagePath.toStdString(),
             catalog_dir.toStdString());
    switch (an::ui::inspectGaiaCatalogDirectory(catalog_dir))
    {
    case an::ui::GaiaCatalogDirectoryStatus::Ready:
        break;

    case an::ui::GaiaCatalogDirectoryStatus::UnsupportedFormat:
        result.errorMessage =
            QStringLiteral("Internal Gaia catalog is a tile catalog and must be re-downloaded as a chunk catalog: %1")
                .arg(catalog_dir);
        return result;

    case an::ui::GaiaCatalogDirectoryStatus::IncompleteInstall:
        result.errorMessage =
            QStringLiteral(
                "Internal Gaia catalog installation is incomplete. Re-download or finish installing the pack: %1")
                .arg(catalog_dir);
        return result;

    case an::ui::GaiaCatalogDirectoryStatus::Invalid:
        result.errorMessage = QStringLiteral("Internal Gaia catalog directory is invalid: %1").arg(catalog_dir);
        return result;

    case an::ui::GaiaCatalogDirectoryStatus::Missing:
    default:
        result.errorMessage = QStringLiteral("Internal Gaia catalog directory is not configured: %1").arg(catalog_dir);
        return result;
    }

    const CancellationBridge stop(token);
    an::Result<an::consumer::OperationScope> scope = session.admit(an::consumer::OperationRequest{
        .purpose = an::consumer::OperationPurpose::Apply, .stopToken = stop.token(), .deadline = run_guard.deadline()});
    if (!scope.hasValue())
    {
        if (run_guard.shouldStop(token.isCancellationRequested()))
        {
            return stoppedResult();
        }
        result.errorMessage =
            QStringLiteral("Internal Gaia solver could not start: %1").arg(describeStatus(scope.status()));
        return result;
    }

    an::Result<an::ui::GaiaCatalogSource> catalog = an::ui::GaiaCatalogSource::open(scope->execution(), catalog_dir);
    if (!catalog.hasValue())
    {
        result.errorMessage = QStringLiteral("Failed to load Gaia catalog: %1").arg(describeStatus(catalog.status()));
        return result;
    }
    if (run_guard.shouldStop(token.isCancellationRequested()))
    {
        return stoppedResult();
    }

    std::optional<an::SkyCoordinate> hint;
    if (settings.useHints)
    {
        hint = positionHint(request, subject);
    }
    std::optional<double> scale = scaleHint(opticsOf(request, settings, subject));
    if (!scale.has_value())
    {
        if (const an::Result<an::WcsSolution> existing =
                an::ui::readImageWcsSolution(cardsOf(subject), subject.width, subject.height);
            existing.hasValue())
        {
            scale = existing->meanPixelScaleArcsecPerPixel();
        }
    }

    // The image is read in the session, a mosaic demosaiced so that its stars are round.
    scope->end();
    an::Result<an::ui::ViewReadResult> read = an::ui::readViewImage(
        session, an::ui::ViewReadRequest{.path = request.imagePath, .demosaic = an::DemosaicMethod::Bilinear},
        stop.token(), nullptr);
    if (!read.hasValue())
    {
        if (run_guard.shouldStop(token.isCancellationRequested()))
        {
            return stoppedResult();
        }
        result.errorMessage = QStringLiteral("Failed to load image pixels for internal Gaia solving: %1")
                                  .arg(describeStatus(read.status()));
        return result;
    }
    an::Result<an::consumer::AssetReadLease> lease = read->asset.read(stop.token());
    if (!lease.hasValue())
    {
        return stoppedResult();
    }
    const std::optional<an::ImageDescriptor> described = read->asset.descriptor();
    const int channel_count = described.has_value() ? static_cast<int>(described->extent().channelCount()) : 0;
    const std::vector<int> detection_channels = an::ui::internalGaiaDetectionChannels(channel_count);
    if (detection_channels.empty())
    {
        result.errorMessage = QStringLiteral("Internal Gaia solver could not determine an image plane to analyze.");
        return result;
    }

    scope = session.admit(an::consumer::OperationRequest{
        .purpose = an::consumer::OperationPurpose::Apply, .stopToken = stop.token(), .deadline = run_guard.deadline()});
    if (!scope.hasValue())
    {
        if (run_guard.shouldStop(token.isCancellationRequested()))
        {
            return stoppedResult();
        }
        result.errorMessage =
            QStringLiteral("Internal Gaia solver could not start: %1").arg(describeStatus(scope.status()));
        return result;
    }

    const std::optional<double> epoch_year = an::ui::resolveObservationEpochYear(subject.metadata.acquisition());
    an::PlateSolveRequest solve;
    solve.imageWidth = request.imageWidth > 0 ? static_cast<std::uint32_t>(request.imageWidth) : subject.width;
    solve.imageHeight = request.imageHeight > 0 ? static_cast<std::uint32_t>(request.imageHeight) : subject.height;
    solve.initialCenter = hint;
    if (scale.has_value() && std::isfinite(*scale) && *scale > 0.0)
    {
        solve.approximateScaleArcsecPerPixel = *scale;
    }
    solve.searchRadiusDegrees = std::clamp(settings.searchRadius, 0.0, 180.0);
    solve.maximumImageStars = kInternalGaiaMatchedImageStars;
    solve.maximumCatalogStars = kInternalGaiaMatchedCatalogStars;
    const double catalog_magnitude_limit = catalog->metadata().magnitudeLimit;
    solve.magnitudeLimit = std::isfinite(catalog_magnitude_limit) && catalog_magnitude_limit > 0.0
                               ? std::optional<double>(catalog_magnitude_limit)
                               : std::nullopt;

    bool detected_any_stars = false;
    for (size_t attempt_index = 0; attempt_index < detection_channels.size(); ++attempt_index)
    {
        if (run_guard.shouldStop(token.isCancellationRequested()))
        {
            return stoppedResult();
        }

        const int detection_channel = detection_channels[attempt_index];
        an::Result<std::vector<an::PlateSolveStar>> stars =
            detectSolverStars(*scope, *lease, static_cast<std::uint32_t>(detection_channel));
        if (!stars.hasValue())
        {
            if (run_guard.shouldStop(token.isCancellationRequested()))
            {
                return stoppedResult();
            }
            LOG_INFO("[Internal Gaia] Channel {} ({}/{}) detection failed: {}; trying next image plane",
                     detection_channel, attempt_index + 1, detection_channels.size(),
                     describeStatus(stars.status()).toStdString());
            continue;
        }
        if (stars->size() < kInternalGaiaMinimumStars)
        {
            LOG_INFO("[Internal Gaia] Channel {} ({}/{}) detected only {} stars; trying next image plane",
                     detection_channel, attempt_index + 1, detection_channels.size(), stars->size());
            continue;
        }

        detected_any_stars = true;
        LOG_INFO("[Internal Gaia] Channel {} ({}/{}) detected {} stars; matching with up to {} image stars and {} "
                 "catalog stars",
                 detection_channel, attempt_index + 1, detection_channels.size(), stars->size(),
                 solve.maximumImageStars, solve.maximumCatalogStars);
        an::Result<an::PlateSolveResult> solved = solveAgainstGaia(*scope, *catalog, *stars, solve, epoch_year);
        if (token.isCancellationRequested())
        {
            return cancelledResult(QStringLiteral("Internal Gaia solver cancelled."));
        }
        if (run_guard.timedOut())
        {
            return timeoutResult();
        }
        if (!solved.hasValue())
        {
            if (!trialFoundNothing(solved.status()))
            {
                result.errorMessage =
                    QStringLiteral("Internal Gaia solver failed: %1").arg(describeStatus(solved.status()));
                return result;
            }
            LOG_INFO("[Internal Gaia] Channel {} did not solve the field; trying next image plane", detection_channel);
            continue;
        }

        result.succeeded = true;
        result.solution = solved->solution;
        result.solverUsed = QStringLiteral("Internal Gaia");
        populateDerivedFields(result);
        LOG_INFO("[Internal Gaia] Plate solve succeeded on channel {} - matches: {}, scale: {:.4f} arcsec/px, "
                 "center: ({:.4f} deg, {:.4f} deg)",
                 detection_channel, solved->diagnostics.matchedStars, result.imageScaleArcsecPerPixel,
                 result.fieldCenterRaDeg, result.fieldCenterDecDeg);
        return result;
    }

    result.errorMessage = detected_any_stars ? QStringLiteral("Internal Gaia solver failed to match the field.")
                                             : QStringLiteral("Internal Gaia solver could not detect enough stars.");
    return result;
}

[[nodiscard]] an::ui::PlateSolveResult runAstapSolver(const an::consumer::EngineSession& session,
                                                      an::ui::SolverProcessRunner& runner, const QString& executable,
                                                      const an::ui::PlateSolveRequest& request,
                                                      const an::ui::PlateSolveSettings& settings,
                                                      const SolveSubject& subject, const CancellationToken& token)
{
    an::ui::PlateSolveResult result;
    const QString work_dir = solverWorkDir(request, request.imagePath, QStringLiteral("astap"));
    const auto cleanup = [&work_dir]() { cleanupWorkDir(work_dir); };
    if (!QDir().mkpath(work_dir))
    {
        result.errorMessage = QStringLiteral("Failed to create ASTAP work directory.");
        return result;
    }

    const QFileInfo input_info(request.imagePath);
    const QString work_input = QDir(work_dir).filePath(input_info.fileName());
    QFile::remove(work_input);
    if (!QFile::copy(request.imagePath, work_input))
    {
        result.errorMessage = QStringLiteral("Failed to create ASTAP working copy.");
        cleanup();
        return result;
    }

    // If the input is not FITS, convert to a temporary FITS (ASTAP handles FITS float32 reliably)
    QString converted_fits;
    {
        const CancellationBridge stop(token);
        converted_fits = ensureFitsForAstap(session, work_input, work_dir, stop.token());
    }
    const QString solver_input = converted_fits.isEmpty() ? work_input : converted_fits;

    QStringList args;
    args << QStringLiteral("-f") << solver_input << QStringLiteral("-r") << QString::number(settings.searchRadius)
         << QStringLiteral("-z") << QString::number((std::max)(0, settings.downsample)) << QStringLiteral("-wcs");

    if (settings.useHints)
    {
        if (const std::optional<an::SkyCoordinate> hint = positionHint(request, subject); hint.has_value())
        {
            args << QStringLiteral("-ra") << QString::number(hint->rightAscensionDegrees / 15.0, 'f', 8)
                 << QStringLiteral("-spd") << QString::number(hint->declinationDegrees + 90.0, 'f', 8);
        }

        if (const std::optional<double> scale = scaleHint(opticsOf(request, settings, subject)); scale.has_value())
        {
            const int image_w = request.imageWidth > 0 ? request.imageWidth : static_cast<int>(subject.width);
            if (image_w > 0)
            {
                const double fov_deg = *scale * image_w / 3600.0;
                args << QStringLiteral("-fov") << QString::number(fov_deg, 'f', 4);
            }
        }
    }

    LOG_INFO("[ASTAP] Starting plate solve for '{}'", request.imagePath.toStdString());
    LOG_INFO("[ASTAP] Command: {} {}", executable.toStdString(), args.join(QLatin1Char(' ')).toStdString());
    LOG_INFO("[ASTAP] Timeout: {}s, downsample: {}, search radius: {} deg", settings.timeoutSeconds,
             settings.downsample, settings.searchRadius);

    const an::ui::SolverProcessResult process_result =
        runner.run(executable, args, work_dir, settings.timeoutSeconds, token);
    if (process_result.was_cancelled)
    {
        LOG_WARNING("[ASTAP] Plate solve cancelled by user");
        cleanup();
        return cancelledResult(QStringLiteral("ASTAP cancelled."));
    }
    if (process_result.timed_out)
    {
        LOG_WARNING("[ASTAP] Plate solve timed out after {}s", settings.timeoutSeconds);
        result.errorMessage = QStringLiteral("ASTAP timed out.");
        cleanup();
        return result;
    }

    const QString header_path = QFileInfo(solver_input).path() + QDir::separator() +
                                QFileInfo(solver_input).completeBaseName() + QStringLiteral(".wcs");
    const auto header_content = runner.readFile(header_path);
    if (!header_content.has_value())
    {
        LOG_WARNING("[ASTAP] Failed - no .wcs solution file produced");
        result.errorMessage =
            QStringLiteral("ASTAP did not produce a .wcs solution file. stdout=%1 stderr=%2")
                .arg(QString::fromLocal8Bit(process_result.std_out), QString::fromLocal8Bit(process_result.std_err));
        cleanup();
        return result;
    }

    an::Result<an::WcsSolution> solution = an::ui::readSolvedWcsHeader(
        std::string_view(header_content->constData(), static_cast<std::size_t>(header_content->size())), subject.width,
        subject.height);
    if (!solution.hasValue())
    {
        const QString reason = describeStatus(solution.status());
        LOG_WARNING("[ASTAP] Failed - WCS solution file could not be read: {}", reason.toStdString());
        result.errorMessage = QStringLiteral("ASTAP solution file did not contain a valid WCS: %1").arg(reason);
        cleanup();
        return result;
    }

    result.succeeded = true;
    result.solution = std::move(*solution);
    result.solverUsed = QStringLiteral("ASTAP");
    populateDerivedFields(result);
    LOG_INFO("[ASTAP] Plate solve succeeded - scale: {:.4f} arcsec/px, center: ({:.4f} deg, {:.4f} deg)",
             result.imageScaleArcsecPerPixel, result.fieldCenterRaDeg, result.fieldCenterDecDeg);
    cleanup();
    return result;
}

[[nodiscard]] an::ui::PlateSolveResult runSolveFieldSolver(an::ui::SolverProcessRunner& runner,
                                                           const QString& executable,
                                                           const an::ui::PlateSolveRequest& request,
                                                           const an::ui::PlateSolveSettings& settings,
                                                           const SolveSubject& subject, const CancellationToken& token)
{
    an::ui::PlateSolveResult result;
    const QString work_dir = solverWorkDir(request, request.imagePath, QStringLiteral("solvefield"));
    const auto cleanup = [&work_dir]() { cleanupWorkDir(work_dir); };
    if (!QDir().mkpath(work_dir))
    {
        result.errorMessage = QStringLiteral("Failed to create solve-field work directory.");
        return result;
    }

    const QFileInfo input_info(request.imagePath);
    const QString work_input = QDir(work_dir).filePath(input_info.fileName());
    QFile::remove(work_input);
    if (!QFile::copy(request.imagePath, work_input))
    {
        result.errorMessage = QStringLiteral("Failed to create solve-field working copy.");
        cleanup();
        return result;
    }

    const QString solved_base = QStringLiteral("solution");
    const QString solved_fits = QDir(work_dir).filePath(QStringLiteral("solution.new.fits"));
    const QString solved_wcs = QDir(work_dir).filePath(QStringLiteral("solution.wcs"));

    QStringList args;
    args << QStringLiteral("--overwrite") << QStringLiteral("--no-plots") << QStringLiteral("--dir") << work_dir
         << QStringLiteral("--out") << solved_base << QStringLiteral("--new-fits") << solved_fits
         << QStringLiteral("--wcs") << solved_wcs << QStringLiteral("--downsample")
         << QString::number((std::max)(1, settings.downsample)) << QStringLiteral("--radius")
         << QString::number(settings.searchRadius);

    if (settings.useHints)
    {
        if (const std::optional<an::SkyCoordinate> hint = positionHint(request, subject); hint.has_value())
        {
            args << QStringLiteral("--ra") << QString::number(hint->rightAscensionDegrees, 'f', 8)
                 << QStringLiteral("--dec") << QString::number(hint->declinationDegrees, 'f', 8);
        }

        if (const std::optional<double> scale = scaleHint(opticsOf(request, settings, subject)); scale.has_value())
        {
            const double lo = *scale * 0.8;
            const double hi = *scale * 1.2;
            args << QStringLiteral("--scale-low") << QString::number(lo, 'f', 4) << QStringLiteral("--scale-high")
                 << QString::number(hi, 'f', 4) << QStringLiteral("--scale-units") << QStringLiteral("arcsecperpix");
        }
    }

    const QString config_path = writeSolveFieldConfig(work_dir, settings.indexPath);
    if (!config_path.isEmpty())
    {
        args << QStringLiteral("--config") << config_path;
    }

    args << work_input;

    LOG_INFO("[solve-field] Starting plate solve for '{}'", request.imagePath.toStdString());
    LOG_INFO("[solve-field] Command: {} {}", executable.toStdString(), args.join(QLatin1Char(' ')).toStdString());
    LOG_INFO("[solve-field] Timeout: {}s, downsample: {}, search radius: {} deg", settings.timeoutSeconds,
             settings.downsample, settings.searchRadius);

    const an::ui::SolverProcessResult process_result =
        runner.run(executable, args, work_dir, settings.timeoutSeconds, token);
    if (process_result.was_cancelled)
    {
        LOG_WARNING("[solve-field] Plate solve cancelled by user");
        cleanup();
        return cancelledResult(QStringLiteral("solve-field cancelled."));
    }
    if (process_result.timed_out)
    {
        LOG_WARNING("[solve-field] Plate solve timed out after {}s", settings.timeoutSeconds);
        result.errorMessage = QStringLiteral("solve-field timed out.");
        cleanup();
        return result;
    }

    // The header-only answer first, then the primary header of the solved copy, which holds the same cards.
    QString reason;
    for (const QString& answer : {solved_wcs, solved_fits})
    {
        const auto header_content = runner.readFile(answer);
        if (!header_content.has_value())
        {
            continue;
        }
        an::Result<an::WcsSolution> solution = an::ui::readSolvedWcsHeader(
            std::string_view(header_content->constData(), static_cast<std::size_t>(header_content->size())),
            subject.width, subject.height);
        if (solution.hasValue())
        {
            result.solution = std::move(*solution);
            break;
        }
        reason = describeStatus(solution.status());
    }

    if (!result.solution.has_value())
    {
        LOG_WARNING("[solve-field] Failed - no valid WCS solution produced");
        result.errorMessage =
            QStringLiteral("solve-field did not produce a valid WCS solution%1. stdout=%2 stderr=%3")
                .arg(reason.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(reason),
                     QString::fromLocal8Bit(process_result.std_out), QString::fromLocal8Bit(process_result.std_err));
        cleanup();
        return result;
    }

    result.succeeded = true;
    result.solverUsed = QStringLiteral("solve-field");
    populateDerivedFields(result);
    LOG_INFO("[solve-field] Plate solve succeeded - scale: {:.4f} arcsec/px, center: ({:.4f} deg, {:.4f} deg)",
             result.imageScaleArcsecPerPixel, result.fieldCenterRaDeg, result.fieldCenterDecDeg);
    cleanup();
    return result;
}

// --- Astrometry.net Web API solver ---

[[nodiscard]] QJsonObject parseJsonResponse(const QByteArray& data)
{
    QJsonParseError err;
    const auto doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
    {
        return {};
    }
    return doc.object();
}

/// The astrometry.net web service's solve as a state machine on the worker: log in, upload, wait for a job, wait for
/// its solution and download it. Each step starts when the reply before it finishes or a poll timer fires, inside one
/// event loop the solve runs until the machine finishes; a request that outlasts the timeout is aborted, and the
/// cancellation token is watched by a timer of its own.
class AstrometryWebSolve
{
public:
    AstrometryWebSolve(QUrl baseUrl, const std::chrono::milliseconds pollInterval, const int timeoutSeconds,
                       const CancellationToken& token)
        : m_baseUrl(std::move(baseUrl)), m_pollInterval(pollInterval),
          m_timeoutMs((std::max)(30, timeoutSeconds) * 1000), m_token(token)
    {
    }

    /// Runs the solve to its end.
    [[nodiscard]] an::ui::PlateSolveResult run(const QString& apiKey, const QString& imagePath,
                                               QJsonObject uploadSettings, const std::uint32_t width,
                                               const std::uint32_t height)
    {
        m_apiKey = apiKey;
        m_imagePath = imagePath;
        m_uploadSettings = std::move(uploadSettings);
        m_width = width;
        m_height = height;

        QTimer cancelWatch;
        cancelWatch.setInterval(100);
        QObject::connect(&cancelWatch, &QTimer::timeout, &m_loop, [this]() {
            if (m_token.isCancellationRequested())
            {
                finish(cancelledResult(m_cancelledMessage));
            }
        });
        cancelWatch.start();

        login();
        if (!m_finished)
        {
            m_loop.exec();
        }
        return m_result;
    }

private:
    [[nodiscard]] QUrl endpoint(const QString& path) const
    {
        return QUrl(m_baseUrl.toString() + path);
    }

    [[nodiscard]] static QNetworkRequest makeRequest(const QUrl& url)
    {
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("AfterNight/1.0"));
        return request;
    }

    /// Hands a reply to the machine: when it finishes, `next` runs with its body and whether it succeeded.
    void await(QNetworkReply* reply, std::function<void(const QByteArray&, bool)> next)
    {
        m_active = reply;
        auto* timeout = new QTimer(reply);
        timeout->setSingleShot(true);
        QObject::connect(timeout, &QTimer::timeout, reply, [reply]() { reply->abort(); });
        timeout->start(m_timeoutMs);
        QObject::connect(reply, &QNetworkReply::finished, &m_loop, [this, reply, next = std::move(next)]() {
            const QByteArray data = reply->readAll();
            const bool ok = reply->error() == QNetworkReply::NoError;
            reply->deleteLater();
            if (m_active == reply)
            {
                m_active = nullptr;
            }
            if (!m_finished)
            {
                next(data, ok);
            }
        });
    }

    void fail(const QString& message)
    {
        an::ui::PlateSolveResult failed;
        failed.errorMessage = message;
        finish(std::move(failed));
    }

    void finish(an::ui::PlateSolveResult result)
    {
        if (m_finished)
        {
            return;
        }
        m_finished = true;
        m_result = std::move(result);
        if (m_active)
        {
            m_active->abort();
        }
        m_loop.quit();
    }

    void login()
    {
        LOG_INFO("[Astrometry.net web] Authenticating with {}...", m_baseUrl.toString().toStdString());
        QJsonObject payload;
        payload[QStringLiteral("apikey")] = m_apiKey;
        const QByteArray body = QStringLiteral("request-json=").toUtf8() +
                                QJsonDocument(payload).toJson(QJsonDocument::Compact).toPercentEncoding();
        QNetworkRequest request = makeRequest(endpoint(QStringLiteral("/api/login")));
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
        m_cancelledMessage = QStringLiteral("Astrometry.net web: cancelled.");
        await(m_network.post(request, body), [this](const QByteArray& data, const bool ok) {
            if (!ok)
            {
                fail(QStringLiteral("Astrometry.net web: login request failed (network error)."));
                return;
            }
            const QJsonObject json = parseJsonResponse(data);
            if (json.value(QStringLiteral("status")).toString() != QStringLiteral("success"))
            {
                fail(QStringLiteral("Astrometry.net web: login failed - %1")
                         .arg(json.value(QStringLiteral("errormessage")).toString(QStringLiteral("invalid API key"))));
                return;
            }
            m_session = json.value(QStringLiteral("session")).toString();
            if (m_session.isEmpty())
            {
                fail(QStringLiteral("Astrometry.net web: login succeeded but no session returned."));
                return;
            }
            LOG_INFO("[Astrometry.net web] Authenticated successfully");
            upload();
        });
    }

    void upload()
    {
        LOG_INFO("[Astrometry.net web] Uploading image '{}' to server...",
                 QFileInfo(m_imagePath).fileName().toStdString());
        auto* image_file = new QFile(m_imagePath);
        if (!image_file->open(QIODevice::ReadOnly))
        {
            delete image_file;
            fail(QStringLiteral("Astrometry.net web: cannot open image file '%1'.").arg(m_imagePath));
            return;
        }
        m_uploadSettings[QStringLiteral("session")] = m_session;

        auto* multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
        image_file->setParent(multipart);

        QHttpPart json_part;
        json_part.setHeader(QNetworkRequest::ContentDispositionHeader,
                            QStringLiteral("form-data; name=\"request-json\""));
        json_part.setBody(QJsonDocument(m_uploadSettings).toJson(QJsonDocument::Compact));
        multipart->append(json_part);

        QHttpPart file_part;
        file_part.setHeader(
            QNetworkRequest::ContentDispositionHeader,
            QStringLiteral("form-data; name=\"file\"; filename=\"%1\"").arg(QFileInfo(m_imagePath).fileName()));
        file_part.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
        file_part.setBodyDevice(image_file);
        multipart->append(file_part);

        QNetworkReply* reply = m_network.post(makeRequest(endpoint(QStringLiteral("/api/upload"))), multipart);
        multipart->setParent(reply);
        await(reply, [this](const QByteArray& data, const bool ok) {
            if (!ok)
            {
                fail(QStringLiteral("Astrometry.net web: upload request failed (network error)."));
                return;
            }
            const QJsonObject json = parseJsonResponse(data);
            if (json.value(QStringLiteral("status")).toString() != QStringLiteral("success"))
            {
                fail(QStringLiteral("Astrometry.net web: upload failed - %1")
                         .arg(json.value(QStringLiteral("errormessage")).toString(QStringLiteral("unknown error"))));
                return;
            }
            m_submission = json.value(QStringLiteral("subid")).toInt(-1);
            if (m_submission < 0)
            {
                fail(QStringLiteral("Astrometry.net web: upload succeeded but no submission ID returned."));
                return;
            }
            LOG_INFO("[Astrometry.net web] Image uploaded successfully, submission_id={}", m_submission);
            m_attempts = 0;
            m_cancelledMessage = QStringLiteral("Astrometry.net web: cancelled while waiting for job.");
            schedule([this]() { pollSubmission(); });
        });
    }

    /// Runs a step after the poll interval.
    void schedule(std::function<void()> step)
    {
        QTimer::singleShot(m_pollInterval, &m_loop, [this, step = std::move(step)]() {
            if (!m_finished)
            {
                step();
            }
        });
    }

    void pollSubmission()
    {
        if (m_attempts++ >= kAstrometryWebMaxPollAttempts)
        {
            fail(QStringLiteral("Astrometry.net web: timed out waiting for job assignment."));
            return;
        }
        await(m_network.get(makeRequest(endpoint(QStringLiteral("/api/submissions/%1").arg(m_submission)))),
              [this](const QByteArray& data, const bool ok) {
                  if (ok)
                  {
                      const QJsonArray jobs = parseJsonResponse(data).value(QStringLiteral("jobs")).toArray();
                      for (const QJsonValue& job : jobs)
                      {
                          if (job.toInt(-1) > 0)
                          {
                              m_job = job.toInt(-1);
                              break;
                          }
                      }
                  }
                  if (m_job > 0)
                  {
                      LOG_INFO("[Astrometry.net web] Job assigned, job_id={} - waiting for solve result...", m_job);
                      m_attempts = 0;
                      m_cancelledMessage = QStringLiteral("Astrometry.net web: cancelled while waiting for solve.");
                      schedule([this]() { pollJob(); });
                      return;
                  }
                  schedule([this]() { pollSubmission(); });
              });
    }

    void pollJob()
    {
        if (m_attempts++ >= kAstrometryWebMaxPollAttempts)
        {
            fail(QStringLiteral("Astrometry.net web: timed out waiting for solve result."));
            return;
        }
        await(m_network.get(makeRequest(endpoint(QStringLiteral("/api/jobs/%1").arg(m_job)))),
              [this](const QByteArray& data, const bool ok) {
                  if (ok)
                  {
                      const QString status = parseJsonResponse(data).value(QStringLiteral("status")).toString();
                      if (status == QStringLiteral("success"))
                      {
                          download();
                          return;
                      }
                      if (status == QStringLiteral("failure"))
                      {
                          fail(QStringLiteral("Astrometry.net web: plate solving failed on server."));
                          return;
                      }
                  }
                  // "solving", an empty answer or a failed request: ask again.
                  schedule([this]() { pollJob(); });
              });
    }

    void download()
    {
        LOG_INFO("[Astrometry.net web] Server reports solve succeeded - downloading WCS solution...");
        await(m_network.get(makeRequest(endpoint(QStringLiteral("/wcs_file/%1").arg(m_job)))),
              [this](const QByteArray& data, const bool ok) {
                  if (!ok || data.isEmpty())
                  {
                      fail(QStringLiteral("Astrometry.net web: failed to download WCS solution."));
                      return;
                  }
                  an::Result<an::WcsSolution> solution = an::ui::readSolvedWcsHeader(
                      std::string_view(data.constData(), static_cast<std::size_t>(data.size())), m_width, m_height);
                  if (!solution.hasValue())
                  {
                      fail(QStringLiteral("Astrometry.net web: WCS file could not be parsed: %1")
                               .arg(describeStatus(solution.status())));
                      return;
                  }
                  an::ui::PlateSolveResult solved;
                  solved.succeeded = true;
                  solved.solution = std::move(*solution);
                  solved.solverUsed = QStringLiteral("Astrometry.net (web)");
                  finish(std::move(solved));
              });
    }

    QUrl m_baseUrl;
    std::chrono::milliseconds m_pollInterval;
    int m_timeoutMs;
    const CancellationToken& m_token;

    QNetworkAccessManager m_network;
    QEventLoop m_loop;
    QPointer<QNetworkReply> m_active;
    an::ui::PlateSolveResult m_result;
    QString m_cancelledMessage = QStringLiteral("Astrometry.net web: cancelled.");
    bool m_finished = false;

    QString m_apiKey;
    QString m_imagePath;
    QJsonObject m_uploadSettings;
    std::uint32_t m_width = 0u;
    std::uint32_t m_height = 0u;
    QString m_session;
    int m_submission = -1;
    int m_job = -1;
    int m_attempts = 0;
};

[[nodiscard]] an::ui::PlateSolveResult
runAstrometryWebSolver(const QUrl& baseUrl, const std::chrono::milliseconds pollInterval,
                       const an::ui::PlateSolveRequest& request, const an::ui::PlateSolveSettings& settings,
                       const SolveSubject& subject, const CancellationToken& token)
{
    LOG_INFO("[Astrometry.net web] Starting plate solve for '{}'", request.imagePath.toStdString());

    QJsonObject upload_settings;
    upload_settings[QStringLiteral("allow_commercial_use")] = QStringLiteral("n");
    upload_settings[QStringLiteral("allow_modifications")] = QStringLiteral("n");
    upload_settings[QStringLiteral("publicly_visible")] = QStringLiteral("n");

    if (settings.useHints)
    {
        if (const std::optional<an::SkyCoordinate> hint = positionHint(request, subject); hint.has_value())
        {
            upload_settings[QStringLiteral("center_ra")] = hint->rightAscensionDegrees;
            upload_settings[QStringLiteral("center_dec")] = hint->declinationDegrees;
            upload_settings[QStringLiteral("radius")] = settings.searchRadius;
        }

        if (const std::optional<double> scale = scaleHint(opticsOf(request, settings, subject)); scale.has_value())
        {
            upload_settings[QStringLiteral("scale_lower")] = *scale * 0.8;
            upload_settings[QStringLiteral("scale_upper")] = *scale * 1.2;
            upload_settings[QStringLiteral("scale_units")] = QStringLiteral("arcsecperpix");
            upload_settings[QStringLiteral("scale_type")] = QStringLiteral("ul");
        }
    }

    if (settings.downsample > 1)
    {
        upload_settings[QStringLiteral("downsample_factor")] = settings.downsample;
    }

    AstrometryWebSolve solve(baseUrl, pollInterval, settings.timeoutSeconds, token);
    an::ui::PlateSolveResult result =
        solve.run(settings.astrometryApiKey, request.imagePath, upload_settings, subject.width, subject.height);
    if (result.succeeded)
    {
        populateDerivedFields(result);
        LOG_INFO("[Astrometry.net web] Plate solve succeeded - scale: {:.4f} arcsec/px, center: ({:.4f} deg, "
                 "{:.4f} deg)",
                 result.imageScaleArcsecPerPixel, result.fieldCenterRaDeg, result.fieldCenterDecDeg);
    }
    return result;
}

/// The image a request is about: the metadata it carries or the file's, and the frame's size from the request or the
/// file.
[[nodiscard]] an::Result<SolveSubject> resolveSubject(const an::consumer::EngineSession& session,
                                                      const an::ui::PlateSolveRequest& request,
                                                      const CancellationToken& token)
{
    SolveSubject subject;
    subject.width = request.imageWidth > 0 ? static_cast<std::uint32_t>(request.imageWidth) : 0u;
    subject.height = request.imageHeight > 0 ? static_cast<std::uint32_t>(request.imageHeight) : 0u;
    if (request.existingMetadata.has_value() && subject.width > 0u && subject.height > 0u)
    {
        subject.metadata = *request.existingMetadata;
        return subject;
    }
    if (request.imagePath.trimmed().isEmpty())
    {
        return refusal(an::ErrorCode::InvalidArgument, QStringLiteral("No image path is available for plate solving."));
    }
    const CancellationBridge stop(token);
    an::Result<an::consumer::OperationScope> scope = session.admit(
        an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply, .stopToken = stop.token()});
    if (!scope.hasValue())
    {
        return scope.status();
    }
    const std::string path = enginePath(request.imagePath);
    const an::Result<an::CodecImageInfo> probed =
        an::probeImage(scope->engine(), scope->execution(),
                       an::CodecReadRequest{.path = path, .readMetadata = true, .validatePayload = false});
    if (!probed.hasValue())
    {
        return an::Status::failure(
            probed.status().code(),
            an::StatusDetail{.message = QStringLiteral("Failed to read metadata for %1 (%2)")
                                            .arg(request.imagePath, describeStatus(probed.status()))
                                            .toStdString()});
    }
    subject.metadata = request.existingMetadata.has_value() ? *request.existingMetadata : probed->metadata;
    if (subject.width == 0u)
    {
        subject.width = probed->descriptor.extent().width();
    }
    if (subject.height == 0u)
    {
        subject.height = probed->descriptor.extent().height();
    }
    return subject;
}

} // namespace

namespace an::ui
{

/// What every solve of a service shares, kept alive by an asynchronous solve that outlives the service.
struct PlateSolveService::Core
{
    std::unique_ptr<SolverProcessRunner> runner;
    QUrl astrometryWebBaseUrl{QString::fromLatin1(kAstrometryWebDefaultBaseUrl)};
    std::chrono::milliseconds astrometryWebPollInterval{kAstrometryWebDefaultPollInterval};

    [[nodiscard]] PlateSolveResult solve(const PlateSolveRequest& request, const PlateSolveSettings& settings,
                                         const CancellationToken& token);
};

/// One asynchronous solve: its cancellation, which the service and the worker share.
struct PlateSolveService::Run
{
    CancellationToken token;
    std::atomic<bool> running{true};
};

W_OBJECT_IMPL(PlateSolveService)

PlateSolveService::PlateSolveService(QObject* parent)
    : PlateSolveService(std::make_unique<DefaultSolverProcessRunner>(), parent)
{
}

PlateSolveService::PlateSolveService(std::unique_ptr<SolverProcessRunner> runner, QObject* parent)
    : QObject(parent), m_core(std::make_shared<Core>())
{
    m_core->runner = std::move(runner);
    qRegisterMetaType<an::ui::PlateSolveResult>();
}

PlateSolveService::~PlateSolveService()
{
    // A solve still running stops on its own and reports to nobody: nothing here waits for it.
    cancel();
}

void PlateSolveService::setAstrometryWebEndpoint(const QUrl& baseUrl, const std::chrono::milliseconds pollInterval)
{
    m_core->astrometryWebBaseUrl = baseUrl;
    m_core->astrometryWebPollInterval = pollInterval;
}

PlateSolveResult PlateSolveService::solve(const PlateSolveRequest& request, const PlateSolveSettings& settings,
                                          const CancellationToken& token)
{
    return m_core->solve(request, settings, token);
}

PlateSolveResult PlateSolveService::Core::solve(const PlateSolveRequest& request, const PlateSolveSettings& settings,
                                                const CancellationToken& token)
{
    PlateSolveResult result;

    const an::consumer::EngineSession* session = currentSession();
    if (session == nullptr)
    {
        result.errorMessage = QStringLiteral("No engine session is available for plate solving.");
        return result;
    }

    an::Result<SolveSubject> subject = resolveSubject(*session, request, token);
    if (!subject.hasValue())
    {
        if (token.isCancellationRequested())
        {
            return cancelledResult(QStringLiteral("Plate solving cancelled for %1.").arg(request.imagePath));
        }
        result.errorMessage = describeStatus(subject.status());
        return result;
    }
    const an::OpticalHints optics = opticsOf(request, settings, *subject);

    if (!request.forceExternalSolve && !request.ignoreEmbeddedWcs)
    {
        if (an::Result<an::WcsSolution> embedded =
                an::ui::readImageWcsSolution(cardsOf(*subject), subject->width, subject->height);
            embedded.hasValue())
        {
            LOG_INFO("Image '{}' has embedded WCS - returning it directly without external solver",
                     request.imagePath.toStdString());
            return embeddedWcsResult(std::move(*embedded), optics);
        }
    }

    if (request.imagePath.trimmed().isEmpty())
    {
        result.errorMessage =
            QStringLiteral("The current image has no source file path. Save it before plate solving.");
        return result;
    }

    if (!QFileInfo::exists(request.imagePath))
    {
        result.errorMessage = QStringLiteral("Image file '%1' does not exist.").arg(request.imagePath);
        return result;
    }

    const QString default_solver = an::ui::preferredConfiguredPlateSolveSolver(settings);
    const auto order = solverOrder(settings);
    LOG_INFO(
        "Plate solve configuration: default solver='{}', allow_external={}, force_external={}, ignore_embedded_wcs={}",
        default_solver.toStdString(), request.allowExternalSolve, request.forceExternalSolve,
        request.ignoreEmbeddedWcs);
    const auto solverName = [](const SolverKind kind) {
        switch (kind)
        {
        case SolverKind::InternalGaia:
            return QStringLiteral("Internal Gaia");
        case SolverKind::Astap:
            return QStringLiteral("ASTAP");
        case SolverKind::SolveField:
            return QStringLiteral("solve-field");
        case SolverKind::AstrometryWeb:
        default:
            return QStringLiteral("Astrometry.net (web)");
        }
    };
    QStringList solver_names;
    for (const auto s : order)
    {
        solver_names << solverName(s);
    }
    LOG_INFO("Plate solve: starting for '{}' - solver order: {}", QFileInfo(request.imagePath).fileName().toStdString(),
             solver_names.join(QStringLiteral(" -> ")).toStdString());

    QElapsedTimer timer;
    timer.start();

    QStringList diagnostics;
    for (const SolverKind solver : order)
    {
        if (token.isCancellationRequested())
        {
            return cancelledResult(QStringLiteral("Plate solving cancelled for %1.").arg(request.imagePath));
        }

        const QString solver_name = solverName(solver);

        PlateSolveResult solver_result;
        if (solver == SolverKind::InternalGaia)
        {
            if (!request.allowExternalSolve)
            {
                diagnostics << QStringLiteral("Internal Gaia skipped because automatic solving is disabled");
                continue;
            }
            if (request.forceExternalSolve)
            {
                diagnostics << QStringLiteral("Internal Gaia skipped because external solving is forced");
                continue;
            }
            if (request.requirePositionHintForInternalGaia &&
                (!settings.useHints || !positionHint(request, *subject).has_value()))
            {
                diagnostics << QStringLiteral("Internal Gaia skipped because this automatic solve has no position "
                                              "hint");
                continue;
            }
            solver_result = runInternalGaiaSolver(*session, request, settings, *subject, token);
        }
        else if (solver == SolverKind::Astap)
        {
            if (!request.allowExternalSolve)
            {
                diagnostics << QStringLiteral("ASTAP skipped because external solving is disabled");
                continue;
            }

            if (!an::ui::isAstapSolverConfigured(settings))
            {
                LOG_INFO("Plate solve: skipping ASTAP (not configured)");
                diagnostics << QStringLiteral("ASTAP not configured");
                continue;
            }

            solver_result =
                runAstapSolver(*session, *runner, settings.astapPath.trimmed(), request, settings, *subject, token);
        }
        else if (solver == SolverKind::SolveField)
        {
            if (!request.allowExternalSolve)
            {
                diagnostics << QStringLiteral("solve-field skipped because external solving is disabled");
                continue;
            }

            if (!an::ui::isAstrometryLocalSolverConfigured(settings))
            {
                LOG_INFO("Plate solve: skipping solve-field (not configured)");
                diagnostics << QStringLiteral("solve-field not configured");
                continue;
            }

            solver_result = runSolveFieldSolver(*runner, settings.astrometryLocalPath.trimmed(), request, settings,
                                                *subject, token);
        }
        else if (solver == SolverKind::AstrometryWeb)
        {
            if (!request.allowExternalSolve)
            {
                diagnostics << QStringLiteral("Astrometry.net (web) skipped because external solving is disabled");
                continue;
            }

            if (!an::ui::isAstrometryWebSolverConfigured(settings))
            {
                LOG_INFO("Plate solve: skipping Astrometry.net (web) (API key not configured)");
                diagnostics << QStringLiteral("Astrometry.net web API key not configured");
                continue;
            }

            solver_result = runAstrometryWebSolver(astrometryWebBaseUrl, astrometryWebPollInterval, request, settings,
                                                   *subject, token);
        }

        if (solver_result.cancelled)
        {
            return solver_result;
        }
        if (solver_result.succeeded)
        {
            if (request.validateSolutionAgainstHints)
            {
                if (const std::optional<an::SkyCoordinate> hint = positionHint(request, *subject); hint.has_value())
                {
                    const double separation_deg = an::angularSeparationDegrees(
                        *hint, an::SkyCoordinate{.rightAscensionDegrees = solver_result.fieldCenterRaDeg,
                                                 .declinationDegrees = solver_result.fieldCenterDecDeg});
                    const double maximum_separation_deg = (std::max)(0.01, settings.searchRadius);
                    if (!std::isfinite(separation_deg) || separation_deg > maximum_separation_deg)
                    {
                        const QString validation_error =
                            QStringLiteral("%1 solution is %2 deg from the requested center, outside the requested "
                                           "search radius of %3 deg")
                                .arg(solver_name)
                                .arg(separation_deg, 0, 'f', 3)
                                .arg(maximum_separation_deg, 0, 'f', 3);
                        diagnostics << validation_error;
                        LOG_WARNING("Plate solve: rejecting {}", validation_error.toStdString());
                        continue;
                    }
                }
            }

            populateDerivedFocalLength(solver_result, optics);
            solver_result.solveTimeMs = static_cast<double>(timer.elapsed());
            LOG_INFO("Plate solve: completed in {:.1f}s using {}", solver_result.solveTimeMs / 1000.0,
                     solver_result.solverUsed.toStdString());
            return solver_result;
        }

        diagnostics << QStringLiteral("%1: %2").arg(solver_name, solver_result.errorMessage);
        LOG_INFO("Plate solve: {} did not solve '{}': {}", solver_name.toStdString(),
                 QFileInfo(request.imagePath).fileName().toStdString(), solver_result.errorMessage.toStdString());
    }

    result.errorMessage = QStringLiteral("Plate solving failed for %1. %2")
                              .arg(request.imagePath, diagnostics.join(QStringLiteral(" | ")));
    result.solveTimeMs = static_cast<double>(timer.elapsed());
    LOG_WARNING("Plate solve: all solvers failed for '{}' after {:.1f}s - {}",
                QFileInfo(request.imagePath).fileName().toStdString(), result.solveTimeMs / 1000.0,
                diagnostics.join(QStringLiteral(" | ")).toStdString());
    return result;
}

void PlateSolveService::solveAsync(const PlateSolveRequest& request, const PlateSolveSettings& settings,
                                   PrepareInput prepare)
{
    if (isSolving())
    {
        return;
    }

    auto run = std::make_shared<Run>();
    m_run = run;
    emit solveProgress(QStringLiteral("Plate solving..."));

    QPointer<PlateSolveService> guard(this);
    QThreadPool::globalInstance()->start(
        [guard, core = m_core, run, request, settings, prepare = std::move(prepare)]() mutable {
            PlateSolveResult result;
            const an::Status prepared = prepare ? prepare() : an::Status::success();
            if (prepared.failed())
            {
                result.errorMessage = describeStatus(prepared);
            }
            else if (run->token.isCancellationRequested())
            {
                result = cancelledResult(QStringLiteral("Plate solving cancelled for %1.").arg(request.imagePath));
            }
            else
            {
                result = core->solve(request, settings, run->token);
            }
            QCoreApplication* application = QCoreApplication::instance();
            if (application == nullptr)
            {
                run->running.store(false);
                return;
            }
            QMetaObject::invokeMethod(
                application,
                [guard, run, result = std::move(result)]() {
                    run->running.store(false);
                    if (guard && guard->m_run == run)
                    {
                        emit guard->solveFinished(result);
                    }
                },
                Qt::QueuedConnection);
        });
}

void PlateSolveService::cancel()
{
    if (m_run)
    {
        m_run->token.requestCancel();
    }
}

bool PlateSolveService::isSolving() const noexcept
{
    return m_run && m_run->running.load();
}

} // namespace an::ui
