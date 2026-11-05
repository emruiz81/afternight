// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "SystemInformationDialog.h"
#include "SystemMemoryInfoParser.h"
#include "app/FileDialogUtils.h"
#include "controls/IAppContext.h"
#include "theme/AnDesignTokens.h"
#include "controls/EngineVersionReport.h"
#include <AfterNightPlatform/Logging.h>

#include <QDir>
#include <QSysInfo>
#include <QStorageInfo>
#include <QStandardPaths>
#include <QProgressBar>
#include <QGridLayout>
#include <QFrame>
#include <QScrollArea>
#include <QThread>
#include <QApplication>
#include <QCoreApplication>
#include <QProcess>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QMessageBox>
#include <QDateTime>
#include <QScreen>
#include <QClipboard>
#include <QVector>

#ifdef Q_OS_WIN
#include <windows.h>
#include <sysinfoapi.h>
#include <powerbase.h>
#pragma comment(lib, "PowrProf.lib")
#elif defined(Q_OS_LINUX)
#include <sys/sysinfo.h>
#include <fstream>
#include <unistd.h>
#endif

namespace
{

an::ui::IDialogService* currentDialogService()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getDialogService();
    }

    return nullptr;
}

an::ui::IGlobalSettingsProvider* currentGlobalSettings()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getGlobalSettings();
    }

    return nullptr;
}

an::ui::IPathMemoryService* currentPathMemoryService()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getPathMemoryService();
    }

    return nullptr;
}

#ifdef Q_OS_LINUX
an::ui::system_memory::MemorySnapshot getLinuxMemorySnapshot()
{
    using an::ui::system_memory::MemorySnapshot;

    MemorySnapshot snapshot;

    QProcess process;
    process.start(QStringLiteral("free"), QStringList{QStringLiteral("--bytes")});
    if (process.waitForFinished(1000))
    {
        const std::string freeOutput = process.readAllStandardOutput().toStdString();
        snapshot = an::ui::system_memory::parseLinuxFreeBytesOutput(freeOutput);
    }

    if (snapshot.totalBytes > 0 && snapshot.availableBytes > 0)
    {
        return snapshot;
    }

    QFile memInfoFile(QStringLiteral("/proc/meminfo"));
    if (memInfoFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        const std::string memInfoContent = QString::fromUtf8(memInfoFile.readAll()).toStdString();
        snapshot = an::ui::system_memory::parseLinuxMeminfo(memInfoContent);
    }

    if (snapshot.totalBytes <= 0 || snapshot.availableBytes <= 0)
    {
        struct sysinfo memInfo;
        if (sysinfo(&memInfo) == 0)
        {
            const auto sysSnapshot = an::ui::system_memory::buildWindowsSnapshot(
                static_cast<std::uint64_t>(memInfo.totalram) * memInfo.mem_unit,
                static_cast<std::uint64_t>(memInfo.freeram) * memInfo.mem_unit);
            if (snapshot.totalBytes <= 0)
                snapshot.totalBytes = sysSnapshot.totalBytes;
            if (snapshot.availableBytes <= 0)
                snapshot.availableBytes = sysSnapshot.availableBytes;
        }
    }

    return an::ui::system_memory::clampSnapshot(snapshot);
}
#endif
} // namespace

SystemInformationDialog::SystemInformationDialog(QWidget* parent) : QDialog(parent)
{
    m_ui = an::ui::controls::buildSystemInformationDialogUi(this);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    LOG_DEBUG("SystemInformationDialog started");
    initialize();
}

SystemInformationDialog::~SystemInformationDialog()
{
    LOG_DEBUG("SystemInformationDialog ended");
}

void SystemInformationDialog::initialize()
{
    connect(m_ui.closeButton, &QPushButton::clicked, this, &SystemInformationDialog::on_closeButton_clicked);
    connect(m_ui.exportButton, &QPushButton::clicked, this, &SystemInformationDialog::on_exportButton_clicked);
    connect(m_ui.copyButton, &QPushButton::clicked, this, &SystemInformationDialog::on_copyButton_clicked);

    // Set platform-specific titles with emojis
#ifdef Q_OS_WIN
    // Windows: Emojis render properly with default fonts
    m_ui.titleLabel->setText(QStringLiteral("💻 System Information"));
    m_ui.osGroupBox->setTitle(QStringLiteral("🖥️ Operating System"));
    m_ui.appGroupBox->setTitle(QStringLiteral("📦 Application"));
    m_ui.depsGroupBox->setTitle(QStringLiteral("📚 Dependencies"));
    m_ui.pcGroupBox->setTitle(QStringLiteral("⚙️ Hardware"));
    m_ui.diskGroupBox->setTitle(QStringLiteral("💾 Storage Devices"));
    m_ui.exportButton->setText(QStringLiteral("📄 Export to File..."));
#elif defined(Q_OS_LINUX)
    // Linux: Skip emojis due to rendering issues with QGroupBox titles
    // QGroupBox on Linux has font metric issues with emoji characters that cause cutoff
    m_ui.titleLabel->setText(QStringLiteral("System Information"));
    m_ui.osGroupBox->setTitle(QStringLiteral("Operating System"));
    m_ui.appGroupBox->setTitle(QStringLiteral("Application"));
    m_ui.depsGroupBox->setTitle(QStringLiteral("Dependencies"));
    m_ui.pcGroupBox->setTitle(QStringLiteral("Hardware"));
    m_ui.diskGroupBox->setTitle(QStringLiteral("Storage Devices"));
    m_ui.exportButton->setText(QStringLiteral("Export to File..."));
#endif

    gatherSystemInformation();
}

void SystemInformationDialog::on_closeButton_clicked()
{
    accept();
}

void SystemInformationDialog::on_exportButton_clicked()
{
    exportToFile();
}

void SystemInformationDialog::on_copyButton_clicked()
{
    QClipboard* clipboard = QApplication::clipboard();
    clipboard->setText(generateSystemInfoReport());

    LOG_DEBUG("System information copied to clipboard");

    if (auto* dialogService = currentDialogService())
    {
        dialogService->notifyInfoMessage(this, tr("Copied"), tr("System information has been copied to clipboard."));
        return;
    }

    QMessageBox::information(this, tr("Copied"), tr("System information has been copied to clipboard."));
}

void SystemInformationDialog::gatherSystemInformation()
{
    LOG_DEBUG("Gathering system information...");

    // OS Information
    m_ui.osNameValue->setText(getOperatingSystemName());
    m_ui.osVersionValue->setText(getOperatingSystemVersion());
    m_ui.osTypeValue->setText(getOperatingSystemType());
    m_ui.osBuildValue->setText(getOperatingSystemBuild());
    m_ui.osUptimeValue->setText(getSystemUptime());

    // Linux-specific information
#ifdef Q_OS_LINUX
    m_ui.osDisplayServerValue->setText(getLinuxDisplayServer());
    m_ui.osDesktopValue->setText(getLinuxDesktopEnvironment());
    m_ui.osDisplayServerLabel->setVisible(true);
    m_ui.osDisplayServerValue->setVisible(true);
    m_ui.osDesktopLabel->setVisible(true);
    m_ui.osDesktopValue->setVisible(true);
#else
    m_ui.osDisplayServerLabel->setVisible(false);
    m_ui.osDisplayServerValue->setVisible(false);
    m_ui.osDesktopLabel->setVisible(false);
    m_ui.osDesktopValue->setVisible(false);
#endif

    // PC Hardware Information
    m_ui.processorValue->setText(getProcessorInfo());

    // Memory with progress bar
    QString memInfo = getMemoryInfo();
    m_ui.memoryValue->setText(memInfo);

    // GPU Information
    QString gpuInfo = getGPUInfo();
    if (!gpuInfo.isEmpty())
    {
        m_ui.gpuValue->setText(gpuInfo);
    }
    else
    {
        m_ui.gpuValue->setText("Not available");
    }

    // Display Information
    m_ui.displayTextEdit->setText(getDisplayInfo());

    // Application Information
    m_ui.appVersionValue->setText(getApplicationVersion());
    m_ui.appPathValue->setText(getApplicationPath());
    m_ui.appQtVersionValue->setText(qVersion());

    // Build type detection
#ifdef DEBUG // Note: DEBUG is defined in Debug builds in this project
    m_ui.appBuildTypeValue->setText("Debug");
#else
    m_ui.appBuildTypeValue->setText("Release");
#endif

    // Dependencies
    m_ui.depsTextEdit->setText(getDependencyVersions());

    // Populate all disks
    populateDiskInfo();
}

QString SystemInformationDialog::getOperatingSystemName()
{
    return QSysInfo::prettyProductName();
}

QString SystemInformationDialog::getOperatingSystemVersion()
{
    return QSysInfo::productVersion();
}

QString SystemInformationDialog::getOperatingSystemType()
{
#ifdef Q_OS_WIN
    return QString("Windows (%1-bit)").arg(QSysInfo::WordSize);
#elif defined(Q_OS_LINUX)
    return QString("Linux (%1-bit)").arg(QSysInfo::WordSize);
#elif defined(Q_OS_MAC)
    return QString("macOS (%1-bit)").arg(QSysInfo::WordSize);
#else
    return QString("Unknown (%1-bit)").arg(QSysInfo::WordSize);
#endif
}

QString SystemInformationDialog::getOperatingSystemBuild()
{
    return QSysInfo::buildCpuArchitecture() + " / " + QSysInfo::kernelVersion();
}

QString SystemInformationDialog::getProcessorInfo()
{
    QString cpuInfo = QSysInfo::currentCpuArchitecture();

#ifdef Q_OS_WIN
    // Try to get more detailed CPU information on Windows
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    int numCores = sysInfo.dwNumberOfProcessors;
    cpuInfo += QString(" (%1 cores)").arg(numCores);

    // Try to read CPU name from registry
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &hKey) ==
        ERROR_SUCCESS)
    {
        char cpuName[256];
        DWORD bufferSize = sizeof(cpuName);
        if (RegQueryValueExA(hKey, "ProcessorNameString", nullptr, nullptr, (LPBYTE)cpuName, &bufferSize) ==
            ERROR_SUCCESS)
        {
            cpuInfo = QString::fromLocal8Bit(cpuName).trimmed() + QString(" (%1 cores)").arg(numCores);
        }
        RegCloseKey(hKey);
    }
#elif defined(Q_OS_LINUX)
    // Try to read CPU info from /proc/cpuinfo on Linux
    std::ifstream cpuInfoFile("/proc/cpuinfo");
    if (cpuInfoFile.is_open())
    {
        std::string line;
        std::string modelName;
        int coreCount = 0;

        while (std::getline(cpuInfoFile, line))
        {
            if (line.find("model name") != std::string::npos && modelName.empty())
            {
                size_t pos = line.find(":");
                if (pos != std::string::npos)
                {
                    modelName = line.substr(pos + 1);
                    // Trim leading spaces
                    size_t start = modelName.find_first_not_of(" \t");
                    if (start != std::string::npos)
                    {
                        modelName = modelName.substr(start);
                    }
                }
            }
            if (line.find("processor") != std::string::npos)
            {
                coreCount++;
            }
        }
        cpuInfoFile.close();

        if (!modelName.empty())
        {
            cpuInfo = QString::fromStdString(modelName) + QString(" (%1 cores)").arg(coreCount);
        }
        else if (coreCount > 0)
        {
            cpuInfo += QString(" (%1 cores)").arg(coreCount);
        }
    }
#endif

    return cpuInfo;
}

QString SystemInformationDialog::getMemoryInfo()
{
#ifdef Q_OS_WIN
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);

    const auto snapshot = an::ui::system_memory::buildWindowsSnapshot(memInfo.ullTotalPhys, memInfo.ullAvailPhys);
    const qint64 totalMemory = snapshot.totalBytes;
    const qint64 availMemory = snapshot.availableBytes;
    const qint64 usedMemory = totalMemory - availMemory;
    const int usagePercent = an::ui::system_memory::calculateUsagePercent(snapshot);

    // Update progress bar
    m_ui.memoryProgressBar->setValue(usagePercent);
    m_ui.memoryProgressBar->setFormat(QString("%1%").arg(usagePercent));

    return QString("%1 / %2 (%3% used)").arg(formatBytes(usedMemory)).arg(formatBytes(totalMemory)).arg(usagePercent);
#elif defined(Q_OS_LINUX)
    const auto mem = getLinuxMemorySnapshot();
    const qint64 totalMemory = mem.totalBytes;
    const qint64 availableMemory = mem.availableBytes;
    const qint64 usedMemory = totalMemory - availableMemory;
    const int usagePercent = an::ui::system_memory::calculateUsagePercent(mem);

    // Update progress bar
    m_ui.memoryProgressBar->setValue(usagePercent);
    m_ui.memoryProgressBar->setFormat(QString("%1%").arg(usagePercent));

    return QString("%1 / %2 (%3% used)").arg(formatBytes(usedMemory)).arg(formatBytes(totalMemory)).arg(usagePercent);
#else
    return "Memory information not available";
#endif
}

QString SystemInformationDialog::formatBytes(qint64 bytes)
{
    const qint64 KB = 1024;
    const qint64 MB = KB * 1024;
    const qint64 GB = MB * 1024;
    const qint64 TB = GB * 1024;

    if (bytes >= TB)
        return QString("%1 TB").arg(bytes / static_cast<double>(TB), 0, 'f', 2);
    else if (bytes >= GB)
        return QString("%1 GB").arg(bytes / static_cast<double>(GB), 0, 'f', 2);
    else if (bytes >= MB)
        return QString("%1 MB").arg(bytes / static_cast<double>(MB), 0, 'f', 2);
    else if (bytes >= KB)
        return QString("%1 KB").arg(bytes / static_cast<double>(KB), 0, 'f', 2);
    else
        return QString("%1 bytes").arg(bytes);
}

void SystemInformationDialog::populateDiskInfo()
{
    // Clear any existing disk widgets
    QLayoutItem* child;
    while ((child = m_ui.diskLayout->takeAt(0)) != nullptr)
    {
        if (child->widget())
            child->widget()->deleteLater();
        delete child;
    }

    // Clear the disk info list
    diskInfoList.clear();

    // Get all available volumes
    QList<QStorageInfo> volumes = QStorageInfo::mountedVolumes();

    for (const QStorageInfo& storage : volumes)
    {
        if (!storage.isValid() || storage.isReadOnly())
            continue;

        // Skip special filesystems on Linux
        QString fsType = QString::fromUtf8(storage.fileSystemType());
        if (fsType == "tmpfs" || fsType == "devtmpfs" || fsType == "squashfs" || fsType == "overlay" ||
            fsType == "proc" || fsType == "sysfs")
            continue;

        QString volumeName = storage.name();
        if (volumeName.isEmpty())
            volumeName = storage.displayName();
        if (volumeName.isEmpty())
            volumeName = storage.rootPath();

        qint64 total = storage.bytesTotal();
        qint64 available = storage.bytesAvailable();
        qint64 used = total - available;
        int usagePercent = total > 0 ? static_cast<int>((used * 100) / total) : 0;

        // Store disk info for export
        DiskInfo diskInfo;
        diskInfo.name = volumeName;
        diskInfo.path = storage.rootPath();
        diskInfo.total = total;
        diskInfo.available = available;
        diskInfo.used = used;
        diskInfo.usagePercent = usagePercent;
        diskInfoList.append(diskInfo);

        addDiskInfoWidget(volumeName, storage.rootPath(), total, available);
    }

    // Add a stretch at the end
    m_ui.diskLayout->addStretch();
}

void SystemInformationDialog::addDiskInfoWidget(const QString& name, const QString& path, qint64 total,
                                                qint64 available)
{
    // Create a frame for this disk
    QFrame* diskFrame = new QFrame(this);
    diskFrame->setFrameShape(QFrame::StyledPanel);
    diskFrame->setFrameShadow(QFrame::Raised);

    QVBoxLayout* frameLayout = new QVBoxLayout(diskFrame);
    frameLayout->setContentsMargins(8, 6, 8, 6); // Reduced margins
    frameLayout->setSpacing(3); // Reduced spacing between widgets

    // Title with disk name and path
    QLabel* titleLabel = new QLabel(QString("<b>%1</b>").arg(name), diskFrame);
    titleLabel->setFont(an::ui::tokens().typography.heading3);
    QLabel* pathLabel = new QLabel(path, diskFrame);
    pathLabel->setProperty("anStatusTone", "secondary");
    pathLabel->setFont(an::ui::tokens().typography.caption);

    frameLayout->addWidget(titleLabel);
    frameLayout->addWidget(pathLabel);

    // Calculate usage
    qint64 used = total - available;
    int usagePercent = total > 0 ? static_cast<int>((used * 100) / total) : 0;

    // Progress bar
    QProgressBar* progressBar = new QProgressBar(diskFrame);
    progressBar->setMinimum(0);
    progressBar->setMaximum(100);
    progressBar->setValue(usagePercent);
    progressBar->setFormat(QString("%1%").arg(usagePercent));
    progressBar->setTextVisible(true);
    progressBar->setMaximumHeight(18); // Limit progress bar height

    // Set semantic usage tone; AnProxyStyle resolves the actual chunk color.
    if (usagePercent >= 90)
        progressBar->setProperty("anUsageTone", "high");
    else if (usagePercent >= 75)
        progressBar->setProperty("anUsageTone", "medium");
    else
        progressBar->setProperty("anUsageTone", "low");

    frameLayout->addWidget(progressBar);

    // Info label
    QString infoText =
        QString("%1 used of %2 (%3 free)").arg(formatBytes(used)).arg(formatBytes(total)).arg(formatBytes(available));

    QLabel* infoLabel = new QLabel(infoText, diskFrame);
    infoLabel->setProperty("anStatusTone", "secondary");
    infoLabel->setFont(an::ui::tokens().typography.caption);
    frameLayout->addWidget(infoLabel);

    m_ui.diskLayout->addWidget(diskFrame);
}

QString SystemInformationDialog::getGPUInfo()
{
    QString gpuInfo;

#ifdef Q_OS_WIN
    // Try to get GPU information from Windows registry
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\0000", 0,
                      KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        char driverDesc[256];
        DWORD bufferSize = sizeof(driverDesc);
        if (RegQueryValueExA(hKey, "DriverDesc", nullptr, nullptr, (LPBYTE)driverDesc, &bufferSize) == ERROR_SUCCESS)
        {
            gpuInfo = QString::fromLocal8Bit(driverDesc).trimmed();
        }
        RegCloseKey(hKey);
    }
#elif defined(Q_OS_LINUX)
    // Try to read GPU info from lspci on Linux
    QProcess process;
    process.start("lspci", QStringList() << "-v");
    if (process.waitForFinished(1000))
    {
        QString output = process.readAllStandardOutput();
        QStringList lines = output.split('\n');
        for (const QString& line : lines)
        {
            if (line.contains("VGA compatible controller", Qt::CaseInsensitive))
            {
                int pos = line.indexOf(":");
                if (pos != -1)
                {
                    gpuInfo = line.mid(pos + 1).trimmed();
                    break;
                }
            }
        }
    }
#endif

    return gpuInfo;
}

QString SystemInformationDialog::getApplicationPath()
{
    return QCoreApplication::applicationDirPath();
}

QString SystemInformationDialog::getApplicationVersion()
{
    const QString version = an::ui::productVersion();
    return version.isEmpty() ? QStringLiteral("dev") : version;
}

QString SystemInformationDialog::getDependencyVersions()
{
    QString versions;
    QTextStream stream(&versions);

    // The engine states its own libraries; each is shown with the version its headers declared and, when it differs,
    // the version the loaded copy reports.
    const std::vector<an::ui::EngineLibrary> libraries = an::ui::engineLibraries();
    const QString engineLabel = QStringLiteral("AfterNight engine");
    qsizetype maxNameLength = engineLabel.length();
    for (const an::ui::EngineLibrary& library : libraries)
    {
        maxNameLength = (std::max)(maxNameLength, library.label.length());
    }

    stream << engineLabel.leftJustified(maxNameLength + 2, ' ') << an::ui::engineVersionText() << "\n";
    for (const an::ui::EngineLibrary& library : libraries)
    {
        stream << library.label.leftJustified(maxNameLength + 2, ' ') << an::ui::engineLibraryVersionText(library)
               << "\n";
    }

    return versions;
}

QString SystemInformationDialog::getDisplayInfo()
{
    QString displayInfo;
    QTextStream stream(&displayInfo);

    QList<QScreen*> screens = QApplication::screens();
    stream << screens.size() << " display(s) detected\n";

    for (int i = 0; i < screens.size(); ++i)
    {
        QScreen* screen = screens[i];
        QRect geometry = screen->geometry();
        qreal dpi = screen->logicalDotsPerInch();
        qreal devicePixelRatio = screen->devicePixelRatio();

        stream << "\nDisplay " << (i + 1);
        if (!screen->name().isEmpty())
            stream << " (" << screen->name() << ")";
        stream << ":\n";
        stream << "  Resolution: " << geometry.width() << "x" << geometry.height() << "\n";
        stream << "  DPI: " << QString::number(dpi, 'f', 1) << "\n";
        stream << "  Scale Factor: " << QString::number(devicePixelRatio, 'f', 2) << "\n";
        stream << "  Refresh Rate: " << screen->refreshRate() << " Hz\n";

        if (screen == QApplication::primaryScreen())
            stream << "  (Primary Display)\n";
    }

    return displayInfo;
}

QString SystemInformationDialog::getLinuxDisplayServer()
{
#ifdef Q_OS_LINUX
    // Check for Wayland
    QString waylandDisplay = qEnvironmentVariable("WAYLAND_DISPLAY");
    if (!waylandDisplay.isEmpty())
    {
        return QString("Wayland (WAYLAND_DISPLAY=%1)").arg(waylandDisplay);
    }

    // Check for X11
    QString x11Display = qEnvironmentVariable("DISPLAY");
    if (!x11Display.isEmpty())
    {
        return QString("X11 (DISPLAY=%1)").arg(x11Display);
    }

    // Check XDG_SESSION_TYPE as fallback
    QString sessionType = qEnvironmentVariable("XDG_SESSION_TYPE");
    if (!sessionType.isEmpty())
    {
        return QString("%1 (XDG_SESSION_TYPE)").arg(sessionType);
    }

    return "Unknown";
#else
    return "Not applicable (not Linux)";
#endif
}

QString SystemInformationDialog::getLinuxDesktopEnvironment()
{
#ifdef Q_OS_LINUX
    // Try XDG_CURRENT_DESKTOP first (most reliable)
    QString currentDesktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP");
    if (!currentDesktop.isEmpty())
    {
        return currentDesktop;
    }

    // Try DESKTOP_SESSION
    QString desktopSession = qEnvironmentVariable("DESKTOP_SESSION");
    if (!desktopSession.isEmpty())
    {
        return desktopSession;
    }

    // Try to detect by checking for specific environment variables
    if (!qEnvironmentVariable("KDE_FULL_SESSION").isEmpty())
        return "KDE";
    if (!qEnvironmentVariable("GNOME_DESKTOP_SESSION_ID").isEmpty())
        return "GNOME";
    if (!qEnvironmentVariable("MATE_DESKTOP_SESSION_ID").isEmpty())
        return "MATE";
    if (!qEnvironmentVariable("TDE_FULL_SESSION").isEmpty())
        return "Trinity";

    // Check for specific processes
    QProcess process;
    process.start("pgrep", QStringList() << "-x" << "gnome-shell");
    if (process.waitForFinished(500) && process.exitCode() == 0)
        return "GNOME";

    process.start("pgrep", QStringList() << "-x" << "plasmashell");
    if (process.waitForFinished(500) && process.exitCode() == 0)
        return "KDE Plasma";

    process.start("pgrep", QStringList() << "-x" << "xfce4-session");
    if (process.waitForFinished(500) && process.exitCode() == 0)
        return "Xfce";

    process.start("pgrep", QStringList() << "-x" << "lxsession");
    if (process.waitForFinished(500) && process.exitCode() == 0)
        return "LXDE";

    process.start("pgrep", QStringList() << "-x" << "lxqt-session");
    if (process.waitForFinished(500) && process.exitCode() == 0)
        return "LXQt";

    return "Unknown";
#else
    return "Not applicable (not Linux)";
#endif
}

QString SystemInformationDialog::getSystemUptime()
{
#ifdef Q_OS_LINUX
    std::ifstream uptimeFile("/proc/uptime");
    if (uptimeFile.is_open())
    {
        double uptimeSeconds;
        uptimeFile >> uptimeSeconds;
        uptimeFile.close();

        int days = static_cast<int>(uptimeSeconds / 86400);
        int hours = static_cast<int>((uptimeSeconds - days * 86400) / 3600);
        int minutes = static_cast<int>((uptimeSeconds - days * 86400 - hours * 3600) / 60);

        if (days > 0)
            return QString("%1 days, %2 hours, %3 minutes").arg(days).arg(hours).arg(minutes);
        else if (hours > 0)
            return QString("%1 hours, %2 minutes").arg(hours).arg(minutes);
        else
            return QString("%1 minutes").arg(minutes);
    }
#elif defined(Q_OS_WIN)
    // GetTickCount64 returns milliseconds since boot
    ULONGLONG uptimeMs = GetTickCount64();
    int days = static_cast<int>(uptimeMs / (1000 * 60 * 60 * 24));
    int hours = static_cast<int>((uptimeMs / (1000 * 60 * 60)) % 24);
    int minutes = static_cast<int>((uptimeMs / (1000 * 60)) % 60);

    if (days > 0)
        return QString("%1 days, %2 hours, %3 minutes").arg(days).arg(hours).arg(minutes);
    else if (hours > 0)
        return QString("%1 hours, %2 minutes").arg(hours).arg(minutes);
    else
        return QString("%1 minutes").arg(minutes);
#endif

    return "Not available";
}

QString SystemInformationDialog::getQtBuildInfo()
{
    QString qtInfo;
    QTextStream stream(&qtInfo);

    stream << "Qt Version: " << qVersion() << "\n";
    stream << "Build ABI: " << QSysInfo::buildAbi() << "\n";

#ifdef NDEBUG
    stream << "Build Type: Release\n";
#else
    stream << "Build Type: Debug\n";
#endif

    return qtInfo;
}

QString SystemInformationDialog::generateSystemInfoReport()
{
    QString report;
    QTextStream stream(&report);

    // Header
    stream << "========================================\n";
    stream << "   AfterNight System Information Report\n";
    stream << "========================================\n";
    stream << "Generated: " << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << "\n\n";

    // Application Information
    stream << "APPLICATION\n";
    stream << "-----------\n";
    stream << "Version:   " << getApplicationVersion() << "\n";
    stream << "Path:      " << getApplicationPath() << "\n";
    stream << getQtBuildInfo() << "\n";

    // Dependencies
    stream << "DEPENDENCIES\n";
    stream << "------------\n";
    QString depsText = getDependencyVersions();
    if (!depsText.isEmpty())
    {
        stream << depsText << "\n";
    }
    else
    {
        stream << "No dependency information available\n\n";
    }

    // Operating System Information
    stream << "OPERATING SYSTEM\n";
    stream << "----------------\n";
    stream << "Name:    " << getOperatingSystemName() << "\n";
    stream << "Version: " << getOperatingSystemVersion() << "\n";
    stream << "Type:    " << getOperatingSystemType() << "\n";
    stream << "Build:   " << getOperatingSystemBuild() << "\n";
    stream << "Uptime:  " << getSystemUptime() << "\n";

#ifdef Q_OS_LINUX
    stream << "Display Server: " << getLinuxDisplayServer() << "\n";
    stream << "Desktop Environment: " << getLinuxDesktopEnvironment() << "\n";
#endif
    stream << "\n";

    // Hardware Information
    stream << "HARDWARE\n";
    stream << "--------\n";
    stream << "Processor: " << getProcessorInfo() << "\n";

    QString gpuInfo = getGPUInfo();
    if (!gpuInfo.isEmpty())
        stream << "Graphics:  " << gpuInfo << "\n";
    else
        stream << "Graphics:  Not available\n";

    // Display Information
    stream << "\nDISPLAY\n";
    stream << "-------\n";
    stream << getDisplayInfo() << "\n";

    // Memory Information
    stream << "\nMEMORY\n";
    stream << "------\n";
#ifdef Q_OS_WIN
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);

    const auto snapshot = an::ui::system_memory::buildWindowsSnapshot(memInfo.ullTotalPhys, memInfo.ullAvailPhys);
    const qint64 totalMemory = snapshot.totalBytes;
    const qint64 availMemory = snapshot.availableBytes;
    const qint64 usedMemory = totalMemory - availMemory;
    const int usagePercent = an::ui::system_memory::calculateUsagePercent(snapshot);

    stream << "Total:     " << formatBytes(totalMemory) << "\n";
    stream << "Used:      " << formatBytes(usedMemory) << " (" << usagePercent << "%)\n";
    stream << "Available: " << formatBytes(availMemory) << "\n";
#elif defined(Q_OS_LINUX)
    const auto mem = getLinuxMemorySnapshot();
    const qint64 totalMemory = mem.totalBytes;
    const qint64 availableMemory = mem.availableBytes;
    const qint64 usedMemory = totalMemory - availableMemory;
    const int usagePercent = an::ui::system_memory::calculateUsagePercent(mem);

    stream << "Total:     " << formatBytes(totalMemory) << "\n";
    stream << "Used:      " << formatBytes(usedMemory) << " (" << usagePercent << "%)\n";
    stream << "Available: " << formatBytes(availableMemory) << "\n";
#endif

    // Storage Information
    stream << "\nSTORAGE DEVICES\n";
    stream << "---------------\n";

    for (int i = 0; i < diskInfoList.size(); ++i)
    {
        const DiskInfo& disk = diskInfoList[i];
        stream << "\nDevice " << (i + 1) << ": " << disk.name << "\n";
        stream << "  Path:      " << disk.path << "\n";
        stream << "  Total:     " << formatBytes(disk.total) << "\n";
        stream << "  Used:      " << formatBytes(disk.used) << " (" << disk.usagePercent << "%)\n";
        stream << "  Available: " << formatBytes(disk.available) << "\n";
    }

    // Footer
    stream << "\n========================================\n";
    stream << "End of Report\n";
    stream << "========================================\n";

    return report;
}

void SystemInformationDialog::exportToFile()
{
    // Generate default filename with timestamp
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString defaultFileName = QString("AfterNight_SystemInfo_%1.txt").arg(timestamp);

    // Resolve initial directory from per-context tracker
    static const QString contextKey = QStringLiteral("__SYSTEM_INFO_EXPORT__");
    QString initialDir;
    if (auto* pathMemory = currentPathMemoryService())
    {
        initialDir = pathMemory->selectPath(contextKey);
        if (!initialDir.isEmpty() && !QDir(initialDir).exists())
        {
            initialDir.clear();
        }
    }
    if (initialDir.isEmpty())
    {
        initialDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    }
    QString defaultPath = initialDir + "/" + defaultFileName;

    // Open file dialog
    QString fileName = an::ui::dialogs::getSaveFileName(currentGlobalSettings(), this, tr("Export System Information"),
                                                        defaultPath, tr("Text Files (*.txt);;All Files (*.*)"));

    if (fileName.isEmpty())
        return; // User cancelled

    // Persist the selected folder for next time
    if (auto* pathMemory = currentPathMemoryService())
    {
        pathMemory->upsertPath(contextKey, QFileInfo(fileName).absolutePath());
    }

    // Ensure .txt extension
    if (!fileName.endsWith(".txt", Qt::CaseInsensitive))
        fileName += ".txt";

    // Write to file
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        const QString message = tr("Could not write to file:\n%1\n\nError: %2").arg(fileName).arg(file.errorString());
        if (auto* dialogService = currentDialogService())
        {
            dialogService->notifyErrorMessage(this, tr("Export Failed"), message);
        }
        else
        {
            QMessageBox::critical(this, tr("Export Failed"), message);
        }
        return;
    }

    QTextStream out(&file);
    out << generateSystemInfoReport();
    file.close();

    LOG_INFO("System information exported to file: {}", fileName.toStdString());

    // Show success message
    const QString message = tr("System information has been exported to:\n%1").arg(fileName);
    if (auto* dialogService = currentDialogService())
    {
        dialogService->notifyInfoMessage(this, tr("Export Successful"), message);
        return;
    }

    QMessageBox::information(this, tr("Export Successful"), message);
}
