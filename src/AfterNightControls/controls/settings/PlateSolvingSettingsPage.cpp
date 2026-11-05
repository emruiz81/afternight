// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "PlateSolvingSettingsPage.h"
#include "SettingsKeys.h"
#include "SettingsDefaults.h"
#include "PathPickerWidget.h"
#include "../GlobalSettingsPersistence.h"
#include "app/PlateSolveConfiguration.h"
#include "theme/AnDesignTokens.h"

#include <verdigris/wobjectimpl.h>

#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStandardItemModel>
#include <QDir>
#include <QFileInfo>

W_OBJECT_IMPL(PlateSolvingSettingsPage)

namespace
{

#ifdef _WIN32
const QStringList ASTAP_SEARCH_PATHS = {
    "C:/Program Files/astap",
    "C:/Program Files (x86)/astap",
    QDir::homePath() + "/astap",
};
constexpr auto ASTAP_EXECUTABLE = "astap.exe";
#else
const QStringList ASTAP_SEARCH_PATHS = {
    "/usr/local/bin",
    "/usr/bin",
    "/opt/astap",
    QDir::homePath() + "/astap",
};
constexpr auto ASTAP_EXECUTABLE = "astap";
#endif

bool isValidExecutable(const QString& path)
{
    if (path.isEmpty())
    {
        return false;
    }
    QFileInfo info(path);
    return info.exists() && info.isExecutable();
}

} // namespace

PlateSolvingSettingsPage::PlateSolvingSettingsPage(GlobalSettingsPersistence* settingsPersistence, QWidget* parent)
    : SettingsPage(parent), m_Settings(settingsPersistence)
{
    setupUi();
    connectSignals();
}

QString PlateSolvingSettingsPage::pageTitle() const
{
    return tr("Plate Solving");
}

QIcon PlateSolvingSettingsPage::pageIcon() const
{
    return contextIcon(QStringLiteral("search"));
}

QStringList PlateSolvingSettingsPage::searchKeywords() const
{
    return QStringList{
        tr("Plate Solving"), tr("Gaia"),     tr("ASTAP"),      tr("Astrometry"), tr("Solver"),        tr("Index"),
        tr("Catalog"),       tr("Download"), tr("Downsample"), tr("Timeout"),    tr("Search Radius"), tr("API Key"),
    };
}

void PlateSolvingSettingsPage::setupUi()
{
    const auto& tokenSet = an::ui::tokens();

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(tokenSet.spacing.lg);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // === General Section ===
    auto* generalGroup = new QGroupBox(tr("General"), this);
    auto* generalLayout = new QFormLayout(generalGroup);
    generalLayout->setSpacing(tokenSet.spacing.md);

    m_DefaultSolverCombo = new QComboBox(generalGroup);
    m_DefaultSolverCombo->addItem(tr("Internal Gaia"), QStringLiteral("internal_gaia"));
    m_DefaultSolverCombo->addItem(tr("ASTAP"), QStringLiteral("astap"));
#ifndef _WIN32
    m_DefaultSolverCombo->addItem(tr("Astrometry.net (local)"), QStringLiteral("astrometry_local"));
#endif
    m_DefaultSolverCombo->addItem(tr("Astrometry.net (web)"), QStringLiteral("astrometry_web"));
    m_DefaultSolverCombo->setPlaceholderText(tr("No configured solver"));
    m_DefaultSolverCombo->setToolTip(tr("Preferred solver. If it fails, the other solvers are tried automatically."));
    generalLayout->addRow(tr("Default solver:"), m_DefaultSolverCombo);

    m_SearchRadiusSpin = new QDoubleSpinBox(generalGroup);
    m_SearchRadiusSpin->setRange(1.0, 180.0);
    m_SearchRadiusSpin->setDecimals(1);
    m_SearchRadiusSpin->setSuffix(tr(" deg"));
    m_SearchRadiusSpin->setToolTip(tr("Search radius around the hint coordinates"));
    generalLayout->addRow(tr("Search radius:"), m_SearchRadiusSpin);

    m_DownsampleSpin = new QSpinBox(generalGroup);
    m_DownsampleSpin->setRange(1, 8);
    m_DownsampleSpin->setToolTip(tr("Downsample factor applied before solving (higher = faster but less accurate)"));
    generalLayout->addRow(tr("Downsample:"), m_DownsampleSpin);

    m_TimeoutSpin = new QSpinBox(generalGroup);
    m_TimeoutSpin->setRange(10, 600);
    m_TimeoutSpin->setSuffix(tr(" s"));
    m_TimeoutSpin->setToolTip(tr("Maximum time allowed for a plate solve attempt"));
    generalLayout->addRow(tr("Timeout:"), m_TimeoutSpin);

    generalLayout->addRow(QString(),
                          createInfoLabel(tr("Internal Gaia catalog data is managed in Settings -> Catalogs & Data.")));

    mainLayout->addWidget(generalGroup);

    // === ASTAP Section ===
    auto* astapGroup = new QGroupBox(tr("ASTAP"), this);
    auto* astapLayout = new QFormLayout(astapGroup);
    astapLayout->setSpacing(tokenSet.spacing.md);

    m_AstapPathPicker = new PathPickerWidget(PathPickerWidget::Mode::Executable, astapGroup);
    m_AstapPathPicker->setPlaceholderText(tr("Path to ASTAP executable..."));
    m_AstapPathPicker->setDialogTitle(tr("Select ASTAP Executable"));
#ifdef _WIN32
    m_AstapPathPicker->setFileFilter(tr("Executable files (*.exe);;All Files (*)"));
#else
    m_AstapPathPicker->setFileFilter(tr("All Files (*)"));
#endif
    m_AstapPathPicker->setDetectFunction([this]() { return detectAstap(); });
    astapLayout->addRow(tr("Executable:"), m_AstapPathPicker);

    auto* astapInfo = createInfoLabel(tr("ASTAP is a free astrometric solver. It ships with its own star "
                                         "database — no additional index files are needed."));
    astapLayout->addRow("", astapInfo);

    mainLayout->addWidget(astapGroup);

    // === Astrometry.net Section ===
    auto* astrometryGroup = new QGroupBox(tr("Astrometry.net"), this);
    auto* astrometryLayout = new QFormLayout(astrometryGroup);
    astrometryLayout->setSpacing(tokenSet.spacing.md);

    m_AstrometryLocalPathPicker = new PathPickerWidget(PathPickerWidget::Mode::Executable, astrometryGroup);
    m_AstrometryLocalPathPicker->setPlaceholderText(tr("Path to local solve-field executable..."));
    m_AstrometryLocalPathPicker->setDialogTitle(tr("Select solve-field Executable"));
    m_AstrometryLocalPathPicker->setToolTip(
        tr("Used only by Astrometry.net local solving on Linux. This setting is ignored by the web solver."));
#ifndef _WIN32
    m_AstrometryLocalPathPicker->setFileFilter(tr("All Files (*)"));
#endif
    m_AstrometryLocalPathPicker->setDetectFunction([this]() { return detectAstrometryLocal(); });

    m_IndexPathPicker = new PathPickerWidget(PathPickerWidget::Mode::Directory, astrometryGroup);
    m_IndexPathPicker->setPlaceholderText(tr("Path to index files directory..."));
    m_IndexPathPicker->setDialogTitle(tr("Select Index Files Directory"));
    m_IndexPathPicker->setToolTip(tr("Used only by Astrometry.net local solving on Linux. "
                                     "Directory containing astrometry.net index files "
                                     "(e.g. 4200-series)."));

#ifndef _WIN32
    auto* localAstrometryLabel = createInfoLabel(tr("Astrometry.net local (Linux only)"));
    astrometryLayout->addRow(QString(), localAstrometryLabel);
    astrometryLayout->addRow(tr("Local solve-field:"), m_AstrometryLocalPathPicker);
    astrometryLayout->addRow(tr("Local index files:"), m_IndexPathPicker);

    auto* localAstrometryInfo =
        createInfoLabel(tr("These settings are used only by Astrometry.net local solving on Linux. "
                           "They are ignored when using Astrometry.net (web)."));
    astrometryLayout->addRow(QString(), localAstrometryInfo);
#else
    m_AstrometryLocalPathPicker->hide();
    m_IndexPathPicker->hide();
#endif

    m_AstrometryApiKeyEdit = new QLineEdit(astrometryGroup);
    m_AstrometryApiKeyEdit->setPlaceholderText(tr("API key for online solving (nova.astrometry.net)..."));
    m_AstrometryApiKeyEdit->setEchoMode(QLineEdit::Password);
    m_AstrometryApiKeyEdit->setToolTip(tr("Used only by Astrometry.net web solving through nova.astrometry.net. "
                                          "This setting is not required for local solve-field."));

    auto* webAstrometryLabel = createInfoLabel(tr("Astrometry.net web"));
    astrometryLayout->addRow(QString(), webAstrometryLabel);
    astrometryLayout->addRow(tr("Web API key:"), m_AstrometryApiKeyEdit);

#ifndef _WIN32
    auto* astrometryInfo = createInfoLabel(tr("The web API key is used only for Astrometry.net (web). "
                                              "Register at nova.astrometry.net to obtain one."));
#else
    auto* astrometryInfo = createInfoLabel(tr("Astrometry.net local solving is available only on Linux. "
                                              "The API key enables Astrometry.net web solving via nova.astrometry.net. "
                                              "Register at nova.astrometry.net to obtain one."));
#endif
    astrometryLayout->addRow("", astrometryInfo);

    mainLayout->addWidget(astrometryGroup);

    mainLayout->addStretch(1);
}

void PlateSolvingSettingsPage::connectSignals()
{
    connect(m_AstapPathPicker, &PathPickerWidget::pathChanged, this, [this]() {
        markAsModified();
        updateAstapStatus();
        refreshDefaultSolverOptions();
    });
    connect(m_AstrometryLocalPathPicker, &PathPickerWidget::pathChanged, this, [this]() {
        markAsModified();
        updateAstrometryLocalStatus();
        refreshDefaultSolverOptions();
    });
    connect(m_AstrometryApiKeyEdit, &QLineEdit::textChanged, this, [this]() {
        markAsModified();
        refreshDefaultSolverOptions();
    });

    trackChanges(m_DefaultSolverCombo);
    trackChanges(m_SearchRadiusSpin);
    trackChanges(m_DownsampleSpin);
    trackChanges(m_TimeoutSpin);
    connect(m_IndexPathPicker, &PathPickerWidget::pathChanged, this, &PlateSolvingSettingsPage::markAsModified);
}

void PlateSolvingSettingsPage::disconnectSignals()
{
    m_AstapPathPicker->disconnect(this);
    m_AstrometryLocalPathPicker->disconnect(this);
    m_AstrometryApiKeyEdit->disconnect(this);
    m_DefaultSolverCombo->disconnect(this);
    m_SearchRadiusSpin->disconnect(this);
    m_DownsampleSpin->disconnect(this);
    m_TimeoutSpin->disconnect(this);
    m_IndexPathPicker->disconnect(this);
}

void PlateSolvingSettingsPage::loadSettings()
{
    disconnectSignals();

    if (!m_Settings)
    {
        resetToDefaultsInternal();
        connectSignals();
        return;
    }

    // ASTAP
    m_AstapPathPicker->setPath(m_Settings->select(SettingsKeys::TOOLS_ASTAP_PATH, SettingsDefaults::TOOLS_ASTAP_PATH));

    // Astrometry.net local
    m_AstrometryLocalPathPicker->setPath(
        m_Settings->select(SettingsKeys::TOOLS_ASTROMETRY_LOCAL_PATH, SettingsDefaults::TOOLS_ASTROMETRY_LOCAL_PATH));

    // Astrometry.net API key
    m_AstrometryApiKeyEdit->setText(
        m_Settings->select(SettingsKeys::TOOLS_ASTROMETRY_API_KEY, SettingsDefaults::TOOLS_ASTROMETRY_API_KEY));

    // Index path
    m_IndexPathPicker->setPath(
        m_Settings->select(SettingsKeys::PLATESOLVE_INDEX_PATH, SettingsDefaults::PLATESOLVE_INDEX_PATH));

    // Search radius
    m_SearchRadiusSpin->setValue(m_Settings
                                     ->select(SettingsKeys::PLATESOLVE_SEARCH_RADIUS,
                                              QString::number(SettingsDefaults::PLATESOLVE_SEARCH_RADIUS))
                                     .toDouble());

    // Downsample
    m_DownsampleSpin->setValue(
        m_Settings
            ->select(SettingsKeys::PLATESOLVE_DOWNSAMPLE, QString::number(SettingsDefaults::PLATESOLVE_DOWNSAMPLE))
            .toInt());

    // Timeout
    m_TimeoutSpin->setValue(
        m_Settings->select(SettingsKeys::PLATESOLVE_TIMEOUT, QString::number(SettingsDefaults::PLATESOLVE_TIMEOUT))
            .toInt());

    refreshDefaultSolverOptions(
        m_Settings->select(SettingsKeys::PLATESOLVE_DEFAULT_SOLVER, SettingsDefaults::PLATESOLVE_DEFAULT_SOLVER));

    connectSignals();
    clearModified();

    updateAstapStatus();
    updateAstrometryLocalStatus();
}

void PlateSolvingSettingsPage::saveSettings()
{
    if (!m_Settings)
    {
        return;
    }

    m_Settings->upsert(SettingsKeys::TOOLS_ASTAP_PATH, m_AstapPathPicker->path());
    m_Settings->upsert(SettingsKeys::TOOLS_ASTROMETRY_LOCAL_PATH, m_AstrometryLocalPathPicker->path());
    m_Settings->upsert(SettingsKeys::TOOLS_ASTROMETRY_API_KEY, m_AstrometryApiKeyEdit->text());
    m_Settings->upsert(SettingsKeys::PLATESOLVE_DEFAULT_SOLVER,
                       an::ui::preferredConfiguredPlateSolveSolver(currentUiSettings(),
                                                                   m_DefaultSolverCombo->currentData().toString()));
    m_Settings->upsert(SettingsKeys::PLATESOLVE_INDEX_PATH, m_IndexPathPicker->path());
    m_Settings->upsert(SettingsKeys::PLATESOLVE_SEARCH_RADIUS, QString::number(m_SearchRadiusSpin->value(), 'f', 1));
    m_Settings->upsert(SettingsKeys::PLATESOLVE_DOWNSAMPLE, QString::number(m_DownsampleSpin->value()));
    m_Settings->upsert(SettingsKeys::PLATESOLVE_TIMEOUT, QString::number(m_TimeoutSpin->value()));

    clearModified();
}

void PlateSolvingSettingsPage::resetToDefaults()
{
    disconnectSignals();
    resetToDefaultsInternal();
    connectSignals();
}

void PlateSolvingSettingsPage::resetToDefaultsInternal()
{
    m_AstapPathPicker->setPath(QString(SettingsDefaults::TOOLS_ASTAP_PATH));
    m_AstapPathPicker->clearStatus();

    m_AstrometryLocalPathPicker->setPath(QString(SettingsDefaults::TOOLS_ASTROMETRY_LOCAL_PATH));
    m_AstrometryLocalPathPicker->clearStatus();

    m_AstrometryApiKeyEdit->setText(QString(SettingsDefaults::TOOLS_ASTROMETRY_API_KEY));
    refreshDefaultSolverOptions(QString(SettingsDefaults::PLATESOLVE_DEFAULT_SOLVER));

    m_IndexPathPicker->setPath(QString(SettingsDefaults::PLATESOLVE_INDEX_PATH));

    m_SearchRadiusSpin->setValue(SettingsDefaults::PLATESOLVE_SEARCH_RADIUS);
    m_DownsampleSpin->setValue(SettingsDefaults::PLATESOLVE_DOWNSAMPLE);
    m_TimeoutSpin->setValue(SettingsDefaults::PLATESOLVE_TIMEOUT);

    markAsModified();
}

ValidationResult PlateSolvingSettingsPage::validateSettings() const
{
    return ValidationResult::valid();
}

void PlateSolvingSettingsPage::refreshDefaultSolverOptions(const QString& preferredSolver)
{
    if (!m_DefaultSolverCombo)
    {
        return;
    }

    const QString preferred =
        preferredSolver.trimmed().isEmpty() ? m_DefaultSolverCombo->currentData().toString() : preferredSolver;
    const an::ui::PlateSolveSettings settings = currentUiSettings();
    auto* model = qobject_cast<QStandardItemModel*>(m_DefaultSolverCombo->model());
    const QSignalBlocker blocker(m_DefaultSolverCombo);

    for (int index = 0; index < m_DefaultSolverCombo->count(); ++index)
    {
        const QString solverId = m_DefaultSolverCombo->itemData(index).toString();
        const bool configured = an::ui::isPlateSolveSolverConfigured(settings, solverId);
        if (model)
        {
            if (QStandardItem* item = model->item(index))
            {
                item->setEnabled(configured);
                item->setToolTip(configured ? tr("Configured and available.")
                                            : tr("Configure this solver before selecting it as the default."));
            }
        }
    }

    const QString selectedSolver = an::ui::preferredConfiguredPlateSolveSolver(settings, preferred);
    const int selectedIndex = selectedSolver.isEmpty() ? -1 : m_DefaultSolverCombo->findData(selectedSolver);
    m_DefaultSolverCombo->setCurrentIndex(selectedIndex);
}

an::ui::PlateSolveSettings PlateSolvingSettingsPage::currentUiSettings() const
{
    an::ui::PlateSolveSettings settings;
    settings.defaultSolver = m_DefaultSolverCombo ? m_DefaultSolverCombo->currentData().toString() : QString();
    settings.astapPath = m_AstapPathPicker ? m_AstapPathPicker->path() : QString();
    settings.astrometryLocalPath = m_AstrometryLocalPathPicker ? m_AstrometryLocalPathPicker->path() : QString();
    settings.astrometryApiKey = m_AstrometryApiKeyEdit ? m_AstrometryApiKeyEdit->text() : QString();
    settings.gaiaCatalogDirectory =
        m_Settings ? m_Settings->select(SettingsKeys::PLATESOLVE_GAIA_CATALOG_DIRECTORY,
                                        QString(SettingsDefaults::PLATESOLVE_GAIA_CATALOG_DIRECTORY))
                   : QString(SettingsDefaults::PLATESOLVE_GAIA_CATALOG_DIRECTORY);
    settings.gaiaCatalogPack = m_Settings ? m_Settings->select(SettingsKeys::PLATESOLVE_GAIA_CATALOG_PACK,
                                                               QString(SettingsDefaults::PLATESOLVE_GAIA_CATALOG_PACK))
                                          : QString(SettingsDefaults::PLATESOLVE_GAIA_CATALOG_PACK);
    return settings;
}

// === Detection Functions ===

QString PlateSolvingSettingsPage::detectAstap() const
{
    QString pathResult = QStandardPaths::findExecutable(ASTAP_EXECUTABLE);
    if (!pathResult.isEmpty())
    {
        return pathResult;
    }

    for (const QString& searchDir : ASTAP_SEARCH_PATHS)
    {
        QString candidate = QDir(searchDir).filePath(ASTAP_EXECUTABLE);
        if (isValidExecutable(candidate))
        {
            return candidate;
        }
    }

    return {};
}

QString PlateSolvingSettingsPage::detectAstrometryLocal() const
{
#ifndef _WIN32
    QString pathResult = QStandardPaths::findExecutable("solve-field");
    if (!pathResult.isEmpty())
    {
        return pathResult;
    }

    const QStringList searchPaths = {
        "/usr/bin/solve-field",
        "/usr/local/bin/solve-field",
        "/opt/astrometry/bin/solve-field",
    };

    for (const QString& path : searchPaths)
    {
        if (isValidExecutable(path))
        {
            return path;
        }
    }
#endif

    return {};
}

// === Status Update Functions ===

void PlateSolvingSettingsPage::updateAstapStatus()
{
    const QString path = m_AstapPathPicker->path();
    if (path.isEmpty())
    {
        m_AstapPathPicker->clearStatus();
        return;
    }

    if (isValidExecutable(path))
    {
        m_AstapPathPicker->setStatus(tr("\u2713 ASTAP detected"), false);
    }
    else
    {
        m_AstapPathPicker->setStatus(tr("\u2717 Executable not found"), true);
    }
}

void PlateSolvingSettingsPage::updateAstrometryLocalStatus()
{
    const QString path = m_AstrometryLocalPathPicker->path();
    if (path.isEmpty())
    {
        m_AstrometryLocalPathPicker->clearStatus();
        return;
    }

    if (isValidExecutable(path))
    {
        m_AstrometryLocalPathPicker->setStatus(tr("\u2713 Astrometry.net solve-field detected"), false);
    }
    else
    {
        m_AstrometryLocalPathPicker->setStatus(tr("\u2717 Executable not found"), true);
    }
}
