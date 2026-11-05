// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QDialog>
#include <QProgressBar>
#include <QLabel>
#include <QVBoxLayout>
#include "AfterNightControlsExport.h"
#include "SystemInformationDialogUiBuilder.h"

class AFTERNIGHTCONTROLS_API SystemInformationDialog : public QDialog
{
public:
    SystemInformationDialog(QWidget* parent = nullptr);
    ~SystemInformationDialog();

private:
    void on_closeButton_clicked();
    void on_exportButton_clicked();
    void on_copyButton_clicked();

private:
    void initialize();
    void gatherSystemInformation();

    QString getOperatingSystemName();
    QString getOperatingSystemVersion();
    QString getOperatingSystemType();
    QString getOperatingSystemBuild();
    QString getProcessorInfo();
    QString getMemoryInfo();
    void populateDiskInfo();
    void addDiskInfoWidget(const QString& name, const QString& path, qint64 total, qint64 available);
    QString formatBytes(qint64 bytes);

    // GPU information
    QString getGPUInfo();

    // Display information
    QString getDisplayInfo();

    // System information
    QString getSystemUptime();
    QString getQtBuildInfo();
    QString getLinuxDisplayServer(); // Wayland or X11
    QString getLinuxDesktopEnvironment(); // KDE, GNOME, etc.

    // Application information
    QString getApplicationPath();
    QString getApplicationVersion();
    QString getDependencyVersions();

    // Export functionality
    QString generateSystemInfoReport();
    void exportToFile();

    // Store disk information for export
    struct DiskInfo
    {
        QString name;
        QString path;
        qint64 total;
        qint64 available;
        qint64 used;
        int usagePercent;
    };
    an::ui::controls::SystemInformationDialogUiElements m_ui;

    QList<DiskInfo> diskInfoList;
};
