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

class ImageFITSHeaderViewerControlUiElements
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
    an::ui::AnCardWidget* headerTableCard = nullptr;

    void setupUi(QWidget* ImageFITSHeaderViewerControl)
    {
        if (ImageFITSHeaderViewerControl->objectName().isEmpty())
            ImageFITSHeaderViewerControl->setObjectName("ImageFITSHeaderViewerControl");
        ImageFITSHeaderViewerControl->resize(760, 520);
        QSizePolicy sizePolicy(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Preferred);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(ImageFITSHeaderViewerControl->sizePolicy().hasHeightForWidth());
        ImageFITSHeaderViewerControl->setSizePolicy(sizePolicy);
        ImageFITSHeaderViewerControl->setMinimumSize(QSize(680, 420));
        ImageFITSHeaderViewerControl->setMaximumSize(QSize(5000, 5000));
        const auto& t = an::ui::tokens();
        verticalLayout_2 = new QVBoxLayout(ImageFITSHeaderViewerControl);
        verticalLayout_2->setObjectName("verticalLayout_2");
        verticalLayout_2->setContentsMargins(t.spacing.lg, t.spacing.lg, t.spacing.lg, t.spacing.md);
        verticalLayout_2->setSpacing(t.spacing.md);
        verticalLayout = new QVBoxLayout();
        verticalLayout->setObjectName("verticalLayout");
        verticalLayout->setContentsMargins(0, 0, 0, 0);
        verticalLayout->setSpacing(t.spacing.md);

        headerSection = new QWidget(ImageFITSHeaderViewerControl);
        headerSection->setObjectName(QStringLiteral("imageFitsHeaderSection"));
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

        headerTableCard = new an::ui::AnCardWidget(QString(), ImageFITSHeaderViewerControl);
        headerTableCard->setObjectName(QStringLiteral("imageFitsTableCard"));
        auto* tableCardLayout = new QVBoxLayout();
        tableCardLayout->setContentsMargins(0, 0, 0, 0);
        tableCardLayout->setSpacing(0);

        tableWidget = new an::ui::AnStyledTable(headerTableCard);
        tableWidget->setObjectName("tableWidget");
        tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
        tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
        tableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
        tableWidget->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
        tableWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        tableCardLayout->addWidget(tableWidget);

        headerTableCard->setContentLayout(tableCardLayout);
        verticalLayout->addWidget(headerTableCard, 1);

        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName("horizontalLayout");
        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        horizontalLayout->addItem(horizontalSpacer);

        closeButton = new QPushButton(ImageFITSHeaderViewerControl);
        closeButton->setObjectName("closeButton");
        closeButton->setProperty("buttonVariant", QStringLiteral("primary"));
        closeButton->setMinimumWidth(100);
        closeButton->setDefault(true);

        horizontalLayout->addWidget(closeButton);

        verticalLayout->addLayout(horizontalLayout);

        verticalLayout_2->addLayout(verticalLayout);

        retranslateUi(ImageFITSHeaderViewerControl);

        QMetaObject::connectSlotsByName(ImageFITSHeaderViewerControl);
    } // setupUi

    void retranslateUi(QWidget* ImageFITSHeaderViewerControl)
    {
        ImageFITSHeaderViewerControl->setWindowTitle(
            QCoreApplication::translate("ImageFITSHeaderViewerControl", "FITS Header Viewer", nullptr));
        headerTitleLabel->setText(QCoreApplication::translate("ImageFITSHeaderViewerControl", "FITS Header", nullptr));
        headerSubtitleLabel->setText(
            QCoreApplication::translate("ImageFITSHeaderViewerControl",
                                        "Review keyword/value/comment records from the active FITS image.", nullptr));
        headerTableCard->setTitle(
            QCoreApplication::translate("ImageFITSHeaderViewerControl", "Header Keywords", nullptr));
        closeButton->setText(QCoreApplication::translate("ImageFITSHeaderViewerControl", "Close", nullptr));
    } // retranslateUi
};

inline ImageFITSHeaderViewerControlUiElements buildImageFITSHeaderViewerControlUi(QWidget* parent)
{
    ImageFITSHeaderViewerControlUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::controls
