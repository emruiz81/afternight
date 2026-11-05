// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ImageFITSHeaderViewerControl.h"
#include "controls/ImageInspectorContent.h"
#include "controls/ImageViewControl.h"

namespace
{

QString fitsHeaderTitle(const ImageViewControl& view)
{
    return QStringLiteral("FITS Header Viewer <%1>").arg(QString::fromUtf8(view.getUniqueName()));
}

} // namespace

ImageFITSHeaderViewerControl::ImageFITSHeaderViewerControl(ImageViewControl* control)
    : QDialog(control), ImageViewControlListener(control)
{
    m_ui = an::ui::controls::buildImageFITSHeaderViewerControlUi(this);

    this->setAttribute(Qt::WA_DeleteOnClose);

    QObject::connect(m_ui.closeButton, &QPushButton::released, this,
                     &ImageFITSHeaderViewerControl::on_closeButtonPushed);

    setWindowTitle(fitsHeaderTitle(*m_image_view_control));

    initialize();

    m_image_view_control->enableAction(ImageViewControlAction::execute_fits_header, false);

    recalculate();
}

void ImageFITSHeaderViewerControl::onImageLoaded(ImageViewControl*)
{
    recalculate();
}

void ImageFITSHeaderViewerControl::onImageUpdated(ImageViewControl*)
{
    recalculate();
}

void ImageFITSHeaderViewerControl::onImageRenamed(ImageViewControl*)
{
    setWindowTitle(fitsHeaderTitle(*m_image_view_control));
}

void ImageFITSHeaderViewerControl::recalculate()
{
    if (!m_image_view_control)
    {
        return;
    }
    const std::optional<an::ui::InspectedImage> inspected = an::ui::inspectDocument(m_image_view_control->document());
    if (!inspected.has_value())
    {
        return;
    }

    const std::vector<an::ui::InspectorRow> rows = an::ui::fitsHeaderRows(inspected->metadata.codec());
    m_ui.tableWidget->clearContents();
    m_ui.tableWidget->setColumnCount(3);
    m_ui.tableWidget->setHorizontalHeaderLabels({tr("Key"), tr("Value"), tr("Comment")});
    m_ui.tableWidget->setRowCount(static_cast<int>(rows.size()));
    int row = 0;
    for (const an::ui::InspectorRow& item : rows)
    {
        m_ui.tableWidget->setItem(row, 0, new QTableWidgetItem(item.first));
        m_ui.tableWidget->setItem(row, 1, new QTableWidgetItem(item.second));
        m_ui.tableWidget->setItem(row, 2, new QTableWidgetItem(item.third));
        ++row;
    }
}

ImageFITSHeaderViewerControl::~ImageFITSHeaderViewerControl() {}

void ImageFITSHeaderViewerControl::on_closeButtonPushed()
{
    this->close();
}

void ImageFITSHeaderViewerControl::done(int r)
{
    if (m_image_view_control)
    {
        m_image_view_control->enableAction(ImageViewControlAction::execute_fits_header, true);
    }

    QDialog::done(r);
}

void ImageFITSHeaderViewerControl::initialize()
{
    auto horizontalHeader = m_ui.tableWidget->horizontalHeader();

    horizontalHeader->setSectionResizeMode(QHeaderView::Interactive);
    horizontalHeader->setStretchLastSection(true);

    m_ui.tableWidget->setAlternatingRowColors(true);
}
