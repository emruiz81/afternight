// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ReportModelJson.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonValue>
#include <QVariant>

#include <array>
#include <cmath>
#include <concepts>
#include <limits>
#include <type_traits>
#include <utility>

namespace ImageIntegration::Reporting
{
namespace
{
/// The fields of a structure, named once: `visit` hands each key and member to a writer or a reader, so the two can
/// never disagree about a field.
template <class T> struct Fields;

/// The names an enumeration's values are stored under.
template <class T> struct EnumNames;

template <class T>
concept Described = requires { typename Fields<T>::Described; };

template <class T>
concept NamedEnum = std::is_enum_v<T> && requires { EnumNames<T>::kNames; };

template <class E> using EnumName = std::pair<E, const char*>;

template <> struct EnumNames<ReportOutcome>
{
    static constexpr std::array kNames = {
        EnumName<ReportOutcome>{ReportOutcome::Completed, "completed"},
        EnumName<ReportOutcome>{ReportOutcome::CompletedWithWarnings, "completedWithWarnings"},
        EnumName<ReportOutcome>{ReportOutcome::Partial, "partial"},
        EnumName<ReportOutcome>{ReportOutcome::Failed, "failed"},
        EnumName<ReportOutcome>{ReportOutcome::Cancelled, "cancelled"},
    };
};

template <> struct EnumNames<ReportIssueSeverity>
{
    static constexpr std::array kNames = {
        EnumName<ReportIssueSeverity>{ReportIssueSeverity::Critical, "critical"},
        EnumName<ReportIssueSeverity>{ReportIssueSeverity::Warning, "warning"},
        EnumName<ReportIssueSeverity>{ReportIssueSeverity::Note, "note"},
    };
};

template <> struct EnumNames<ReportIssueKind>
{
    static constexpr std::array kNames = {
        EnumName<ReportIssueKind>{ReportIssueKind::RunFailed, "runFailed"},
        EnumName<ReportIssueKind>{ReportIssueKind::RunCancelled, "runCancelled"},
        EnumName<ReportIssueKind>{ReportIssueKind::MasterFailed, "masterFailed"},
        EnumName<ReportIssueKind>{ReportIssueKind::MasterNotReached, "masterNotReached"},
        EnumName<ReportIssueKind>{ReportIssueKind::OutputNotRetained, "outputNotRetained"},
        EnumName<ReportIssueKind>{ReportIssueKind::LightsWithoutFlat, "lightsWithoutFlat"},
        EnumName<ReportIssueKind>{ReportIssueKind::LightsWithoutDark, "lightsWithoutDark"},
        EnumName<ReportIssueKind>{ReportIssueKind::NoFlats, "noFlats"},
        EnumName<ReportIssueKind>{ReportIssueKind::NoDarks, "noDarks"},
        EnumName<ReportIssueKind>{ReportIssueKind::FlatMasterUncalibrated, "flatMasterUncalibrated"},
        EnumName<ReportIssueKind>{ReportIssueKind::CosmeticCorrectionWithoutDark, "cosmeticCorrectionWithoutDark"},
        EnumName<ReportIssueKind>{ReportIssueKind::RegistrationFailures, "registrationFailures"},
        EnumName<ReportIssueKind>{ReportIssueKind::MasterWithoutAstrometry, "masterWithoutAstrometry"},
        EnumName<ReportIssueKind>{ReportIssueKind::LowFrameUse, "lowFrameUse"},
        EnumName<ReportIssueKind>{ReportIssueKind::RegistrationSuccessLow, "registrationSuccessLow"},
        EnumName<ReportIssueKind>{ReportIssueKind::LightsLeftOutByStack, "lightsLeftOutByStack"},
        EnumName<ReportIssueKind>{ReportIssueKind::ExposureMetadataIncomplete, "exposureMetadataIncomplete"},
        EnumName<ReportIssueKind>{ReportIssueKind::StackedTwice, "stackedTwice"},
        EnumName<ReportIssueKind>{ReportIssueKind::MasterReused, "masterReused"},
        EnumName<ReportIssueKind>{ReportIssueKind::CalibrationMasterReused, "calibrationMasterReused"},
        EnumName<ReportIssueKind>{ReportIssueKind::PreCalibratedLights, "preCalibratedLights"},
        EnumName<ReportIssueKind>{ReportIssueKind::OutputNotRetainedByPolicy, "outputNotRetainedByPolicy"},
        EnumName<ReportIssueKind>{ReportIssueKind::RunMessage, "runMessage"},
    };
};

template <> struct EnumNames<ReportMasterKind>
{
    static constexpr std::array kNames = {
        EnumName<ReportMasterKind>{ReportMasterKind::Stack, "stack"},
        EnumName<ReportMasterKind>{ReportMasterKind::CombinedRgb, "combinedRgb"},
    };
};

template <> struct EnumNames<ReportMasterState>
{
    static constexpr std::array kNames = {
        EnumName<ReportMasterState>{ReportMasterState::Made, "made"},
        EnumName<ReportMasterState>{ReportMasterState::Reused, "reused"},
        EnumName<ReportMasterState>{ReportMasterState::Failed, "failed"},
        EnumName<ReportMasterState>{ReportMasterState::NotReached, "notReached"},
        EnumName<ReportMasterState>{ReportMasterState::Cancelled, "cancelled"},
    };
};

template <> struct EnumNames<ReportRetention>
{
    static constexpr std::array kNames = {
        EnumName<ReportRetention>{ReportRetention::Unknown, "unknown"},
        EnumName<ReportRetention>{ReportRetention::Retained, "retained"},
        EnumName<ReportRetention>{ReportRetention::NotRetainedByPolicy, "notRetainedByPolicy"},
        EnumName<ReportRetention>{ReportRetention::NotRetainedByArchiveFailure, "notRetainedByArchiveFailure"},
    };
};

template <> struct EnumNames<ReportAstrometryOutcome>
{
    static constexpr std::array kNames = {
        EnumName<ReportAstrometryOutcome>{ReportAstrometryOutcome::NotRecorded, "notRecorded"},
        EnumName<ReportAstrometryOutcome>{ReportAstrometryOutcome::Inherited, "inherited"},
        EnumName<ReportAstrometryOutcome>{ReportAstrometryOutcome::Solved, "solved"},
        EnumName<ReportAstrometryOutcome>{ReportAstrometryOutcome::None, "none"},
    };
};

template <> struct EnumNames<ReportFrameState>
{
    static constexpr std::array kNames = {
        EnumName<ReportFrameState>{ReportFrameState::Used, "used"},
        EnumName<ReportFrameState>{ReportFrameState::ExcludedByUser, "excludedByUser"},
        EnumName<ReportFrameState>{ReportFrameState::StackExcludedByUser, "stackExcludedByUser"},
        EnumName<ReportFrameState>{ReportFrameState::RegistrationFailed, "registrationFailed"},
        EnumName<ReportFrameState>{ReportFrameState::LeftOutByStack, "leftOutByStack"},
        EnumName<ReportFrameState>{ReportFrameState::ExcludedOnRestack, "excludedOnRestack"},
        EnumName<ReportFrameState>{ReportFrameState::NotStacked, "notStacked"},
    };
};

template <> struct EnumNames<ReportCalibrationKind>
{
    static constexpr std::array kNames = {
        EnumName<ReportCalibrationKind>{ReportCalibrationKind::Bias, "bias"},
        EnumName<ReportCalibrationKind>{ReportCalibrationKind::Dark, "dark"},
        EnumName<ReportCalibrationKind>{ReportCalibrationKind::Flat, "flat"},
    };
};

template <> struct EnumNames<ReportCalibrationState>
{
    static constexpr std::array kNames = {
        EnumName<ReportCalibrationState>{ReportCalibrationState::Made, "made"},
        EnumName<ReportCalibrationState>{ReportCalibrationState::Reused, "reused"},
        EnumName<ReportCalibrationState>{ReportCalibrationState::Supplied, "supplied"},
        EnumName<ReportCalibrationState>{ReportCalibrationState::Failed, "failed"},
    };
};

template <> struct EnumNames<ReportStageState>
{
    static constexpr std::array kNames = {
        EnumName<ReportStageState>{ReportStageState::Ran, "ran"},
        EnumName<ReportStageState>{ReportStageState::Partial, "partial"},
        EnumName<ReportStageState>{ReportStageState::Reused, "reused"},
        EnumName<ReportStageState>{ReportStageState::Failed, "failed"},
        EnumName<ReportStageState>{ReportStageState::Cancelled, "cancelled"},
        EnumName<ReportStageState>{ReportStageState::NotReached, "notReached"},
        EnumName<ReportStageState>{ReportStageState::Skipped, "skipped"},
    };
};

template <> struct EnumNames<ReportMapKind>
{
    static constexpr std::array kNames = {
        EnumName<ReportMapKind>{ReportMapKind::Rejection, "rejection"},
        EnumName<ReportMapKind>{ReportMapKind::Quality, "quality"},
    };
};

template <> struct EnumNames<ReportClusterMap>
{
    static constexpr std::array kNames = {
        EnumName<ReportClusterMap>{ReportClusterMap::Merged, "merged"},
        EnumName<ReportClusterMap>{ReportClusterMap::Low, "low"},
        EnumName<ReportClusterMap>{ReportClusterMap::High, "high"},
    };
};

struct Writer;
struct Reader;

/// How each kind of value is stored. A number that is not finite is stored as text, since JSON has no form for it; a
/// time is stored with its offset from UTC, to the millisecond.
struct Json
{
    [[nodiscard]] static QJsonValue write(const QString& value)
    {
        return value;
    }

    [[nodiscard]] static QJsonValue write(const bool value)
    {
        return value;
    }

    template <std::integral T>
        requires(!std::same_as<T, bool>)
    [[nodiscard]] static QJsonValue write(const T value)
    {
        return QJsonValue(static_cast<qint64>(value));
    }

    [[nodiscard]] static QJsonValue write(const double value)
    {
        if (std::isfinite(value))
        {
            return value;
        }
        if (std::isnan(value))
        {
            return QStringLiteral("NaN");
        }
        return value > 0.0 ? QStringLiteral("Infinity") : QStringLiteral("-Infinity");
    }

    [[nodiscard]] static QJsonValue write(const QStringList& value)
    {
        return QJsonArray::fromStringList(value);
    }

    [[nodiscard]] static QJsonValue write(const QDate& value)
    {
        return value.toString(Qt::ISODate);
    }

    [[nodiscard]] static QJsonValue write(const QDateTime& value)
    {
        return value.toOffsetFromUtc(value.offsetFromUtc()).toString(Qt::ISODateWithMs);
    }

    template <NamedEnum E> [[nodiscard]] static QJsonValue write(const E value)
    {
        for (const auto& [candidate, name] : EnumNames<E>::kNames)
        {
            if (candidate == value)
            {
                return QString::fromLatin1(name);
            }
        }
        return QString();
    }

    template <Described T> [[nodiscard]] static QJsonValue write(const T& value);

    template <class T> [[nodiscard]] static QJsonValue write(const std::vector<T>& values)
    {
        QJsonArray array;
        for (const T& value : values)
        {
            array.append(write(value));
        }
        return array;
    }

    static void read(const QJsonValue& stored, QString& value)
    {
        value = stored.toString();
    }

    static void read(const QJsonValue& stored, bool& value)
    {
        value = stored.toBool();
    }

    template <std::integral T>
        requires(!std::same_as<T, bool>)
    static void read(const QJsonValue& stored, T& value)
    {
        value = static_cast<T>(stored.toVariant().toLongLong());
    }

    static void read(const QJsonValue& stored, double& value)
    {
        if (stored.isString())
        {
            const QString text = stored.toString();
            value = text == QStringLiteral("Infinity")    ? std::numeric_limits<double>::infinity()
                    : text == QStringLiteral("-Infinity") ? -std::numeric_limits<double>::infinity()
                                                          : std::numeric_limits<double>::quiet_NaN();
            return;
        }
        value = stored.toDouble();
    }

    static void read(const QJsonValue& stored, QStringList& value)
    {
        value.clear();
        const QJsonArray array = stored.toArray();
        value.reserve(array.size());
        for (const QJsonValue& entry : array)
        {
            value.append(entry.toString());
        }
    }

    static void read(const QJsonValue& stored, QDate& value)
    {
        value = QDate::fromString(stored.toString(), Qt::ISODate);
    }

    static void read(const QJsonValue& stored, QDateTime& value)
    {
        value = QDateTime::fromString(stored.toString(), Qt::ISODateWithMs);
    }

    template <NamedEnum E> static void read(const QJsonValue& stored, E& value)
    {
        const QString text = stored.toString();
        for (const auto& [candidate, name] : EnumNames<E>::kNames)
        {
            if (text == QLatin1String(name))
            {
                value = candidate;
                return;
            }
        }
    }

    template <Described T> static void read(const QJsonValue& stored, T& value);

    template <class T> static void read(const QJsonValue& stored, std::vector<T>& values)
    {
        values.clear();
        const QJsonArray array = stored.toArray();
        values.reserve(static_cast<std::size_t>(array.size()));
        for (const QJsonValue& entry : array)
        {
            T value{};
            read(entry, value);
            values.push_back(std::move(value));
        }
    }
};

/// Writes each field of a structure into an object; a value that is not stated is left out.
struct Writer
{
    QJsonObject object;

    template <class T> void operator()(const char* key, const T& value)
    {
        object.insert(QLatin1String(key), Json::write(value));
    }

    template <class T> void operator()(const char* key, const std::optional<T>& value)
    {
        if (value.has_value())
        {
            object.insert(QLatin1String(key), Json::write(*value));
        }
    }
};

/// Reads each field of a structure from an object; a key that is absent leaves the field as it was made.
struct Reader
{
    const QJsonObject& object;

    template <class T> void operator()(const char* key, T& value) const
    {
        const auto found = object.constFind(QLatin1String(key));
        if (found != object.constEnd())
        {
            Json::read(*found, value);
        }
    }

    template <class T> void operator()(const char* key, std::optional<T>& value) const
    {
        const auto found = object.constFind(QLatin1String(key));
        if (found == object.constEnd() || found->isNull())
        {
            value.reset();
            return;
        }
        T stated{};
        Json::read(*found, stated);
        value = std::move(stated);
    }
};

template <Described T> QJsonValue Json::write(const T& value)
{
    Writer writer;
    Fields<T>::visit(writer, value);
    return writer.object;
}

template <Described T> void Json::read(const QJsonValue& stored, T& value)
{
    const QJsonObject object = stored.toObject();
    const Reader reader{object};
    Fields<T>::visit(reader, value);
}

template <> struct Fields<ReportSettingsRow>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("name", s.name);
        v("value", s.value);
        v("requested", s.requested);
        v("reason", s.reason);
    }
};

template <> struct Fields<ReportSettingsBlock>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("title", s.title);
        v("rows", s.rows);
    }
};

template <> struct Fields<ReportDecision>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("area", s.area);
        v("groupKey", s.groupKey);
        v("group", s.group);
        v("requested", s.requested);
        v("resolved", s.resolved);
        v("reason", s.reason);
    }
};

template <> struct Fields<ReportExposureBin>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("seconds", s.seconds);
        v("frames", s.frames);
    }
};

template <> struct Fields<ReportExposure>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("frames", s.frames);
        v("framesWithExposure", s.framesWithExposure);
        v("integrationMs", s.integrationMs);
        v("bins", s.bins);
    }
};

template <> struct Fields<ReportChannelStatistics>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("samples", s.samples);
        v("unclippedSamples", s.unclippedSamples);
        v("minimum", s.minimum);
        v("maximum", s.maximum);
        v("mean", s.mean);
        v("median", s.median);
        v("standardDeviation", s.standardDeviation);
        v("averageDeviation", s.averageDeviation);
        v("variance", s.variance);
        v("medianAbsoluteDeviation", s.medianAbsoluteDeviation);
        v("backgroundNoise", s.backgroundNoise);
        v("biweightMidvariance", s.biweightMidvariance);
        v("meanOfSquares", s.meanOfSquares);
        v("sumOfSquares", s.sumOfSquares);
        v("ikssLocation", s.ikssLocation);
        v("ikssScale", s.ikssScale);
    }
};

template <> struct Fields<ReportImageStatistics>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("width", s.width);
        v("height", s.height);
        v("channels", s.channels);
        v("channelStats", s.channelStats);
    }
};

template <> struct Fields<ReportSampleDistribution>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("lowest", s.lowest);
        v("highest", s.highest);
        v("counts", s.counts);
        v("outside", s.outside);
    }
};

template <> struct Fields<ReportPhase>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("name", s.name);
        v("status", s.status);
        v("durationMs", s.durationMs);
    }
};

template <> struct Fields<ReportRejectionCluster>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("map", s.map);
        v("channel", s.channel);
        v("pixels", s.pixels);
        v("minX", s.minX);
        v("minY", s.minY);
        v("maxX", s.maxX);
        v("maxY", s.maxY);
        v("centroidX", s.centroidX);
        v("centroidY", s.centroidY);
    }
};

template <> struct Fields<ReportStackRejection>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("global", s.global);
        v("lowestFrame", s.lowestFrame);
        v("meanFrame", s.meanFrame);
        v("medianFrame", s.medianFrame);
        v("highestFrame", s.highestFrame);
    }
};

template <> struct Fields<ReportStackWeights>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("lowest", s.lowest);
        v("highest", s.highest);
        v("deviation", s.deviation);
        v("efficiency", s.efficiency);
    }
};

template <> struct Fields<ReportLocalWeights>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("windowSize", s.windowSize);
        v("gridSpacing", s.gridSpacing);
        v("snrExponent", s.snrExponent);
        v("grids", s.grids);
        v("lowest", s.lowest);
        v("highest", s.highest);
        v("mean", s.mean);
        v("deviation", s.deviation);
    }
};

template <> struct Fields<ReportStackClusters>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("count", s.count);
        v("pixels", s.pixels);
        v("fraction", s.fraction);
        v("largestPixels", s.largestPixels);
        v("largestFraction", s.largestFraction);
        v("rejectedElements", s.rejectedElements);
        v("rejectedElementFraction", s.rejectedElementFraction);
        v("clusters", s.clusters);
    }
};

template <> struct Fields<ReportRestackExclusion>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("index", s.index);
        v("fileName", s.fileName);
        v("rejectedFraction", s.rejectedFraction);
    }
};

template <> struct Fields<ReportRestack>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("applied", s.applied);
        v("flaggedFrameThreshold", s.flaggedFrameThreshold);
        v("initialClusters", s.initialClusters);
        v("excluded", s.excluded);
    }
};

template <> struct Fields<ReportStack>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("inputFrames", s.inputFrames);
        v("effectiveFrames", s.effectiveFrames);
        v("measuredSnrGain", s.measuredSnrGain);
        v("theoreticalSnrGain", s.theoreticalSnrGain);
        v("efficiency", s.efficiency);
        v("lowestCoverage", s.lowestCoverage);
        v("medianCoverage", s.medianCoverage);
        v("highestCoverage", s.highestCoverage);
        v("coverageDeviation", s.coverageDeviation);
        v("normalization", s.normalization);
        v("rejection", s.rejection);
        v("weights", s.weights);
        v("localWeights", s.localWeights);
        v("clusters", s.clusters);
        v("restack", s.restack);
    }
};

template <> struct Fields<ReportMap>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("kind", s.kind);
        v("title", s.title);
        v("file", s.file);
        v("bytes", s.bytes);
        v("preview", s.preview);
        v("clusters", s.clusters);
        v("weightSum", s.weightSum);
    }
};

template <> struct Fields<ReportMasterSnapshot>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("mean", s.mean);
        v("backgroundNoise", s.backgroundNoise);
        v("snr", s.snr);
        v("dynamicRange", s.dynamicRange);
    }
};

template <> struct Fields<ReportMasterWeighting>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("mode", s.mode);
        v("minimumWeight", s.minimumWeight);
        v("floorApplies", s.floorApplies);
        v("framesWithMetrics", s.framesWithMetrics);
        v("framesRegistered", s.framesRegistered);
        v("framesAtFloor", s.framesAtFloor);
        v("weightedIntegrationMs", s.weightedIntegrationMs);
        v("medianWeight", s.medianWeight);
        v("medianQualityScore", s.medianQualityScore);
        v("medianZeroPoint", s.medianZeroPoint);
        v("medianAstrometricRmsArcsec", s.medianAstrometricRmsArcsec);
        v("medianCatalogMatches", s.medianCatalogMatches);
    }
};

template <> struct Fields<ReportFrame>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("fileName", s.fileName);
        v("path", s.path);
        v("state", s.state);
        v("stateDetail", s.stateDetail);
        v("night", s.night);
        v("capturedAt", s.capturedAt);
        v("exposureSeconds", s.exposureSeconds);
        v("registered", s.registered);
        v("stars", s.stars);
        v("fwhm", s.fwhm);
        v("eccentricity", s.eccentricity);
        v("snr", s.snr);
        v("backgroundNoise", s.backgroundNoise);
        v("qualityScore", s.qualityScore);
        v("zeroPoint", s.zeroPoint);
        v("zeroPointScatter", s.zeroPointScatter);
        v("astrometricRmsPixels", s.astrometricRmsPixels);
        v("astrometricRmsArcsec", s.astrometricRmsArcsec);
        v("limitingMagnitude", s.limitingMagnitude);
        v("catalogMatches", s.catalogMatches);
        v("shiftX", s.shiftX);
        v("shiftY", s.shiftY);
        v("transformKind", s.transformKind);
        v("normalizationScale", s.normalizationScale);
        v("normalizationOffset", s.normalizationOffset);
        v("weight", s.weight);
        v("stackShare", s.stackShare);
        v("rejectedFraction", s.rejectedFraction);
        v("gain", s.gain);
        v("offset", s.offset);
        v("temperature", s.temperature);
    }
};

template <> struct Fields<ReportBaseline>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("runId", s.runId);
        v("archivedAt", s.archivedAt);
        v("made", s.made);
        v("frames", s.frames);
        v("integrationMs", s.integrationMs);
        v("backgroundNoise", s.backgroundNoise);
        v("snr", s.snr);
        v("fwhm", s.fwhm);
        v("effectiveFrames", s.effectiveFrames);
        v("rejectedFraction", s.rejectedFraction);
        v("preview", s.preview);
        v("detail", s.detail);
    }
};

template <> struct Fields<ReportAstrometrySolution>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("centreRightAscension", s.centreRightAscension);
        v("centreDeclination", s.centreDeclination);
        v("pixelScale", s.pixelScale);
        v("fieldWidth", s.fieldWidth);
        v("fieldHeight", s.fieldHeight);
        v("orientation", s.orientation);
        v("mirrored", s.mirrored);
    }
};

template <> struct Fields<ReportMasterPhotometry>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("frames", s.frames);
        v("medianZeroPoint", s.medianZeroPoint);
        v("medianZeroPointScatter", s.medianZeroPointScatter);
        v("medianLimitingMagnitude", s.medianLimitingMagnitude);
        v("medianCatalogMatches", s.medianCatalogMatches);
    }
};

template <> struct Fields<ReportMasterCalibration>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("bias", s.bias);
        v("dark", s.dark);
        v("flat", s.flat);
    }
};

template <> struct Fields<ReportMaster>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("key", s.key);
        v("slug", s.slug);
        v("previewFolder", s.previewFolder);
        v("name", s.name);
        v("target", s.target);
        v("filter", s.filter);
        v("kind", s.kind);
        v("state", s.state);
        v("failure", s.failure);
        v("madeByRun", s.madeByRun);
        v("fileName", s.fileName);
        v("file", s.file);
        v("bytes", s.bytes);
        v("retention", s.retention);
        v("retentionReason", s.retentionReason);
        v("lightsCombined", s.lightsCombined);
        v("exposure", s.exposure);
        v("captureResolution", s.captureResolution);
        v("overlapRetained", s.overlapRetained);
        v("calibration", s.calibration);
        v("lightGroups", s.lightGroups);
        v("registrationGroupKey", s.registrationGroupKey);
        v("registrationInputPolicy", s.registrationInputPolicy);
        v("astrometry", s.astrometry);
        v("astrometryDetail", s.astrometryDetail);
        v("solution", s.solution);
        v("photometry", s.photometry);
        v("stackingSummary", s.stackingSummary);
        v("drizzleSummary", s.drizzleSummary);
        v("durationMs", s.durationMs);
        v("finalization", s.finalization);
        v("statistics", s.statistics);
        v("distribution", s.distribution);
        v("snapshot", s.snapshot);
        v("stack", s.stack);
        v("preview", s.preview);
        v("detail", s.detail);
        v("maps", s.maps);
        v("rejectionMapsExpected", s.rejectionMapsExpected);
        v("settings", s.settings);
        v("weighting", s.weighting);
        v("frames", s.frames);
        v("baseline", s.baseline);
    }
};

template <> struct Fields<ReportCalibrationMaster>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("kind", s.kind);
        v("key", s.key);
        v("slug", s.slug);
        v("name", s.name);
        v("fileName", s.fileName);
        v("file", s.file);
        v("state", s.state);
        v("frames", s.frames);
        v("inputFiles", s.inputFiles);
        v("exposureSeconds", s.exposureSeconds);
        v("bytes", s.bytes);
        v("modifiedAt", s.modifiedAt);
        v("biasMaster", s.biasMaster);
        v("darkMaster", s.darkMaster);
        v("biasMasterSlug", s.biasMasterSlug);
        v("darkMasterSlug", s.darkMasterSlug);
        v("stackingSummary", s.stackingSummary);
        v("settings", s.settings);
        v("level", s.level);
        v("noise", s.noise);
        v("measuredFraction", s.measuredFraction);
        v("measuredByRun", s.measuredByRun);
        v("preview", s.preview);
        v("usedBy", s.usedBy);
    }
};

template <> struct Fields<ReportLightGroup>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("key", s.key);
        v("name", s.name);
        v("masterKey", s.masterKey);
        v("lights", s.lights);
        v("preCalibrated", s.preCalibrated);
        v("biasMaster", s.biasMaster);
        v("darkMaster", s.darkMaster);
        v("flatMaster", s.flatMaster);
        v("biasMasterSlug", s.biasMasterSlug);
        v("darkMasterSlug", s.darkMasterSlug);
        v("flatMasterSlug", s.flatMasterSlug);
        v("settings", s.settings);
    }
};

template <> struct Fields<ReportCometRun>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("status", s.status);
        v("mode", s.mode);
        v("fitModel", s.fitModel);
        v("rmsPixels", s.rmsPixels);
        v("largestResidualPixels", s.largestResidualPixels);
        v("warnings", s.warnings);
        v("inputsAccepted", s.inputsAccepted);
        v("inputsRejected", s.inputsRejected);
        v("inputsPredictedOnly", s.inputsPredictedOnly);
        v("pointsManual", s.pointsManual);
        v("pointsAutomatic", s.pointsAutomatic);
        v("pointsPredictedOnly", s.pointsPredictedOnly);
        v("pointsOutliers", s.pointsOutliers);
        v("readiness", s.readiness);
        v("transformsSucceeded", s.transformsSucceeded);
        v("transformsFailed", s.transformsFailed);
        v("transformsMissing", s.transformsMissing);
        v("readinessFailure", s.readinessFailure);
        v("outputPlane", s.outputPlane);
    }
};

template <> struct Fields<ReportCatalogHandoff>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("settingsVersion", s.settingsVersion);
        v("frames", s.frames);
        v("references", s.references);
        v("sources", s.sources);
        v("satisfied", s.satisfied);
        v("missingSource", s.missingSource);
        v("missingChunkOrCache", s.missingChunkOrCache);
        v("manifestMismatch", s.manifestMismatch);
        v("staleMatch", s.staleMatch);
        v("rejectedQualityFields", s.rejectedQualityFields);
        v("untested", s.untested);
        v("astrometricCatalog", s.astrometricCatalog);
        v("photometricCatalog", s.photometricCatalog);
        v("medianResidualPixels", s.medianResidualPixels);
        v("medianSeparationArcsec", s.medianSeparationArcsec);
    }
};

template <> struct Fields<ReportTransformUse>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("kind", s.kind);
        v("frames", s.frames);
    }
};

template <> struct Fields<ReportRegistrationGroup>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("key", s.key);
        v("slug", s.slug);
        v("name", s.name);
        v("target", s.target);
        v("lights", s.lights);
        v("registered", s.registered);
        v("failed", s.failed);
        v("reference", s.reference);
        v("referenceChosenAutomatically", s.referenceChosenAutomatically);
        v("alignmentMode", s.alignmentMode);
        v("transform", s.transform);
        v("transformsUsed", s.transformsUsed);
        v("aligned", s.aligned);
        v("distortionCorrected", s.distortionCorrected);
        v("chromaticAberrationCorrected", s.chromaticAberrationCorrected);
        v("chromaticAberrationPassedThrough", s.chromaticAberrationPassedThrough);
        v("overlap", s.overlap);
        v("medianAstrometricRmsArcsec", s.medianAstrometricRmsArcsec);
        v("medianCatalogMatches", s.medianCatalogMatches);
        v("cometRuns", s.cometRuns);
        v("catalogHandoff", s.catalogHandoff);
        v("settings", s.settings);
        v("masters", s.masters);
    }
};

template <> struct Fields<ReportStage>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("key", s.key);
        v("name", s.name);
        v("state", s.state);
        v("status", s.status);
        v("durationMs", s.durationMs);
        v("groups", s.groups);
        v("note", s.note);
    }
};

template <> struct Fields<ReportIssue>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("kind", s.kind);
        v("severity", s.severity);
        v("title", s.title);
        v("detail", s.detail);
        v("action", s.action);
        v("groupKey", s.groupKey);
        v("anchor", s.anchor);
    }
};

template <> struct Fields<ReportNotStated>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("what", s.what);
        v("why", s.why);
    }
};

template <> struct Fields<ReportGroupCounts>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("total", s.total);
        v("made", s.made);
        v("reused", s.reused);
        v("failed", s.failed);
        v("notReached", s.notReached);
    }
};

template <> struct Fields<ReportPreparationTiming>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("manifestBuildMs", s.manifestBuildMs);
        v("planAndValidationMs", s.planAndValidationMs);
    }
};

template <> struct Fields<ReportExecutorTiming>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("stageExecutionMs", s.stageExecutionMs);
        v("stateCommitMs", s.stateCommitMs);
        v("archiveMs", s.archiveMs);
        v("reportMs", s.reportMs);
    }
};

template <> struct Fields<ReportCounter>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("key", s.key);
        v("name", s.name);
        v("value", s.value);
    }
};

template <> struct Fields<ReportEngineMemory>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("peakBytes", s.peakBytes);
        v("peakBytesBeforeRun", s.peakBytesBeforeRun);
        v("liveBytesAtEnd", s.liveBytesAtEnd);
        v("allocations", s.allocations);
        v("bytesAllocated", s.bytesAllocated);
        v("deniedAllocations", s.deniedAllocations);
    }
};

template <> struct Fields<ReportDiskUse>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("kind", s.kind);
        v("bytes", s.bytes);
        v("files", s.files);
    }
};

template <> struct Fields<ReportNightFilter>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("filter", s.filter);
        v("frames", s.frames);
        v("integrationMs", s.integrationMs);
    }
};

template <> struct Fields<ReportNight>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("date", s.date);
        v("frames", s.frames);
        v("integrationMs", s.integrationMs);
        v("filters", s.filters);
    }
};

template <> struct Fields<ReportAcquisitionRow>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("groupKey", s.groupKey);
        v("group", s.group);
        v("target", s.target);
        v("filter", s.filter);
        v("exposure", s.exposure);
        v("share", s.share);
        v("lowestTemperature", s.lowestTemperature);
        v("highestTemperature", s.highestTemperature);
        v("gain", s.gain);
        v("offset", s.offset);
    }
};

template <> struct Fields<ReportEquipmentRow>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("masters", s.masters);
        v("target", s.target);
        v("camera", s.camera);
        v("captureResolution", s.captureResolution);
        v("outputResolution", s.outputResolution);
        v("gain", s.gain);
        v("offset", s.offset);
        v("telescope", s.telescope);
        v("site", s.site);
    }
};

template <> struct Fields<ReportHeaderField>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("name", s.name);
        v("present", s.present);
    }
};

template <> struct Fields<ReportHeaderCompleteness>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("lights", s.lights);
        v("fields", s.fields);
    }
};

template <> struct Fields<ReportTarget>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("name", s.name);
        v("frames", s.frames);
        v("integrationMs", s.integrationMs);
        v("filters", s.filters);
        v("masters", s.masters);
        v("snr", s.snr);
        v("overlapRetained", s.overlapRetained);
    }
};

template <> struct Fields<ReportFrameStateCount>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("state", s.state);
        v("frames", s.frames);
    }
};

template <> struct Fields<ReportRun>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("outcome", s.outcome);
        v("verdict", s.verdict);
        v("title", s.title);
        v("process", s.process);
        v("profile", s.profile);
        v("profileId", s.profileId);
        v("targets", s.targets);
        v("mode", s.mode);
        v("scope", s.scope);
        v("scopeKey", s.scopeKey);
        v("scopeName", s.scopeName);
        v("incremental", s.incremental);
        v("changedGroups", s.changedGroups);
        v("groups", s.groups);
        v("frames", s.frames);
        v("framesUsed", s.framesUsed);
        v("frameStates", s.frameStates);
        v("capturedMs", s.capturedMs);
        v("integratedMs", s.integratedMs);
        v("warnings", s.warnings);
        v("errors", s.errors);
        v("processingMs", s.processingMs);
        v("preparation", s.preparation);
        v("executor", s.executor);
        v("counters", s.counters);
        v("cacheDiagnostics", s.cacheDiagnostics);
        v("engineMemory", s.engineMemory);
        v("disk", s.disk);
        v("sessionNotes", s.sessionNotes);
        v("sessionNotesChangedAt", s.sessionNotesChangedAt);
        v("nights", s.nights);
        v("acquisition", s.acquisition);
        v("equipment", s.equipment);
        v("headers", s.headers);
        v("lowestTemperature", s.lowestTemperature);
        v("highestTemperature", s.highestTemperature);
        v("captureResolution", s.captureResolution);
        v("applicationVersion", s.applicationVersion);
        v("executionId", s.executionId);
        v("archivedAt", s.archivedAt);
        v("timeZoneId", s.timeZoneId);
        v("utcOffsetSeconds", s.utcOffsetSeconds);
        v("platform", s.platform);
        v("archiveFormat", s.archiveFormat);
        v("manifestFormat", s.manifestFormat);
        v("fingerprint", s.fingerprint);
        v("settingsFingerprint", s.settingsFingerprint);
        v("inputsFingerprint", s.inputsFingerprint);
        v("outputFolder", s.outputFolder);
        v("settings", s.settings);
        v("log", s.log);
    }
};

template <> struct Fields<ReportRunTotals>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("frames", s.frames);
        v("integrationMs", s.integrationMs);
        v("outputBytes", s.outputBytes);
        v("snr", s.snr);
        v("overlapRetained", s.overlapRetained);
    }
};

template <> struct Fields<ReportSettingChange>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("stage", s.stage);
        v("groupKey", s.groupKey);
        v("group", s.group);
        v("name", s.name);
        v("before", s.before);
        v("after", s.after);
    }
};

template <> struct Fields<ReportInputChange>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("groupKey", s.groupKey);
        v("group", s.group);
        v("added", s.added);
        v("removed", s.removed);
    }
};

template <> struct Fields<ReportComparison>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("baselineRunId", s.baselineRunId);
        v("baselineArchivedAt", s.baselineArchivedAt);
        v("current", s.current);
        v("previous", s.previous);
        v("settingsChanged", s.settingsChanged);
        v("inputsChanged", s.inputsChanged);
        v("previousApplicationVersion", s.previousApplicationVersion);
        v("previousFingerprint", s.previousFingerprint);
        v("previousSettingsFingerprint", s.previousSettingsFingerprint);
        v("previousInputsFingerprint", s.previousInputsFingerprint);
    }
};

template <> struct Fields<ReportHistoryEntry>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("runId", s.runId);
        v("archivedAt", s.archivedAt);
        v("outcome", s.outcome);
        v("framesUsed", s.framesUsed);
        v("frames", s.frames);
        v("integrationMs", s.integrationMs);
        v("masters", s.masters);
        v("backgroundNoise", s.backgroundNoise);
        v("report", s.report);
    }
};

template <> struct Fields<ReportModel>
{
    using Described = void;

    template <class V, class S> static void visit(V& v, S& s)
    {
        v("reportSchema", s.reportSchema);
        v("run", s.run);
        v("issues", s.issues);
        v("targets", s.targets);
        v("masters", s.masters);
        v("calibrationMasters", s.calibrationMasters);
        v("lightGroups", s.lightGroups);
        v("registrationGroups", s.registrationGroups);
        v("stages", s.stages);
        v("decisions", s.decisions);
        v("comparison", s.comparison);
        v("history", s.history);
        v("notStated", s.notStated);
    }
};

[[nodiscard]] ReportNotStated refusal(const QString& why)
{
    return ReportNotStated{.what = QCoreApplication::translate("ImageIntegrationReport", "The report this run stored"),
                           .why = why};
}
} // namespace

QJsonObject ReportModelJson::toJson(const ReportModel& model)
{
    Writer writer;
    Fields<ReportModel>::visit(writer, model);
    return writer.object;
}

ReportModelRead ReportModelJson::fromJson(const QJsonObject& document)
{
    const QJsonValue stated = document.value(QLatin1String("reportSchema"));
    const int schema = stated.isDouble() ? stated.toInt(0) : 0;
    if (schema < 1)
    {
        return ReportModelRead{
            .model = std::nullopt,
            .refusal = refusal(QCoreApplication::translate("ImageIntegrationReport",
                                                           "The stored report states no format, so it is not read."))};
    }
    if (schema > kReportSchema)
    {
        return ReportModelRead{
            .model = std::nullopt,
            .refusal = refusal(QCoreApplication::translate(
                                   "ImageIntegrationReport",
                                   "The stored report is of format %1, written by a newer version of the application; "
                                   "this version reads up to format %2.")
                                   .arg(schema)
                                   .arg(kReportSchema))};
    }

    ReportModel model;
    const Reader reader{document};
    Fields<ReportModel>::visit(reader, model);
    return ReportModelRead{.model = std::move(model), .refusal = std::nullopt};
}
} // namespace ImageIntegration::Reporting
