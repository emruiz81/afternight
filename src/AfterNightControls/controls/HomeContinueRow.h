// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// Home's Continue row: the featured image - the featured file card and its presenter, over the recent images, with
/// its Continue and its pin, kept in the dashboard's settings - and beside it the last project's card,
/// with its picture, what it holds and Open project, and the last sequence's card, with its picture, its frames and its
/// Resume.

#include "AfterNightControlsExport.h"
#include "controls/HomeParts.h"
#include "controls/HomeProjectsRow.h"

#include <QString>
#include <QWidget>
#include <verdigris/wobjectdefs.h>

#include <deque>
#include <memory>
#include <optional>

class DashboardThumbnailCache;
class QAbstractButton;

namespace an::ui
{

class FeaturedFileCard;

class AFTERNIGHTCONTROLS_API HomeContinueRow : public QWidget
{
    W_OBJECT(HomeContinueRow)

public:
    explicit HomeContinueRow(QWidget* parent = nullptr);
    ~HomeContinueRow() override;

    /// The cache of decoded thumbnails the featured image shares with the recent files; null keeps the row's own.
    void setThumbnailCache(DashboardThumbnailCache* cache);

    /// What reads a sequence's facts; `probeHomeRecentFile` unless set.
    void setFactsProbe(HomeRecentProbe probe);

    /// The recent files, most recent first and normalized: the featured image is the pinned one, or else the most
    /// recent image with a thumbnail; the sequence card shows the most recent sequence. A pinned file no longer among
    /// them, or no longer on disk, is unpinned.
    void setRecentFiles(const std::deque<QString>& recentFiles);

    /// Takes the icons the application's icon provider gives now.
    void refreshIcons();

    /// The featured file card the row shows.
    [[nodiscard]] FeaturedFileCard* featuredCard() const;

    /// The featured image; empty while there is none.
    [[nodiscard]] QString featuredFilePath() const;

    /// Whether the featured image is pinned.
    [[nodiscard]] bool isFeaturedPinned() const;

    /// The last sequence's card.
    [[nodiscard]] QWidget* sequenceCard() const;

    /// The last sequence; empty when no recent file is a sequence.
    [[nodiscard]] QString lastSequencePath() const;

    /// Whether the sequence's card shows the sequence's thumbnail.
    [[nodiscard]] bool hasSequencePicture() const;

    /// The facts the sequence's card shows: its container, its frames and its size.
    [[nodiscard]] QString sequenceFactsText() const;

    /// The sequence card's Resume.
    [[nodiscard]] QAbstractButton* resumeButton() const;

    /// The project used last, shown between the featured image and the last sequence; none hides its card.
    void setLastProject(const std::optional<HomeProjectSummary>& project);

    /// The last project's card, its name, what it holds and its Open project.
    [[nodiscard]] QWidget* lastProjectCard() const;
    [[nodiscard]] QString lastProjectName() const;
    [[nodiscard]] QString lastProjectCountsText() const;
    [[nodiscard]] QAbstractButton* openProjectButton() const;

    /// Open project on the last project's card: its folder.
    void openProjectRequested(const QString& folder) W_SIGNAL(openProjectRequested, folder);

    /// A recent file is asked to be opened: the featured image's Continue, the sequence's Resume.
    void openRecentFileRequested(const QString& filePath) W_SIGNAL(openRecentFileRequested, filePath);

protected:
    bool event(QEvent* event) override;

private:
    void setPinned(const QString& filePath, bool pinned);
    void showSequence(const QString& filePath);
    void showProjectPicture();
    void refreshFeatured();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui
