// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QtCore/QVariant>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "theme/AnDesignTokens.h"

namespace an::ui::controls
{

class ImageSelectorControlUiElements
{
public:
    QWidget* verticalLayoutWidget;
    QVBoxLayout* verticalLayout;
    QListWidget* listWidget;
    QHBoxLayout* horizontalLayout_2;
    QHBoxLayout* horizontalLayout;
    QPushButton* selectAllButton;
    QPushButton* clearAllButton;
    QSpacerItem* horizontalSpacer;
    QDialogButtonBox* buttonBox;

    void setupUi(QWidget* ImageSelectorControl)
    {
        if (ImageSelectorControl->objectName().isEmpty())
            ImageSelectorControl->setObjectName("ImageSelectorControl");
        ImageSelectorControl->resize(400, 250);
        ImageSelectorControl->setSizePolicy(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Preferred);
        ImageSelectorControl->setMinimumSize(QSize(400, 200));
        const auto& t = an::ui::tokens();
        auto* mainLayout = new QVBoxLayout(ImageSelectorControl);
        mainLayout->setObjectName("mainLayout");
        mainLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        mainLayout->setSpacing(0);
        verticalLayoutWidget = new QWidget(ImageSelectorControl);
        verticalLayoutWidget->setObjectName("verticalLayoutWidget");
        mainLayout->addWidget(verticalLayoutWidget);
        verticalLayout = new QVBoxLayout(verticalLayoutWidget);
        verticalLayout->setObjectName("verticalLayout");
        verticalLayout->setContentsMargins(t.spacing.md, t.spacing.sm, t.spacing.md, t.spacing.sm);
        listWidget = new QListWidget(verticalLayoutWidget);
        listWidget->setObjectName("listWidget");

        verticalLayout->addWidget(listWidget);

        horizontalLayout_2 = new QHBoxLayout();
        horizontalLayout_2->setObjectName("horizontalLayout_2");

        verticalLayout->addLayout(horizontalLayout_2);

        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName("horizontalLayout");
        horizontalLayout->setContentsMargins(-1, t.spacing.xs, -1, -1);
        selectAllButton = new QPushButton(verticalLayoutWidget);
        selectAllButton->setObjectName("selectAllButton");

        horizontalLayout->addWidget(selectAllButton);

        clearAllButton = new QPushButton(verticalLayoutWidget);
        clearAllButton->setObjectName("clearAllButton");

        horizontalLayout->addWidget(clearAllButton);

        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        horizontalLayout->addItem(horizontalSpacer);

        buttonBox = new QDialogButtonBox(verticalLayoutWidget);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setStandardButtons(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);

        horizontalLayout->addWidget(buttonBox);

        verticalLayout->addLayout(horizontalLayout);

        retranslateUi(ImageSelectorControl);

        QMetaObject::connectSlotsByName(ImageSelectorControl);
    } // setupUi

    void retranslateUi(QWidget* ImageSelectorControl)
    {
        ImageSelectorControl->setWindowTitle(
            QCoreApplication::translate("ImageSelectorControl", "Image Selector", nullptr));
        selectAllButton->setText(QCoreApplication::translate("ImageSelectorControl", "Select All", nullptr));
        clearAllButton->setText(QCoreApplication::translate("ImageSelectorControl", "Clear All", nullptr));
    } // retranslateUi
};

inline ImageSelectorControlUiElements buildImageSelectorControlUi(QWidget* parent)
{
    ImageSelectorControlUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::controls
