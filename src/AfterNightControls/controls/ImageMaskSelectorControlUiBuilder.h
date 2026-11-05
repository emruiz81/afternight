// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QtCore/QVariant>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "components/AnSectionHeader.h"
#include "theme/AnDesignTokens.h"

namespace an::ui::controls
{

class ImageMaskSelectorControlUiElements
{
public:
    QWidget* verticalLayoutWidget = nullptr;
    QVBoxLayout* verticalLayout = nullptr;
    an::ui::AnSectionHeader* sectionHeader = nullptr;
    QGroupBox* bodyGroupBox = nullptr;
    QLabel* bodyDescription = nullptr;
    QFormLayout* formLayout = nullptr;
    QLabel* label = nullptr;
    QLineEdit* currentMask = nullptr;
    QLabel* label_2 = nullptr;
    QComboBox* maskSelector = nullptr;
    QHBoxLayout* horizontalLayout_3 = nullptr;
    QDialogButtonBox* buttonBox = nullptr;

    void setupUi(QWidget* ImageMaskSelectorControl)
    {
        constexpr int kDialogWidth = 560;
        constexpr int kDialogHeight = 300;

        if (ImageMaskSelectorControl->objectName().isEmpty())
            ImageMaskSelectorControl->setObjectName("ImageMaskSelectorControl");
        ImageMaskSelectorControl->setFixedSize(kDialogWidth, kDialogHeight);
        const auto& t = an::ui::tokens();

        verticalLayoutWidget = ImageMaskSelectorControl;
        verticalLayout = new QVBoxLayout(ImageMaskSelectorControl);
        verticalLayout->setObjectName("verticalLayout");
        verticalLayout->setContentsMargins(t.spacing.md, t.spacing.xs, t.spacing.md, t.spacing.sm);
        verticalLayout->setSpacing(t.spacing.xs);

        sectionHeader = new an::ui::AnSectionHeader(QString(), QString(), QString(), ImageMaskSelectorControl);
        sectionHeader->setObjectName(QStringLiteral("imageMaskSelectorSectionHeader"));
        sectionHeader->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        verticalLayout->addWidget(sectionHeader);

        bodyGroupBox = new QGroupBox(ImageMaskSelectorControl);
        bodyGroupBox->setObjectName(QStringLiteral("imageMaskSelectorBodyGroupBox"));
        bodyGroupBox->setProperty("anGroupTitleMode", QStringLiteral("embedded"));
        bodyGroupBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

        auto* bodyLayout = new QVBoxLayout(bodyGroupBox);
        bodyLayout->setContentsMargins(t.spacing.sm, t.spacing.sm, t.spacing.sm, t.spacing.sm);
        bodyLayout->setSpacing(t.spacing.sm);

        bodyDescription = new QLabel(bodyGroupBox);
        bodyDescription->setObjectName(QStringLiteral("imageMaskSelectorBodyDescription"));
        bodyDescription->setWordWrap(true);
        bodyLayout->addWidget(bodyDescription);

        formLayout = new QFormLayout();
        formLayout->setObjectName(QStringLiteral("imageMaskSelectorFormLayout"));
        formLayout->setContentsMargins(0, 0, 0, 0);
        formLayout->setHorizontalSpacing(t.spacing.md);
        formLayout->setVerticalSpacing(t.spacing.sm);

        label = new QLabel(bodyGroupBox);
        label->setObjectName("label");
        formLayout->setWidget(0, QFormLayout::LabelRole, label);

        currentMask = new QLineEdit(bodyGroupBox);
        currentMask->setObjectName("currentMask");
        currentMask->setReadOnly(true);
        formLayout->setWidget(0, QFormLayout::FieldRole, currentMask);

        label_2 = new QLabel(bodyGroupBox);
        label_2->setObjectName("label_2");
        formLayout->setWidget(1, QFormLayout::LabelRole, label_2);

        maskSelector = new QComboBox(bodyGroupBox);
        maskSelector->setObjectName("maskSelector");
        formLayout->setWidget(1, QFormLayout::FieldRole, maskSelector);

        bodyLayout->addLayout(formLayout);
        verticalLayout->addWidget(bodyGroupBox, 0);

        verticalLayout->addStretch(1);

        horizontalLayout_3 = new QHBoxLayout();
        horizontalLayout_3->setObjectName("horizontalLayout_3");
        horizontalLayout_3->setContentsMargins(0, 0, 0, 0);
        horizontalLayout_3->addStretch(1);

        buttonBox = new QDialogButtonBox(ImageMaskSelectorControl);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setOrientation(Qt::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
        if (QPushButton* okButton = buttonBox->button(QDialogButtonBox::Ok))
        {
            okButton->setProperty("buttonVariant", QStringLiteral("primary"));
            okButton->setDefault(true);
        }

        horizontalLayout_3->addWidget(buttonBox);

        verticalLayout->addLayout(horizontalLayout_3);

        retranslateUi(ImageMaskSelectorControl);

        QMetaObject::connectSlotsByName(ImageMaskSelectorControl);
    } // setupUi

    void retranslateUi(QWidget* ImageMaskSelectorControl)
    {
        ImageMaskSelectorControl->setWindowTitle(
            QCoreApplication::translate("ImageMaskSelectorControl", "Select Mask", nullptr));
        sectionHeader->setTitle(QCoreApplication::translate("ImageMaskSelectorControl", "Mask Selector", nullptr));
        sectionHeader->setDescription(QCoreApplication::translate(
            "ImageMaskSelectorControl", "Select a compatible image to use as a mask for the active image.", nullptr));
        bodyGroupBox->setTitle(QString());
        bodyDescription->setText(QCoreApplication::translate("ImageMaskSelectorControl",
                                                             "Compatible masks must match dimensions, avoid circular "
                                                             "references, and respect color format compatibility.",
                                                             nullptr));
        label->setText(QCoreApplication::translate("ImageMaskSelectorControl", "Current Mask", nullptr));
        label_2->setText(QCoreApplication::translate("ImageMaskSelectorControl", "New Mask", nullptr));
    } // retranslateUi
};

inline ImageMaskSelectorControlUiElements buildImageMaskSelectorControlUi(QWidget* parent)
{
    ImageMaskSelectorControlUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::controls
