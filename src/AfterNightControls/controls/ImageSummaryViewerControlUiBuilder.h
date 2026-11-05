// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "components/AnCardWidget.h"
#include "theme/AnDesignTokens.h"

namespace an::ui::controls
{

class ImageSummaryViewerControlUiElements
{
public:
    QWidget* verticalLayoutWidget = nullptr;
    QVBoxLayout* verticalLayout = nullptr;
    QHBoxLayout* horizontalLayout = nullptr;
    QSpacerItem* horizontalSpacer = nullptr;
    QPushButton* copyButton = nullptr;
    QPushButton* closeButton = nullptr;
    QScrollArea* summaryScrollArea = nullptr;
    QWidget* summaryContentWidget = nullptr;
    QVBoxLayout* summaryContentLayout = nullptr;
    QWidget* headerSection = nullptr;
    QLabel* headerTitleLabel = nullptr;
    QLabel* headerSubtitleLabel = nullptr;
    an::ui::AnCardWidget* summaryCard = nullptr;

    void setupUi(QWidget* ImageSummaryViewerControl)
    {
        if (ImageSummaryViewerControl->objectName().isEmpty())
            ImageSummaryViewerControl->setObjectName("ImageSummaryViewerControl");
        ImageSummaryViewerControl->resize(760, 520);
        QSizePolicy sizePolicy(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Preferred);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(ImageSummaryViewerControl->sizePolicy().hasHeightForWidth());
        ImageSummaryViewerControl->setSizePolicy(sizePolicy);
        ImageSummaryViewerControl->setMinimumSize(QSize(680, 420));
        ImageSummaryViewerControl->setMaximumSize(QSize(5000, 5000));
        const auto& t = an::ui::tokens();
        verticalLayoutWidget = ImageSummaryViewerControl;
        verticalLayout = new QVBoxLayout(ImageSummaryViewerControl);
        verticalLayout->setObjectName("verticalLayout");
        verticalLayout->setContentsMargins(t.spacing.lg, t.spacing.lg, t.spacing.lg, t.spacing.md);
        verticalLayout->setSpacing(t.spacing.md);

        headerSection = new QWidget(ImageSummaryViewerControl);
        headerSection->setObjectName(QStringLiteral("imageSummaryHeaderSection"));
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

        summaryCard = new an::ui::AnCardWidget(QString(), ImageSummaryViewerControl);
        summaryCard->setObjectName(QStringLiteral("imageSummaryBodyCard"));
        auto* summaryLayout = new QVBoxLayout();
        summaryLayout->setContentsMargins(0, 0, 0, 0);
        summaryLayout->setSpacing(t.spacing.sm);

        summaryScrollArea = new QScrollArea(summaryCard);
        summaryScrollArea->setObjectName(QStringLiteral("summaryScrollArea"));
        summaryScrollArea->setWidgetResizable(true);
        summaryScrollArea->setFrameShape(QFrame::NoFrame);

        summaryContentWidget = new QWidget(summaryScrollArea);
        summaryContentWidget->setObjectName(QStringLiteral("summaryContentWidget"));
        summaryContentLayout = new QVBoxLayout(summaryContentWidget);
        summaryContentLayout->setObjectName(QStringLiteral("summaryContentLayout"));
        summaryContentLayout->setContentsMargins(t.spacing.sm, t.spacing.sm, t.spacing.sm, t.spacing.sm);
        summaryContentLayout->setSpacing(t.spacing.md);
        summaryContentLayout->addStretch(1);

        summaryScrollArea->setWidget(summaryContentWidget);
        summaryLayout->addWidget(summaryScrollArea);

        summaryCard->setContentLayout(summaryLayout);
        verticalLayout->addWidget(summaryCard, 1);

        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName("horizontalLayout");
        horizontalLayout->setContentsMargins(0, 0, 0, 0);
        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        horizontalLayout->addItem(horizontalSpacer);

        copyButton = new QPushButton(ImageSummaryViewerControl);
        copyButton->setObjectName("copyButton");
        copyButton->setProperty("buttonVariant", QStringLiteral("secondary"));
        copyButton->setMinimumWidth(100);
        horizontalLayout->addWidget(copyButton);

        closeButton = new QPushButton(ImageSummaryViewerControl);
        closeButton->setObjectName("closeButton");
        closeButton->setProperty("buttonVariant", QStringLiteral("primary"));
        closeButton->setMinimumWidth(100);
        closeButton->setDefault(true);

        horizontalLayout->addWidget(closeButton);
        verticalLayout->addLayout(horizontalLayout);

        retranslateUi(ImageSummaryViewerControl);

        QMetaObject::connectSlotsByName(ImageSummaryViewerControl);
    } // setupUi

    void retranslateUi(QWidget* ImageSummaryViewerControl)
    {
        ImageSummaryViewerControl->setWindowTitle(
            QCoreApplication::translate("ImageSummaryViewerControl", "Image Summary Viewer", nullptr));
        headerTitleLabel->setText(
            QCoreApplication::translate("ImageSummaryViewerControl", "Image and Sequence Summary", nullptr));
        headerSubtitleLabel->setText(QCoreApplication::translate(
            "ImageSummaryViewerControl", "Review acquisition metadata and processing context for the active target.",
            nullptr));
        summaryCard->setTitle(QCoreApplication::translate("ImageSummaryViewerControl", "Summary Report", nullptr));
        copyButton->setText(QCoreApplication::translate("ImageSummaryViewerControl", "Copy to Clipboard", nullptr));
        closeButton->setText(QCoreApplication::translate("ImageSummaryViewerControl", "Close", nullptr));
    } // retranslateUi
};

inline ImageSummaryViewerControlUiElements buildImageSummaryViewerControlUi(QWidget* parent)
{
    ImageSummaryViewerControlUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::controls
