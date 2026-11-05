// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <memory>
#include <unordered_map>

#include <QtGui/qcolor.h>
#include <QtGui/qicon.h>
#include <QtGui/qpixmap.h>
#include <QtCore/qsize.h>
#include <QtCore/qstring.h>

class AFTERNIGHTCONTROLS_API IconManager
{
public:
    enum class SemanticIconTone
    {
        None,
        Info,
        Warning,
        Error,
        Question,
        Success
    };

    IconManager();

    void setColors(const QColor& normal, const QColor& disabled);
    void setSemanticColors(const QColor& info, const QColor& warning, const QColor& error, const QColor& question,
                           const QColor& success);

    void loadIcons(const QString& path);

    size_t getNumOfLoadedIcons() const;

    void clearLoadedIcons();

    bool hasIcon(const QString& icon) const;

    QIcon getIcon(const QString& icon);

    QIcon loadSvgFileAsIcon(const QString& fileName);
    QPixmap loadSvgFileAsPixmap(const QString& fileName, const QSize size, const QColor color);

    const QColor getColorNormal() const;
    const QColor getColorDisabled() const;
    const QColor getSemanticColor(SemanticIconTone tone) const;

    ~IconManager();

private:
    QIcon loadSvgFileAsIcon(const QString& fileName, const QColor& normalColor);

    QColor m_ColorNormal;
    QColor m_ColorDisabled;
    QColor m_ColorInfo;
    QColor m_ColorWarning;
    QColor m_ColorError;
    QColor m_ColorQuestion;
    QColor m_ColorSuccess;

    std::unordered_map<QString, QIcon> m_LoadedIcons;
};

typedef std::shared_ptr<IconManager> IconManagerSptr;
