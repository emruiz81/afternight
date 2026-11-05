// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "controls/ProcessButtonIcons.h"

#include "controls/IAppContext.h"

#include <QDialogButtonBox>
#include <QPushButton>

namespace process_utils
{

namespace
{

constexpr int ICON_SIZE = 64;

/// Build a two-mode QIcon for primary-action buttons (Ok / Apply).
/// Normal state: white glyph (always on accent-colored background).
/// Disabled state: muted gray matching @TEXT_TERTIARY@.
QIcon buildPrimaryActionIcon(an::ui::IIconProvider* iconProvider, const QString& svgPath)
{
    const QColor normalColor(255, 255, 255);
    const QColor disabledColor(120, 130, 150);
    const QSize size(ICON_SIZE, ICON_SIZE);

    QIcon icon;
    icon.addPixmap(iconProvider->loadSvgFileAsPixmap(svgPath, size, normalColor), QIcon::Normal);
    icon.addPixmap(iconProvider->loadSvgFileAsPixmap(svgPath, size, disabledColor), QIcon::Disabled);
    return icon;
}

} // anonymous namespace

void applyDialogButtonBoxIcons(QDialogButtonBox* buttonBox, an::ui::IIconProvider* iconProvider)
{
    if (!buttonBox || !iconProvider)
    {
        return;
    }

    // Primary-action buttons get a check-mark icon with Normal + Disabled modes.
    static const QString checkSvg = QStringLiteral(":/AfterNight/icons/SVG/check.svg");

    if (auto* btn = buttonBox->button(QDialogButtonBox::Ok))
    {
        btn->setIcon(buildPrimaryActionIcon(iconProvider, checkSvg));
    }
    if (auto* btn = buttonBox->button(QDialogButtonBox::Apply))
    {
        btn->setIcon(buildPrimaryActionIcon(iconProvider, checkSvg));
    }

    // Secondary / neutral buttons use the icon provider's themed icon.
    if (auto* btn = buttonBox->button(QDialogButtonBox::Close))
    {
        btn->setIcon(iconProvider->getIcon("close"));
    }
    if (auto* btn = buttonBox->button(QDialogButtonBox::Cancel))
    {
        btn->setIcon(iconProvider->getIcon("close"));
    }
    if (auto* btn = buttonBox->button(QDialogButtonBox::Reset))
    {
        btn->setIcon(iconProvider->getIcon("refresh"));
    }
}

void applyDialogButtonBoxIcons(QDialogButtonBox* buttonBox, IconManager* iconManager)
{
    // Delegate through IAppContext — the IconManagerAdapter wraps IconManager
    // as IIconProvider. This overload exists for backward compatibility with
    // process files that still hold iconManager references directly.
    if (auto* ctx = an::ui::appContext())
    {
        applyDialogButtonBoxIcons(buttonBox, ctx->getIconProvider().get());
    }
}

} // namespace process_utils
