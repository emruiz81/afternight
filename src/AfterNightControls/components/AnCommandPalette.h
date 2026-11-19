// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The command palette: the popup that shows what a command source answers as a person types, and runs what they
/// choose.

#include "AfterNightControlsExport.h"
#include "components/ICommandSource.h"

#include <QIcon>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <verdigris/wobjectdefs.h>

#include <functional>
#include <memory>
#include <vector>

class QAbstractButton;
class QEvent;
class QHideEvent;
class QLineEdit;
class QListView;
class QMouseEvent;
class QPaintEvent;

namespace an::ui
{

/// The command palette: a popup the palette's width on the third surface, with a hairline border, the cards' corners
/// and the raised parts' elevation. At its top a query row - a search icon, the query in a field of its own, the key
/// hint Esc - and under it the body: the groups the source answers, each under a caps header in the tertiary text, its
/// rows the palette's row height with an icon, the title and its quiet tag when it has one - its tooltip saying what
/// the tag means - and at the right the subtitle in the secondary text and the command's keys. A group shows the rows
/// its limit allows until its header is clicked; the header then shows them all. The selected row is the accent's
/// container in its text colour with the accent's bar at its left edge; a hovered one lays the state colour over
/// itself; a command that cannot run now is drawn dimmed and does not run. A favoritable row shows a star at its right
/// while it is hovered or selected - filled, always, for a favourite - which toggles it. The body takes at most 60 % of
/// the window's height and scrolls past it. At its foot, on the second surface, what the keys do, the three prefixes
/// and the button that asks for every menu.
///
/// The palette knows no command: it shows what its source answers (`ICommandSource`), reads the source again as it
/// opens, asks it again at each keystroke and has it run the command chosen. Up and Down move the selection from row
/// to row across the groups, Page Up and Page Down a page, Home and End to the ends; Tab goes to the next group's first
/// row and Shift+Tab to the previous one's; Enter runs the selected command, and closes the palette first; a click on a
/// row selects and runs it. Esc, a click outside the surface and a run close it. Assistive technology reads a combo
/// box holding the editable query and the list of the rows.
class AFTERNIGHTCONTROLS_API AnCommandPalette : public QWidget
{
    W_OBJECT(AnCommandPalette)

public:
    /// What turns an icon's key into an icon; the default reads the product's icon files.
    using IconResolver = std::function<QIcon(const QString& key)>;

    explicit AnCommandPalette(QWidget* parent = nullptr);
    ~AnCommandPalette() override;

    AnCommandPalette(const AnCommandPalette&) = delete;
    AnCommandPalette& operator=(const AnCommandPalette&) = delete;

    /// What the palette shows; null shows nothing. The source outlives the palette or is replaced first.
    void setSource(ICommandSource* source);
    [[nodiscard]] ICommandSource* source() const noexcept;

    void setIconResolver(IconResolver resolver);

    /// Opens the palette: the source is read again and asked what an empty query answers, the first row is selected,
    /// and the surface is shown centred under `anchor`, a global point, 12 px below it, its body at most 60 % of
    /// `windowHeight`.
    void open(const QPoint& anchor, int windowHeight);

    [[nodiscard]] QLineEdit* queryField() const noexcept;
    [[nodiscard]] QListView* list() const noexcept;
    [[nodiscard]] QAbstractButton* allMenusButton() const noexcept;

    /// The surface, inside the room its shadow is drawn in, and its parts, in the palette.
    [[nodiscard]] QRect surfaceRect() const;
    [[nodiscard]] QRect queryRowRect() const;
    [[nodiscard]] QRect bodyRect() const;
    [[nodiscard]] QRect footerRect() const;

    /// What the source answered last.
    [[nodiscard]] const std::vector<CommandRowGroup>& groups() const noexcept;

    /// The ids of the rows the body shows, in order; a group's hidden rows are not among them.
    [[nodiscard]] QStringList shownRowIds() const;

    /// The selected row's id; empty for none.
    [[nodiscard]] QString selectedId() const;

    /// Selects the row of an id the body shows; nothing for any other.
    void setSelectedId(const QString& id);

    /// Where a row, a group's header and a row's star are, in the palette; empty for one not shown.
    [[nodiscard]] QRect rowRect(const QString& id) const;
    [[nodiscard]] QRect headerRect(const QString& groupId) const;
    [[nodiscard]] QRect starRect(const QString& id) const;
    /// Where a row's tag is, in the palette: right after its title, whose tooltip is the row's `tagToolTip`; empty for
    /// a row with no tag or one not shown.
    [[nodiscard]] QRect tagRect(const QString& id) const;

    [[nodiscard]] QSize sizeHint() const override;

    /// A command ran, chosen in the palette.
void commandRun(const QString& id) W_SIGNAL(commandRun, id)

    /// The person asked for every menu, from the footer.
    void allMenusRequested() W_SIGNAL(allMenusRequested)

    /// The palette closed, by any way.
    void closed() W_SIGNAL(closed)

        protected : bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui
