// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ImageHistogramControl.h"
#include "controls/ImageViewControl.h"
#include "controls/IAppContext.h"
#include "session/ImageDocument.h"

#include <AfterNightEngine/Image/ImageDescriptor.h>

#include <verdigris/wobjectimpl.h>

#include <array>
#include <cstdint>
#include <optional>
#include <utility>

namespace
{

QIcon histogramSvgIcon(const QString& path)
{
    if (auto* context = an::ui::appContext())
    {
        if (const auto iconProvider = context->getIconProvider())
        {
            constexpr QSize iconSize(64, 64);
            return QIcon(iconProvider->loadSvgFileAsPixmap(path, iconSize, iconProvider->getColorNormal()));
        }
    }

    return QIcon(path);
}

} // anonymous namespace

W_OBJECT_IMPL(ImageHistogramControl)

ImageHistogramControl::ImageHistogramControl(ImageViewControl* control)
    : QDialog(control), ImageViewControlListener(control)
{
    m_ui = an::ui::controls::buildImageHistogramControlUi(this);

    // Minimize button.. disabled for now
    // setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint);

    this->setAttribute(Qt::WA_DeleteOnClose);

    QObject::connect(m_ui.buttonBox, &QDialogButtonBox::clicked, this,
                     &ImageHistogramControl::on_dialog_box_button_clicked);
    QObject::connect(m_ui.channelSelector, &QComboBox::currentTextChanged, this,
                     &ImageHistogramControl::on_channelSelector_currentTextChanged);
    QObject::connect(m_ui.plotResolution, &QComboBox::currentTextChanged, this,
                     &ImageHistogramControl::on_plotResolution_currentTextChanged);

    QObject::connect(m_ui.localChannelNormalization, &QAbstractButton::clicked, this,
                     &ImageHistogramControl::on_localChannelNormalization);
    QObject::connect(m_ui.showClippingInfo, &QAbstractButton::clicked, this,
                     &ImageHistogramControl::on_showClippingInfo);
    QObject::connect(m_ui.gridButton, &QAbstractButton::clicked, this, &ImageHistogramControl::on_gridClicked);
    QObject::connect(m_ui.zoomIn, &QAbstractButton::clicked, this, &ImageHistogramControl::on_zoomInButton);
    QObject::connect(m_ui.zoomOut, &QAbstractButton::clicked, this, &ImageHistogramControl::on_zoomOutButton);
    QObject::connect(m_ui.zoomReset, &QAbstractButton::clicked, this, &ImageHistogramControl::on_zoomResetButton);
    QObject::connect(m_ui.showHistogramAreas, &QAbstractButton::clicked, this,
                     &ImageHistogramControl::on_showHistogramAreas);

    std::string title = fmt::format("Histogram <{}>", m_image_view_control->getUniqueName());
    setWindowTitle(QString::fromUtf8(title));

    m_image_view_control->enableAction(ImageViewControlAction::execute_histogram, false);

    // The scroll area owns the widget; the window reaches it through a guard.
    m_HistogramWidget = new HistogramWidget(nullptr);
    m_HistogramWidget->setClippingInfoEnabled(true);
    m_ui.chartScrollArea->setWidget(m_HistogramWidget);

    m_HistogramWidget->setMouseTracking(true);
    m_HistogramWidget->setParentScrollArea(m_ui.chartScrollArea);
    m_HistogramWidget->setInteractiveZoomEnabled(true);

    m_Scheduler = std::make_unique<an::ui::HistogramCountScheduler>(m_HistogramWidget);

    initialize();

    countViewImage();
    m_HistogramWidget->setAreaModeEnabled(true);
    // showHistogramAreas is initialised as checked by buildHistogramToolbar (showHistogramAreasChecked = true)
}

void ImageHistogramControl::onImageLoaded(ImageViewControl* ctrl)
{
    countViewImage();

    updateChannelSelector();
}

void ImageHistogramControl::onImageUpdated(ImageViewControl* ctrl)
{
    countViewImage();

    updateChannelSelector();
}

void ImageHistogramControl::countViewImage()
{
    m_Scheduler->count(m_image_view_control != nullptr ? m_image_view_control->document().asset() : nullptr);
}

HistogramWidget* ImageHistogramControl::histogramWidget() const
{
    return m_HistogramWidget;
}

const an::ui::HistogramCountScheduler& ImageHistogramControl::scheduler() const
{
    return *m_Scheduler;
}

void ImageHistogramControl::onImageRenamed(ImageViewControl* ctrl)
{
    std::string title = fmt::format("Histogram <{}>", m_image_view_control->getUniqueName());
    setWindowTitle(QString::fromUtf8(title));
}

ImageHistogramControl::~ImageHistogramControl() = default;

void ImageHistogramControl::on_dialog_box_button_clicked(QAbstractButton* button)
{
    QDialogButtonBox* buttonBox = qobject_cast<QDialogButtonBox*>(sender());
    if (!buttonBox)
        return;

    QDialogButtonBox::StandardButton sb = buttonBox->standardButton(button);

    if (sb == QDialogButtonBox::Close)
    {
        this->close();
    }
    else if (sb == QDialogButtonBox::Reset)
    {
        resetSettings();
    }
}

void ImageHistogramControl::on_channelSelector_currentTextChanged(const QString& str)
{
    if (str == "Red")
    {
        m_HistogramWidget->setDisplayChannels(true, false, false);
    }
    else if (str == "Green")
    {
        m_HistogramWidget->setDisplayChannels(false, true, false);
    }
    else if (str == "Blue")
    {
        m_HistogramWidget->setDisplayChannels(false, false, true);
    }
    else
    {
        m_HistogramWidget->setDisplayChannels(true, true, true);
    }
}

void ImageHistogramControl::on_plotResolution_currentTextChanged(const QString& str)
{
    static const std::array<std::pair<const char*, an::ui::HistogramResolution>, 11> kResolutions{{
        {"6-bit (64)", an::ui::HistogramResolution::Bits6},
        {"7-bit (128)", an::ui::HistogramResolution::Bits7},
        {"8-bit (256)", an::ui::HistogramResolution::Bits8},
        {"9-bit (512)", an::ui::HistogramResolution::Bits9},
        {"10-bit (1024)", an::ui::HistogramResolution::Bits10},
        {"12-bit (4096)", an::ui::HistogramResolution::Bits12},
        {"14-bit (16K)", an::ui::HistogramResolution::Bits14},
        {"16-bit (64K)", an::ui::HistogramResolution::Bits16},
        {"100 levels", an::ui::HistogramResolution::Levels100},
        {"1000 levels", an::ui::HistogramResolution::Levels1000},
        {"10000 levels", an::ui::HistogramResolution::Levels10000},
    }};
    for (const auto& [label, resolution] : kResolutions)
    {
        if (str == QLatin1StringView(label))
        {
            if (m_HistogramWidget && m_HistogramWidget->getHistogramResolution() != resolution)
            {
                m_HistogramWidget->setHistogramResolution(resolution);
                m_Scheduler->recount();
            }
            return;
        }
    }
}

void ImageHistogramControl::on_gridClicked(bool checked)
{
    m_HistogramWidget->setGridEnabled(checked);
}

void ImageHistogramControl::on_zoomInButton()
{
    m_HistogramWidget->zoomIn();
}

void ImageHistogramControl::on_zoomOutButton()
{
    m_HistogramWidget->zoomOut();
}

void ImageHistogramControl::on_zoomResetButton()
{
    m_HistogramWidget->resetZoom();
}

void ImageHistogramControl::on_localChannelNormalization(bool checked)
{
    m_HistogramWidget->setLocalChannelNormalization(checked);
}

void ImageHistogramControl::on_showClippingInfo(bool checked)
{
    m_HistogramWidget->setClippingInfoEnabled(checked);
}

void ImageHistogramControl::on_showHistogramAreas(bool checked)
{
    m_HistogramWidget->setAreaModeEnabled(checked);
}

void ImageHistogramControl::done(int r)
{
    if (m_image_view_control)
    {
        m_image_view_control->enableAction(ImageViewControlAction::execute_histogram, true);
    }

    QDialog::done(r);
}

void ImageHistogramControl::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange ||
        event->type() == QEvent::ThemeChange)
    {
        refreshIcons();
    }

    QDialog::changeEvent(event);
}

void ImageHistogramControl::refreshIcons()
{
    auto* context = an::ui::appContext();
    auto iconProvider = context ? context->getIconProvider() : an::ui::IIconProviderPtr{};

    if (!iconProvider)
    {
        return;
    }

    m_ui.zoomIn->setIcon(iconProvider->getIcon("zoom_in"));
    m_ui.zoomOut->setIcon(iconProvider->getIcon("zoom_out"));
    m_ui.zoomReset->setIcon(iconProvider->getIcon("zoom_fit"));
    m_ui.gridButton->setIcon(iconProvider->getIcon("grid"));
    m_ui.localChannelNormalization->setIcon(iconProvider->getIcon("scale"));
    m_ui.showClippingInfo->setIcon(histogramSvgIcon(QStringLiteral(":/AfterNight/icons/SVG/info.svg")));
    m_ui.showHistogramAreas->setIcon(iconProvider->getIcon("area_chart"));

    // Also refresh channel selector icons
    updateChannelSelector();
}

void ImageHistogramControl::initialize()
{
    auto* context = an::ui::appContext();
    auto iconProvider = context ? context->getIconProvider() : an::ui::IIconProviderPtr{};

    if (iconProvider)
    {
        m_ui.zoomIn->setIcon(iconProvider->getIcon("zoom_in"));
        m_ui.zoomOut->setIcon(iconProvider->getIcon("zoom_out"));
        m_ui.zoomReset->setIcon(iconProvider->getIcon("zoom_fit"));
        m_ui.gridButton->setIcon(iconProvider->getIcon("grid"));
        m_ui.localChannelNormalization->setIcon(iconProvider->getIcon("scale"));
        m_ui.showClippingInfo->setIcon(histogramSvgIcon(QStringLiteral(":/AfterNight/icons/SVG/info.svg")));
        m_ui.showHistogramAreas->setIcon(iconProvider->getIcon("area_chart"));
    }

    m_ui.plotResolution->addItem("6-bit (64)");
    m_ui.plotResolution->addItem("7-bit (128)");
    m_ui.plotResolution->addItem("8-bit (256)");
    m_ui.plotResolution->addItem("9-bit (512)");
    m_ui.plotResolution->addItem("10-bit (1024)");
    m_ui.plotResolution->addItem("12-bit (4096)");
    m_ui.plotResolution->addItem("14-bit (16K)");
    m_ui.plotResolution->addItem("16-bit (64K)");
    m_ui.plotResolution->addItem("100 levels");
    m_ui.plotResolution->addItem("1000 levels");
    m_ui.plotResolution->addItem("10000 levels");

    m_ui.plotResolution->setCurrentText("8-bit (256)");

    updateChannelSelector();
}

void ImageHistogramControl::updateChannelSelector()
{
    auto* context = an::ui::appContext();
    auto iconProvider = context ? context->getIconProvider() : an::ui::IIconProviderPtr{};

    const std::optional<an::ImageDescriptor> descriptor =
        m_image_view_control != nullptr ? m_image_view_control->document().descriptor() : std::nullopt;
    const std::uint32_t channels = descriptor.has_value() ? descriptor->extent().channelCount() : 1u;

    m_ui.channelSelector->clear();

    // A colour image with alpha shows its three colours, as one without does.
    if (channels < 3)
    {
        m_ui.channelSelector->addItem("Grayscale");
        m_ui.channelSelector->setEnabled(false);

        // m_ui.channelSelector->setCurrentText("Grayscale");
    }
    else
    {
        m_ui.channelSelector->setEnabled(true);
        if (iconProvider)
        {
            m_ui.channelSelector->addItem(iconProvider->getIcon("rgb"), "RGB");
            m_ui.channelSelector->addItem(iconProvider->getIcon("red_circle"), "Red");
            m_ui.channelSelector->addItem(iconProvider->getIcon("green_circle"), "Green");
            m_ui.channelSelector->addItem(iconProvider->getIcon("blue_circle"), "Blue");
        }
        else
        {
            m_ui.channelSelector->addItem("RGB");
            m_ui.channelSelector->addItem("Red");
            m_ui.channelSelector->addItem("Green");
            m_ui.channelSelector->addItem("Blue");
        }

        // m_ui.channelSelector->setCurrentText("RGB");
    }
}

void ImageHistogramControl::resetSettings()
{
    updateChannelSelector();
    m_ui.plotResolution->setCurrentText("8-bit (256)");

    m_HistogramWidget->resetZoom();
    m_ui.localChannelNormalization->setChecked(false);
    m_ui.showClippingInfo->setChecked(true);
    m_ui.gridButton->setChecked(true);
    m_HistogramWidget->setLocalChannelNormalization(false);
    m_HistogramWidget->setClippingInfoEnabled(true);
    m_HistogramWidget->setGridEnabled(true);
}
