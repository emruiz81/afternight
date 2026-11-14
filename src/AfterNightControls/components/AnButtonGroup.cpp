// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "components/AnButtonGroup.h"

#include "theme/AnDesignTokens.h"

#include <QHBoxLayout>
#include <QPushButton>

#include <algorithm>

namespace an::ui
{

AnButtonGroup::AnButtonGroup(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("anButtonGroup"));

    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(8);
    m_layout->addStretch(1);

    refreshTheme();
}

QPushButton* AnButtonGroup::addButton(const QString& text, const ButtonVariant variant)
{
    if (!m_layout)
    {
        return nullptr;
    }

    auto* button = new QPushButton(text.trimmed(), this);
    button->setObjectName(QStringLiteral("anButtonGroupButton"));
    setButtonVariant(button, variant);

    const int insertionIndex = (std::max)(0, m_layout->count() - 1);
    m_layout->insertWidget(insertionIndex, button);
    button->setFont(tokens().typography.body);

    return button;
}

void AnButtonGroup::setButtonVariant(QPushButton* button, const ButtonVariant variant)
{
    if (!button)
    {
        return;
    }

    const QString variantValue = variantName(variant);
    if (button->property("anButtonVariant").toString() == variantValue)
    {
        return;
    }

    button->setProperty("anButtonVariant", variantValue);
    button->style()->unpolish(button);
    button->style()->polish(button);
    button->update();
}

AnButtonGroup::ButtonVariant AnButtonGroup::buttonVariant(const QPushButton* button) const
{
    if (!button)
    {
        return ButtonVariant::Secondary;
    }

    return button->property("anButtonVariant").toString() == QStringLiteral("primary") ? ButtonVariant::Primary
                                                                                       : ButtonVariant::Secondary;
}

QList<QPushButton*> AnButtonGroup::buttons() const
{
    QList<QPushButton*> result;
    if (!m_layout)
    {
        return result;
    }

    for (int i = 0; i < m_layout->count(); ++i)
    {
        if (auto* button = qobject_cast<QPushButton*>(m_layout->itemAt(i)->widget()))
        {
            result.push_back(button);
        }
    }

    return result;
}

void AnButtonGroup::clearButtons()
{
    if (!m_layout)
    {
        return;
    }

    const QList<QPushButton*> existingButtons = buttons();
    for (QPushButton* button : existingButtons)
    {
        m_layout->removeWidget(button);
        delete button;
    }
}

void AnButtonGroup::refreshTheme()
{
    refreshTokens();
    const auto& tokenSet = tokens();

    if (m_layout)
    {
        // A panel's gap apart under the Observatory profile.
        m_layout->setSpacing(styleProfile() == StyleProfile::Observatory ? tokenSet.metrics.gap : tokenSet.spacing.sm);
    }

    const QList<QPushButton*> buttonList = buttons();
    for (QPushButton* button : buttonList)
    {
        button->setFont(tokenSet.typography.body);
        button->style()->unpolish(button);
        button->style()->polish(button);
        button->update();
    }
}

QString AnButtonGroup::variantName(const ButtonVariant variant)
{
    return variant == ButtonVariant::Primary ? QStringLiteral("primary") : QStringLiteral("secondary");
}

} // namespace an::ui
