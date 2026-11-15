// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ImageInfoStatusBarWidget.h"
#include "controls/ImageViewControl.h"
#include "widgets/ImageStatusTexts.h"

ImageInfoStatusBarWidget::ImageInfoStatusBarWidget()
{
    m_ImageResolution = std::make_unique<QLabel>();
    m_ImageZoom = std::make_unique<QLabel>();
    m_ra_label = std::make_unique<QLabel>();
    m_dec_label = std::make_unique<QLabel>();

    m_ra_label->setObjectName(QStringLiteral("imageStatusRaLabel"));
    m_dec_label->setObjectName(QStringLiteral("imageStatusDecLabel"));

    clearCelestialCoords();
}

void ImageInfoStatusBarWidget::initialize(QStatusBar* bar)
{
    bar->addWidget(m_ImageResolution.get());
    bar->addWidget(m_ImageZoom.get());
    bar->addPermanentWidget(m_ra_label.get());
    bar->addPermanentWidget(m_dec_label.get());
}

void ImageInfoStatusBarWidget::updateImageInfo(ImageViewControl* control)
{
    const QString facts = an::ui::imageFactsStatusText(control);
    if (facts.isEmpty())
    {
        clearImageInfo();
        return;
    }
    m_ImageResolution->setText(facts);
    updateImageZoomInfo(control);
}

void ImageInfoStatusBarWidget::updateImageZoomInfo(ImageViewControl* control)
{
    const QString zoom = an::ui::zoomStatusText(control);
    if (zoom.isEmpty())
    {
        clearImageInfo();
        return;
    }
    m_ImageZoom->setText(zoom);
}

void ImageInfoStatusBarWidget::updateCelestialCoords(const double ra_deg, const double dec_deg)
{
    m_ra_label->setText(an::ui::rightAscensionStatusText(ra_deg));
    m_dec_label->setText(an::ui::declinationStatusText(dec_deg));
    m_ra_label->setToolTip(an::ui::rightAscensionStatusToolTip(ra_deg));
    m_dec_label->setToolTip(an::ui::declinationStatusToolTip(dec_deg));
    m_ra_label->show();
    m_dec_label->show();
}

void ImageInfoStatusBarWidget::clearCelestialCoords()
{
    m_ra_label->clear();
    m_dec_label->clear();
    m_ra_label->setToolTip(QString());
    m_dec_label->setToolTip(QString());
    m_ra_label->hide();
    m_dec_label->hide();
}

void ImageInfoStatusBarWidget::clearImageInfo()
{
    m_ImageResolution->clear();
    m_ImageZoom->clear();
    clearCelestialCoords();
}

ImageInfoStatusBarWidget::~ImageInfoStatusBarWidget() {}
