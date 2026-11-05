// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QtCore/QVariant>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "processes/RTPreviewUiCommon.h"

#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

class StarmaskRecombinationProcessUiElements
{
public:
    QWidget* horizontalLayoutWidget_4;
    QHBoxLayout* horizontalLayoutMain;
    QVBoxLayout* verticalLayout;
    QGridLayout* gridLayout;
    QLabel* label;
    QComboBox* starlessImage;
    QLabel* label_2;
    QComboBox* starmaskImage;
    QLabel* label_4;
    QComboBox* maskImage;
    QCheckBox* invertMask;
    QSpacerItem* verticalSpacer;
    QFormLayout* formLayout;
    QHBoxLayout* horizontalLayout_4;
    QCheckBox* preview;
    QSpacerItem* horizontalSpacer;
    QHBoxLayout* horizontalLayout_3;
    QDialogButtonBox* buttonBox;
    QVBoxLayout* verticalLayout_2;
    QWidget* widget;

    void setupUi(QWidget* StarmaskRecombinationProcess)
    {
        const auto& t = an::ui::tokens();

        if (StarmaskRecombinationProcess->objectName().isEmpty())
            StarmaskRecombinationProcess->setObjectName("StarmaskRecombinationProcess");
        StarmaskRecombinationProcess->resize(687, 321);
        StarmaskRecombinationProcess->setMinimumSize(QSize(300, 200));
        horizontalLayoutWidget_4 = StarmaskRecombinationProcess;
        horizontalLayoutMain = new QHBoxLayout(horizontalLayoutWidget_4);
        horizontalLayoutMain->setSpacing(t.spacing.md);
        horizontalLayoutMain->setObjectName("horizontalLayoutMain");
        horizontalLayoutMain->setSizeConstraint(QLayout::SetMinimumSize);
        horizontalLayoutMain->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        verticalLayout = new QVBoxLayout();
        verticalLayout->setObjectName("verticalLayout");
        verticalLayout->setSizeConstraint(QLayout::SetDefaultConstraint);
        verticalLayout->setContentsMargins(-1, t.spacing.md, -1, -1);
        gridLayout = new QGridLayout();
        gridLayout->setObjectName("gridLayout");
        label = new QLabel(horizontalLayoutWidget_4);
        label->setObjectName("label");

        gridLayout->addWidget(label, 0, 0, 1, 1);

        starlessImage = new QComboBox(horizontalLayoutWidget_4);
        starlessImage->setObjectName("starlessImage");
        starlessImage->setMinimumSize(QSize(200, 0));
        starlessImage->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed));

        gridLayout->addWidget(starlessImage, 0, 1, 1, 2);

        label_2 = new QLabel(horizontalLayoutWidget_4);
        label_2->setObjectName("label_2");

        gridLayout->addWidget(label_2, 1, 0, 1, 1);

        starmaskImage = new QComboBox(horizontalLayoutWidget_4);
        starmaskImage->setObjectName("starmaskImage");
        starmaskImage->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed));

        gridLayout->addWidget(starmaskImage, 1, 1, 1, 2);

        label_4 = new QLabel(horizontalLayoutWidget_4);
        label_4->setObjectName("label_4");

        gridLayout->addWidget(label_4, 2, 0, 1, 1);

        maskImage = new QComboBox(horizontalLayoutWidget_4);
        maskImage->setObjectName("maskImage");
        maskImage->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed));

        gridLayout->addWidget(maskImage, 2, 1, 1, 1);

        invertMask = new QCheckBox(horizontalLayoutWidget_4);
        invertMask->setObjectName("invertMask");

        gridLayout->addWidget(invertMask, 2, 2, 1, 1);

        gridLayout->setColumnStretch(1, 5);
        gridLayout->setColumnStretch(2, 2);

        verticalLayout->addLayout(gridLayout);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

        verticalLayout->addItem(verticalSpacer);

        formLayout = new QFormLayout();
        formLayout->setObjectName("formLayout");

        verticalLayout->addLayout(formLayout);

        horizontalLayout_4 = new QHBoxLayout();
        horizontalLayout_4->setObjectName("horizontalLayout_4");
        preview = new QCheckBox(horizontalLayoutWidget_4);
        preview->setObjectName("preview");
        preview->setChecked(true);

        horizontalLayout_4->addWidget(preview);

        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        horizontalLayout_4->addItem(horizontalSpacer);

        verticalLayout->addLayout(horizontalLayout_4);

        horizontalLayout_3 = new QHBoxLayout();
        horizontalLayout_3->setObjectName("horizontalLayout_3");
        horizontalLayout_3->setSizeConstraint(QLayout::SetDefaultConstraint);
        buttonBox = new QDialogButtonBox(horizontalLayoutWidget_4);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setStandardButtons(QDialogButtonBox::Close | QDialogButtonBox::Ok);

        horizontalLayout_3->addWidget(buttonBox);

        verticalLayout->addLayout(horizontalLayout_3);

        const auto split = rt_preview_ui::buildMainProcessSplitter(horizontalLayoutMain, horizontalLayoutWidget_4);
        split.controlsLayout->addLayout(verticalLayout);

        verticalLayout_2 = new QVBoxLayout();
        verticalLayout_2->setObjectName("verticalLayout_2");
        widget = new QWidget(split.previewPanel);
        widget->setObjectName("widget");

        verticalLayout_2->addWidget(widget);

        split.previewLayout->addLayout(verticalLayout_2);

        retranslateUi(StarmaskRecombinationProcess);

        QMetaObject::connectSlotsByName(StarmaskRecombinationProcess);
    } // setupUi

    void retranslateUi(QWidget* StarmaskRecombinationProcess)
    {
        StarmaskRecombinationProcess->setWindowTitle(
            QCoreApplication::translate("StarmaskRecombinationProcess", "Starmask Recombination", nullptr));
        label->setText(QCoreApplication::translate("StarmaskRecombinationProcess", "Starless Image", nullptr));
        label_2->setText(QCoreApplication::translate("StarmaskRecombinationProcess", "Stars Image", nullptr));
        label_4->setText(QCoreApplication::translate("StarmaskRecombinationProcess", "Mask (optional)", nullptr));
        invertMask->setText(QCoreApplication::translate("StarmaskRecombinationProcess", "Invert", nullptr));
        preview->setText(QCoreApplication::translate("StarmaskRecombinationProcess", "Preview", nullptr));

        // Tooltips
#if QT_CONFIG(tooltip)
        starlessImage->setToolTip(
            QCoreApplication::translate("StarmaskRecombinationProcess",
                                        "Select the starless image to use as the base for recombination.", nullptr));
        starmaskImage->setToolTip(QCoreApplication::translate(
            "StarmaskRecombinationProcess", "Select the star mask image to add back to the starless image.", nullptr));
        maskImage->setToolTip(QCoreApplication::translate(
            "StarmaskRecombinationProcess",
            "Optional mask image to blend the recombined result. Leave empty to skip masking.", nullptr));
        invertMask->setToolTip(QCoreApplication::translate(
            "StarmaskRecombinationProcess", "Invert the mask before applying it to the recombined image.", nullptr));
        preview->setToolTip(QCoreApplication::translate(
            "StarmaskRecombinationProcess", "Enable real-time preview of the recombination result.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Close))
            btn->setToolTip(QCoreApplication::translate("StarmaskRecombinationProcess",
                                                        "Close this dialog without applying any changes.", nullptr));
#endif // QT_CONFIG(tooltip)
    } // retranslateUi
};

inline StarmaskRecombinationProcessUiElements buildStarmaskRecombinationProcessUi(QWidget* parent)
{
    StarmaskRecombinationProcessUiElements ui;
    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
