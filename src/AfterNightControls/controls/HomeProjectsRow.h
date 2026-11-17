// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// Home's Projects row: the four projects used last as cards - each with its picture, its name, its object, its folder,
/// what it holds, its size and when it was opened - then the Open project... tile. What a card says of a project is
/// given to it (`HomeProjectSummary`): the shell reads it from the project's folder off the GUI thread.

#include "AfterNightControlsExport.h"

#include <QAbstractButton>
#include <QDateTime>
#include <QString>
#include <QWidget>
#include <verdigris/wobjectdefs.h>

#include <cstddef>
#include <memory>
#include <vector>

class QEnterEvent;
class QFocusEvent;
class QPaintEvent;

namespace an::ui
{

class AnTag;
class HomeText;

/// What Home says of a project: what its card shows.
struct HomeProjectSummary
{
    /// The project's folder, which opening it opens.
    QString folder;
    QString name;

    /// The object the project is about, and where that came from - "the OBJECT card", "a plate solution"; both empty
    /// for none.
    QString object;
    QString objectSource;

    int images = 0;
    int sequences = 0;
    int integrations = 0;

    /// The undo steps its images' histories keep.
    std::size_t steps = 0;
    int workspaces = 0;

    /// The bytes its folder holds; negative when not known.
    qint64 bytes = -1;

    QDateTime lastOpened;
    bool fromIntegrationJob = false;

    /// An image of the project whose picture stands for it: the one opened last; empty for none.
    QString picture;

    /// Whether its folder holds the project: false for one on a drive that is not there.
    bool present = true;
};

/// What a project holds, as one line: "6 images · 2 integrations · 41 steps", the sequences and the integrations only
/// when there are some. `history` writes "history steps", as the Continue row's card does, and leaves the sequences
/// out.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString homeProjectCountsText(const HomeProjectSummary& summary, bool history);

/// A size in gigabytes to one decimal, as the person's locale writes it: "71.4 GB"; empty for a negative size.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString homeSizeText(qint64 bytes);

/// A project's card in the Projects row: a button that opens its project.
class AFTERNIGHTCONTROLS_API HomeProjectCard : public QAbstractButton
{
    W_OBJECT(HomeProjectCard)

public:
    explicit HomeProjectCard(QWidget* parent = nullptr);
    ~HomeProjectCard() override;

    void setSummary(const HomeProjectSummary& summary);
    [[nodiscard]] const HomeProjectSummary& summary() const noexcept;

    /// What the card says: its counts, its foot - the size and the day it was opened - and its folder.
    [[nodiscard]] QString countsText() const;
    [[nodiscard]] QString footText() const;
    [[nodiscard]] QString folderText() const;

    /// The object's tag, shown when the project has one; the origin's, "from an integration job", when one made it.
    [[nodiscard]] AnTag* objectTag() const;
    [[nodiscard]] AnTag* originTag() const;

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

class AFTERNIGHTCONTROLS_API HomeProjectsRow : public QWidget
{
    W_OBJECT(HomeProjectsRow)

public:
    /// How many projects the row shows.
    static constexpr std::size_t kShown = 4;

    explicit HomeProjectsRow(QWidget* parent = nullptr);
    ~HomeProjectsRow() override;

    /// The projects used last, most recent first; the row shows the first four.
    void setProjects(const std::vector<HomeProjectSummary>& projects);

    /// The cards shown, in their order.
    [[nodiscard]] std::vector<HomeProjectCard*> cards() const;

    /// The Open project... tile.
    [[nodiscard]] QAbstractButton* openProjectTile() const;

    /// Takes the icons the application's icon provider gives now.
    void refreshIcons();

    /// A card was clicked: its project's folder.
    void openProjectRequested(const QString& folder) W_SIGNAL(openProjectRequested, folder);

    /// The Open project... tile was clicked.
    void openOtherProjectRequested() W_SIGNAL(openOtherProjectRequested);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui
