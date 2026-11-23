// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The parts of Image Integration's window as its builder makes them: the menu bar, the eight pages of the job's
/// stages - the four frame sources, then calibration, registration, stacking and the summary - and the output folder's
/// label, field and button. The builder lays out the menu bar alone; the process puts the pages and the output folder
/// under the list of its stages (`widgets/StageListNavigator.h`), which takes the rest of the window.

#pragma once

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

namespace an::ui::processes
{

class ImageIntegrationProcessUiElements
{
public:
    QVBoxLayout* verticalLayout;
    QMenuBar* mainMenuBar;
    QWidget* biasTab;
    QVBoxLayout* verticalLayout_2;
    QWidget* darkTab;
    QVBoxLayout* verticalLayout_3;
    QWidget* flatTab;
    QVBoxLayout* verticalLayout_4;
    QWidget* lightTab;
    QVBoxLayout* verticalLayout_5;
    QWidget* calibrationTab;
    QWidget* registrationTab;
    QWidget* stackingTab;
    QWidget* summaryTab;
    QLabel* label;
    QLineEdit* outputDirectoryEdit;
    QPushButton* outputDirectoryBrowseButton;

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

        // The pages are the window's until the process hands them to the list of its stages, which lays them out.
        const auto makePage = [ImageIntegrationProcess](const char* name) {
            auto* page = new QWidget(ImageIntegrationProcess);
            page->setObjectName(name);
            return page;
        };
        biasTab = makePage("biasTab");
        verticalLayout_2 = new QVBoxLayout(biasTab);
        verticalLayout_2->setObjectName("verticalLayout_2");
        darkTab = makePage("darkTab");
        verticalLayout_3 = new QVBoxLayout(darkTab);
        verticalLayout_3->setObjectName("verticalLayout_3");
        flatTab = makePage("flatTab");
        verticalLayout_4 = new QVBoxLayout(flatTab);
        verticalLayout_4->setObjectName("verticalLayout_4");
        lightTab = makePage("lightTab");
        verticalLayout_5 = new QVBoxLayout(lightTab);
        verticalLayout_5->setObjectName("verticalLayout_5");
        calibrationTab = makePage("calibrationTab");
        registrationTab = makePage("registrationTab");
        stackingTab = makePage("stackingTab");
        summaryTab = makePage("summaryTab");

        // The output folder, which the list's foot lays out.
        label = new QLabel(ImageIntegrationProcess);
        label->setObjectName("label");
        outputDirectoryEdit = new QLineEdit(ImageIntegrationProcess);
        outputDirectoryEdit->setObjectName("outputDirectoryEdit");
        outputDirectoryBrowseButton = new QPushButton(ImageIntegrationProcess);
        outputDirectoryBrowseButton->setObjectName("outputDirectoryBrowseButton");
        outputDirectoryBrowseButton->setProperty("anButtonVariant", QStringLiteral("secondary"));

        retranslateUi(ImageIntegrationProcess);

        QMetaObject::connectSlotsByName(ImageIntegrationProcess);
    } // setupUi

    void retranslateUi(QWidget* ImageIntegrationProcess)
    {
        ImageIntegrationProcess->setWindowTitle(
            QCoreApplication::translate("ImageIntegrationProcess", "Image Integration", nullptr));
        label->setText(QCoreApplication::translate("ImageIntegrationProcess", "Output Directory:", nullptr));
        outputDirectoryBrowseButton->setText(QCoreApplication::translate("ImageIntegrationProcess", "...", nullptr));
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
