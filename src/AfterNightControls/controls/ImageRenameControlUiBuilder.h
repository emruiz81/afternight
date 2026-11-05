// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QtCore/QVariant>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "theme/AnDesignTokens.h"

namespace an::ui::controls
{

class ImageRenameControlUiElements
{
public:
    QDialogButtonBox* buttonBox;
    QLineEdit* newImageName;
    QVBoxLayout* mainLayout;

    void setupUi(QWidget* ImageRenameControl)
    {
        if (ImageRenameControl->objectName().isEmpty())
            ImageRenameControl->setObjectName("ImageRenameControl");
        ImageRenameControl->resize(364, 110);
        ImageRenameControl->setMinimumSize(QSize(364, 110));
        ImageRenameControl->setSizePolicy(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Preferred);
        const auto& t = an::ui::tokens();
        mainLayout = new QVBoxLayout(ImageRenameControl);
        mainLayout->setObjectName("mainLayout");
        mainLayout->setContentsMargins(t.spacing.lg, t.spacing.lg, t.spacing.lg, t.spacing.lg);
        mainLayout->setSpacing(t.spacing.md);
        buttonBox = new QDialogButtonBox(ImageRenameControl);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setOrientation(Qt::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
        newImageName = new QLineEdit(ImageRenameControl);
        newImageName->setObjectName("newImageName");
        mainLayout->addWidget(newImageName);
        mainLayout->addWidget(buttonBox);

        retranslateUi(ImageRenameControl);

        QMetaObject::connectSlotsByName(ImageRenameControl);
    } // setupUi

    void retranslateUi(QWidget* ImageRenameControl)
    {
        ImageRenameControl->setWindowTitle(QCoreApplication::translate("ImageRenameControl", "Dialog", nullptr));
    } // retranslateUi
};

inline ImageRenameControlUiElements buildImageRenameControlUi(QWidget* parent)
{
    ImageRenameControlUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::controls
