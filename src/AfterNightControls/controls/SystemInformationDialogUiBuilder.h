// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "theme/AnDesignTokens.h"

namespace an::ui::controls
{

class SystemInformationDialogUiElements
{
public:
    QVBoxLayout* verticalLayout;
    QLabel* titleLabel;
    QScrollArea* scrollArea;
    QWidget* scrollAreaWidgetContents;
    QVBoxLayout* scrollLayout;
    QGroupBox* osGroupBox;
    QGridLayout* osGridLayout;
    QLabel* osNameLabel;
    QLabel* osNameValue;
    QLabel* osVersionLabel;
    QLabel* osVersionValue;
    QLabel* osTypeLabel;
    QLabel* osTypeValue;
    QLabel* osBuildLabel;
    QLabel* osBuildValue;
    QLabel* osUptimeLabel;
    QLabel* osUptimeValue;
    QLabel* osDisplayServerLabel;
    QLabel* osDisplayServerValue;
    QLabel* osDesktopLabel;
    QLabel* osDesktopValue;
    QGroupBox* appGroupBox;
    QGridLayout* appGridLayout;
    QLabel* appVersionLabel;
    QLabel* appVersionValue;
    QLabel* appPathLabel;
    QLabel* appPathValue;
    QLabel* appQtVersionLabel;
    QLabel* appQtVersionValue;
    QLabel* appBuildTypeLabel;
    QLabel* appBuildTypeValue;
    QGroupBox* depsGroupBox;
    QVBoxLayout* depsGroupLayout;
    QTextEdit* depsTextEdit;
    QGroupBox* pcGroupBox;
    QGridLayout* pcGridLayout;
    QLabel* processorLabel;
    QLabel* processorValue;
    QLabel* gpuLabel;
    QLabel* gpuValue;
    QLabel* memoryLabel;
    QLabel* memoryValue;
    QLabel* memoryBarLabel;
    QProgressBar* memoryProgressBar;
    QGroupBox* displayGroupBox;
    QVBoxLayout* displayGroupLayout;
    QTextEdit* displayTextEdit;
    QGroupBox* diskGroupBox;
    QVBoxLayout* diskGroupLayout;
    QVBoxLayout* diskLayout;
    QHBoxLayout* buttonLayout;
    QSpacerItem* horizontalSpacer;
    QPushButton* copyButton;
    QPushButton* exportButton;
    QPushButton* closeButton;

    void setupUi(QWidget* SystemInformationDialog)
    {
        if (SystemInformationDialog->objectName().isEmpty())
            SystemInformationDialog->setObjectName("SystemInformationDialog");
        SystemInformationDialog->resize(800, 700);
        QSizePolicy sizePolicy(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Preferred);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(SystemInformationDialog->sizePolicy().hasHeightForWidth());
        SystemInformationDialog->setSizePolicy(sizePolicy);
        SystemInformationDialog->setMinimumSize(QSize(700, 600));
        const auto& t = an::ui::tokens();
        verticalLayout = new QVBoxLayout(SystemInformationDialog);
        verticalLayout->setObjectName("verticalLayout");
        titleLabel = new QLabel(SystemInformationDialog);
        titleLabel->setObjectName("titleLabel");
        titleLabel->setAlignment(Qt::AlignCenter);
        titleLabel->setFont(t.typography.heading2);
        titleLabel->setTextFormat(Qt::PlainText);

        verticalLayout->addWidget(titleLabel);

        scrollArea = new QScrollArea(SystemInformationDialog);
        scrollArea->setObjectName("scrollArea");
        scrollArea->setWidgetResizable(true);
        scrollArea->setFrameShape(QFrame::NoFrame);
        scrollAreaWidgetContents = new QWidget();
        scrollAreaWidgetContents->setObjectName("scrollAreaWidgetContents");
        scrollAreaWidgetContents->setMinimumSize(QSize(780, 620));
        scrollLayout = new QVBoxLayout(scrollAreaWidgetContents);
        scrollLayout->setObjectName("scrollLayout");
        osGroupBox = new QGroupBox(scrollAreaWidgetContents);
        osGroupBox->setObjectName("osGroupBox");
        osGridLayout = new QGridLayout(osGroupBox);
        osGridLayout->setObjectName("osGridLayout");
        osNameLabel = new QLabel(osGroupBox);
        osNameLabel->setObjectName("osNameLabel");
        osNameLabel->setFont(t.typography.heading3);
        osNameLabel->setMinimumSize(QSize(120, 0));

        osGridLayout->addWidget(osNameLabel, 0, 0, 1, 1);

        osNameValue = new QLabel(osGroupBox);
        osNameValue->setObjectName("osNameValue");
        osNameValue->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::TextSelectableByMouse);

        osGridLayout->addWidget(osNameValue, 0, 1, 1, 1);

        osVersionLabel = new QLabel(osGroupBox);
        osVersionLabel->setObjectName("osVersionLabel");
        osVersionLabel->setFont(t.typography.heading3);

        osGridLayout->addWidget(osVersionLabel, 1, 0, 1, 1);

        osVersionValue = new QLabel(osGroupBox);
        osVersionValue->setObjectName("osVersionValue");
        osVersionValue->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::TextSelectableByMouse);

        osGridLayout->addWidget(osVersionValue, 1, 1, 1, 1);

        osTypeLabel = new QLabel(osGroupBox);
        osTypeLabel->setObjectName("osTypeLabel");
        osTypeLabel->setFont(t.typography.heading3);

        osGridLayout->addWidget(osTypeLabel, 2, 0, 1, 1);

        osTypeValue = new QLabel(osGroupBox);
        osTypeValue->setObjectName("osTypeValue");
        osTypeValue->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::TextSelectableByMouse);

        osGridLayout->addWidget(osTypeValue, 2, 1, 1, 1);

        osBuildLabel = new QLabel(osGroupBox);
        osBuildLabel->setObjectName("osBuildLabel");
        osBuildLabel->setFont(t.typography.heading3);

        osGridLayout->addWidget(osBuildLabel, 3, 0, 1, 1);

        osBuildValue = new QLabel(osGroupBox);
        osBuildValue->setObjectName("osBuildValue");
        osBuildValue->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::TextSelectableByMouse);

        osGridLayout->addWidget(osBuildValue, 3, 1, 1, 1);

        osUptimeLabel = new QLabel(osGroupBox);
        osUptimeLabel->setObjectName("osUptimeLabel");
        osUptimeLabel->setFont(t.typography.heading3);

        osGridLayout->addWidget(osUptimeLabel, 4, 0, 1, 1);

        osUptimeValue = new QLabel(osGroupBox);
        osUptimeValue->setObjectName("osUptimeValue");
        osUptimeValue->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::TextSelectableByMouse);

        osGridLayout->addWidget(osUptimeValue, 4, 1, 1, 1);

        osDisplayServerLabel = new QLabel(osGroupBox);
        osDisplayServerLabel->setObjectName("osDisplayServerLabel");
        osDisplayServerLabel->setFont(t.typography.heading3);

        osGridLayout->addWidget(osDisplayServerLabel, 5, 0, 1, 1);

        osDisplayServerValue = new QLabel(osGroupBox);
        osDisplayServerValue->setObjectName("osDisplayServerValue");
        osDisplayServerValue->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::TextSelectableByMouse);

        osGridLayout->addWidget(osDisplayServerValue, 5, 1, 1, 1);

        osDesktopLabel = new QLabel(osGroupBox);
        osDesktopLabel->setObjectName("osDesktopLabel");
        osDesktopLabel->setFont(t.typography.heading3);

        osGridLayout->addWidget(osDesktopLabel, 6, 0, 1, 1);

        osDesktopValue = new QLabel(osGroupBox);
        osDesktopValue->setObjectName("osDesktopValue");
        osDesktopValue->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::TextSelectableByMouse);

        osGridLayout->addWidget(osDesktopValue, 6, 1, 1, 1);

        scrollLayout->addWidget(osGroupBox);

        appGroupBox = new QGroupBox(scrollAreaWidgetContents);
        appGroupBox->setObjectName("appGroupBox");
        appGridLayout = new QGridLayout(appGroupBox);
        appGridLayout->setObjectName("appGridLayout");
        appVersionLabel = new QLabel(appGroupBox);
        appVersionLabel->setObjectName("appVersionLabel");
        appVersionLabel->setFont(t.typography.heading3);
        appVersionLabel->setMinimumSize(QSize(120, 0));

        appGridLayout->addWidget(appVersionLabel, 0, 0, 1, 1);

        appVersionValue = new QLabel(appGroupBox);
        appVersionValue->setObjectName("appVersionValue");
        appVersionValue->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::TextSelectableByMouse);

        appGridLayout->addWidget(appVersionValue, 0, 1, 1, 1);

        appPathLabel = new QLabel(appGroupBox);
        appPathLabel->setObjectName("appPathLabel");
        appPathLabel->setFont(t.typography.heading3);
        appPathLabel->setMinimumSize(QSize(120, 0));

        appGridLayout->addWidget(appPathLabel, 1, 0, 1, 1);

        appPathValue = new QLabel(appGroupBox);
        appPathValue->setObjectName("appPathValue");
        appPathValue->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::TextSelectableByMouse);
        appPathValue->setWordWrap(true);

        appGridLayout->addWidget(appPathValue, 1, 1, 1, 1);

        appQtVersionLabel = new QLabel(appGroupBox);
        appQtVersionLabel->setObjectName("appQtVersionLabel");
        appQtVersionLabel->setFont(t.typography.heading3);
        appQtVersionLabel->setMinimumSize(QSize(120, 0));

        appGridLayout->addWidget(appQtVersionLabel, 2, 0, 1, 1);

        appQtVersionValue = new QLabel(appGroupBox);
        appQtVersionValue->setObjectName("appQtVersionValue");
        appQtVersionValue->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::TextSelectableByMouse);

        appGridLayout->addWidget(appQtVersionValue, 2, 1, 1, 1);

        appBuildTypeLabel = new QLabel(appGroupBox);
        appBuildTypeLabel->setObjectName("appBuildTypeLabel");
        appBuildTypeLabel->setFont(t.typography.heading3);
        appBuildTypeLabel->setMinimumSize(QSize(120, 0));

        appGridLayout->addWidget(appBuildTypeLabel, 3, 0, 1, 1);

        appBuildTypeValue = new QLabel(appGroupBox);
        appBuildTypeValue->setObjectName("appBuildTypeValue");
        appBuildTypeValue->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::TextSelectableByMouse);

        appGridLayout->addWidget(appBuildTypeValue, 3, 1, 1, 1);

        scrollLayout->addWidget(appGroupBox);

        depsGroupBox = new QGroupBox(scrollAreaWidgetContents);
        depsGroupBox->setObjectName("depsGroupBox");
        depsGroupLayout = new QVBoxLayout(depsGroupBox);
        depsGroupLayout->setObjectName("depsGroupLayout");
        depsTextEdit = new QTextEdit(depsGroupBox);
        depsTextEdit->setObjectName("depsTextEdit");
        depsTextEdit->setReadOnly(true);
        depsTextEdit->setMaximumHeight(150);
        depsTextEdit->setFont(t.typography.monospace);
        depsTextEdit->setLineWrapMode(QTextEdit::NoWrap);

        depsGroupLayout->addWidget(depsTextEdit);

        scrollLayout->addWidget(depsGroupBox);

        pcGroupBox = new QGroupBox(scrollAreaWidgetContents);
        pcGroupBox->setObjectName("pcGroupBox");
        pcGridLayout = new QGridLayout(pcGroupBox);
        pcGridLayout->setObjectName("pcGridLayout");
        processorLabel = new QLabel(pcGroupBox);
        processorLabel->setObjectName("processorLabel");
        processorLabel->setFont(t.typography.heading3);
        processorLabel->setMinimumSize(QSize(120, 0));

        pcGridLayout->addWidget(processorLabel, 0, 0, 1, 1);

        processorValue = new QLabel(pcGroupBox);
        processorValue->setObjectName("processorValue");
        processorValue->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::TextSelectableByMouse);
        processorValue->setWordWrap(true);

        pcGridLayout->addWidget(processorValue, 0, 1, 1, 2);

        gpuLabel = new QLabel(pcGroupBox);
        gpuLabel->setObjectName("gpuLabel");
        gpuLabel->setFont(t.typography.heading3);

        pcGridLayout->addWidget(gpuLabel, 1, 0, 1, 1);

        gpuValue = new QLabel(pcGroupBox);
        gpuValue->setObjectName("gpuValue");
        gpuValue->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::TextSelectableByMouse);
        gpuValue->setWordWrap(true);

        pcGridLayout->addWidget(gpuValue, 1, 1, 1, 2);

        memoryLabel = new QLabel(pcGroupBox);
        memoryLabel->setObjectName("memoryLabel");
        memoryLabel->setFont(t.typography.heading3);

        pcGridLayout->addWidget(memoryLabel, 2, 0, 1, 1);

        memoryValue = new QLabel(pcGroupBox);
        memoryValue->setObjectName("memoryValue");
        memoryValue->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::TextSelectableByMouse);

        pcGridLayout->addWidget(memoryValue, 2, 1, 1, 1);

        memoryBarLabel = new QLabel(pcGroupBox);
        memoryBarLabel->setObjectName("memoryBarLabel");

        pcGridLayout->addWidget(memoryBarLabel, 3, 0, 1, 1);

        memoryProgressBar = new QProgressBar(pcGroupBox);
        memoryProgressBar->setObjectName("memoryProgressBar");
        memoryProgressBar->setValue(0);
        memoryProgressBar->setTextVisible(true);

        pcGridLayout->addWidget(memoryProgressBar, 3, 1, 1, 2);

        scrollLayout->addWidget(pcGroupBox);

        displayGroupBox = new QGroupBox(scrollAreaWidgetContents);
        displayGroupBox->setObjectName("displayGroupBox");
        displayGroupLayout = new QVBoxLayout(displayGroupBox);
        displayGroupLayout->setObjectName("displayGroupLayout");
        displayTextEdit = new QTextEdit(displayGroupBox);
        displayTextEdit->setObjectName("displayTextEdit");
        displayTextEdit->setReadOnly(true);
        displayTextEdit->setMaximumHeight(120);
        displayTextEdit->setFont(t.typography.monospace);
        displayTextEdit->setLineWrapMode(QTextEdit::NoWrap);

        displayGroupLayout->addWidget(displayTextEdit);

        scrollLayout->addWidget(displayGroupBox);

        diskGroupBox = new QGroupBox(scrollAreaWidgetContents);
        diskGroupBox->setObjectName("diskGroupBox");
        diskGroupLayout = new QVBoxLayout(diskGroupBox);
        diskGroupLayout->setObjectName("diskGroupLayout");
        diskLayout = new QVBoxLayout();
        diskLayout->setObjectName("diskLayout");

        diskGroupLayout->addLayout(diskLayout);

        scrollLayout->addWidget(diskGroupBox);

        scrollArea->setWidget(scrollAreaWidgetContents);

        verticalLayout->addWidget(scrollArea);

        buttonLayout = new QHBoxLayout();
        buttonLayout->setObjectName("buttonLayout");
        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        buttonLayout->addItem(horizontalSpacer);

        copyButton = new QPushButton(SystemInformationDialog);
        copyButton->setObjectName("copyButton");

        buttonLayout->addWidget(copyButton);

        exportButton = new QPushButton(SystemInformationDialog);
        exportButton->setObjectName("exportButton");

        buttonLayout->addWidget(exportButton);

        closeButton = new QPushButton(SystemInformationDialog);
        closeButton->setObjectName("closeButton");

        buttonLayout->addWidget(closeButton);

        verticalLayout->addLayout(buttonLayout);

        retranslateUi(SystemInformationDialog);

        closeButton->setDefault(true);

        QMetaObject::connectSlotsByName(SystemInformationDialog);
    } // setupUi

    void retranslateUi(QWidget* SystemInformationDialog)
    {
        SystemInformationDialog->setWindowTitle(
            QCoreApplication::translate("SystemInformationDialog", "System Information", nullptr));
        titleLabel->setText(QCoreApplication::translate("SystemInformationDialog", "System Information", nullptr));
        osGroupBox->setTitle(QCoreApplication::translate("SystemInformationDialog", "Operating System", nullptr));
        osNameLabel->setText(QCoreApplication::translate("SystemInformationDialog", "OS Name:", nullptr));
        osNameValue->setText(QCoreApplication::translate("SystemInformationDialog", "-", nullptr));
        osVersionLabel->setText(QCoreApplication::translate("SystemInformationDialog", "Version:", nullptr));
        osVersionValue->setText(QCoreApplication::translate("SystemInformationDialog", "-", nullptr));
        osTypeLabel->setText(QCoreApplication::translate("SystemInformationDialog", "Type:", nullptr));
        osTypeValue->setText(QCoreApplication::translate("SystemInformationDialog", "-", nullptr));
        osBuildLabel->setText(QCoreApplication::translate("SystemInformationDialog", "Build:", nullptr));
        osBuildValue->setText(QCoreApplication::translate("SystemInformationDialog", "-", nullptr));
        osUptimeLabel->setText(QCoreApplication::translate("SystemInformationDialog", "Uptime:", nullptr));
        osUptimeValue->setText(QCoreApplication::translate("SystemInformationDialog", "-", nullptr));
        osDisplayServerLabel->setText(
            QCoreApplication::translate("SystemInformationDialog", "Display Server:", nullptr));
        osDisplayServerValue->setText(QCoreApplication::translate("SystemInformationDialog", "-", nullptr));
        osDesktopLabel->setText(QCoreApplication::translate("SystemInformationDialog", "Desktop:", nullptr));
        osDesktopValue->setText(QCoreApplication::translate("SystemInformationDialog", "-", nullptr));
        appGroupBox->setTitle(QCoreApplication::translate("SystemInformationDialog", "Application", nullptr));
        appVersionLabel->setText(QCoreApplication::translate("SystemInformationDialog", "Version:", nullptr));
        appVersionValue->setText(QCoreApplication::translate("SystemInformationDialog", "-", nullptr));
        appPathLabel->setText(QCoreApplication::translate("SystemInformationDialog", "Install Path:", nullptr));
        appPathValue->setText(QCoreApplication::translate("SystemInformationDialog", "-", nullptr));
        appQtVersionLabel->setText(QCoreApplication::translate("SystemInformationDialog", "Qt Version:", nullptr));
        appQtVersionValue->setText(QCoreApplication::translate("SystemInformationDialog", "-", nullptr));
        appBuildTypeLabel->setText(QCoreApplication::translate("SystemInformationDialog", "Build Type:", nullptr));
        appBuildTypeValue->setText(QCoreApplication::translate("SystemInformationDialog", "-", nullptr));
        depsGroupBox->setTitle(QCoreApplication::translate("SystemInformationDialog", "Dependencies", nullptr));
        pcGroupBox->setTitle(QCoreApplication::translate("SystemInformationDialog", "Hardware", nullptr));
        processorLabel->setText(QCoreApplication::translate("SystemInformationDialog", "Processor:", nullptr));
        processorValue->setText(QCoreApplication::translate("SystemInformationDialog", "-", nullptr));
        gpuLabel->setText(QCoreApplication::translate("SystemInformationDialog", "Graphics:", nullptr));
        gpuValue->setText(QCoreApplication::translate("SystemInformationDialog", "-", nullptr));
        memoryLabel->setText(QCoreApplication::translate("SystemInformationDialog", "Memory:", nullptr));
        memoryValue->setText(QCoreApplication::translate("SystemInformationDialog", "-", nullptr));
        memoryBarLabel->setText(QString());
        memoryProgressBar->setFormat(QCoreApplication::translate("SystemInformationDialog", "%p%", nullptr));
        displayGroupBox->setTitle(QCoreApplication::translate("SystemInformationDialog", "Display", nullptr));
        diskGroupBox->setTitle(QCoreApplication::translate("SystemInformationDialog", "Storage Devices", nullptr));
        copyButton->setText(QCoreApplication::translate("SystemInformationDialog", "Copy to Clipboard", nullptr));
#if QT_CONFIG(tooltip)
        copyButton->setToolTip(
            QCoreApplication::translate("SystemInformationDialog", "Copy system information to clipboard", nullptr));
#endif // QT_CONFIG(tooltip)
        exportButton->setText(QCoreApplication::translate("SystemInformationDialog", "Export to File...", nullptr));
#if QT_CONFIG(tooltip)
        exportButton->setToolTip(QCoreApplication::translate(
            "SystemInformationDialog", "Export system information to a text file for support tickets", nullptr));
#endif // QT_CONFIG(tooltip)
        closeButton->setText(QCoreApplication::translate("SystemInformationDialog", "Close", nullptr));
    } // retranslateUi
};

inline SystemInformationDialogUiElements buildSystemInformationDialogUi(QWidget* parent)
{
    SystemInformationDialogUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::controls
