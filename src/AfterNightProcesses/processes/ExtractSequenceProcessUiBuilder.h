// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QAbstractButton>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QSizePolicy>
#include <QSlider>
#include <QSpacerItem>
#include <QSpinBox>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

struct ExtractSequenceProcessUiElements
{
    QVBoxLayout* verticalLayout = nullptr;
    QSpinBox* startFrameValue = nullptr;
    QSlider* startFrameSlider = nullptr;
    QSpinBox* endFrameValue = nullptr;
    QSlider* endFrameSlider = nullptr;
    QLabel* selectedFramesLabel = nullptr;
    QLabel* sequenceSizeLabel = nullptr;
    QDialogButtonBox* buttonBox = nullptr;
};

inline ExtractSequenceProcessUiElements buildExtractSequenceProcessUi(QWidget* parent)
{
    ExtractSequenceProcessUiElements ui;

    if (!parent)
    {
        return ui;
    }

    parent->setObjectName(QStringLiteral("ExtractSequenceProcess"));
    parent->resize(429, 203);
    parent->setMinimumSize(QSize(300, 150));
    parent->setWindowTitle(QStringLiteral("Extract Sequence"));

    const auto& t = an::ui::tokens();

    ui.verticalLayout = new QVBoxLayout(parent);
    ui.verticalLayout->setObjectName(QStringLiteral("verticalLayout"));
    ui.verticalLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
    ui.verticalLayout->setSpacing(t.spacing.md);

    auto* gridLayout = new QGridLayout();
    gridLayout->setObjectName(QStringLiteral("gridLayout"));

    auto* sequenceSizeTitle = new QLabel(QStringLiteral("Sequence Size"), parent);
    sequenceSizeTitle->setObjectName(QStringLiteral("label_4"));
    gridLayout->addWidget(sequenceSizeTitle, 3, 0);

    auto* endFrameTitle = new QLabel(QStringLiteral("End Frame"), parent);
    endFrameTitle->setObjectName(QStringLiteral("label_2"));
    gridLayout->addWidget(endFrameTitle, 1, 0);

    auto* startFrameTitle = new QLabel(QStringLiteral("Start Frame"), parent);
    startFrameTitle->setObjectName(QStringLiteral("label"));
    gridLayout->addWidget(startFrameTitle, 0, 0);

    ui.startFrameValue = new QSpinBox(parent);
    ui.startFrameValue->setObjectName(QStringLiteral("startFrameValue"));
    ui.startFrameValue->setMinimumWidth(80);
    ui.startFrameValue->setToolTip(QStringLiteral("Index of the first frame to include in the extracted sequence."));
    gridLayout->addWidget(ui.startFrameValue, 0, 1);

    ui.startFrameSlider = new QSlider(Qt::Horizontal, parent);
    ui.startFrameSlider->setObjectName(QStringLiteral("startFrameSlider"));
    gridLayout->addWidget(ui.startFrameSlider, 0, 2);

    ui.endFrameValue = new QSpinBox(parent);
    ui.endFrameValue->setObjectName(QStringLiteral("endFrameValue"));
    ui.endFrameValue->setMinimumWidth(80);
    ui.endFrameValue->setToolTip(QStringLiteral("Index of the last frame to include in the extracted sequence."));
    gridLayout->addWidget(ui.endFrameValue, 1, 1);

    ui.endFrameSlider = new QSlider(Qt::Horizontal, parent);
    ui.endFrameSlider->setObjectName(QStringLiteral("endFrameSlider"));
    gridLayout->addWidget(ui.endFrameSlider, 1, 2);

    auto* selectedFramesTitle = new QLabel(QStringLiteral("Selected Frames"), parent);
    selectedFramesTitle->setObjectName(QStringLiteral("label_3"));
    gridLayout->addWidget(selectedFramesTitle, 2, 0);

    ui.selectedFramesLabel = new QLabel(parent);
    ui.selectedFramesLabel->setObjectName(QStringLiteral("selectedFramesLabel"));
    ui.selectedFramesLabel->setMinimumSize(QSize(70, 0));
    ui.selectedFramesLabel->setMaximumSize(QSize(70, QWIDGETSIZE_MAX));
    ui.selectedFramesLabel->setText(QString());
    gridLayout->addWidget(ui.selectedFramesLabel, 2, 1);

    ui.sequenceSizeLabel = new QLabel(parent);
    ui.sequenceSizeLabel->setObjectName(QStringLiteral("sequenceSizeLabel"));
    ui.sequenceSizeLabel->setText(QString());
    gridLayout->addWidget(ui.sequenceSizeLabel, 3, 1);

    ui.verticalLayout->addLayout(gridLayout);
    ui.verticalLayout->addItem(new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding));

    ui.buttonBox =
        new QDialogButtonBox(QDialogButtonBox::Close | QDialogButtonBox::Ok | QDialogButtonBox::Reset, parent);
    ui.buttonBox->setObjectName(QStringLiteral("buttonBox"));
    if (auto* btn = ui.buttonBox->button(QDialogButtonBox::Close))
        btn->setToolTip(QStringLiteral("Close this dialog without applying any changes."));
    if (auto* btn = ui.buttonBox->button(QDialogButtonBox::Reset))
        btn->setToolTip(QStringLiteral("Reset all parameters to their default values."));
    if (auto* btn = ui.buttonBox->button(QDialogButtonBox::Ok))
        btn->setToolTip(QStringLiteral("Extract the selected frame range from the sequence."));
    ui.verticalLayout->addWidget(ui.buttonBox);

    return ui;
}

} // namespace an::ui::processes
