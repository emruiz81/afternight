// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "components/AnWindowHeading.h"

#include "app/WindowPlacement.h"
#include "components/AnLayoutUtils.h"
#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"

#include <QBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QLayout>
#include <QLayoutItem>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QVBoxLayout>

#include <verdigris/wobjectimpl.h>

#include <algorithm>

W_OBJECT_IMPL(an::ui::AnWindowHeading)

namespace an::ui
{

AnWindowHeading::AnWindowHeading(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("anWindowHeading"));
    // No taller than its lines where something beside it stretches, and free to grow with a description that wraps: a
    // fixed height would be the height of its lines unwrapped.
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    auto* const lines = new QVBoxLayout(this);
    lines->setContentsMargins(0, 0, 0, 0);
    lines->setSpacing(parts::kWindowHeadingLineGap);
    // The lines keep their own gap: the style would snap it to the spacing scale.
    lines->setProperty("anKeepLayoutMargins", true);
    lines->setProperty("anKeepLayoutSpacing", true);

    m_eyebrow = new QLabel(this);
    m_eyebrow->setObjectName(QStringLiteral("anWindowHeadingEyebrow"));
    m_eyebrow->setTextFormat(Qt::PlainText);
    m_eyebrow->setVisible(false);
    lines->addWidget(m_eyebrow);

    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("anWindowHeadingTitle"));
    m_title->setTextFormat(Qt::PlainText);
    lines->addWidget(m_title);

    m_description = new QLabel(this);
    m_description->setObjectName(QStringLiteral("anWindowHeadingDescription"));
    m_description->setTextFormat(Qt::PlainText);
    m_description->setVisible(false);
    // Wrapping is set last: a size policy set after it would take the label's height for its width away.
    m_description->setWordWrap(true);
    lines->addWidget(m_description);
}

AnWindowHeading::AnWindowHeading(const QString& title, const QString& description, QWidget* parent)
    : AnWindowHeading(parent)
{
    setTitle(title);
    setDescription(description);
}

void AnWindowHeading::setEyebrow(const QString& eyebrow)
{
    m_eyebrow->setText(eyebrow);
    m_eyebrow->setVisible(!eyebrow.isEmpty());
}

QString AnWindowHeading::eyebrow() const
{
    return m_eyebrow->text();
}

void AnWindowHeading::setTitle(const QString& title)
{
    m_title->setText(title);
    setAccessibleName(title);
}

QString AnWindowHeading::title() const
{
    return m_title->text();
}

void AnWindowHeading::setDescription(const QString& description)
{
    m_description->setText(description);
    m_description->setVisible(!description.isEmpty());
}

QString AnWindowHeading::description() const
{
    return m_description->text();
}

QLabel* AnWindowHeading::eyebrowLabel() const noexcept
{
    return m_eyebrow;
}

QLabel* AnWindowHeading::titleLabel() const noexcept
{
    return m_title;
}

QLabel* AnWindowHeading::descriptionLabel() const noexcept
{
    return m_description;
}

bool windowsTakeHeadings()
{
    return styleProfile() == StyleProfile::Observatory;
}

QString windowEyebrow(const QString& holder, const QString& subject)
{
    return subject.isEmpty() ? holder : holder + QStringLiteral(" \u00B7 ") + subject;
}

AnWindowHeading* headWindow(QWidget& window, const QString& title, const QString& description, const QString& eyebrow)
{
    auto* const column = qobject_cast<QBoxLayout*>(window.layout());
    if (!windowsTakeHeadings() || column == nullptr ||
        (column->direction() != QBoxLayout::TopToBottom && column->direction() != QBoxLayout::BottomToTop))
    {
        return nullptr;
    }
    auto* const heading = new AnWindowHeading(title, description, &window);
    heading->setEyebrow(eyebrow);
    column->insertWidget(column->direction() == QBoxLayout::TopToBottom ? 0 : column->count(), heading);
    return heading;
}

AnWindowHeading* headWindowInPlaceOf(QWidget* header, const QString& eyebrow)
{
    if (header == nullptr)
    {
        return nullptr;
    }
    const auto* const title = header->findChild<QLabel*>(QStringLiteral("anProcessShellHeaderTitle"));
    const auto* const description = header->findChild<QLabel*>(QStringLiteral("anProcessShellHeaderDescription"));
    return headWindowInPlaceOf(header, title != nullptr ? title->text() : QString(),
                               description != nullptr ? description->text() : QString(), eyebrow);
}

AnWindowHeading* headWindowInPlaceOf(QWidget* header, const QString& title, const QString& description,
                                     const QString& eyebrow)
{
    if (!windowsTakeHeadings() || header == nullptr || header->parentWidget() == nullptr ||
        header->parentWidget()->layout() == nullptr)
    {
        return nullptr;
    }
    auto* const heading = new AnWindowHeading(title, description, header->parentWidget());
    heading->setEyebrow(eyebrow);
    // The layout hands the header's place to the heading; the header stays the window's, shown no more.
    delete header->parentWidget()->layout()->replaceWidget(header, heading);
    header->setVisible(false);
    return heading;
}

void setWindowEyebrow(QWidget& window, const QString& eyebrow)
{
    if (auto* const heading = window.findChild<AnWindowHeading*>(QStringLiteral("anWindowHeading")))
    {
        heading->setEyebrow(eyebrow);
    }
}

namespace
{

/// The area a window's body scrolls in: it scrolls up and down alone, so it is never narrower than what it holds
/// needs beside its bar - a plain scroll area asks for no more than its bars' width, and would cut its content at
/// the right in a narrow window.
class WindowBodyArea final : public QScrollArea
{
public:
    using QScrollArea::QScrollArea;

    [[nodiscard]] QSize minimumSizeHint() const override
    {
        QSize least = QScrollArea::minimumSizeHint();
        if (const QWidget* const content = widget())
        {
            least.setWidth((std::max)(least.width(), content->minimumSizeHint().width() +
                                                         verticalScrollBar()->sizeHint().width() + 2 * frameWidth()));
        }
        return least;
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        return QScrollArea::sizeHint().expandedTo(minimumSizeHint());
    }
};

} // namespace

QScrollArea* scrollWindowBody(QBoxLayout& column, const int first, const int count)
{
    QWidget* const holder = column.parentWidget();
    if (!windowsTakeHeadings() || holder == nullptr || first < 0 || count <= 0 || first + count > column.count() ||
        (column.direction() != QBoxLayout::TopToBottom && column.direction() != QBoxLayout::BottomToTop))
    {
        return nullptr;
    }
    auto* const area = new WindowBodyArea(holder);
    area->setObjectName(QString::fromLatin1(kWindowBodyName));
    area->setFrameShape(QFrame::NoFrame);
    area->setWidgetResizable(true);
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* const content = new QWidget(area);
    content->setObjectName(QStringLiteral("anWindowBodyContent"));
    auto* const body = new QVBoxLayout(content);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(column.spacing());
    for (int moved = 0; moved < count; ++moved)
    {
        const int stretch = column.stretch(first);
        QLayoutItem* const item = column.takeAt(first);
        if (QWidget* const widget = item->widget())
        {
            const Qt::Alignment alignment = item->alignment();
            delete item;
            body->addWidget(widget, stretch, alignment);
        }
        else if (QLayout* const layout = item->layout())
        {
            // A layout is its own item: it is handed over whole, and what it holds goes to the body with it.
            body->addLayout(layout, stretch);
        }
        else
        {
            body->addItem(item);
        }
    }
    area->setWidget(content);
    keepScrollContentSizedForItsWidth(area);
    column.insertWidget(first, area, 1);
    return area;
}

QScrollArea* scrollWindowBody(QBoxLayout& column, const QWidget* after, const QWidget* before)
{
    const int first = after != nullptr ? column.indexOf(after) + 1 : 0;
    const int end = before != nullptr ? column.indexOf(before) : column.count();
    if ((after != nullptr && first == 0) || end < first)
    {
        return nullptr;
    }
    return scrollWindowBody(column, first, end - first);
}

} // namespace an::ui
