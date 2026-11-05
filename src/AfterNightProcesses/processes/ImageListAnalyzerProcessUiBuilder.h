// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Programmatic UI builder derived from the former Qt Designer layout
// for ImageListAnalyzerProcess.

#pragma once

#include <QtCore/QVariant>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>
#include <widgets/HistogramWidget.h>
#include "theme/AnDesignTokens.h"
#include "widgets/TableWidget.h"

namespace an::ui::processes
{

class ImageListAnalyzerProcessUiElements
{
public:
    QWidget* layoutWidget;
    QVBoxLayout* verticalLayout_Main;
    QHBoxLayout* imagePreviewLayout;
    QVBoxLayout* verticalLayout_2;
    QWidget* imagePreviewWidget;

    QVBoxLayout* verticalLayout;
    QTextEdit* imageSummary;
    HistogramWidget* histogramWidget;
    QPushButton* optimizeButton;
    QLabel* showInfoLabel;
    QHBoxLayout* horizontalLayout;
    QPushButton* exportSequenceButton;
    QVBoxLayout* tableLayout;
    TableWidget* tableWidget;
    QHBoxLayout* horizontalLayout_3;
    QPushButton* addFiles;
    QPushButton* addFolder;
    QPushButton* removeSelected;
    QLabel* label_27;
    QLabel* currentImageLabel;
    QLabel* label_2;
    QLabel* totalImagesLabel;
    QSpacerItem* horizontalSpacer_2;
    QPushButton* clearAll;
    QHBoxLayout* horizontalLayout_7;
    QDialogButtonBox* buttonBox;

    void setupUi(QWidget* ImageListAnalyzerProcess)
    {
        const auto& t = an::ui::tokens();

        if (ImageListAnalyzerProcess->objectName().isEmpty())
            ImageListAnalyzerProcess->setObjectName("ImageListAnalyzerProcess");
        ImageListAnalyzerProcess->setWindowModality(Qt::WindowModality::NonModal);
        ImageListAnalyzerProcess->resize(1398, 1042);
        ImageListAnalyzerProcess->setMinimumSize(QSize(1200, 900));
        ImageListAnalyzerProcess->setMaximumSize(QSize(16777215, 16777215));
        ImageListAnalyzerProcess->setProperty("sizeGripEnabled", QVariant(true));

        auto* rootLayout = new QVBoxLayout(ImageListAnalyzerProcess);
        rootLayout->setObjectName("rootLayout");
        rootLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        rootLayout->setSpacing(0);

        layoutWidget = new QWidget(ImageListAnalyzerProcess);
        layoutWidget->setObjectName("layoutWidget");
        rootLayout->addWidget(layoutWidget);
        verticalLayout_Main = new QVBoxLayout(layoutWidget);
        verticalLayout_Main->setSpacing(t.spacing.md);
        verticalLayout_Main->setObjectName("verticalLayout_Main");
        verticalLayout_Main->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        imagePreviewLayout = new QHBoxLayout();
        imagePreviewLayout->setObjectName("imagePreviewLayout");
        verticalLayout_2 = new QVBoxLayout();
        verticalLayout_2->setObjectName("verticalLayout_2");
        imagePreviewWidget = new QWidget(layoutWidget);
        imagePreviewWidget->setObjectName("imagePreviewWidget");
        imagePreviewWidget->setMinimumSize(QSize(500, 400));

        verticalLayout_2->addWidget(imagePreviewWidget);

        verticalLayout_2->setStretch(0, 1);

        imagePreviewLayout->addLayout(verticalLayout_2);

        verticalLayout = new QVBoxLayout();
        verticalLayout->setObjectName("verticalLayout");
        imageSummary = new QTextEdit(layoutWidget);
        imageSummary->setObjectName("imageSummary");
        imageSummary->setReadOnly(true);

        verticalLayout->addWidget(imageSummary);

        histogramWidget = new HistogramWidget(layoutWidget);
        histogramWidget->setObjectName("histogramWidget");
        histogramWidget->setMinimumSize(QSize(0, 250));

        verticalLayout->addWidget(histogramWidget);

        optimizeButton = new QPushButton(layoutWidget);
        optimizeButton->setObjectName("optimizeButton");
        optimizeButton->setMinimumSize(QSize(0, 0));

        verticalLayout->addWidget(optimizeButton);

        showInfoLabel = new QLabel(layoutWidget);
        showInfoLabel->setObjectName("showInfoLabel");
        showInfoLabel->setAlignment(Qt::AlignmentFlag::AlignCenter);

        verticalLayout->addWidget(showInfoLabel);

        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName("horizontalLayout");
        exportSequenceButton = new QPushButton(layoutWidget);
        exportSequenceButton->setObjectName("exportSequenceButton");
        exportSequenceButton->setMinimumSize(QSize(0, 50));

        horizontalLayout->addWidget(exportSequenceButton);

        verticalLayout->addLayout(horizontalLayout);

        imagePreviewLayout->addLayout(verticalLayout);

        imagePreviewLayout->setStretch(0, 2);
        imagePreviewLayout->setStretch(1, 1);

        verticalLayout_Main->addLayout(imagePreviewLayout);

        tableLayout = new QVBoxLayout();
        tableLayout->setObjectName("tableLayout");
        tableWidget = new TableWidget(layoutWidget);
        tableWidget->setObjectName("tableWidget");
        tableWidget->setMinimumSize(QSize(0, 250));

        tableLayout->addWidget(tableWidget);

        horizontalLayout_3 = new QHBoxLayout();
        horizontalLayout_3->setObjectName("horizontalLayout_3");
        addFiles = new QPushButton(layoutWidget);
        addFiles->setObjectName("addFiles");

        horizontalLayout_3->addWidget(addFiles);

        addFolder = new QPushButton(layoutWidget);
        addFolder->setObjectName("addFolder");

        horizontalLayout_3->addWidget(addFolder);

        removeSelected = new QPushButton(layoutWidget);
        removeSelected->setObjectName("removeSelected");

        horizontalLayout_3->addWidget(removeSelected);

        label_27 = new QLabel(layoutWidget);
        label_27->setObjectName("label_27");

        horizontalLayout_3->addWidget(label_27);

        currentImageLabel = new QLabel(layoutWidget);
        currentImageLabel->setObjectName("currentImageLabel");
        currentImageLabel->setMinimumSize(QSize(45, 0));

        horizontalLayout_3->addWidget(currentImageLabel);

        label_2 = new QLabel(layoutWidget);
        label_2->setObjectName("label_2");

        horizontalLayout_3->addWidget(label_2);

        totalImagesLabel = new QLabel(layoutWidget);
        totalImagesLabel->setObjectName("totalImagesLabel");
        totalImagesLabel->setMinimumSize(QSize(45, 0));

        horizontalLayout_3->addWidget(totalImagesLabel);

        horizontalSpacer_2 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        horizontalLayout_3->addItem(horizontalSpacer_2);

        clearAll = new QPushButton(layoutWidget);
        clearAll->setObjectName("clearAll");

        horizontalLayout_3->addWidget(clearAll);

        tableLayout->addLayout(horizontalLayout_3);

        verticalLayout_Main->addLayout(tableLayout);

        horizontalLayout_7 = new QHBoxLayout();
        horizontalLayout_7->setObjectName("horizontalLayout_7");
        buttonBox = new QDialogButtonBox(layoutWidget);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setStandardButtons(QDialogButtonBox::StandardButton::Close |
                                      QDialogButtonBox::StandardButton::Reset);

        horizontalLayout_7->addWidget(buttonBox);

        verticalLayout_Main->addLayout(horizontalLayout_7);

        verticalLayout_Main->setStretch(0, 2);
        verticalLayout_Main->setStretch(1, 1);

        retranslateUi(ImageListAnalyzerProcess);

        QMetaObject::connectSlotsByName(ImageListAnalyzerProcess);
    } // setupUi

    void retranslateUi(QWidget* ImageListAnalyzerProcess)
    {
        ImageListAnalyzerProcess->setWindowTitle(
            QCoreApplication::translate("ImageListAnalyzerProcess", "Image List Analyzer", nullptr));

        optimizeButton->setText(
            QCoreApplication::translate("ImageListAnalyzerProcess", "Optimize For Fast Play", nullptr));
        showInfoLabel->setText(QString());
        exportSequenceButton->setText(
            QCoreApplication::translate("ImageListAnalyzerProcess", "Export Sequence", nullptr));
        addFiles->setText(QCoreApplication::translate("ImageListAnalyzerProcess", "Add Files", nullptr));
        addFolder->setText(QCoreApplication::translate("ImageListAnalyzerProcess", "Add Folder", nullptr));
        removeSelected->setText(QCoreApplication::translate("ImageListAnalyzerProcess", "Remove Selected", nullptr));
        label_27->setText(QCoreApplication::translate("ImageListAnalyzerProcess", "Current Image: ", nullptr));
        currentImageLabel->setText(QString());
        label_2->setText(QCoreApplication::translate("ImageListAnalyzerProcess", "Total Images:", nullptr));
        totalImagesLabel->setText(QString());
        clearAll->setText(QCoreApplication::translate("ImageListAnalyzerProcess", "Clear All", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Close))
            btn->setToolTip(QCoreApplication::translate("ImageListAnalyzerProcess",
                                                        "Close this dialog without applying any changes.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Reset))
            btn->setToolTip(QCoreApplication::translate("ImageListAnalyzerProcess",
                                                        "Reset all parameters to their default values.", nullptr));
        optimizeButton->setToolTip(QCoreApplication::translate(
            "ImageListAnalyzerProcess",
            "Optimise the image sequence for fast playback by writing a stretched, full-resolution 8-bit proxy of "
            "every image.",
            nullptr));
        exportSequenceButton->setToolTip(QCoreApplication::translate(
            "ImageListAnalyzerProcess", "Export the analysed image sequence to a SER, AVI, MP4 or WebM file.",
            nullptr));
        addFiles->setToolTip(
            QCoreApplication::translate("ImageListAnalyzerProcess", "Add image files to the analysis list.", nullptr));
        addFolder->setToolTip(QCoreApplication::translate(
            "ImageListAnalyzerProcess",
            "Add all image files from a folder with optional recursive scanning and metadata filtering.", nullptr));
        removeSelected->setToolTip(QCoreApplication::translate(
            "ImageListAnalyzerProcess", "Remove selected images from the analysis list.", nullptr));
        clearAll->setToolTip(QCoreApplication::translate("ImageListAnalyzerProcess",
                                                         "Remove all images from the analysis list.", nullptr));
    } // retranslateUi
};

inline ImageListAnalyzerProcessUiElements buildImageListAnalyzerProcessUi(QWidget* parent)
{
    ImageListAnalyzerProcessUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
