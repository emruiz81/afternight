// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The heading of a window that stays a window: what the window is, said at its head.

#include "AfterNightControlsExport.h"

#include <QString>
#include <QWidget>

#include <verdigris/wobjectdefs.h>

class QBoxLayout;
class QLabel;
class QScrollArea;

namespace an::ui
{

/// A window's heading: an optional eyebrow in capitals - what holds the window, and the image it works on - the title,
/// and an optional description of what the window does, one under the other at the head of the window's content. A line
/// with no text is left out, and a description wider than the heading wraps, the heading growing with it.
///
/// Each line is a label the proxy style draws by its name (`anWindowHeadingEyebrow`, `anWindowHeadingTitle`,
/// `anWindowHeadingDescription`): the eyebrow the caption in semibold capitals in the secondary text, the title in
/// semibold at the heading's size in the primary text, the description the small text in the secondary text. A theme, a
/// preset or a density reaches them as it reaches every label. Assistive technology reads the title as the heading's
/// name.
class AFTERNIGHTCONTROLS_API AnWindowHeading : public QWidget
{
    W_OBJECT(AnWindowHeading)

public:
    explicit AnWindowHeading(QWidget* parent = nullptr);

    /// A heading that says a title over a description.
    AnWindowHeading(const QString& title, const QString& description, QWidget* parent = nullptr);

    /// The line over the title; with no text it is left out.
    void setEyebrow(const QString& eyebrow);
    [[nodiscard]] QString eyebrow() const;

    void setTitle(const QString& title);
    [[nodiscard]] QString title() const;

    /// The line under the title; with no text it is left out.
    void setDescription(const QString& description);
    [[nodiscard]] QString description() const;

    [[nodiscard]] QLabel* eyebrowLabel() const noexcept;
    [[nodiscard]] QLabel* titleLabel() const noexcept;
    [[nodiscard]] QLabel* descriptionLabel() const noexcept;

private:
    QLabel* m_eyebrow = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_description = nullptr;
};

/// What a heading's eyebrow says: what holds the window and, after a middle dot, what it works on - "Analysis ·
/// M31_Lum" - or what holds it alone where it works on nothing with a name.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString windowEyebrow(const QString& holder, const QString& subject = QString());

/// Puts a heading at the head of a window whose layout is a column - a box laid out from top to bottom - and hands it
/// back. For a window laid out otherwise nothing is done and null is handed back.
AFTERNIGHTCONTROLS_API AnWindowHeading* headWindow(QWidget& window, const QString& title, const QString& description,
                                                   const QString& eyebrow = QString());

/// Puts a heading in the place of a window's own header - the widget that holds its title and its description, which
/// is hidden - saying what the header's two labels say (`anProcessShellHeaderTitle`,
/// `anProcessShellHeaderDescription`), and hands it back. For a null header, or one with no parent laid out, nothing is
/// done and null is handed back.
AFTERNIGHTCONTROLS_API AnWindowHeading* headWindowInPlaceOf(QWidget* header, const QString& eyebrow = QString());

/// The same, for a header whose labels go by other names: the heading says the title and the description given.
AFTERNIGHTCONTROLS_API AnWindowHeading* headWindowInPlaceOf(QWidget* header, const QString& title,
                                                            const QString& description, const QString& eyebrow);

/// Sets the eyebrow of a window's heading - as the image it works on is renamed; a window with no heading is left as
/// it is.
AFTERNIGHTCONTROLS_API void setWindowEyebrow(QWidget& window, const QString& eyebrow);

/// Lets a part of a window's column scroll up and down - `count` of its items from the one at `first`, moved as they
/// are into a scroll area that takes their place and the column's room - and hands the area back. What stays outside
/// the area, the heading and the footer, stays in view, and the window's least height is theirs and no longer the
/// whole column's: a window as tall as a small display shows all it holds there. The area has no frame, never scrolls
/// sideways and goes by `kWindowBodyName`; a window held at its first show (`holdWithinAvailableAreaWhenFirstShown`)
/// opens as high as shows the body whole where its screen has room. For a range the column does not hold nothing is
/// done and null is handed back.
AFTERNIGHTCONTROLS_API QScrollArea* scrollWindowBody(QBoxLayout& column, int first, int count);

/// The same, for the items that lie between two widgets of the column, neither of them moved; a null `after` is the
/// column's head and a null `before` its foot.
AFTERNIGHTCONTROLS_API QScrollArea* scrollWindowBody(QBoxLayout& column, const QWidget* after, const QWidget* before);

} // namespace an::ui
