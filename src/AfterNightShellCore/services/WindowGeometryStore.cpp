// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "services/WindowGeometryStore.h"

#include "core/ApplicationCore.h"

#include "controls/GlobalSettingsPersistence.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"

#include <AfterNightPlatform/Logging.h>

#include <QGuiApplication>
#include <QScreen>
#include <QString>
#include <QWidget>

#include <algorithm>

namespace an::ui::shell
{

namespace
{

QString key(const char* name)
{
    return QString::fromLatin1(name);
}

} // namespace

WindowGeometryStore::WindowGeometryStore(ApplicationCore& core) : m_core(core) {}

void WindowGeometryStore::save(const QWidget& window) const
{
    GlobalSettingsPersistence* const settings = m_core.globalSettingsStore();
    if (settings == nullptr)
    {
        return;
    }

    const bool maximized = window.isMaximized();
    settings->upsert(key(SettingsKeys::INTERFACE_WINDOW_MAXIMIZED),
                     maximized ? QStringLiteral("1") : QStringLiteral("0"));

    const QRect normal = window.normalGeometry();
    settings->upsert(key(SettingsKeys::INTERFACE_WINDOW_GEOMETRY_X), QString::number(normal.x()));
    settings->upsert(key(SettingsKeys::INTERFACE_WINDOW_GEOMETRY_Y), QString::number(normal.y()));
    settings->upsert(key(SettingsKeys::INTERFACE_WINDOW_GEOMETRY_WIDTH), QString::number(normal.width()));
    settings->upsert(key(SettingsKeys::INTERFACE_WINDOW_GEOMETRY_HEIGHT), QString::number(normal.height()));

    LOG_DEBUG("Saved window geometry: {}x{} at ({},{}) maximized={}", normal.width(), normal.height(), normal.x(),
              normal.y(), maximized);
}

WindowGeometryStore::Placement WindowGeometryStore::restoredPlacement(const QList<QRect>& screens,
                                                                      const QSize& minimum) const
{
    GlobalSettingsPersistence* const settings = m_core.globalSettingsStore();
    if (settings == nullptr)
    {
        return {};
    }

    const QString remember =
        settings->select(key(SettingsKeys::INTERFACE_REMEMBER_WINDOW_SIZE),
                         SettingsDefaults::INTERFACE_REMEMBER_WINDOW_SIZE ? QStringLiteral("1") : QStringLiteral("0"));
    if (remember != QStringLiteral("1"))
    {
        return {};
    }

    const QString width = settings->select(key(SettingsKeys::INTERFACE_WINDOW_GEOMETRY_WIDTH), QString());
    const QString height = settings->select(key(SettingsKeys::INTERFACE_WINDOW_GEOMETRY_HEIGHT), QString());
    if (width.isEmpty() || height.isEmpty())
    {
        return {};
    }
    if (settings->select(key(SettingsKeys::INTERFACE_WINDOW_MAXIMIZED), QStringLiteral("1")) == QStringLiteral("1"))
    {
        return {};
    }

    const int x = settings->select(key(SettingsKeys::INTERFACE_WINDOW_GEOMETRY_X), QStringLiteral("100")).toInt();
    const int y = settings->select(key(SettingsKeys::INTERFACE_WINDOW_GEOMETRY_Y), QStringLiteral("100")).toInt();
    const QRect place(QPoint(x, y), clampedToMinimum(QSize(width.toInt(), height.toInt()), minimum));
    if (!intersectsAnyScreen(place, screens))
    {
        return {};
    }
    return Placement{.maximized = false, .geometry = place};
}

void WindowGeometryStore::show(QWidget& window, const QList<QRect>& screens) const
{
    const Placement placement = restoredPlacement(screens, window.minimumSize());
    if (placement.maximized)
    {
        window.showMaximized();
        return;
    }
    window.setGeometry(placement.geometry);
    window.showNormal();
}

void WindowGeometryStore::show(QWidget& window) const
{
    show(window, availableScreenGeometries());
}

QList<QRect> WindowGeometryStore::availableScreenGeometries()
{
    QList<QRect> geometries;
    for (QScreen* const screen : QGuiApplication::screens())
    {
        if (screen != nullptr)
        {
            geometries.push_back(screen->availableGeometry());
        }
    }
    return geometries;
}

QSize WindowGeometryStore::clampedToMinimum(const QSize& size, const QSize& minimum)
{
    return QSize((std::max)(size.width(), minimum.width()), (std::max)(size.height(), minimum.height()));
}

bool WindowGeometryStore::intersectsAnyScreen(const QRect& place, const QList<QRect>& screens)
{
    return std::any_of(screens.cbegin(), screens.cend(),
                       [&place](const QRect& screen) { return screen.intersects(place); });
}

} // namespace an::ui::shell
