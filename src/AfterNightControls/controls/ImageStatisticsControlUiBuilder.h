// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QtCore/QVariant>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QHBoxLayout>
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

namespace detail
{

[[nodiscard]] constexpr bool statisticsShouldExcludeClippedSamples(const bool unclippedChecked)
{
    return !unclippedChecked;
}

} // namespace detail

class ImageStatisticsControlUiElements
{
public:
    QVBoxLayout* mainLayout = nullptr;
    QTableWidget* tableWidget = nullptr;
    QComboBox* comboBox = nullptr;
    QPushButton* refreshButton = nullptr;
    QPushButton* closeButton = nullptr;
    QCheckBox* unclipped = nullptr;
    QHBoxLayout* dialogButtonsLayout = nullptr;
    QSpacerItem* horizontalSpacer = nullptr;
    QWidget* headerSection = nullptr;
    QLabel* headerTitleLabel = nullptr;
    QLabel* headerSubtitleLabel = nullptr;
    an::ui::AnCardWidget* statisticsCard = nullptr;

    void setupUi(QWidget* ImageStatisticsControl)
    {
        if (ImageStatisticsControl->objectName().isEmpty())
            ImageStatisticsControl->setObjectName("ImageStatisticsControl");
        ImageStatisticsControl->resize(760, 520);
        QSizePolicy sizePolicy(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Preferred);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(ImageStatisticsControl->sizePolicy().hasHeightForWidth());
        ImageStatisticsControl->setSizePolicy(sizePolicy);
        ImageStatisticsControl->setMinimumSize(QSize(680, 420));
        ImageStatisticsControl->setMaximumSize(QSize(5000, 5000));
        const auto& t = an::ui::tokens();

        mainLayout = new QVBoxLayout(ImageStatisticsControl);
        mainLayout->setObjectName(QStringLiteral("mainLayout"));
        mainLayout->setContentsMargins(t.spacing.lg, t.spacing.lg, t.spacing.lg, t.spacing.md);
        mainLayout->setSpacing(t.spacing.md);

        headerSection = new QWidget(ImageStatisticsControl);
        headerSection->setObjectName(QStringLiteral("imageStatisticsHeaderSection"));
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

        mainLayout->addWidget(headerSection);

        statisticsCard = new an::ui::AnCardWidget(QString(), ImageStatisticsControl);
        statisticsCard->setObjectName(QStringLiteral("imageStatisticsBodyCard"));
        auto* statisticsLayout = new QVBoxLayout();
        statisticsLayout->setContentsMargins(0, 0, 0, 0);
        statisticsLayout->setSpacing(t.spacing.sm);

        tableWidget = new an::ui::AnStyledTable(statisticsCard);
        tableWidget->setObjectName("tableWidget");
        tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
        tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
        tableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
        tableWidget->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
        tableWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        statisticsLayout->addWidget(tableWidget, 1);

        auto* footerLayout = new QHBoxLayout();
        footerLayout->setContentsMargins(0, t.spacing.xs, 0, 0);
        footerLayout->setSpacing(t.spacing.sm);

        comboBox = new QComboBox(statisticsCard);
        comboBox->setObjectName("comboBox");
        comboBox->setMinimumWidth(190);
        footerLayout->addWidget(comboBox);

        unclipped = new QCheckBox(statisticsCard);
        unclipped->setObjectName("unclipped");
        footerLayout->addWidget(unclipped);
        footerLayout->addStretch(1);

        refreshButton = new QPushButton(statisticsCard);
        refreshButton->setObjectName("refreshButton");
        refreshButton->setMinimumWidth(100);
        footerLayout->addWidget(refreshButton);

        statisticsLayout->addLayout(footerLayout, 0);
        statisticsCard->setContentLayout(statisticsLayout);
        mainLayout->addWidget(statisticsCard, 1);

        dialogButtonsLayout = new QHBoxLayout();
        dialogButtonsLayout->setObjectName(QStringLiteral("dialogButtonsLayout"));
        dialogButtonsLayout->setContentsMargins(0, 0, 0, 0);

        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        dialogButtonsLayout->addItem(horizontalSpacer);

        closeButton = new QPushButton(ImageStatisticsControl);
        closeButton->setObjectName("closeButton");
        closeButton->setMinimumWidth(100);
        closeButton->setProperty("buttonVariant", QStringLiteral("primary"));
        closeButton->setDefault(true);
        dialogButtonsLayout->addWidget(closeButton);
        mainLayout->addLayout(dialogButtonsLayout);

        retranslateUi(ImageStatisticsControl);

        QMetaObject::connectSlotsByName(ImageStatisticsControl);
    } // setupUi

    void retranslateUi(QWidget* ImageStatisticsControl)
    {
        ImageStatisticsControl->setWindowTitle(
            QCoreApplication::translate("ImageStatisticsControl", "Image Statistics", nullptr));
        headerTitleLabel->setText(QCoreApplication::translate("ImageStatisticsControl", "Image Statistics", nullptr));
        headerSubtitleLabel->setText(QCoreApplication::translate(
            "ImageStatisticsControl",
            "Analyze per-channel statistics for the active image across multiple numeric ranges.", nullptr));
        statisticsCard->setTitle(QCoreApplication::translate("ImageStatisticsControl", "Statistics Table", nullptr));
        refreshButton->setText(QCoreApplication::translate("ImageStatisticsControl", "Refresh", nullptr));
        closeButton->setText(QCoreApplication::translate("ImageStatisticsControl", "Close", nullptr));
        unclipped->setText(QCoreApplication::translate("ImageStatisticsControl", "Unclipped", nullptr));
    } // retranslateUi
};

inline ImageStatisticsControlUiElements buildImageStatisticsControlUi(QWidget* parent)
{
    ImageStatisticsControlUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::controls
