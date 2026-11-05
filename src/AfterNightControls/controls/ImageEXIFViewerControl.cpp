// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ImageEXIFViewerControl.h"
#include "controls/ImageInspectorContent.h"
#include "controls/ImageViewControl.h"

namespace
{

QString exifTitle(const ImageViewControl& view)
{
    return QStringLiteral("EXIF Header Viewer <%1>").arg(QString::fromUtf8(view.getUniqueName()));
}

} // namespace

ImageEXIFViewerControl::ImageEXIFViewerControl(ImageViewControl* control)
    : QDialog(control), ImageViewControlListener(control)
{
    m_ui = an::ui::controls::buildImageEXIFViewerControlUi(this);

    this->setAttribute(Qt::WA_DeleteOnClose);

    QObject::connect(m_ui.closeButton, &QPushButton::released, this, &ImageEXIFViewerControl::on_closeButtonPushed);

    setWindowTitle(exifTitle(*m_image_view_control));

    initialize();

    m_image_view_control->enableAction(ImageViewControlAction::execute_exif, false);

    recalculate();
}

void ImageEXIFViewerControl::onImageLoaded(ImageViewControl*)
{
    recalculate();
}

void ImageEXIFViewerControl::onImageUpdated(ImageViewControl*)
{
    recalculate();
}

void ImageEXIFViewerControl::onImageRenamed(ImageViewControl*)
{
    setWindowTitle(exifTitle(*m_image_view_control));
}

void ImageEXIFViewerControl::recalculate()
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

    const std::vector<an::ui::InspectorRow> rows = an::ui::exifRows(inspected->metadata.codec());
    m_ui.tableWidget->clearContents();
    m_ui.tableWidget->setColumnCount(3);
    m_ui.tableWidget->setHorizontalHeaderLabels({tr("Tag"), tr("Value"), tr("Key")});
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

ImageEXIFViewerControl::~ImageEXIFViewerControl() {}

void ImageEXIFViewerControl::on_closeButtonPushed()
{
    this->close();
}

void ImageEXIFViewerControl::done(int r)
{
    if (m_image_view_control)
    {
        m_image_view_control->enableAction(ImageViewControlAction::execute_exif, true);
    }

    QDialog::done(r);
}

void ImageEXIFViewerControl::initialize()
{
    auto horizontalHeader = m_ui.tableWidget->horizontalHeader();

    horizontalHeader->setSectionResizeMode(QHeaderView::Interactive);
    horizontalHeader->setStretchLastSection(true);
    m_ui.tableWidget->setAlternatingRowColors(true);
}
