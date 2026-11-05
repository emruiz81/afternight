// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QtCore/QCoreApplication>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "components/AnCardWidget.h"
#include "controls/RTPreviewHistogramToolbar.h"
#include "theme/AnDesignTokens.h"

namespace an::ui::controls
{

class ImageHistogramControlUiElements
{
public:
    QWidget* verticalLayoutWidget = nullptr;
    QVBoxLayout* verticalLayout = nullptr;
    QHBoxLayout* horizontalLayout1 = nullptr;
    QScrollArea* chartScrollArea = nullptr;
    QWidget* scrollAreaWidgetContents = nullptr;

    // Histogram toolbar — QToolButton for visual consistency with process panels
    QToolButton* zoomIn = nullptr;
    QToolButton* zoomOut = nullptr;
    QToolButton* zoomReset = nullptr;
    QToolButton* gridButton = nullptr;
    QToolButton* showClippingInfo = nullptr;
    QToolButton* localChannelNormalization = nullptr;
    QToolButton* showHistogramAreas = nullptr;
    QComboBox* plotResolution = nullptr;

    QLabel* label = nullptr;
    QComboBox* channelSelector = nullptr;
    QHBoxLayout* horizontalLayout_2 = nullptr;
    QHBoxLayout* dialogButtonsLayout = nullptr;
    QDialogButtonBox* buttonBox = nullptr;

    QWidget* headerSection = nullptr;
    QLabel* headerTitleLabel = nullptr;
    QLabel* headerSubtitleLabel = nullptr;
    an::ui::AnCardWidget* bodyCard = nullptr;

    void setupUi(QWidget* parent)
    {
        if (parent->objectName().isEmpty())
            parent->setObjectName("ImageHistogramControl");
        parent->resize(860, 620);
        parent->setMaximumSize(QSize(5000, 5000));
        parent->setMinimumSize(QSize(700, 460));
        const auto& t = an::ui::tokens();

        // The parent stands in as the struct's layout widget.
        verticalLayoutWidget = parent;

        verticalLayout = new QVBoxLayout(parent);
        verticalLayout->setObjectName("verticalLayout");
        verticalLayout->setContentsMargins(t.spacing.lg, t.spacing.lg, t.spacing.lg, t.spacing.md);
        verticalLayout->setSpacing(t.spacing.md);

        headerSection = new QWidget(parent);
        headerSection->setObjectName(QStringLiteral("imageHistogramHeaderSection"));
        auto* headerLayout = new QVBoxLayout(headerSection);
        headerLayout->setContentsMargins(0, 0, 0, 0);
        headerLayout->setSpacing(t.spacing.xs);

        headerTitleLabel = new QLabel(headerSection);
        headerTitleLabel->setObjectName(QStringLiteral("anProcessShellHeaderTitle"));
        headerLayout->addWidget(headerTitleLabel);

        headerSubtitleLabel = new QLabel(headerSection);
        headerSubtitleLabel->setObjectName(QStringLiteral("anProcessShellHeaderDescription"));
        headerSubtitleLabel->setWordWrap(true);
        headerLayout->addWidget(headerSubtitleLabel);

        headerTitleLabel->setText(QCoreApplication::translate("ImageHistogramControl", "Histogram Analysis", nullptr));
        headerSubtitleLabel->setText(QCoreApplication::translate(
            "ImageHistogramControl",
            "Inspect channel distributions, clipping, and scale behavior for the active image.", nullptr));
        verticalLayout->addWidget(headerSection);

        bodyCard = new an::ui::AnCardWidget(QString(), parent);
        bodyCard->setObjectName(QStringLiteral("imageHistogramBodyCard"));
        auto* bodyLayout = new QVBoxLayout();
        bodyLayout->setContentsMargins(0, 0, 0, 0);
        bodyLayout->setSpacing(t.spacing.sm);

        horizontalLayout1 = new QHBoxLayout();
        horizontalLayout1->setObjectName("horizontalLayout1");
        horizontalLayout1->setContentsMargins(0, 0, 0, 0);

        chartScrollArea = new QScrollArea(bodyCard);
        chartScrollArea->setObjectName("chartScrollArea");
        chartScrollArea->setWidgetResizable(true);

        scrollAreaWidgetContents = new QWidget();
        scrollAreaWidgetContents->setObjectName("scrollAreaWidgetContents");
        chartScrollArea->setWidget(scrollAreaWidgetContents);

        horizontalLayout1->addWidget(chartScrollArea);
        bodyLayout->addLayout(horizontalLayout1, 1);

        auto* toolbarLayout = new QHBoxLayout();
        toolbarLayout->setObjectName("histogramSettingsLayout_2");

        const processes::rt_preview_ui::HistogramToolbarOptions toolbarOpts{
            .translationContext = "ImageHistogramControl",
            .includeShowClippingInfo = true,
            .includeShowHistogram = false, // this control is already a full histogram view
            .localChannelNormalizationChecked = false,
            .showHistogramAreasChecked = true,
        };

        const auto toolbar = processes::rt_preview_ui::buildHistogramToolbar(parent, toolbarLayout, toolbarOpts);

        zoomIn = toolbar.zoomIn;
        zoomOut = toolbar.zoomOut;
        zoomReset = toolbar.zoomReset;
        gridButton = toolbar.gridButton;
        showClippingInfo = toolbar.showClippingInfo;
        localChannelNormalization = toolbar.localChannelNormalization;
        showHistogramAreas = toolbar.showHistogramAreas;
        plotResolution = toolbar.plotResolution;

        // Channel selector — appended after the spacer that buildHistogramToolbar added
        label = new QLabel(parent);
        label->setObjectName("label");
        label->setText(QCoreApplication::translate("ImageHistogramControl", "Channel", nullptr));
        toolbarLayout->addWidget(label);

        channelSelector = new QComboBox(parent);
        channelSelector->setObjectName("channelSelector");
        channelSelector->setMinimumWidth(120);
        toolbarLayout->addWidget(channelSelector);

        bodyLayout->addLayout(toolbarLayout, 0);
        bodyCard->setTitle(
            QCoreApplication::translate("ImageHistogramControl", "Histogram Plot and Controls", nullptr));
        bodyCard->setContentLayout(bodyLayout);
        verticalLayout->addWidget(bodyCard, 1);

        horizontalLayout_2 = new QHBoxLayout();
        horizontalLayout_2->setObjectName("horizontalLayout_2");
        horizontalLayout_2->setContentsMargins(0, 0, 0, 0);
        horizontalLayout_2->addStretch(1);

        dialogButtonsLayout = new QHBoxLayout();
        dialogButtonsLayout->setObjectName(QStringLiteral("dialogButtonsLayout"));
        dialogButtonsLayout->setContentsMargins(0, 0, 0, 0);

        buttonBox = new QDialogButtonBox(parent);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setStandardButtons(QDialogButtonBox::StandardButton::Close |
                                      QDialogButtonBox::StandardButton::Reset);
        if (auto* closeBtn = buttonBox->button(QDialogButtonBox::Close))
        {
            closeBtn->setProperty("buttonVariant", QStringLiteral("primary"));
            closeBtn->setDefault(true);
        }

        dialogButtonsLayout->addWidget(buttonBox);
        horizontalLayout_2->addLayout(dialogButtonsLayout);
        verticalLayout->addLayout(horizontalLayout_2);

        parent->setWindowTitle(QCoreApplication::translate("ImageHistogramControl", "Histogram", nullptr));

        QMetaObject::connectSlotsByName(parent);
    }
};

inline ImageHistogramControlUiElements buildImageHistogramControlUi(QWidget* parent)
{
    ImageHistogramControlUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::controls
