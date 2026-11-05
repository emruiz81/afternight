// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Programmatic UI builder derived from the former Qt Designer layout
// for ImageIntegrationProcess.

#pragma once

#include <QtCore/QVariant>
#include <QtGui/QIcon>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QTabBar>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "controls/ProcessTabStyling.h"

namespace an::ui::processes
{

class ImageIntegrationProcessUiElements
{
public:
    QVBoxLayout* verticalLayout;
    QMenuBar* mainMenuBar;
    QTabWidget* rootTabWidget;
    QWidget* pageSources;
    QVBoxLayout* verticalLayout_Sources;
    QTabWidget* sourceTabWidget;
    QWidget* biasTab;
    QVBoxLayout* verticalLayout_2;
    QWidget* darkTab;
    QVBoxLayout* verticalLayout_3;
    QWidget* flatTab;
    QVBoxLayout* verticalLayout_4;
    QWidget* lightTab;
    QVBoxLayout* verticalLayout_5;
    QWidget* pageWorkflow;
    QVBoxLayout* verticalLayout_Workflow;
    QTabWidget* workflowTabWidget;
    QWidget* calibrationTab;
    QWidget* registrationTab;
    QWidget* stackingTab;
    QWidget* summaryTab;
    QWidget* bottomActionBar;
    QVBoxLayout* verticalLayout_7;
    QHBoxLayout* horizontalLayout;
    QLabel* label;
    QLineEdit* outputDirectoryEdit;
    QPushButton* outputDirectoryBrowseButton;
    QDialogButtonBox* dialogButtonBox;

    void setupUi(QWidget* ImageIntegrationProcess)
    {
        if (ImageIntegrationProcess->objectName().isEmpty())
            ImageIntegrationProcess->setObjectName("ImageIntegrationProcess");
        ImageIntegrationProcess->resize(1024, 768);
        verticalLayout = new QVBoxLayout(ImageIntegrationProcess);
        verticalLayout->setSpacing(0);
        verticalLayout->setObjectName("verticalLayout");
        verticalLayout->setContentsMargins(0, 0, 0, 0);
        mainMenuBar = new QMenuBar(ImageIntegrationProcess);
        mainMenuBar->setObjectName("mainMenuBar");
        mainMenuBar->setNativeMenuBar(false);

        verticalLayout->addWidget(mainMenuBar);

        rootTabWidget = new QTabWidget(ImageIntegrationProcess);
        rootTabWidget->setObjectName("rootTabWidget");
        applyStandardProcessTabBarSizing(rootTabWidget);
        pageSources = new QWidget();
        pageSources->setObjectName("pageSources");
        verticalLayout_Sources = new QVBoxLayout(pageSources);
        verticalLayout_Sources->setObjectName("verticalLayout_Sources");
        verticalLayout_Sources->setContentsMargins(0, 0, 0, 0);
        sourceTabWidget = new QTabWidget(pageSources);
        sourceTabWidget->setObjectName("sourceTabWidget");
        applyStandardProcessTabBarSizing(sourceTabWidget);
        biasTab = new QWidget();
        biasTab->setObjectName("biasTab");
        verticalLayout_2 = new QVBoxLayout(biasTab);
        verticalLayout_2->setObjectName("verticalLayout_2");
        sourceTabWidget->addTab(biasTab, QString());
        darkTab = new QWidget();
        darkTab->setObjectName("darkTab");
        verticalLayout_3 = new QVBoxLayout(darkTab);
        verticalLayout_3->setObjectName("verticalLayout_3");
        sourceTabWidget->addTab(darkTab, QString());
        flatTab = new QWidget();
        flatTab->setObjectName("flatTab");
        verticalLayout_4 = new QVBoxLayout(flatTab);
        verticalLayout_4->setObjectName("verticalLayout_4");
        sourceTabWidget->addTab(flatTab, QString());
        lightTab = new QWidget();
        lightTab->setObjectName("lightTab");
        verticalLayout_5 = new QVBoxLayout(lightTab);
        verticalLayout_5->setObjectName("verticalLayout_5");
        sourceTabWidget->addTab(lightTab, QString());

        verticalLayout_Sources->addWidget(sourceTabWidget);

        rootTabWidget->addTab(pageSources, QString());
        pageWorkflow = new QWidget();
        pageWorkflow->setObjectName("pageWorkflow");
        verticalLayout_Workflow = new QVBoxLayout(pageWorkflow);
        verticalLayout_Workflow->setObjectName("verticalLayout_Workflow");
        verticalLayout_Workflow->setContentsMargins(0, 0, 0, 0);
        workflowTabWidget = new QTabWidget(pageWorkflow);
        workflowTabWidget->setObjectName("workflowTabWidget");
        applyStandardProcessTabBarSizing(workflowTabWidget);
        calibrationTab = new QWidget();
        calibrationTab->setObjectName("calibrationTab");
        workflowTabWidget->addTab(calibrationTab, QString());
        registrationTab = new QWidget();
        registrationTab->setObjectName("registrationTab");
        workflowTabWidget->addTab(registrationTab, QString());
        stackingTab = new QWidget();
        stackingTab->setObjectName("stackingTab");
        workflowTabWidget->addTab(stackingTab, QString());
        summaryTab = new QWidget();
        summaryTab->setObjectName("summaryTab");
        workflowTabWidget->addTab(summaryTab, QString());

        verticalLayout_Workflow->addWidget(workflowTabWidget);

        rootTabWidget->addTab(pageWorkflow, QString());

        verticalLayout->addWidget(rootTabWidget);

        bottomActionBar = new QWidget(ImageIntegrationProcess);
        bottomActionBar->setObjectName("bottomActionBar");
        bottomActionBar->setMinimumSize(QSize(0, 64));
        verticalLayout_7 = new QVBoxLayout(bottomActionBar);
        verticalLayout_7->setObjectName("verticalLayout_7");
        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName("horizontalLayout");
        label = new QLabel(bottomActionBar);
        label->setObjectName("label");

        horizontalLayout->addWidget(label);

        outputDirectoryEdit = new QLineEdit(bottomActionBar);
        outputDirectoryEdit->setObjectName("outputDirectoryEdit");

        horizontalLayout->addWidget(outputDirectoryEdit);

        outputDirectoryBrowseButton = new QPushButton(bottomActionBar);
        outputDirectoryBrowseButton->setObjectName("outputDirectoryBrowseButton");
        outputDirectoryBrowseButton->setProperty("anButtonVariant", QStringLiteral("secondary"));

        horizontalLayout->addWidget(outputDirectoryBrowseButton);

        verticalLayout_7->addLayout(horizontalLayout);

        dialogButtonBox = new QDialogButtonBox(bottomActionBar);
        dialogButtonBox->setObjectName("dialogButtonBox");
        dialogButtonBox->setStandardButtons(QDialogButtonBox::Close | QDialogButtonBox::Reset);

        verticalLayout_7->addWidget(dialogButtonBox);

        verticalLayout->addWidget(bottomActionBar);

        retranslateUi(ImageIntegrationProcess);

        rootTabWidget->setCurrentIndex(0);
        sourceTabWidget->setCurrentIndex(0);
        workflowTabWidget->setCurrentIndex(0);

        QMetaObject::connectSlotsByName(ImageIntegrationProcess);
    } // setupUi

    void retranslateUi(QWidget* ImageIntegrationProcess)
    {
        ImageIntegrationProcess->setWindowTitle(
            QCoreApplication::translate("ImageIntegrationProcess", "Image Integration", nullptr));
        sourceTabWidget->setTabText(sourceTabWidget->indexOf(biasTab),
                                    QCoreApplication::translate("ImageIntegrationProcess", "Bias", nullptr));
        sourceTabWidget->setTabText(sourceTabWidget->indexOf(darkTab),
                                    QCoreApplication::translate("ImageIntegrationProcess", "Darks", nullptr));
        sourceTabWidget->setTabText(sourceTabWidget->indexOf(flatTab),
                                    QCoreApplication::translate("ImageIntegrationProcess", "Flats", nullptr));
        sourceTabWidget->setTabText(sourceTabWidget->indexOf(lightTab),
                                    QCoreApplication::translate("ImageIntegrationProcess", "Lights", nullptr));
        rootTabWidget->setTabText(rootTabWidget->indexOf(pageSources),
                                  QCoreApplication::translate("ImageIntegrationProcess", "Frame Sources", nullptr));
        workflowTabWidget->setTabText(workflowTabWidget->indexOf(calibrationTab),
                                      QCoreApplication::translate("ImageIntegrationProcess", "Calibration", nullptr));
        workflowTabWidget->setTabText(workflowTabWidget->indexOf(registrationTab),
                                      QCoreApplication::translate("ImageIntegrationProcess", "Registration", nullptr));
        workflowTabWidget->setTabText(workflowTabWidget->indexOf(stackingTab),
                                      QCoreApplication::translate("ImageIntegrationProcess", "Stacking", nullptr));
        workflowTabWidget->setTabText(workflowTabWidget->indexOf(summaryTab),
                                      QCoreApplication::translate("ImageIntegrationProcess", "Summary", nullptr));
        rootTabWidget->setTabText(rootTabWidget->indexOf(pageWorkflow),
                                  QCoreApplication::translate("ImageIntegrationProcess", "Workflow", nullptr));
        label->setText(QCoreApplication::translate("ImageIntegrationProcess", "Output Directory:", nullptr));
        outputDirectoryBrowseButton->setText(QCoreApplication::translate("ImageIntegrationProcess", "...", nullptr));
        if (auto* btn = dialogButtonBox->button(QDialogButtonBox::Close))
            btn->setToolTip(QCoreApplication::translate("ImageIntegrationProcess",
                                                        "Close this dialog without applying any changes.", nullptr));
        if (auto* btn = dialogButtonBox->button(QDialogButtonBox::Reset))
            btn->setToolTip(QCoreApplication::translate("ImageIntegrationProcess",
                                                        "Reset all parameters to their default values.", nullptr));
    } // retranslateUi
};

inline ImageIntegrationProcessUiElements buildImageIntegrationProcessUi(QWidget* parent)
{
    ImageIntegrationProcessUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
