// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "components/AnCardWidget.h"
#include "components/AnStyledTable.h"
#include "theme/AnDesignTokens.h"

namespace an::ui::controls
{

class ImageEXIFViewerControlUiElements
{
public:
    QVBoxLayout* verticalLayout_2 = nullptr;
    QVBoxLayout* verticalLayout = nullptr;
    QTableWidget* tableWidget = nullptr;
    QHBoxLayout* horizontalLayout = nullptr;
    QSpacerItem* horizontalSpacer = nullptr;
    QPushButton* closeButton = nullptr;
    QWidget* headerSection = nullptr;
    QLabel* headerTitleLabel = nullptr;
    QLabel* headerSubtitleLabel = nullptr;
    an::ui::AnCardWidget* metadataCard = nullptr;

    void setupUi(QWidget* ImageEXIFViewerControl)
    {
        if (ImageEXIFViewerControl->objectName().isEmpty())
            ImageEXIFViewerControl->setObjectName("ImageEXIFViewerControl");
        ImageEXIFViewerControl->resize(760, 520);
        QSizePolicy sizePolicy(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Preferred);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(ImageEXIFViewerControl->sizePolicy().hasHeightForWidth());
        ImageEXIFViewerControl->setSizePolicy(sizePolicy);
        ImageEXIFViewerControl->setMinimumSize(QSize(680, 420));
        ImageEXIFViewerControl->setMaximumSize(QSize(5000, 5000));
        const auto& t = an::ui::tokens();
        verticalLayout_2 = new QVBoxLayout(ImageEXIFViewerControl);
        verticalLayout_2->setObjectName("verticalLayout_2");
        verticalLayout_2->setContentsMargins(t.spacing.lg, t.spacing.lg, t.spacing.lg, t.spacing.md);
        verticalLayout_2->setSpacing(t.spacing.md);
        verticalLayout = new QVBoxLayout();
        verticalLayout->setObjectName("verticalLayout");
        verticalLayout->setContentsMargins(0, 0, 0, 0);
        verticalLayout->setSpacing(t.spacing.md);

        headerSection = new QWidget(ImageEXIFViewerControl);
        headerSection->setObjectName(QStringLiteral("imageExifHeaderSection"));
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

        metadataCard = new an::ui::AnCardWidget(QString(), ImageEXIFViewerControl);
        metadataCard->setObjectName(QStringLiteral("imageExifMetadataCard"));
        auto* metadataLayout = new QVBoxLayout();
        metadataLayout->setContentsMargins(0, 0, 0, 0);
        metadataLayout->setSpacing(0);

        tableWidget = new an::ui::AnStyledTable(metadataCard);
        tableWidget->setObjectName("tableWidget");
        tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
        tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
        tableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
        tableWidget->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
        tableWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        metadataLayout->addWidget(tableWidget);

        metadataCard->setContentLayout(metadataLayout);
        verticalLayout->addWidget(metadataCard, 1);

        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName("horizontalLayout");
        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        horizontalLayout->addItem(horizontalSpacer);

        closeButton = new QPushButton(ImageEXIFViewerControl);
        closeButton->setObjectName("closeButton");
        closeButton->setProperty("buttonVariant", QStringLiteral("primary"));
        closeButton->setMinimumWidth(100);
        closeButton->setDefault(true);

        horizontalLayout->addWidget(closeButton);

        verticalLayout->addLayout(horizontalLayout);

        verticalLayout_2->addLayout(verticalLayout);

        retranslateUi(ImageEXIFViewerControl);

        QMetaObject::connectSlotsByName(ImageEXIFViewerControl);
    } // setupUi

    void retranslateUi(QWidget* ImageEXIFViewerControl)
    {
        ImageEXIFViewerControl->setWindowTitle(
            QCoreApplication::translate("ImageEXIFViewerControl", "EXIF Header Viewer", nullptr));
        headerTitleLabel->setText(QCoreApplication::translate("ImageEXIFViewerControl", "EXIF Metadata", nullptr));
        headerSubtitleLabel->setText(QCoreApplication::translate(
            "ImageEXIFViewerControl", "Inspect camera and capture metadata extracted from the active image.", nullptr));
        metadataCard->setTitle(QCoreApplication::translate("ImageEXIFViewerControl", "Metadata Entries", nullptr));
        closeButton->setText(QCoreApplication::translate("ImageEXIFViewerControl", "Close", nullptr));
    } // retranslateUi
};

inline ImageEXIFViewerControlUiElements buildImageEXIFViewerControlUi(QWidget* parent)
{
    ImageEXIFViewerControlUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::controls
