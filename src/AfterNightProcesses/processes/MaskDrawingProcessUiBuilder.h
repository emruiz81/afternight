// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "controls/drawing/DrawingToolbar.h"
#include "processes/RTPreviewUiCommon.h"
#include "theme/AnDesignTokens.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QSizePolicy>
#include <QSpacerItem>
#include <QVBoxLayout>
#include <QWidget>

namespace an::ui::processes
{

struct MaskDrawingProcessUiElements
{
    QHBoxLayout* horizontalLayoutMain = nullptr;
    QVBoxLayout* previewLayout = nullptr;
    an::ui::drawing::DrawingToolbar* drawingToolbar = nullptr;
    QLabel* instructionsLabel = nullptr;
    QComboBox* targetImageSelector = nullptr;
    QCheckBox* preview = nullptr;
    QDialogButtonBox* buttonBox = nullptr;
};

inline MaskDrawingProcessUiElements buildMaskDrawingProcessUi(QWidget* parent)
{
    MaskDrawingProcessUiElements ui;
    if (!parent)
    {
        return ui;
    }

    const auto& t = an::ui::tokens();
    parent->setObjectName(QStringLiteral("MaskDrawingProcess"));
    parent->resize(960, 640);
    parent->setMinimumSize(QSize(760, 520));
    parent->setWindowTitle(QCoreApplication::translate("MaskDrawingProcess", "Mask Drawing", nullptr));

    ui.horizontalLayoutMain = new QHBoxLayout(parent);
    ui.horizontalLayoutMain->setObjectName(QStringLiteral("horizontalLayoutMain"));
    ui.horizontalLayoutMain->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
    ui.horizontalLayoutMain->setSpacing(t.spacing.md);

    auto panes = rt_preview_ui::buildMainProcessSplitter(ui.horizontalLayoutMain, parent);
    ui.previewLayout = panes.previewLayout;

    panes.controlsLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
    panes.controlsLayout->setSpacing(t.spacing.md);

    ui.instructionsLabel =
        new QLabel(QCoreApplication::translate("MaskDrawingProcess",
                                               "Paint a grayscale mask directly on the RT preview pane.", nullptr),
                   panes.controlsPanel);
    ui.instructionsLabel->setObjectName(QStringLiteral("instructionsLabel"));
    ui.instructionsLabel->setWordWrap(true);
    panes.controlsLayout->addWidget(ui.instructionsLabel);

    ui.drawingToolbar = new an::ui::drawing::DrawingToolbar(panes.controlsPanel);
    ui.drawingToolbar->setObjectName(QStringLiteral("drawingToolbar"));
    panes.controlsLayout->addWidget(ui.drawingToolbar);
    panes.controlsLayout->addItem(new QSpacerItem(20, 20, QSizePolicy::Minimum, QSizePolicy::Expanding));

    auto* selectorLayout = new QFormLayout();
    selectorLayout->setObjectName(QStringLiteral("selectorLayout"));
    selectorLayout->setHorizontalSpacing(t.spacing.md);
    selectorLayout->setVerticalSpacing(t.spacing.sm);

    auto* targetLabel =
        new QLabel(QCoreApplication::translate("MaskDrawingProcess", "Target Image", nullptr), panes.controlsPanel);
    targetLabel->setObjectName(QStringLiteral("targetLabel"));
    selectorLayout->setWidget(0, QFormLayout::LabelRole, targetLabel);

    ui.targetImageSelector = new QComboBox(panes.controlsPanel);
    ui.targetImageSelector->setObjectName(QStringLiteral("targetImageSelector"));
    ui.targetImageSelector->setMinimumWidth(180);
    ui.targetImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    selectorLayout->setWidget(0, QFormLayout::FieldRole, ui.targetImageSelector);
    panes.controlsLayout->addLayout(selectorLayout);

    auto* previewRow = new QHBoxLayout();
    previewRow->setObjectName(QStringLiteral("previewRow"));
    previewRow->setSpacing(t.spacing.md);
    ui.preview =
        new QCheckBox(QCoreApplication::translate("MaskDrawingProcess", "Preview", nullptr), panes.controlsPanel);
    ui.preview->setObjectName(QStringLiteral("preview"));
    ui.preview->setChecked(true);
    ui.preview->setEnabled(false);
    previewRow->addWidget(ui.preview);
    previewRow->addItem(new QSpacerItem(20, 20, QSizePolicy::Expanding, QSizePolicy::Minimum));
    panes.controlsLayout->addLayout(previewRow);

    ui.buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Reset,
                                        panes.controlsPanel);
    ui.buttonBox->setObjectName(QStringLiteral("buttonBox"));
    panes.controlsLayout->addWidget(ui.buttonBox);

    ui.instructionsLabel->setToolTip(QCoreApplication::translate(
        "MaskDrawingProcess", "Use the toolbar to paint or erase directly over the preview image.", nullptr));
    ui.targetImageSelector->setToolTip(QCoreApplication::translate(
        "MaskDrawingProcess", "Choose the open image that the drawn mask should target.", nullptr));
    ui.preview->setToolTip(QCoreApplication::translate(
        "MaskDrawingProcess",
        "The drawing canvas requires the RT preview pane and stays enabled while this process is open.", nullptr));
    if (auto* okButton = ui.buttonBox->button(QDialogButtonBox::Ok))
    {
        okButton->setToolTip(QCoreApplication::translate(
            "MaskDrawingProcess", "Create a new grayscale mask image from the current drawing.", nullptr));
    }
    if (auto* cancelButton = ui.buttonBox->button(QDialogButtonBox::Cancel))
    {
        cancelButton->setToolTip(QCoreApplication::translate(
            "MaskDrawingProcess", "Close this dialog without creating a new mask.", nullptr));
    }
    if (auto* resetButton = ui.buttonBox->button(QDialogButtonBox::Reset))
    {
        resetButton->setToolTip(QCoreApplication::translate(
            "MaskDrawingProcess", "Clear the drawing and restore the default brush settings.", nullptr));
    }

    return ui;
}

} // namespace an::ui::processes
