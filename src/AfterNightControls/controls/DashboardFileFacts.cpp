// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The dashboard's probe of a file's size and acquisition facts, and the texts its cards show.

#include "stdafx.hpp"
#include "controls/DashboardFileFacts.h"

#include "controls/IAppContext.h"
#include "session/IEngineSessionProvider.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecMetadataProjection.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/ImageMetadata.h>

#include <QDateTime>
#include <QStringList>
#include <QTimeZone>

#include <array>
#include <limits>

namespace an::ui
{

namespace
{

/// The suffixes whose headers the dashboard reads.
constexpr std::array<std::string_view, 11u> kAstroSuffixes{"FITS", "FIT", "XISF", "TIFF", "TIF", "CR2",
                                                           "CR3",  "NEF", "ARW",  "DNG",  "RAF"};

/// A header's text without the space around it.
[[nodiscard]] QString factText(const std::string& value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size())).trimmed();
}

/// The day an instant falls on, in UTC, as a frame's header states its time.
[[nodiscard]] QDate dayOf(const an::UtcInstant instant)
{
    return QDateTime::fromMSecsSinceEpoch(instant.nanosecondsSinceEpoch / 1000000, QTimeZone(QTimeZone::UTC)).date();
}

} // namespace

bool isDashboardAstroFormat(const QString& suffix)
{
    const QString upper = suffix.trimmed().toUpper();
    for (const std::string_view known : kAstroSuffixes)
    {
        if (upper == QLatin1String(known.data(), static_cast<qsizetype>(known.size())))
        {
            return true;
        }
    }
    return false;
}

std::optional<DashboardFileFacts> probeDashboardFile(const an::consumer::EngineSession& session, const QString& path)
{
    // Background work: it runs when a thread is free and is never waited for.
    an::Result<an::consumer::OperationScope> scope =
        session.tryAdmit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Background});
    if (!scope.hasValue())
    {
        return std::nullopt;
    }
    const std::string file = path.toUtf8().toStdString();
    an::Result<an::CodecImageInfo> probed = an::probeImage(
        scope->engine(), scope->execution(), an::CodecReadRequest{.path = file, .validatePayload = false});
    if (!probed.hasValue())
    {
        return std::nullopt;
    }
    const std::optional<an::ImageDescriptor> descriptor = probed->descriptor.toImageDescriptor();
    if (!descriptor.has_value())
    {
        return std::nullopt;
    }

    DashboardFileFacts facts;
    const std::uint64_t width = descriptor->extent().width();
    const std::uint64_t height = descriptor->extent().height();
    if (width > 0u && height > 0u && width <= static_cast<std::uint64_t>((std::numeric_limits<int>::max)()) &&
        height <= static_cast<std::uint64_t>((std::numeric_limits<int>::max)()))
    {
        facts.size = QSize(static_cast<int>(width), static_cast<int>(height));
    }

    // A camera RAW probe states the typed record and no card; FITS, XISF and TIFF state cards. A fact is read from
    // the record the decoder stated, and from the cards where the record says nothing.
    const an::AcquisitionMetadata& stated = probed->metadata.acquisition();
    const an::AcquisitionProjection projected = an::projectAcquisition(probed->metadata.codec());
    const an::AcquisitionMetadataRequest& carded = projected.acquisition;
    const auto either = [](const std::string& typed, const std::string& card) {
        const QString text = factText(typed);
        return text.isEmpty() ? factText(card) : text;
    };
    facts.object = either(stated.objectName(), carded.objectName);
    facts.filter = either(stated.filterName(), carded.filterName);
    facts.camera = either(stated.instrumentName(), carded.instrumentName);
    facts.telescope = either(stated.telescopeName(), carded.telescopeName);
    facts.exposureSeconds = stated.exposureSeconds().has_value() ? stated.exposureSeconds() : carded.exposureSeconds;
    if (const std::optional<an::UtcInstant> start =
            stated.exposureStart().has_value() ? stated.exposureStart() : carded.exposureStart;
        start.has_value())
    {
        facts.date = dayOf(*start);
    }
    else if (projected.creationTime.has_value())
    {
        facts.date = dayOf(*projected.creationTime);
    }
    return facts;
}

std::optional<DashboardFileFacts> probeDashboardFile(const QString& path)
{
    auto* context = appContext();
    IEngineSessionProvider* provider = context ? context->getEngineSessionProvider() : nullptr;
    if (provider == nullptr)
    {
        return std::nullopt;
    }
    return probeDashboardFile(provider->engineSession(), path);
}

QString dashboardExposureText(const double seconds)
{
    if (seconds >= 60.0)
    {
        const int minutes = static_cast<int>(seconds) / 60;
        const int remainder = static_cast<int>(seconds) % 60;
        return remainder > 0 ? QStringLiteral("%1m %2s").arg(minutes).arg(remainder)
                             : QStringLiteral("%1m").arg(minutes);
    }
    return QStringLiteral("%1s").arg(seconds, 0, 'g', 4);
}

QString compactDashboardSummary(const DashboardFileFacts& facts)
{
    QStringList parts;
    if (!facts.object.isEmpty())
    {
        parts << facts.object;
    }
    if (!facts.filter.isEmpty())
    {
        parts << facts.filter;
    }
    if (facts.exposureSeconds.has_value() && *facts.exposureSeconds > 0.0)
    {
        parts << dashboardExposureText(*facts.exposureSeconds);
    }
    if (!facts.camera.isEmpty())
    {
        parts << facts.camera;
    }
    return parts.join(QStringLiteral(" · "));
}

} // namespace an::ui
