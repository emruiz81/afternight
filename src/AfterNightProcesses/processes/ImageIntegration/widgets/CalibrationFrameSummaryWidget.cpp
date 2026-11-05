// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "CalibrationFrameSummaryWidget.h"

#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "theme/AnDesignTokens.h"

#include <QGridLayout>
#include <QLabel>
#include <QVBoxLayout>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(ImageIntegration::CalibrationFrameSummaryWidget)

namespace ImageIntegration
{

class CalibrationFrameSummaryWidget::Impl
{
public:
    CalibrationFrameSummaryWidget* q;
    IntegrationDataModel* model;

    QLabel* lblBiasCount = nullptr;
    QLabel* lblDarkCount = nullptr;
    QLabel* lblFlatCount = nullptr;
    QLabel* lblLightCount = nullptr;
    QLabel* lblTotalCount = nullptr;
    QLabel* lblGroupCount = nullptr;

    explicit Impl(CalibrationFrameSummaryWidget* parent, IntegrationDataModel* m) : q(parent), model(m)
    {
        auto* layout = new QVBoxLayout(q);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);

        auto* title = new QLabel(tr("Frame Summary"), q);
        title->setObjectName(QStringLiteral("lblFrameSummaryTitle"));
        title->setFont(an::ui::tokens().typography.heading3);
        layout->addWidget(title);

        auto* grid = new QGridLayout();
        grid->setHorizontalSpacing(8);
        grid->setVerticalSpacing(4);

        auto addRow = [&](int row, const QString& labelText, QLabel*& valueLabel, const QString& objName) {
            auto* label = new QLabel(labelText, q);
            valueLabel = new QLabel(tr("-"), q);
            valueLabel->setObjectName(objName);
            grid->addWidget(label, row, 0, Qt::AlignLeft);
            grid->addWidget(valueLabel, row, 1, Qt::AlignRight);
        };

        addRow(0, tr("Bias"), lblBiasCount, QStringLiteral("lblBiasCount"));
        addRow(1, tr("Darks"), lblDarkCount, QStringLiteral("lblDarkCount"));
        addRow(2, tr("Flats"), lblFlatCount, QStringLiteral("lblFlatCount"));
        addRow(3, tr("Lights"), lblLightCount, QStringLiteral("lblLightCount"));
        addRow(4, tr("Total"), lblTotalCount, QStringLiteral("lblTotalCount"));
        addRow(5, tr("Light Groups"), lblGroupCount, QStringLiteral("lblLightGroupCount"));

        layout->addLayout(grid);
        layout->addStretch(1);

        q->setObjectName(QStringLiteral("frameSummaryWidget"));
    }

    void updateCounts()
    {
        if (!model)
        {
            return;
        }

        const int biasCount = static_cast<int>(model->biasFrames().frames().size());
        const int darkCount = static_cast<int>(model->darkFrames().frames().size());
        const int flatCount = static_cast<int>(model->flatFrames().frames().size());
        const int lightCount = static_cast<int>(model->lightFrames().frames().size());
        const int totalCount = biasCount + darkCount + flatCount + lightCount;
        const int lightGroups = static_cast<int>(model->lightGroups().size());

        lblBiasCount->setText(QString::number(biasCount));
        lblDarkCount->setText(QString::number(darkCount));
        lblFlatCount->setText(QString::number(flatCount));
        lblLightCount->setText(QString::number(lightCount));
        lblTotalCount->setText(QString::number(totalCount));
        lblGroupCount->setText(QString::number(lightGroups));
    }
};

CalibrationFrameSummaryWidget::CalibrationFrameSummaryWidget(IntegrationDataModel* model, QWidget* parent)
    : QWidget(parent), m_impl(std::make_unique<Impl>(this, model))
{
}

CalibrationFrameSummaryWidget::~CalibrationFrameSummaryWidget() = default;

void CalibrationFrameSummaryWidget::refresh()
{
    m_impl->updateCounts();
}

} // namespace ImageIntegration
