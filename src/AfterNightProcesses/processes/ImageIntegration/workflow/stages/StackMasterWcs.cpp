// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A stacked master's world coordinates and cards, decided before it is written.

#include "stdafx.hpp"
#include "processes/ImageIntegration/workflow/stages/StackMasterWcs.h"

#include "app/AppContextHelpers.h"
#include "app/CatalogSearchService.h"
#include "app/PlateSolveConfiguration.h"
#include "app/PlateSolveService.h"
#include "app/PlateSolveUtils.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"
#include "processes/ImageIntegration/core/PersistedChoiceMaps.h"
#include "processes/ImageIntegration/workflow/WorkflowStageServices.h"
#include "processes/ImageIntegration/workflow/stages/StackArtifactFingerprints.h"
#include "processes/ImageIntegration/workflow/stages/StageParsingUtils.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Astrometry/AstrometryHints.h>
#include <AfterNightEngine/Algorithms/Astrometry/Sexagesimal.h>
#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/IO/ObservationCards.h>
#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/Image/RawFrame.h>

#include <AfterNightPlatform/Logging.h>

#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>

#include <algorithm>
#include <cmath>

namespace ImageIntegration::detail
{

namespace
{

[[nodiscard]] std::string fitsNumber(const double value)
{
    return QString::number(value, 'g', 12).toStdString();
}

[[nodiscard]] std::string fitsPreciseNumber(const double value)
{
    return QString::number(value, 'g', 16).toStdString();
}

/// A card's value with its quotes and surrounding spaces stripped.
[[nodiscard]] QString cardText(const an::CodecCard& card)
{
    QString value = QString::fromStdString(card.value()).trimmed();
    if (value.size() >= 2 && value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\'')))
    {
        value = value.mid(1, value.size() - 2).trimmed();
    }
    return value;
}

[[nodiscard]] const an::CodecCard* findFits(std::span<const an::CodecCard> cards, std::string_view keyword)
{
    const auto found = std::find_if(cards.begin(), cards.end(), [keyword](const an::CodecCard& card) {
        return card.origin() == an::CodecNamespace::Fits && card.keyword() == keyword;
    });
    return found == cards.end() ? nullptr : &*found;
}

void appendCard(std::vector<an::CodecCard>& cards, const std::string& keyword, const std::string& value)
{
    an::Result<an::CodecCard> card = an::CodecCard::create(an::CodecNamespace::Fits, keyword, value, std::string());
    if (card.hasValue())
    {
        cards.push_back(std::move(*card));
    }
}

[[nodiscard]] QString formatSexagesimal(const double degrees, const bool rightAscension)
{
    const an::Result<std::string> text =
        rightAscension ? an::formatRightAscension(degrees, 3u) : an::formatDeclination(degrees, 3u);
    return text.hasValue() ? QString::fromStdString(*text) : QString();
}

/// The solution an output-plane record states, on the plane it describes.
[[nodiscard]] an::Result<an::WcsSolution>
recordSolution(const QString& json, stage_utils::OutputPlaneWcsDescriptorInfo& info, QString& error)
{
    std::vector<an::CodecCard> cards;
    if (!stage_utils::parseOutputPlaneWcsJson(json, &cards, &info, error))
    {
        return an::Status::failure(an::ErrorCode::InvalidData, error.toStdString());
    }
    if (info.width <= 0 || info.height <= 0)
    {
        error = QStringLiteral("The registration output-plane record states no extent.");
        return an::Status::failure(an::ErrorCode::InvalidData, error.toStdString());
    }
    return an::parseWcsCards(cards, static_cast<std::uint32_t>(info.width), static_cast<std::uint32_t>(info.height));
}

} // namespace

void appendUniqueWarnings(QStringList& warnings, const QStringList& additions)
{
    for (const auto& warning : additions)
    {
        if (!warning.trimmed().isEmpty() && !warnings.contains(warning))
        {
            warnings.append(warning);
        }
    }
}

std::string fitsString(QString value)
{
    value.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QStringLiteral("'%1'").arg(value).toStdString();
}

an::Result<an::CodecMetadata> withFitsCard(const an::CodecMetadata& metadata, const std::string& keyword,
                                           const std::string& value, const std::string& comment)
{
    std::vector<an::CodecCard> cards;
    cards.reserve(metadata.cards().size() + 1u);
    bool written = false;
    for (const an::CodecCard& card : metadata.cards())
    {
        if (card.origin() != an::CodecNamespace::Fits || card.keyword() != keyword)
        {
            cards.push_back(card);
            continue;
        }
        if (written)
        {
            continue;
        }
        an::Result<an::CodecCard> replaced =
            an::CodecCard::create(an::CodecNamespace::Fits, keyword, value, comment.empty() ? card.comment() : comment);
        if (!replaced.hasValue())
        {
            return replaced.status();
        }
        cards.push_back(std::move(*replaced));
        written = true;
    }
    if (!written)
    {
        an::Result<an::CodecCard> appended = an::CodecCard::create(an::CodecNamespace::Fits, keyword, value, comment);
        if (!appended.hasValue())
        {
            return appended.status();
        }
        cards.push_back(std::move(*appended));
    }
    return an::CodecMetadata::create(metadata.sourceEncoding(), std::move(cards));
}

an::Result<an::CodecMetadata> withoutFitsCard(const an::CodecMetadata& metadata, const std::string_view keyword)
{
    std::vector<an::CodecCard> cards;
    cards.reserve(metadata.cards().size());
    for (const an::CodecCard& card : metadata.cards())
    {
        if (card.origin() != an::CodecNamespace::Fits || card.keyword() != keyword)
        {
            cards.push_back(card);
        }
    }
    return an::CodecMetadata::create(metadata.sourceEncoding(), std::move(cards));
}

bool hasValidAstrometricWcs(const an::WcsSolution& solution, QString* error)
{
    const auto fail = [error](const QString& reason) {
        if (error != nullptr)
        {
            *error = reason;
        }
        return false;
    };
    const an::SkyCoordinate reference = solution.referencePoint();
    if (!std::isfinite(reference.rightAscensionDegrees) || reference.rightAscensionDegrees < 0.0 ||
        reference.rightAscensionDegrees >= 360.0)
    {
        return fail(QStringLiteral("CRVAL1 is outside the supported [0, 360) range."));
    }
    if (!std::isfinite(reference.declinationDegrees) || reference.declinationDegrees < -90.0 ||
        reference.declinationDegrees > 90.0)
    {
        return fail(QStringLiteral("CRVAL2 is outside the supported [-90, 90] range."));
    }
    const double scaleX = solution.pixelScaleXArcsecPerPixel();
    const double scaleY = solution.pixelScaleYArcsecPerPixel();
    if (!std::isfinite(scaleX) || !std::isfinite(scaleY) || !(scaleX > 0.0) || !(scaleY > 0.0))
    {
        return fail(QStringLiteral("The astrometric solution reported a non-positive image scale."));
    }
    if (!solution.fieldCenter().hasValue())
    {
        return fail(QStringLiteral("The astrometric solution does not place the frame's centre on the sky."));
    }
    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

MasterWcsCandidate buildMasterWcsCandidate(const MasterWcsRequest& request)
{
    MasterWcsCandidate candidate;
    if (request.registered)
    {
        if (!request.outputPlaneWcsJson.has_value() || request.outputPlaneWcsJson->trimmed().isEmpty())
        {
            candidate.diagnostic =
                QStringLiteral("Persisted registration output-plane WCS is unavailable for this group.");
            candidate.missingOutputPlaneRecord = true;
            return candidate;
        }
        stage_utils::OutputPlaneWcsDescriptorInfo info;
        QString error;
        an::Result<an::WcsSolution> solution = recordSolution(*request.outputPlaneWcsJson, info, error);
        if (!solution.hasValue())
        {
            candidate.diagnostic =
                error.isEmpty() ? QStringLiteral("Failed to read the persisted registration output-plane WCS: %1")
                                      .arg(QString::fromUtf8(solution.status().message()))
                                : error;
            return candidate;
        }
        const int expectedWidth = request.drizzle.has_value() ? 0 : static_cast<int>(request.planeWidth);
        const int expectedHeight = request.drizzle.has_value() ? 0 : static_cast<int>(request.planeHeight);
        if (!stage_utils::validateOutputPlaneWcsDescriptor(info, request.registrationReference, request.alignmentMode,
                                                           expectedWidth, expectedHeight, request.expectedSipCorrection,
                                                           error))
        {
            candidate.diagnostic = error;
            return candidate;
        }
        if (request.drizzle.has_value())
        {
            const DrizzleMasterGeometry& drizzle = *request.drizzle;
            const std::uint32_t canvasWidth = request.masterWidth + 2u * drizzle.edgeGuard;
            const std::uint32_t canvasHeight = request.masterHeight + 2u * drizzle.edgeGuard;
            solution = an::translateWcsSolution(*solution, drizzle.shiftX, drizzle.shiftY);
            if (solution.hasValue())
            {
                solution = an::resizeWcsSolution(*solution, drizzle.scale, drizzle.scale, canvasWidth, canvasHeight);
            }
            if (solution.hasValue())
            {
                solution = an::cropWcsSolution(*solution, drizzle.edgeGuard, drizzle.edgeGuard, request.masterWidth,
                                               request.masterHeight);
            }
            if (!solution.hasValue())
            {
                candidate.diagnostic =
                    QStringLiteral("The registration output-plane WCS could not be moved onto the drizzled canvas: %1")
                        .arg(QString::fromUtf8(solution.status().message()));
                return candidate;
            }
        }
        candidate.sourceDescription = QStringLiteral("registration's persisted output plane");
        QString validationError;
        if (!hasValidAstrometricWcs(*solution, &validationError))
        {
            candidate.diagnostic =
                QStringLiteral("Propagated registration output-plane WCS is invalid: %1").arg(validationError);
            return candidate;
        }
        candidate.solution = std::move(*solution);
        return candidate;
    }

    if (request.drizzle.has_value() || request.referenceCards.empty())
    {
        return candidate;
    }
    an::Result<an::WcsSolution> solution =
        an::parseWcsCards(request.referenceCards, request.referenceWidth, request.referenceHeight);
    if (!solution.hasValue())
    {
        candidate.diagnostic =
            QStringLiteral("The direct-stack reference input does not contain a valid astrometric WCS: %1")
                .arg(QString::fromUtf8(solution.status().message()));
        return candidate;
    }
    QString validationError;
    if (!hasValidAstrometricWcs(*solution, &validationError))
    {
        candidate.diagnostic =
            QStringLiteral("The direct-stack reference input does not contain a valid astrometric WCS: %1")
                .arg(validationError);
        return candidate;
    }
    candidate.sourceDescription = QStringLiteral("reference light's own world-coordinate cards");
    candidate.solution = std::move(*solution);
    return candidate;
}

an::Result<an::CodecMetadata> masterCodecMetadata(const MasterCardsRequest& request)
{
    std::vector<an::CodecCard> cards;
    if (request.integrationSeconds.has_value() && std::isfinite(*request.integrationSeconds) &&
        *request.integrationSeconds > 0.0)
    {
        appendCard(cards, "EXPTIME", fitsNumber(*request.integrationSeconds));
        appendCard(cards, "TOTEXP", fitsNumber(*request.integrationSeconds));
    }
    if (request.combinedFrames.has_value() && *request.combinedFrames > 0u)
    {
        appendCard(cards, "STACKCNT", std::to_string(*request.combinedFrames));
    }
    if (request.drizzle.has_value())
    {
        const auto& drizzle = *request.drizzle;
        // The kernel by its key, as drizzle headers name one, whichever way the group spells it.
        const ImageIntegration::PersistedChoiceValue stated =
            ImageIntegration::persistedChoiceFromText(ImageIntegration::PersistedChoice::DrizzleKernel, drizzle.kernel);
        const QString kernel = stated.known                         ? stated.key
                               : drizzle.kernel.trimmed().isEmpty() ? QStringLiteral("square")
                                                                    : drizzle.kernel.trimmed();
        appendCard(cards, "DRIZZLE", "T");
        appendCard(cards, "DRIZMULT", fitsNumber(drizzle.scale));
        appendCard(cards, "DRIZPIXF", fitsNumber(drizzle.pixelFraction));
        appendCard(cards, "DRIZKERN", fitsString(kernel));
        appendCard(cards, "NDRIZIM", std::to_string(request.drizzledLights));
        appendCard(cards, "D001PIXF", fitsNumber(drizzle.pixelFraction));
        appendCard(cards, "D001KERN", fitsString(kernel));
        std::optional<double> outputScale;
        if (request.solution.has_value())
        {
            outputScale = request.solution->meanPixelScaleArcsecPerPixel();
        }
        else if (request.referenceCards != nullptr)
        {
            const an::Result<double> native =
                an::resolvePlateScaleHint(an::readOpticalHints(request.referenceCards->cards()));
            if (native.hasValue() && drizzle.scale > 0.0f)
            {
                outputScale = *native / static_cast<double>(drizzle.scale);
            }
        }
        if (outputScale.has_value() && std::isfinite(*outputScale) && *outputScale > 0.0)
        {
            appendCard(cards, "D001SCAL", fitsNumber(*outputScale));
        }
    }
    for (const an::CodecCard& card : request.pointing)
    {
        cards.push_back(card);
    }
    an::Result<an::CodecMetadata> metadata = an::CodecMetadata::create(an::SourceEncoding{}, std::move(cards));
    if (!metadata.hasValue())
    {
        return metadata;
    }
    if (request.solution.has_value())
    {
        metadata = an::withWcsCards(*metadata, *request.solution);
        if (!metadata.hasValue())
        {
            return metadata;
        }
    }
    if (request.referenceCards != nullptr)
    {
        metadata = an::withObservationCards(*metadata, *request.referenceCards);
    }
    return metadata;
}

std::vector<an::CodecCard> mosaicPointingCards(std::span<const an::CodecCard> reference,
                                               const std::vector<std::vector<an::CodecCard>>& lights,
                                               const std::optional<an::WcsSolution>& solution)
{
    std::vector<an::CodecCard> cards;
    const an::Result<an::PointingHint> referencePointing = an::readPointingHint(reference, an::observedPointingCards());
    if (referencePointing.hasValue())
    {
        const an::PointingKeywords keywords = an::pointingCardKeywords(referencePointing->source);
        const an::CodecCard* ra = findFits(reference, keywords.rightAscension);
        const an::CodecCard* dec = findFits(reference, keywords.declination);
        if (ra != nullptr && dec != nullptr)
        {
            appendCard(cards, "ANREFRA", ra->value());
            appendCard(cards, "ANREFDEC", dec->value());
            appendCard(cards, "ANREFSRC",
                       fitsString(QStringLiteral("%1/%2").arg(
                           QString::fromLatin1(keywords.rightAscension.data(),
                                               static_cast<qsizetype>(keywords.rightAscension.size())),
                           QString::fromLatin1(keywords.declination.data(),
                                               static_cast<qsizetype>(keywords.declination.size())))));
        }
    }
    if (const an::CodecCard* rotation = findFits(reference, "OBJCTROT"))
    {
        appendCard(cards, "ANREFROT", rotation->value());
    }

    std::optional<an::SkyCoordinate> centre;
    std::optional<double> orientation;
    QString method;
    std::size_t used = 0u;
    if (solution.has_value())
    {
        const an::Result<an::SkyCoordinate> field = solution->fieldCenter();
        if (field.hasValue())
        {
            centre = *field;
            const double degrees = solution->orientationDegrees();
            if (std::isfinite(degrees))
            {
                orientation = degrees;
            }
            method = QStringLiteral("WCS");
        }
    }
    if (!centre.has_value())
    {
        std::vector<an::SkyCoordinate> directions;
        for (const auto& light : lights)
        {
            const an::Result<an::PointingHint> pointing = an::readPointingHint(light, an::observedPointingCards());
            if (pointing.hasValue())
            {
                directions.push_back(pointing->direction);
            }
        }
        const an::Result<an::SkyCoordinate> mean = an::sphericalMeanDirection(directions);
        if (mean.hasValue())
        {
            centre = *mean;
            method = QStringLiteral("SPHAVG");
            used = directions.size();
        }
    }
    if (!centre.has_value())
    {
        return cards;
    }
    appendCard(cards, "RA", fitsPreciseNumber(centre->rightAscensionDegrees));
    appendCard(cards, "DEC", fitsPreciseNumber(centre->declinationDegrees));
    appendCard(cards, "OBJCTRA", fitsString(formatSexagesimal(centre->rightAscensionDegrees, true)));
    appendCard(cards, "OBJCTDEC", fitsString(formatSexagesimal(centre->declinationDegrees, false)));
    appendCard(cards, "ANPOSMTH", fitsString(method));
    if (orientation.has_value())
    {
        appendCard(cards, "OBJCTROT", fitsPreciseNumber(*orientation));
    }
    if (used > 0u)
    {
        appendCard(cards, "ANPOSCNT", std::to_string(used));
    }
    return cards;
}

namespace
{

void insertOptionalDouble(QJsonObject& object, const QString& key, const std::optional<double>& value)
{
    if (value.has_value())
    {
        object.insert(key, *value);
    }
}

[[nodiscard]] QString finalMasterWcsCacheKey(const WorkflowManifest::IntegrationGroup& group,
                                             const an::ui::PlateSolveRequest& request,
                                             const an::ui::PlateSolveSettings& settings)
{
    const QString registrationKey = group.registrationGroupKey.toString().trimmed();
    if (registrationKey.isEmpty() || request.imageWidth <= 0 || request.imageHeight <= 0)
    {
        return QString();
    }
    QJsonObject solver;
    solver.insert(QStringLiteral("defaultSolver"), settings.defaultSolver);
    solver.insert(QStringLiteral("astapPath"), settings.astapPath);
    solver.insert(QStringLiteral("astrometryLocalPath"), settings.astrometryLocalPath);
    solver.insert(QStringLiteral("astrometryWebConfigured"), !settings.astrometryApiKey.trimmed().isEmpty());
    solver.insert(QStringLiteral("indexPath"), settings.indexPath);
    solver.insert(QStringLiteral("gaiaCatalogDirectory"), settings.gaiaCatalogDirectory);
    solver.insert(QStringLiteral("gaiaCatalogPack"), settings.gaiaCatalogPack);
    solver.insert(QStringLiteral("searchRadius"), settings.searchRadius);
    solver.insert(QStringLiteral("downsample"), settings.downsample);
    solver.insert(QStringLiteral("timeoutSeconds"), settings.timeoutSeconds);
    solver.insert(QStringLiteral("useHints"), settings.useHints);
    insertOptionalDouble(solver, QStringLiteral("focalLengthMm"), settings.focalLengthMm);
    insertOptionalDouble(solver, QStringLiteral("pixelSizeUm"), settings.pixelSizeUm);

    QJsonObject key;
    key.insert(QStringLiteral("registrationGroupKey"), registrationKey);
    key.insert(QStringLiteral("imageWidth"), request.imageWidth);
    key.insert(QStringLiteral("imageHeight"), request.imageHeight);
    insertOptionalDouble(key, QStringLiteral("hintRaDeg"), request.hintRaDeg);
    insertOptionalDouble(key, QStringLiteral("hintDecDeg"), request.hintDecDeg);
    key.insert(QStringLiteral("solverSettings"), solver);
    return fingerprintJson(key);
}

/// Points a solve at the group's object when the master's cards state no pointing and the bundled catalogs name it
/// once.
void applyObjectPositionHints(const WorkflowStageServices& services, const WorkflowManifest& manifest,
                              const WorkflowManifest::IntegrationGroup& group, std::span<const an::CodecCard> cards,
                              an::ui::PlateSolveRequest& request)
{
    if (an::readPointingHint(cards, an::plateSolveHintCards()).hasValue())
    {
        return;
    }
    QString objectName = group.object.trimmed();
    if (objectName.isEmpty())
    {
        const auto registration = std::find_if(manifest.registrationGroups.begin(), manifest.registrationGroups.end(),
                                               [&group](const WorkflowManifest::RegistrationGroup& candidate) {
                                                   return candidate.groupKey == group.registrationGroupKey;
                                               });
        if (registration != manifest.registrationGroups.end())
        {
            objectName = registration->object.trimmed();
        }
    }
    if (objectName.isEmpty())
    {
        return;
    }
    const an::ui::CatalogSearchResult found =
        an::ui::catalog_search::searchLocalCatalog(services.session(), objectName, services.stopToken());
    if (!found.success || found.entries.isEmpty())
    {
        return;
    }
    const an::ui::CatalogSearchEntry* selected = nullptr;
    int exactMatches = 0;
    for (const auto& entry : found.entries)
    {
        const bool exact =
            entry.objectName.compare(objectName, Qt::CaseInsensitive) == 0 ||
            std::any_of(entry.alternateNames.begin(), entry.alternateNames.end(), [&objectName](const QString& alias) {
                return alias.compare(objectName, Qt::CaseInsensitive) == 0;
            });
        if (exact)
        {
            selected = &entry;
            ++exactMatches;
        }
    }
    if (exactMatches > 1 || (exactMatches == 0 && found.entries.size() > 1))
    {
        LOG_WARNING("Final-master WCS fallback ignored ambiguous object '{}' because the local catalog returned {} "
                    "matches",
                    objectName.toStdString(), found.entries.size());
        return;
    }
    if (selected == nullptr)
    {
        selected = &found.entries.front();
    }
    if (!std::isfinite(selected->raDeg) || !std::isfinite(selected->decDeg))
    {
        return;
    }
    request.hintRaDeg = selected->raDeg;
    request.hintDecDeg = selected->decDeg;
}

/// Writes the master as the solver reads it: FITS in single precision, its cards without any solution.
[[nodiscard]] an::Status writeSolveInput(const an::consumer::OperationScope& scope, const an::Image<float>& master,
                                         const an::ImageMetadata& metadata, const QString& path)
{
    const an::Result<an::OutputIntent> intent = an::ui::plateSolveInputIntent();
    if (!intent.hasValue())
    {
        return intent.status();
    }
    an::Result<an::RawFrame> frame = an::borrowImageFrame(master);
    if (!frame.hasValue())
    {
        return frame.status();
    }
    const std::string destination = path.toUtf8().toStdString();
    an::OutputRequest request;
    request.destinationPath = destination;
    request.format = an::codecFormatFromPath(destination);
    request.intent = &*intent;
    request.replacePolicy = an::OutputReplacePolicy::ReplaceExisting;
    const an::Result<an::OutputReport> written =
        an::writeImage(scope.engine(), scope.execution(), request, *frame, metadata);
    return written.hasValue() ? an::Status::success() : written.status();
}

} // namespace

FinalMasterWcsOutcome solveFinalMasterWcs(an::ui::PlateSolveService& service, const WorkflowStageServices& services,
                                          const WorkflowManifest& manifest,
                                          const WorkflowManifest::IntegrationGroup& group,
                                          const an::Image<float>& master, const QString& workDirectory,
                                          const QString& currentIssue, CancellationToken& token,
                                          FinalMasterWcsCache* cache)
{
    FinalMasterWcsOutcome outcome;
    const QString kept = currentIssue.isEmpty() ? QStringLiteral("its existing WCS state") : currentIssue;
    const auto noSolver = [&]() {
        outcome.detail = QStringLiteral("Skipped fallback plate solving for %1 because no plate solver is configured.")
                             .arg(group.outputFilename);
        outcome.warning = QStringLiteral("Final master WCS fallback solve skipped because no plate solver is "
                                         "configured. The master was kept with %1.")
                              .arg(kept);
        return outcome;
    };
    if (cache != nullptr && cache->noConfiguredSolver)
    {
        return noSolver();
    }
    an::ui::PlateSolveSettings settings = manifest.plateSolve;
    settings.astrometryApiKey = process_utils::selectGlobalSetting(SettingsKeys::TOOLS_ASTROMETRY_API_KEY,
                                                                   SettingsDefaults::TOOLS_ASTROMETRY_API_KEY);
    if (!an::ui::hasAnyConfiguredPlateSolveSolver(settings))
    {
        if (cache != nullptr)
        {
            cache->noConfiguredSolver = true;
        }
        return noSolver();
    }

    const std::optional<an::ImageDescriptor> described = master.descriptor();
    if (!described.has_value())
    {
        outcome.warning = QStringLiteral("Final master WCS fallback solve skipped: the master holds nothing.");
        return outcome;
    }
    const an::Result<an::CodecMetadata> unsolvedCards = an::withoutWcsCards(master.metadata().codec());
    if (!unsolvedCards.hasValue())
    {
        outcome.warning = QStringLiteral("Final master WCS fallback solve skipped: %1")
                              .arg(QString::fromUtf8(unsolvedCards.status().message()));
        return outcome;
    }
    const an::ImageMetadata& masterMetadata = master.metadata();
    const an::ImageMetadata unsolved =
        an::ImageMetadata::create(masterMetadata.acquisition(), masterMetadata.semantics(), an::AstrometricSolution{},
                                  masterMetadata.provenance(), *unsolvedCards, std::nullopt);

    an::ui::PlateSolveRequest request;
    request.existingMetadata = unsolved;
    request.imageWidth = static_cast<int>(described->extent().width());
    request.imageHeight = static_cast<int>(described->extent().height());
    request.workDirectoryRoot = workDirectory;
    request.requestTag = group.groupKey.toCompactString();
    request.ignoreEmbeddedWcs = true;
    request.allowMetadataPositionHints = true;
    request.requirePositionHintForInternalGaia = true;
    request.validateSolutionAgainstHints = true;
    applyObjectPositionHints(services, manifest, group, unsolvedCards->cards(), request);

    const QString cacheKey = cache != nullptr ? finalMasterWcsCacheKey(group, request, settings) : QString();
    if (cache != nullptr && !cacheKey.isEmpty())
    {
        const auto cached = cache->solvedByKey.find(cacheKey);
        if (cached != cache->solvedByKey.end() && hasValidAstrometricWcs(cached->second.first))
        {
            outcome.solution = cached->second.first;
            outcome.reusedCachedSolution = true;
            outcome.detail = QStringLiteral("Reused fallback WCS solved earlier for %1.").arg(cached->second.second);
            return outcome;
        }
    }

    const QString scratch = an::ui::createUniquePlateSolveInputPath(workDirectory);
    if (scratch.isEmpty())
    {
        outcome.warning = QStringLiteral("Final master WCS fallback solve skipped: no scratch file could be made.");
        return outcome;
    }
    {
        an::Result<an::consumer::OperationScope> scope = services.admitStageOperation();
        if (!scope.hasValue())
        {
            outcome.cancelled = scope.status().code() == an::ErrorCode::Cancelled;
            outcome.warning = QStringLiteral("Final master WCS fallback solve skipped: %1")
                                  .arg(QString::fromUtf8(scope.status().message()));
            return outcome;
        }
        const an::Status written = writeSolveInput(*scope, master, unsolved, scratch);
        if (written.failed())
        {
            QFile::remove(scratch);
            outcome.cancelled = written.code() == an::ErrorCode::Cancelled;
            outcome.warning = QStringLiteral("Final master WCS fallback solve skipped: the master could not be "
                                             "written for the solver (%1).")
                                  .arg(QString::fromUtf8(written.message()));
            return outcome;
        }
    }
    request.imagePath = scratch;
    const an::ui::PlateSolveResult result = service.solve(request, settings, token);
    QFile::remove(scratch);
    if (result.cancelled)
    {
        if (token.isCancellationRequested())
        {
            outcome.cancelled = true;
            return outcome;
        }
        outcome.warning = QStringLiteral("Final-master plate solving was cancelled; keeping the propagated WCS as-is.");
        return outcome;
    }
    QString validationError;
    if (!result.succeeded || !result.solution.has_value() ||
        !hasValidAstrometricWcs(*result.solution, &validationError))
    {
        const QString reason = !result.errorMessage.trimmed().isEmpty() ? result.errorMessage.trimmed()
                               : !validationError.isEmpty()
                                   ? validationError
                                   : QStringLiteral("The solver did not return a valid astrometric solution.");
        outcome.warning = QStringLiteral("Final master WCS fallback solve failed (%1). The master was kept with %2.")
                              .arg(reason, kept);
        return outcome;
    }
    outcome.solution = result.solution;
    outcome.solver = result.solverUsed;
    outcome.detail = QStringLiteral("Solved %1 with %2.").arg(group.outputFilename, result.solverUsed);
    if (cache != nullptr && !cacheKey.isEmpty())
    {
        cache->solvedByKey.insert_or_assign(cacheKey, std::pair{*result.solution, group.groupKey.toCompactString()});
    }
    return outcome;
}

void cleanupLeftoverWcsTempFiles(const QDir& outputRoot)
{
    // Non-recursive: a rewrite through a temporary copy leaves it beside the masters only.
    QDirIterator iterator(
        outputRoot.path(),
        QStringList{QStringLiteral("*.wcs_tmp"), QStringLiteral("*.wcs_tmp.*"), QStringLiteral("*.wcs_tmp.backup")},
        QDir::Files);
    while (iterator.hasNext())
    {
        const QString path = iterator.next();
        if (!QFile::remove(path))
        {
            LOG_WARNING("LightsStackingStage: failed to remove stale WCS temp file '{}'", path.toStdString());
        }
    }
}

} // namespace ImageIntegration::detail
