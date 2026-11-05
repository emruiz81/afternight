// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QtCore/QVariant>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QApplication>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "components/AnCardWidget.h"
#include "components/AnStyledTable.h"
#include "theme/AnDesignTokens.h"

namespace an::ui::controls
{

class ImageHistoryControlUiElements
{
public:
    QWidget* verticalLayoutWidget = nullptr;
    QVBoxLayout* verticalLayout = nullptr;
    QHBoxLayout* contentLayout = nullptr;
    QTableWidget* tableWidget = nullptr;
    QHBoxLayout* horizontalLayout = nullptr;
    QSpacerItem* horizontalSpacer = nullptr;
    QPushButton* moveUpButton = nullptr;
    QPushButton* moveDownButton = nullptr;
    QPushButton* saveLogButton = nullptr;
    QPushButton* toggleLineageButton = nullptr;
    QPushButton* refreshButton = nullptr;
    QPushButton* closeButton = nullptr;
    QWidget* headerSection = nullptr;
    QLabel* headerTitleLabel = nullptr;
    QLabel* headerSubtitleLabel = nullptr;
    an::ui::AnCardWidget* historyCard = nullptr;
    an::ui::AnCardWidget* previewCard = nullptr;
    QLabel* selectedStepTitleLabel = nullptr;
    QLabel* selectedStepSubtitleLabel = nullptr;
    QWidget* previewWidgetHost = nullptr;
    QVBoxLayout* previewWidgetHostLayout = nullptr;
    QWidget* histogramWidgetHost = nullptr;
    QVBoxLayout* histogramWidgetHostLayout = nullptr;
    QScrollArea* histogramScrollArea = nullptr;

    void setupUi(QWidget* ImageHistoryControl)
    {
        if (ImageHistoryControl->objectName().isEmpty())
            ImageHistoryControl->setObjectName("ImageHistoryControl");
        ImageHistoryControl->resize(1040, 700);
        QSizePolicy sizePolicy(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Preferred);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(ImageHistoryControl->sizePolicy().hasHeightForWidth());
        ImageHistoryControl->setSizePolicy(sizePolicy);
        ImageHistoryControl->setMinimumSize(QSize(920, 580));
        ImageHistoryControl->setMaximumSize(QSize(5000, 5000));
        const auto& t = an::ui::tokens();
        verticalLayoutWidget = nullptr;
        verticalLayout = new QVBoxLayout(ImageHistoryControl);
        verticalLayout->setObjectName("verticalLayout");
        verticalLayout->setContentsMargins(t.spacing.lg, t.spacing.lg, t.spacing.lg, t.spacing.md);
        verticalLayout->setSpacing(t.spacing.md);

        headerSection = new QWidget(ImageHistoryControl);
        headerSection->setObjectName(QStringLiteral("imageHistoryHeaderSection"));
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

        verticalLayout->addWidget(headerSection);

        contentLayout = new QHBoxLayout();
        contentLayout->setObjectName(QStringLiteral("contentLayout"));
        contentLayout->setContentsMargins(0, 0, 0, 0);
        contentLayout->setSpacing(t.spacing.md);

        historyCard = new an::ui::AnCardWidget(QString(), ImageHistoryControl);
        historyCard->setObjectName(QStringLiteral("imageHistoryBodyCard"));
        auto* cardLayout = new QVBoxLayout();
        cardLayout->setContentsMargins(0, 0, 0, 0);
        cardLayout->setSpacing(t.spacing.sm);

        tableWidget = new an::ui::AnStyledTable(historyCard);
        tableWidget->setObjectName("tableWidget");
        tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
        tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
        tableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
        tableWidget->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
        tableWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        cardLayout->addWidget(tableWidget, 1);

        historyCard->setContentLayout(cardLayout);
        contentLayout->addWidget(historyCard, 5);

        previewCard = new an::ui::AnCardWidget(QString(), ImageHistoryControl);
        previewCard->setObjectName(QStringLiteral("imageHistoryPreviewCard"));
        auto* previewLayout = new QVBoxLayout();
        previewLayout->setContentsMargins(0, 0, 0, 0);
        previewLayout->setSpacing(t.spacing.sm);

        selectedStepTitleLabel = new QLabel(previewCard);
        selectedStepTitleLabel->setObjectName(QStringLiteral("historyStepPreviewTitle"));
        selectedStepTitleLabel->setWordWrap(true);
        previewLayout->addWidget(selectedStepTitleLabel);

        selectedStepSubtitleLabel = new QLabel(previewCard);
        selectedStepSubtitleLabel->setObjectName(QStringLiteral("historyStepPreviewSubtitle"));
        selectedStepSubtitleLabel->setWordWrap(true);
        previewLayout->addWidget(selectedStepSubtitleLabel);

        previewWidgetHost = new QWidget(previewCard);
        previewWidgetHost->setObjectName(QStringLiteral("historyStepPreviewHost"));
        previewWidgetHost->setMinimumHeight(220);
        previewWidgetHostLayout = new QVBoxLayout(previewWidgetHost);
        previewWidgetHostLayout->setContentsMargins(0, 0, 0, 0);
        previewWidgetHostLayout->setSpacing(0);
        previewLayout->addWidget(previewWidgetHost, 3);

        histogramWidgetHost = new QWidget(previewCard);
        histogramWidgetHost->setObjectName(QStringLiteral("historyStepHistogramHost"));
        histogramWidgetHost->setMinimumHeight(160);
        histogramWidgetHostLayout = new QVBoxLayout(histogramWidgetHost);
        histogramWidgetHostLayout->setContentsMargins(0, 0, 0, 0);
        histogramWidgetHostLayout->setSpacing(0);
        // The histogram is the histogram widgets' to make, inside this scroll area.
        histogramScrollArea = new QScrollArea(histogramWidgetHost);
        histogramScrollArea->setObjectName(QStringLiteral("historyStepHistogramScrollArea"));
        histogramScrollArea->setFrameShape(QFrame::NoFrame);
        histogramScrollArea->setWidgetResizable(true);
        histogramWidgetHostLayout->addWidget(histogramScrollArea);
        previewLayout->addWidget(histogramWidgetHost, 2);

        previewCard->setContentLayout(previewLayout);
        contentLayout->addWidget(previewCard, 4);

        verticalLayout->addLayout(contentLayout, 1);

        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName("horizontalLayout");
        horizontalLayout->setContentsMargins(0, 0, 0, 0);

        moveUpButton = new QPushButton(ImageHistoryControl);
        moveUpButton->setObjectName(QStringLiteral("moveUpButton"));
        moveUpButton->setProperty("anButtonVariant", QStringLiteral("secondary"));
        moveUpButton->setMinimumWidth(44);
        horizontalLayout->addWidget(moveUpButton);

        moveDownButton = new QPushButton(ImageHistoryControl);
        moveDownButton->setObjectName(QStringLiteral("moveDownButton"));
        moveDownButton->setProperty("anButtonVariant", QStringLiteral("secondary"));
        moveDownButton->setMinimumWidth(44);
        horizontalLayout->addWidget(moveDownButton);

        toggleLineageButton = new QPushButton(ImageHistoryControl);
        toggleLineageButton->setObjectName(QStringLiteral("toggleLineageButton"));
        toggleLineageButton->setProperty("anButtonVariant", QStringLiteral("secondary"));
        toggleLineageButton->setCheckable(true);
        toggleLineageButton->setChecked(true);
        toggleLineageButton->setMinimumWidth(44);
        horizontalLayout->addWidget(toggleLineageButton);

        saveLogButton = new QPushButton(ImageHistoryControl);
        saveLogButton->setObjectName(QStringLiteral("saveLogButton"));
        saveLogButton->setProperty("anButtonVariant", QStringLiteral("secondary"));
        saveLogButton->setMinimumWidth(44);
        horizontalLayout->addWidget(saveLogButton);

        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        horizontalLayout->addItem(horizontalSpacer);

        refreshButton = new QPushButton(ImageHistoryControl);
        refreshButton->setObjectName("refreshButton");
        refreshButton->setProperty("buttonVariant", QStringLiteral("secondary"));
        refreshButton->setMinimumWidth(100);

        horizontalLayout->addWidget(refreshButton);

        closeButton = new QPushButton(ImageHistoryControl);
        closeButton->setObjectName("closeButton");
        closeButton->setProperty("buttonVariant", QStringLiteral("primary"));
        closeButton->setMinimumWidth(100);
        closeButton->setDefault(true);

        horizontalLayout->addWidget(closeButton);

        verticalLayout->addLayout(horizontalLayout);

        retranslateUi(ImageHistoryControl);

        QMetaObject::connectSlotsByName(ImageHistoryControl);
    } // setupUi

    void retranslateUi(QWidget* ImageHistoryControl)
    {
        ImageHistoryControl->setWindowTitle(
            QCoreApplication::translate("ImageHistoryControl", "Image History", nullptr));
        headerTitleLabel->setText(QCoreApplication::translate("ImageHistoryControl", "Image History", nullptr));
        headerSubtitleLabel->setText(QCoreApplication::translate(
            "ImageHistoryControl",
            "Review the sequence of edits applied to the active image and refresh the timeline when needed.", nullptr));
        historyCard->setTitle(QCoreApplication::translate("ImageHistoryControl", "History Timeline", nullptr));
        previewCard->setTitle(QCoreApplication::translate("ImageHistoryControl", "Selected Step Preview", nullptr));
        selectedStepTitleLabel->setText(QCoreApplication::translate("ImageHistoryControl", "Current Step", nullptr));
        selectedStepSubtitleLabel->setText(QCoreApplication::translate(
            "ImageHistoryControl", "Select a row to inspect its preview, histogram, and available actions.", nullptr));
        moveUpButton->setText(QString());
        moveDownButton->setText(QString());
        toggleLineageButton->setText(QString());
        saveLogButton->setText(QString());
        refreshButton->setText(QCoreApplication::translate("ImageHistoryControl", "Refresh", nullptr));
        closeButton->setText(QCoreApplication::translate("ImageHistoryControl", "Close", nullptr));
    } // retranslateUi
};

inline ImageHistoryControlUiElements buildImageHistoryControlUi(QWidget* parent)
{
    ImageHistoryControlUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::controls
