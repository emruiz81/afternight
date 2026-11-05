// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QComboBox>
#include <QCoreApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QSize>
#include <QSizePolicy>
#include <QSpacerItem>
#include <QString>
#include <QToolButton>
#include <QWidget>

#include "controls/IAppContext.h"

namespace an::ui::processes::rt_preview_ui
{

struct HistogramToolbarOptions
{
    const char* translationContext = nullptr;
    bool includeShowClippingInfo = true;
    bool includeShowHistogram = true;
    bool includeTransferCurve = false;
    bool localChannelNormalizationChecked = false;
    bool showHistogramAreasChecked = false;
    bool showHistogramChecked = true;
    bool transferCurveChecked = true;
};

struct HistogramToolbarElements
{
    QToolButton* zoomIn = nullptr;
    QToolButton* zoomOut = nullptr;
    QToolButton* zoomReset = nullptr;
    QToolButton* gridButton = nullptr;
    QToolButton* showClippingInfo = nullptr;
    QToolButton* transferCurve = nullptr;
    QToolButton* localChannelNormalization = nullptr;
    QToolButton* showHistogramAreas = nullptr;
    QToolButton* showHistogram = nullptr;
    QComboBox* plotResolution = nullptr;
};

inline QString trText(const char* context, const char* text)
{
    if (!context)
    {
        return QString::fromUtf8(text);
    }

    return QCoreApplication::translate(context, text, nullptr);
}

inline void initToolbarButton(QToolButton* button, const QString& tooltip, const QString& iconName,
                              const bool useMonochromeTone = false)
{
    if (!button)
    {
        return;
    }

    button->setToolTip(tooltip);
    if (auto* context = an::ui::appContext())
    {
        if (auto iconProvider = context->getIconProvider())
        {
            if (useMonochromeTone)
            {
                const QString iconPath = QStringLiteral(":/AfterNight/icons/SVG/%1.svg").arg(iconName);
                button->setIcon(
                    QIcon(iconProvider->loadSvgFileAsPixmap(iconPath, QSize(18, 18), iconProvider->getColorNormal())));
            }
            else
            {
                button->setIcon(iconProvider->getIcon(iconName));
            }
        }
    }
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setAutoRaise(true);
    button->setIconSize(QSize(18, 18));
    button->setFixedSize(QSize(28, 28));
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    button->setFocusPolicy(Qt::NoFocus);
}

inline HistogramToolbarElements buildHistogramToolbar(QWidget* parent, QHBoxLayout* toolbarLayout,
                                                      const HistogramToolbarOptions& options = {})
{
    HistogramToolbarElements ui;

    if (!parent || !toolbarLayout)
    {
        return ui;
    }

    toolbarLayout->setContentsMargins(0, 0, 0, 0);

    ui.zoomIn = new QToolButton(parent);
    ui.zoomIn->setObjectName(QStringLiteral("zoomIn"));
    initToolbarButton(ui.zoomIn, trText(options.translationContext, "Zoom in"), QStringLiteral("zoom_in"));
    toolbarLayout->addWidget(ui.zoomIn);

    ui.zoomOut = new QToolButton(parent);
    ui.zoomOut->setObjectName(QStringLiteral("zoomOut"));
    initToolbarButton(ui.zoomOut, trText(options.translationContext, "Zoom out"), QStringLiteral("zoom_out"));
    toolbarLayout->addWidget(ui.zoomOut);

    ui.zoomReset = new QToolButton(parent);
    ui.zoomReset->setObjectName(QStringLiteral("zoomReset"));
    initToolbarButton(ui.zoomReset, trText(options.translationContext, "Zoom reset"), QStringLiteral("zoom_fit"));
    toolbarLayout->addWidget(ui.zoomReset);

    auto* leftSeparator = new QFrame(parent);
    leftSeparator->setFrameShape(QFrame::VLine);
    leftSeparator->setFrameShadow(QFrame::Sunken);
    toolbarLayout->addWidget(leftSeparator);

    ui.gridButton = new QToolButton(parent);
    ui.gridButton->setObjectName(QStringLiteral("gridButton"));
    initToolbarButton(ui.gridButton, trText(options.translationContext, "Show grid"), QStringLiteral("grid"));
    ui.gridButton->setCheckable(true);
    ui.gridButton->setChecked(true);
    toolbarLayout->addWidget(ui.gridButton);

    if (options.includeShowClippingInfo)
    {
        ui.showClippingInfo = new QToolButton(parent);
        ui.showClippingInfo->setObjectName(QStringLiteral("showClippingInfo"));
        initToolbarButton(ui.showClippingInfo, trText(options.translationContext, "Show clipping info"),
                          QStringLiteral("info"), true);
        ui.showClippingInfo->setCheckable(true);
        ui.showClippingInfo->setChecked(true);
        toolbarLayout->addWidget(ui.showClippingInfo);
    }

    if (options.includeTransferCurve)
    {
        ui.transferCurve = new QToolButton(parent);
        ui.transferCurve->setObjectName(QStringLiteral("transferCurve"));
        initToolbarButton(ui.transferCurve, trText(options.translationContext, "Show transfer curve"),
                          QStringLiteral("stat_line"));
        ui.transferCurve->setCheckable(true);
        ui.transferCurve->setChecked(options.transferCurveChecked);
        toolbarLayout->addWidget(ui.transferCurve);
    }

    ui.localChannelNormalization = new QToolButton(parent);
    ui.localChannelNormalization->setObjectName(QStringLiteral("localChannelNormalization"));
    initToolbarButton(ui.localChannelNormalization, trText(options.translationContext, "Absolute scales"),
                      QStringLiteral("scale"));
    ui.localChannelNormalization->setCheckable(true);
    ui.localChannelNormalization->setChecked(options.localChannelNormalizationChecked);
    toolbarLayout->addWidget(ui.localChannelNormalization);

    ui.showHistogramAreas = new QToolButton(parent);
    ui.showHistogramAreas->setObjectName(QStringLiteral("showHistogramAreas"));
    initToolbarButton(ui.showHistogramAreas, trText(options.translationContext, "Draw histogram areas"),
                      QStringLiteral("area_chart"));
    ui.showHistogramAreas->setCheckable(true);
    ui.showHistogramAreas->setChecked(options.showHistogramAreasChecked);
    toolbarLayout->addWidget(ui.showHistogramAreas);

    if (options.includeShowHistogram)
    {
        ui.showHistogram = new QToolButton(parent);
        ui.showHistogram->setObjectName(QStringLiteral("showHistogram"));
        initToolbarButton(ui.showHistogram, trText(options.translationContext, "Show histogram"),
                          QStringLiteral("histogram"));
        ui.showHistogram->setCheckable(true);
        ui.showHistogram->setChecked(options.showHistogramChecked);
        toolbarLayout->addWidget(ui.showHistogram);
    }

    auto* rightSeparator = new QFrame(parent);
    rightSeparator->setFrameShape(QFrame::VLine);
    rightSeparator->setFrameShadow(QFrame::Sunken);
    toolbarLayout->addWidget(rightSeparator);

    ui.plotResolution = new QComboBox(parent);
    ui.plotResolution->setObjectName(QStringLiteral("plotResolution"));
    ui.plotResolution->setMinimumWidth(150);
    ui.plotResolution->setToolTip(trText(options.translationContext, "Plot resolution"));
    toolbarLayout->addWidget(ui.plotResolution);

    toolbarLayout->addItem(new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum));

    return ui;
}

} // namespace an::ui::processes::rt_preview_ui
