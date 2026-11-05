// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "SettingsPage.h"
#include "controls/IAppContext.h"
#include "theme/AnDesignTokens.h"
#include <verdigris/wobjectimpl.h>

#include <functional>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QSlider>
#include <QAbstractButton>
#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStyle>
#include <QSize>

W_OBJECT_IMPL(SettingsPage)

namespace
{

QPixmap contextMessageIconPixmap(QWidget* widget, const QString& iconName)
{
    if (auto* context = an::ui::appContext())
    {
        if (const auto iconProvider = context->getIconProvider())
        {
            return iconProvider->getIcon(iconName).pixmap(QSize(18, 18));
        }
    }

    QStyle* style = widget ? widget->style() : QApplication::style();
    if (!style)
    {
        return {};
    }

    if (iconName == QStringLiteral("info"))
    {
        return style->standardIcon(QStyle::SP_MessageBoxInformation).pixmap(QSize(18, 18));
    }

    if (iconName == QStringLiteral("warning"))
    {
        return style->standardIcon(QStyle::SP_MessageBoxWarning).pixmap(QSize(18, 18));
    }

    return {};
}

} // anonymous namespace

SettingsPage::SettingsPage(QWidget* parent) : QWidget(parent) {}

QStringList SettingsPage::searchKeywords() const
{
    return QStringList{pageTitle()};
}

bool SettingsPage::hasUnsavedChanges() const
{
    return m_Modified;
}

ValidationResult SettingsPage::validateSettings() const
{
    return ValidationResult::valid();
}

void SettingsPage::markAsModified()
{
    if (!m_Modified)
    {
        m_Modified = true;
        emit settingsModified();
    }
}

void SettingsPage::clearModified()
{
    m_Modified = false;
}

// ═══════════════════════════════════════════════════════════════════════════
// Change Tracking Helpers
// ═══════════════════════════════════════════════════════════════════════════

void SettingsPage::trackChanges(QCheckBox* checkbox)
{
    if (checkbox)
    {
        connect(checkbox, &QCheckBox::checkStateChanged, this, &SettingsPage::markAsModified);
    }
}

void SettingsPage::trackChanges(QComboBox* combobox)
{
    if (combobox)
    {
        connect(combobox, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
                &SettingsPage::markAsModified);
    }
}

void SettingsPage::trackChanges(QSpinBox* spinbox)
{
    if (spinbox)
    {
        connect(spinbox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this,
                &SettingsPage::markAsModified);
    }
}

void SettingsPage::trackChanges(QDoubleSpinBox* spinbox)
{
    if (spinbox)
    {
        connect(spinbox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this,
                &SettingsPage::markAsModified);
    }
}

void SettingsPage::trackChanges(QLineEdit* lineEdit)
{
    if (lineEdit)
    {
        connect(lineEdit, &QLineEdit::textChanged, this, &SettingsPage::markAsModified);
    }
}

void SettingsPage::trackChanges(QSlider* slider)
{
    if (slider)
    {
        connect(slider, &QSlider::valueChanged, this, &SettingsPage::markAsModified);
    }
}

void SettingsPage::trackChanges(QAbstractButton* button)
{
    if (button)
    {
        connect(button, &QAbstractButton::toggled, this, &SettingsPage::markAsModified);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// UI Helper Methods
// ═══════════════════════════════════════════════════════════════════════════

QWidget* SettingsPage::createSeparator()
{
    auto* separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    separator->setFixedHeight(2);
    return separator;
}

QWidget* SettingsPage::createSectionHeader(const QString& title, const QString& description)
{
    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 16, 0, 8);
    layout->setSpacing(4);

    // Section title
    auto* titleLabel = new QLabel(title, container);
    titleLabel->setFont(an::ui::tokens().typography.heading3);
    layout->addWidget(titleLabel);

    // Optional description
    if (!description.isEmpty())
    {
        auto* descLabel = new QLabel(description, container);
        descLabel->setWordWrap(true);
        descLabel->setProperty("anStatusTone", QStringLiteral("secondary"));
        descLabel->setFont(an::ui::tokens().typography.caption);
        layout->addWidget(descLabel);
    }

    // Underline
    auto* line = new QFrame(container);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line);

    return container;
}

QWidget* SettingsPage::createInfoLabel(const QString& text)
{
    auto* container = new QFrame(this);
    container->setObjectName(QStringLiteral("anContextMessage"));
    container->setProperty("anContextMessageTone", QStringLiteral("info"));
    container->setFrameShape(QFrame::StyledPanel);
    container->setAttribute(Qt::WA_StyledBackground, true);

    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(8);

    // Info icon
    auto* iconLabel = new QLabel(container);
    iconLabel->setFixedSize(18, 18);
    iconLabel->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    iconLabel->setProperty("anContextMessageIcon", QStringLiteral("info"));
    iconLabel->setPixmap(contextMessageIconPixmap(this, QStringLiteral("info")));
    layout->addWidget(iconLabel);

    // Text
    auto* textLabel = new QLabel(text, container);
    textLabel->setWordWrap(true);
    textLabel->setProperty("anStatusTone", QStringLiteral("secondary"));
    layout->addWidget(textLabel, 1);

    return container;
}

QWidget* SettingsPage::createWarningLabel(const QString& text)
{
    auto* container = new QFrame(this);
    container->setObjectName(QStringLiteral("anContextMessage"));
    container->setProperty("anContextMessageTone", QStringLiteral("warning"));
    container->setFrameShape(QFrame::StyledPanel);
    container->setAttribute(Qt::WA_StyledBackground, true);

    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(8);

    // Warning icon
    auto* iconLabel = new QLabel(container);
    iconLabel->setFixedSize(18, 18);
    iconLabel->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    iconLabel->setProperty("anContextMessageIcon", QStringLiteral("warning"));
    iconLabel->setPixmap(contextMessageIconPixmap(this, QStringLiteral("warning")));
    layout->addWidget(iconLabel);

    // Text
    auto* textLabel = new QLabel(text, container);
    textLabel->setWordWrap(true);
    textLabel->setProperty("anStatusTone", "warning");
    textLabel->setFont(an::ui::tokens().typography.caption);
    layout->addWidget(textLabel, 1);

    return container;
}

QIcon SettingsPage::contextIcon(const QString& iconName) const
{
    if (auto* context = an::ui::appContext())
    {
        if (const auto iconProvider = context->getIconProvider())
        {
            return iconProvider->getIcon(iconName);
        }
    }

    return {};
}

// ═══════════════════════════════════════════════════════════════════════════
// SignalBlocker Implementation
// ═══════════════════════════════════════════════════════════════════════════

SettingsPage::SignalBlocker::SignalBlocker(QWidget* widget)
    : m_Widget(widget), m_WasBlocked(widget ? widget->signalsBlocked() : false)
{
    if (m_Widget)
    {
        m_Widget->blockSignals(true);
    }
}

SettingsPage::SignalBlocker::~SignalBlocker()
{
    if (m_Widget)
    {
        m_Widget->blockSignals(m_WasBlocked);
    }
}

SettingsPage::SignalBlocker SettingsPage::blockSignalsDuringLoad()
{
    return SignalBlocker(this);
}

void SettingsPage::applySecondaryTextStyle(QLabel* label) const
{
    if (!label)
    {
        return;
    }

    label->setProperty("anStatusTone", QStringLiteral("secondary"));

    label->setFont(an::ui::tokens().typography.caption);

    // Re-polish so proxy style applies the secondary color
    label->style()->unpolish(label);
    label->style()->polish(label);
    label->update();
}

void SettingsPage::refreshStyles()
{
    // Refresh themed icons used by context info messages.
    const auto iconLabels = findChildren<QLabel*>();
    for (auto* iconLabel : iconLabels)
    {
        if (!iconLabel)
        {
            continue;
        }

        const QString iconName = iconLabel->property("anContextMessageIcon").toString();
        if (iconName.isEmpty())
        {
            continue;
        }

        iconLabel->setPixmap(contextMessageIconPixmap(this, iconName));
    }

    // Re-polish child widgets so token-based styling is reapplied.
    for (auto* child : findChildren<QWidget*>())
    {
        if (!child)
        {
            continue;
        }

        if (auto* style = child->style())
        {
            style->unpolish(child);
            style->polish(child);
        }
        child->update();
    }
}
