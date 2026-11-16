// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the dashboard's recent-file cards and its featured file say about a file beyond its name: its size and the
/// acquisition facts its header states, read without decoding a pixel.
///
/// A card is read on the dashboard's worker and, for a thumbnail already cached, on the GUI thread; either way the
/// probe is background work that runs only when the session has a thread free and is never waited for, so a busy
/// session leaves the card with its file facts alone, as a file that states nothing does.
#pragma once

#include "AfterNightControlsExport.h"

#include <QDate>
#include <QSize>
#include <QString>

#include <cstdint>
#include <optional>

namespace an::consumer
{
class EngineSession;
} // namespace an::consumer

namespace an::ui
{

/// What a probe read of a file.
struct DashboardFileFacts
{
    /// The image's width and height in pixels.
    QSize size;

    /// The target, from `OBJECT`.
    QString object;

    /// The filter, from `FILTER`.
    QString filter;

    /// The camera, from `Exif.Image.Model` or `INSTRUME`.
    QString camera;

    /// The telescope, from `TELESCOP`.
    QString telescope;

    /// The exposure in seconds, when the file states one.
    std::optional<double> exposureSeconds;

    /// The day the frame was taken - `DATE-OBS` or `Exif.Photo.DateTimeOriginal` - or, failing those, the day the file
    /// says it was made.
    std::optional<QDate> date;
};

/// Whether the dashboard reads a file's header for its card: FITS, XISF, TIFF and the camera RAW formats it lists,
/// by suffix, case aside.
///
/// @param suffix the file's suffix, without the dot.
/// @return true when the card reads the header.
[[nodiscard]] AFTERNIGHTCONTROLS_API bool isDashboardAstroFormat(const QString& suffix);

/// Probes a file's description and header in a session, as background work that never waits: refused when the session
/// has no thread free.
///
/// Thread-safe.
///
/// @param session the process's session.
/// @param path the file.
/// @return the facts, or nothing when the session refused, the file cannot be probed or holds samples the engine does
///         not process.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::optional<DashboardFileFacts>
probeDashboardFile(const an::consumer::EngineSession& session, const QString& path);

/// Probes a file in the session the application context lends, when it lends one.
///
/// @param path the file.
/// @return the facts, or nothing as `probeDashboardFile` answers, and nothing where no session is lent.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::optional<DashboardFileFacts> probeDashboardFile(const QString& path);

/// An exposure as a card shows it: whole minutes and seconds from a minute up, seconds to four significant digits
/// below it.
///
/// @param seconds the exposure.
/// @return the text, as "5m", "2m 30s" or "0.25s".
[[nodiscard]] AFTERNIGHTCONTROLS_API QString dashboardExposureText(double seconds);

/// A recent-file card's one-line summary: the target, the filter, the exposure and the camera a file states, each
/// that is stated, separated by middle dots.
///
/// @param facts what the probe read.
/// @return the line; empty when the file states none of them.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString compactDashboardSummary(const DashboardFileFacts& facts);

/// What a recent sequence's card says about it beyond its name: its container, the size of a frame and how many frames
/// it holds, as its container states them.
struct DashboardSequenceFacts
{
    /// The container, as a card names it: "SER", "Image list", "AVI", "MP4" or "Video".
    QString container;

    /// A frame's width and height in pixels; invalid when the frames describe themselves one by one.
    QSize frameSize;

    /// How many frames it holds; absent when the container does not state it.
    std::optional<std::uint64_t> frameCount;
};

/// Whether a file is a sequence the product reads - a SER, an image list or a video - by the container its name says,
/// case aside.
///
/// @param path the file.
/// @return true when it names a sequence the product opens.
[[nodiscard]] AFTERNIGHTCONTROLS_API bool isDashboardSequence(const QString& path);

/// Probes a sequence file's container in a session, as background work that never waits: refused when the session has
/// no thread free. The container is opened, its description read and it is closed; no frame is decoded.
///
/// Thread-safe.
///
/// @param session the process's session.
/// @param path the file.
/// @return the facts, or nothing when the session refused, the file is no sequence the product reads or cannot be
///         opened.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::optional<DashboardSequenceFacts>
probeDashboardSequence(const an::consumer::EngineSession& session, const QString& path);

/// Probes a sequence file in the session the application context lends, when it lends one.
///
/// @param path the file.
/// @return the facts, or nothing as `probeDashboardSequence` answers, and nothing where no session is lent.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::optional<DashboardSequenceFacts> probeDashboardSequence(const QString& path);

} // namespace an::ui
