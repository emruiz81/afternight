// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "controls/PlateSolveControl.h"

#include "app/CatalogDownloadService.h"
#include "app/GaiaCatalogSettings.h"
#include "app/PlateSolveConfiguration.h"
#include "app/AppContextHelpers.h"
#include "app/PlateSolveService.h"
#include "app/PlateSolveUtils.h"
#include "controls/ImageViewFiles.h"
#include <QGroupBox>
#include "components/AnCollapsibleSection.h"
#include "components/AnInfoBanner.h"
#include "controls/ImageAstrometryInfoFormatter.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"
#include "dialogs/CatalogSearchDialog.h"
#include "session/IEngineSessionProvider.h"
#include "theme/AnDesignTokens.h"
#include "SexagesimalFormatter.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Astrometry/AstrometryHints.h>
#include <AfterNightEngine/Algorithms/Astrometry/Sexagesimal.h>

#include <QComboBox>
#include <QFileInfo>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFile>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QVBoxLayout>
#include <verdigris/wobjectimpl.h>

#include <AfterNightPlatform/Logging.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <stop_token>
#include <utility>
#include <variant>

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

QString formatRaSexagesimal(const double ra_deg)
{
    return an::ui::rightAscensionLabel(ra_deg, 2u);
}

QString formatDecSexagesimal(const double dec_deg)
{
    return an::ui::declinationLabel(dec_deg, 1u);
}

QString formatScale(const double value)
{
    return QStringLiteral("%1 arcsec/px").arg(QString::number(value, 'f', 3));
}

QString formatRotation(const double value)
{
    return QStringLiteral("%1 deg").arg(QString::number(value, 'f', 2));
}

QString formatFov(const double width_arcmin, const double height_arcmin)
{
    return QStringLiteral("%1 x %2 arcmin")
        .arg(QString::number(width_arcmin, 'f', 2), QString::number(height_arcmin, 'f', 2));
}

QString sanitizeFitsText(QString value)
{
    value = value.trimmed();
    if ((value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\''))) ||
        (value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"'))))
    {
        if (value.size() >= 2)
        {
            value = value.mid(1, value.size() - 2);
        }
    }
    return value.simplified();
}

/// The fields of a right ascension a spin box holds: rounded once, to the thousandth of a second the box shows.
void degreesToRaComponents(const double ra_deg, int& out_hours, int& out_minutes, double& out_seconds)
{
    const an::Result<an::SexagesimalAngle> split = an::splitRightAscension(ra_deg, 3u);
    if (!split.hasValue())
    {
        out_hours = 0;
        out_minutes = 0;
        out_seconds = 0.0;
        return;
    }
    out_hours = static_cast<int>(split->whole);
    out_minutes = static_cast<int>(split->minutes);
    out_seconds = static_cast<double>(split->seconds) + static_cast<double>(split->fraction) / 1000.0;
}

/// The fields of a declination a spin box holds: rounded once, to the thousandth of an arcsecond the box shows.
void degreesToDecComponents(const double dec_deg, int& out_degrees, int& out_minutes, double& out_seconds,
                            bool& out_negative)
{
    const an::Result<an::SexagesimalAngle> split = an::splitDeclination(std::clamp(dec_deg, -90.0, 90.0), 3u);
    if (!split.hasValue())
    {
        out_degrees = 0;
        out_minutes = 0;
        out_seconds = 0.0;
        out_negative = false;
        return;
    }
    out_negative = split->negative;
    out_degrees = static_cast<int>(split->whole);
    out_minutes = static_cast<int>(split->minutes);
    out_seconds = static_cast<double>(split->seconds) + static_cast<double>(split->fraction) / 1000.0;
}

void addResultRow(QGridLayout* layout, const int row, const QString& title, QLabel*& valueLabel, QWidget* parent)
{
    auto* keyLabel = new QLabel(title, parent);
    keyLabel->setProperty("anStatusTone", QStringLiteral("secondary"));
    keyLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(keyLabel, row, 0, Qt::AlignTop);

    valueLabel = new QLabel(QStringLiteral("-"), parent);
    valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    valueLabel->setWordWrap(true);
    layout->addWidget(valueLabel, row, 1, Qt::AlignTop);
}

/// The process's session, or null when no context lends one.
const an::consumer::EngineSession* currentSession()
{
    if (auto* provider = process_utils::currentEngineSessionProvider())
    {
        return &provider->engineSession();
    }
    return nullptr;
}

} // namespace

W_OBJECT_IMPL(an::ui::PlateSolveControl)

namespace an::ui
{

PlateSolveControl::PlateSolveControl(ImageViewControl* viewControl, QWidget* parent)
    : QDialog(parent ? parent : viewControl), ImageViewControlListener(viewControl), m_viewControl(viewControl),
      m_service(new an::ui::PlateSolveService(this))
{
    setAttribute(Qt::WA_DeleteOnClose);
    setModal(false);
    setWindowTitle(tr("Plate Solve"));

    const auto& tokenSet = an::ui::tokens();

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(tokenSet.spacing.lg, tokenSet.spacing.lg, tokenSet.spacing.lg, tokenSet.spacing.lg);
    rootLayout->setSpacing(tokenSet.spacing.md);

    auto* headerWidget = new QWidget(this);
    auto* headerLayout = new QVBoxLayout(headerWidget);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(tokenSet.spacing.xs);

    auto* headerTitleLabel = new QLabel(tr("Plate Solve"), headerWidget);
    headerTitleLabel->setFont(tokenSet.typography.heading1);
    {
        QPalette pal = headerTitleLabel->palette();
        pal.setColor(QPalette::WindowText, tokenSet.colors.textPrimary);
        headerTitleLabel->setPalette(pal);
    }
    headerLayout->addWidget(headerTitleLabel);

    auto* headerDescriptionLabel =
        new QLabel(tr("Solve the current image astrometrically, use acquisition hints to improve "
                      "matching, and optionally write the solved WCS metadata back to the image."),
                   headerWidget);
    headerDescriptionLabel->setWordWrap(true);
    headerDescriptionLabel->setFont(tokenSet.typography.body);
    {
        QPalette pal = headerDescriptionLabel->palette();
        pal.setColor(QPalette::WindowText, tokenSet.colors.textSecondary);
        headerDescriptionLabel->setPalette(pal);
    }
    headerLayout->addWidget(headerDescriptionLabel);
    rootLayout->addWidget(headerWidget);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("plateSolveStatus"));
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setProperty("anStatusTone", QStringLiteral("secondary"));
    rootLayout->addWidget(m_statusLabel);

    m_banner = new an::ui::AnInfoBanner(QString(), an::ui::AnInfoBanner::Tone::Info, this);
    m_banner->setObjectName(QStringLiteral("plateSolveBanner"));
    m_banner->setVisible(false);
    rootLayout->addWidget(m_banner);

    // Solver-not-configured banner with configure button
    m_solverNotConfiguredBanner =
        new an::ui::AnInfoBanner(tr("No plate solving tool is configured. Set up ASTAP or astrometry.net in "
                                    "External Tools settings to enable plate solving."),
                                 an::ui::AnInfoBanner::Tone::Warning, this);
    m_solverNotConfiguredBanner->setVisible(false);
    rootLayout->addWidget(m_solverNotConfiguredBanner);

    m_configureButton = new QPushButton(tr("Configure\u2026"), this);
    m_configureButton->setProperty("anProcessButtonRole", QStringLiteral("secondary"));
    m_configureButton->setToolTip(tr("Open External Tools settings to configure a plate solving tool"));
    m_configureButton->setVisible(false);
    connect(m_configureButton, &QPushButton::clicked, this, &PlateSolveControl::openSolverSettings);
    rootLayout->addWidget(m_configureButton);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 1);
    m_progress->setValue(0);
    m_progress->setTextVisible(false);
    m_progress->setVisible(false);
    rootLayout->addWidget(m_progress);

    // Acquisition hints section (collapsible)
    m_hintsSection = new an::ui::AnCollapsibleSection(tr("Acquisition Hints"), this);
    auto* hintsLayout = new QFormLayout();
    hintsLayout->setHorizontalSpacing(tokenSet.spacing.md);
    hintsLayout->setVerticalSpacing(tokenSet.spacing.sm);

    // Force '.' decimal separator for all spinboxes in this dialog
    const QLocale dotLocale(QLocale::C);

    // RA and Dec share a grid so the H/D, M, S columns align
    auto* coordGrid = new QGridLayout();
    coordGrid->setSpacing(tokenSet.spacing.xs);

    // Row 0: RA label + (empty) + hours + minutes + seconds
    coordGrid->addWidget(new QLabel(tr("RA"), m_hintsSection->contentWidget()), 0, 0);

    m_raHoursSpin = new QSpinBox(m_hintsSection->contentWidget());
    m_raHoursSpin->setObjectName(QStringLiteral("plateSolveRaHours"));
    m_raHoursSpin->setRange(0, 23);
    m_raHoursSpin->setSuffix(tr("h"));
    m_raHoursSpin->setToolTip(tr("Right Ascension \u2014 hours (0\u201323)"));
    coordGrid->addWidget(m_raHoursSpin, 0, 2);

    m_raMinutesSpin = new QSpinBox(m_hintsSection->contentWidget());
    m_raMinutesSpin->setObjectName(QStringLiteral("plateSolveRaMinutes"));
    m_raMinutesSpin->setRange(0, 59);
    m_raMinutesSpin->setSuffix(tr("m"));
    m_raMinutesSpin->setToolTip(tr("Right Ascension \u2014 minutes (0\u201359)"));
    coordGrid->addWidget(m_raMinutesSpin, 0, 3);

    m_raSecondsSpin = new QDoubleSpinBox(m_hintsSection->contentWidget());
    m_raSecondsSpin->setObjectName(QStringLiteral("plateSolveRaSeconds"));
    m_raSecondsSpin->setLocale(dotLocale);
    m_raSecondsSpin->setRange(0.0, 59.999);
    m_raSecondsSpin->setDecimals(3);
    m_raSecondsSpin->setSuffix(tr("s"));
    m_raSecondsSpin->setToolTip(tr("Right Ascension \u2014 seconds (0\u201359.999)"));
    coordGrid->addWidget(m_raSecondsSpin, 0, 4);

    // Row 1: Dec label + hemisphere combo + degrees + arcminutes + arcseconds
    coordGrid->addWidget(new QLabel(tr("Dec"), m_hintsSection->contentWidget()), 1, 0);

    m_decHemisphereCombo = new QComboBox(m_hintsSection->contentWidget());
    m_decHemisphereCombo->setObjectName(QStringLiteral("plateSolveDecHemisphere"));
    m_decHemisphereCombo->addItem(tr("N (+)"));
    m_decHemisphereCombo->addItem(tr("S (\u2212)"));
    m_decHemisphereCombo->setToolTip(tr("Hemisphere: North (positive) or South (negative)"));
    coordGrid->addWidget(m_decHemisphereCombo, 1, 1);

    m_decDegreesSpin = new QSpinBox(m_hintsSection->contentWidget());
    m_decDegreesSpin->setObjectName(QStringLiteral("plateSolveDecDegrees"));
    m_decDegreesSpin->setRange(0, 90);
    m_decDegreesSpin->setSuffix(tr("\u00B0"));
    m_decDegreesSpin->setToolTip(tr("Declination \u2014 degrees (0\u201390)"));
    coordGrid->addWidget(m_decDegreesSpin, 1, 2);

    m_decMinutesSpin = new QSpinBox(m_hintsSection->contentWidget());
    m_decMinutesSpin->setObjectName(QStringLiteral("plateSolveDecMinutes"));
    m_decMinutesSpin->setRange(0, 59);
    m_decMinutesSpin->setSuffix(tr("\u2032"));
    m_decMinutesSpin->setToolTip(tr("Declination \u2014 arcminutes (0\u201359)"));
    coordGrid->addWidget(m_decMinutesSpin, 1, 3);

    m_decSecondsSpin = new QDoubleSpinBox(m_hintsSection->contentWidget());
    m_decSecondsSpin->setObjectName(QStringLiteral("plateSolveDecSeconds"));
    m_decSecondsSpin->setLocale(dotLocale);
    m_decSecondsSpin->setRange(0.0, 59.999);
    m_decSecondsSpin->setDecimals(3);
    m_decSecondsSpin->setSuffix(tr("\u2033"));
    m_decSecondsSpin->setToolTip(tr("Declination \u2014 arcseconds (0\u201359.999)"));
    coordGrid->addWidget(m_decSecondsSpin, 1, 4);

    hintsLayout->addRow(coordGrid);

    m_findObjectButton = new QPushButton(tr("Find object\u2026"), m_hintsSection->contentWidget());
    m_findObjectButton->setToolTip(tr("Search online astronomical catalogs by object name and use the\n"
                                      "selected coordinates to fill the RA/Dec hint fields."));
    connect(m_findObjectButton, &QPushButton::clicked, this, &PlateSolveControl::openCatalogSearch);
    hintsLayout->addRow(QString(), m_findObjectButton);

    m_hintFocalLengthSpin = new QDoubleSpinBox(m_hintsSection->contentWidget());
    m_hintFocalLengthSpin->setObjectName(QStringLiteral("plateSolveFocalLength"));
    m_hintFocalLengthSpin->setLocale(dotLocale);
    m_hintFocalLengthSpin->setRange(0.0, 50000.0);
    m_hintFocalLengthSpin->setDecimals(2);
    m_hintFocalLengthSpin->setSuffix(tr(" mm"));
    m_hintFocalLengthSpin->setToolTip(tr("Telescope/lens focal length in millimetres."));
    hintsLayout->addRow(tr("Focal length"), m_hintFocalLengthSpin);

    m_hintPixelSizeSpin = new QDoubleSpinBox(m_hintsSection->contentWidget());
    m_hintPixelSizeSpin->setObjectName(QStringLiteral("plateSolvePixelSize"));
    m_hintPixelSizeSpin->setLocale(dotLocale);
    m_hintPixelSizeSpin->setRange(0.0, 100.0);
    m_hintPixelSizeSpin->setDecimals(2);
    m_hintPixelSizeSpin->setSuffix(tr(" µm"));
    m_hintPixelSizeSpin->setToolTip(tr("Camera pixel size in micrometres."));
    hintsLayout->addRow(tr("Pixel size"), m_hintPixelSizeSpin);

    m_hintWarningLabel = new QLabel(m_hintsSection->contentWidget());
    m_hintWarningLabel->setWordWrap(true);
    m_hintWarningLabel->setProperty("anStatusTone", QStringLiteral("warning"));
    m_hintWarningLabel->setVisible(false);
    hintsLayout->addRow(QString(), m_hintWarningLabel);

    m_hintsSection->setContentLayout(hintsLayout);
    m_hintsSection->setExpanded(false);
    m_hintsSection->contentWidget()->installEventFilter(this);

    connect(m_hintFocalLengthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double) { updateHintWarning(); });
    connect(m_hintPixelSizeSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double) { updateHintWarning(); });

    rootLayout->addWidget(m_hintsSection);

    m_resultCard = new QGroupBox(tr("Solved Field"), this);
    auto* resultLayout = new QGridLayout(m_resultCard);
    resultLayout->setColumnStretch(1, 1);
    resultLayout->setHorizontalSpacing(tokenSet.spacing.md);
    resultLayout->setVerticalSpacing(tokenSet.spacing.sm);
    addResultRow(resultLayout, 0, tr("Field center RA"), m_raLabel, m_resultCard);
    addResultRow(resultLayout, 1, tr("Field center Dec"), m_decLabel, m_resultCard);
    addResultRow(resultLayout, 2, tr("Image scale"), m_scaleLabel, m_resultCard);
    addResultRow(resultLayout, 3, tr("Rotation"), m_rotationLabel, m_resultCard);
    addResultRow(resultLayout, 4, tr("Field of view"), m_fovLabel, m_resultCard);
    addResultRow(resultLayout, 5, tr("Solver"), m_solverLabel, m_resultCard);
    addResultRow(resultLayout, 6, tr("SIP distortion"), m_sipLabel, m_resultCard);
    m_raLabel->setObjectName(QStringLiteral("plateSolveFieldCenterRa"));
    m_decLabel->setObjectName(QStringLiteral("plateSolveFieldCenterDec"));
    m_solverLabel->setObjectName(QStringLiteral("plateSolveSolver"));
    rootLayout->addWidget(m_resultCard, 1);

    auto* clearAstrometryLayout = new QHBoxLayout();
    clearAstrometryLayout->setContentsMargins(0, 0, 0, 0);
    m_clearAstrometryButton = new QPushButton(tr("Clear Astrometry"), this);
    m_clearAstrometryButton->setObjectName(QStringLiteral("plateSolveClearAstrometry"));
    m_clearAstrometryButton->setProperty("anProcessButtonRole", QStringLiteral("primary"));
    m_clearAstrometryButton->setToolTip(tr("Remove all astrometry (WCS) data from the current image"));
    clearAstrometryLayout->addWidget(m_clearAstrometryButton);
    clearAstrometryLayout->addStretch(1);
    rootLayout->addLayout(clearAstrometryLayout);

    m_writeWcsCheckBox = new QCheckBox(tr("Write WCS to the current image metadata"), this);
    m_writeWcsCheckBox->setObjectName(QStringLiteral("plateSolveWriteWcs"));
    m_writeWcsCheckBox->setChecked(true);
    rootLayout->addWidget(m_writeWcsCheckBox);

    auto* buttonLayout = new QHBoxLayout();
    m_resetButton = new QPushButton(tr("Reset Hints"), this);
    m_resetButton->setProperty("anProcessButtonRole", QStringLiteral("secondary"));
    m_resetButton->setToolTip(tr("Reset the acquisition hint fields (RA, Dec, focal length, pixel size)"));
    buttonLayout->addWidget(m_resetButton);
    buttonLayout->addStretch(1);
    m_solveButton = new QPushButton(tr("Solve"), this);
    m_solveButton->setObjectName(QStringLiteral("plateSolveSolve"));
    m_solveButton->setProperty("anProcessButtonRole", QStringLiteral("primary"));
    buttonLayout->addWidget(m_solveButton);
    m_cancelButton = new QPushButton(tr("Close"), this);
    m_cancelButton->setProperty("anProcessButtonRole", QStringLiteral("secondary"));
    buttonLayout->addWidget(m_cancelButton);
    rootLayout->addLayout(buttonLayout);

    connect(m_solveButton, &QPushButton::clicked, this, &PlateSolveControl::startSolve);
    connect(m_resetButton, &QPushButton::clicked, this, &PlateSolveControl::resetHints);
    connect(m_clearAstrometryButton, &QPushButton::clicked, this, &PlateSolveControl::clearAstrometry);
    connect(m_cancelButton, &QPushButton::clicked, this, &PlateSolveControl::cancelOrClose);
    connect(m_service, &an::ui::PlateSolveService::solveProgress, this,
            [this](const QString& message) { setStatusMessage(message); });
    connect(m_service, &an::ui::PlateSolveService::solveFinished, this, &PlateSolveControl::onSolveFinished);

    clearSolvedDisplay();
    refreshForCurrentImage();

    setFixedWidth(450);
    syncFixedWindowSize();
}

PlateSolveControl::~PlateSolveControl()
{
    cleanupActiveInputFile();
}

bool PlateSolveControl::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_hintsSection->contentWidget())
    {
        switch (event ? event->type() : QEvent::None)
        {
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::Hide:
        case QEvent::LayoutRequest:
            syncFixedWindowSize();
            break;
        default:
            break;
        }
    }

    return QDialog::eventFilter(watched, event);
}

void PlateSolveControl::syncFixedWindowSize()
{
    if (m_syncingWindowSize)
    {
        return;
    }

    m_syncingWindowSize = true;

    if (QLayout* rootLayout = layout())
    {
        rootLayout->activate();
    }

    const QSize targetSize = sizeHint();
    if (targetSize.isValid())
    {
        setFixedHeight(targetSize.height());
    }

    m_syncingWindowSize = false;
}

void PlateSolveControl::cleanupActiveInputFile()
{
    if (m_activeInputPath.isEmpty())
    {
        return;
    }

    const QString path = std::exchange(m_activeInputPath, QString());
    if (QFileInfo::exists(path) && !QFile::remove(path))
    {
        LOG_WARNING("Failed to remove plate solve staging FITS: '{}'", path.toStdString());
    }
}

void PlateSolveControl::onImageLoaded(ImageViewControl* /*ctrl*/)
{
    refreshForCurrentImage();
}

void PlateSolveControl::onImageUpdated(ImageViewControl* /*ctrl*/)
{
    refreshForCurrentImage();
}

void PlateSolveControl::onImageRenamed(ImageViewControl* /*ctrl*/)
{
    refreshForCurrentImage();
}

void PlateSolveControl::onImageViewControlDestroyed(ImageViewControl* ctrl)
{
    ImageViewControlListener::onImageViewControlDestroyed(ctrl);
    if (ctrl == m_viewControl)
    {
        m_viewControl = nullptr;
    }
}

bool PlateSolveControl::isSolving() const
{
    return m_isSolving;
}

void PlateSolveControl::closeEvent(QCloseEvent* event)
{
    if (m_isSolving)
    {
        if (m_service)
        {
            m_service->cancel();
        }
        m_closeOnCancelFinish = true;
        event->ignore();
        return;
    }

    QDialog::closeEvent(event);
}

an::ui::PlateSolveSettings PlateSolveControl::readSettings() const
{
    an::ui::PlateSolveSettings settings;
    auto* persistence = currentGlobalSettings();
    if (!persistence)
    {
        return settings;
    }

    const QString storedDefaultSolver =
        persistence->select(SettingsKeys::PLATESOLVE_DEFAULT_SOLVER, SettingsDefaults::PLATESOLVE_DEFAULT_SOLVER);
    settings.astapPath = persistence->select(SettingsKeys::TOOLS_ASTAP_PATH, SettingsDefaults::TOOLS_ASTAP_PATH);
    settings.astrometryLocalPath =
        persistence->select(SettingsKeys::TOOLS_ASTROMETRY_LOCAL_PATH, SettingsDefaults::TOOLS_ASTROMETRY_LOCAL_PATH);
    settings.astrometryApiKey =
        persistence->select(SettingsKeys::TOOLS_ASTROMETRY_API_KEY, SettingsDefaults::TOOLS_ASTROMETRY_API_KEY);
    settings.indexPath =
        persistence->select(SettingsKeys::PLATESOLVE_INDEX_PATH, SettingsDefaults::PLATESOLVE_INDEX_PATH);
    settings.gaiaCatalogDirectory =
        persistence->select(SettingsKeys::PLATESOLVE_GAIA_CATALOG_DIRECTORY, an::ui::defaultGaiaCatalogDirectory());
    settings.gaiaCatalogPack =
        persistence->select(SettingsKeys::PLATESOLVE_GAIA_CATALOG_PACK, SettingsDefaults::PLATESOLVE_GAIA_CATALOG_PACK);
    settings.searchRadius = persistence
                                ->select(SettingsKeys::PLATESOLVE_SEARCH_RADIUS,
                                         QString::number(SettingsDefaults::PLATESOLVE_SEARCH_RADIUS))
                                .toDouble();
    settings.downsample =
        persistence
            ->select(SettingsKeys::PLATESOLVE_DOWNSAMPLE, QString::number(SettingsDefaults::PLATESOLVE_DOWNSAMPLE))
            .toInt();
    settings.timeoutSeconds =
        persistence->select(SettingsKeys::PLATESOLVE_TIMEOUT, QString::number(SettingsDefaults::PLATESOLVE_TIMEOUT))
            .toInt();
    settings.useHints =
        persistence->select(SettingsKeys::PLATESOLVE_USE_HINTS,
                            SettingsDefaults::PLATESOLVE_USE_HINTS ? QStringLiteral("1") : QStringLiteral("0")) ==
        QStringLiteral("1");

    const double focal = persistence
                             ->select(SettingsKeys::PLATESOLVE_FOCAL_LENGTH_MM,
                                      QString::number(SettingsDefaults::PLATESOLVE_FOCAL_LENGTH_MM))
                             .toDouble();
    if (focal > 0.0)
    {
        settings.focalLengthMm = focal;
    }

    const double pixel = persistence
                             ->select(SettingsKeys::PLATESOLVE_PIXEL_SIZE_UM,
                                      QString::number(SettingsDefaults::PLATESOLVE_PIXEL_SIZE_UM))
                             .toDouble();
    if (pixel > 0.0)
    {
        settings.pixelSizeUm = pixel;
    }

    settings.defaultSolver = an::ui::preferredConfiguredPlateSolveSolver(settings, storedDefaultSolver);
    return settings;
}

QString PlateSolveControl::currentImagePath() const
{
    if (!m_viewControl)
    {
        return QString();
    }

    return QString::fromStdString(m_viewControl->getFileName());
}

std::vector<an::CodecCard> PlateSolveControl::currentImageCards() const
{
    // The cards are read under a lease that never waits: an image a writer holds reads as stating nothing until its
    // next update.
    std::vector<an::CodecCard> cards;
    if (!m_viewControl || !m_viewControl->hasImage())
    {
        return cards;
    }
    if (an::Result<an::consumer::AssetReadLease> lease = m_viewControl->document().tryRead(); lease.hasValue())
    {
        lease->visit([&cards](const auto& typed) {
            const auto stated = typed.metadata().codec().cards();
            cards.assign(stated.begin(), stated.end());
        });
    }
    return cards;
}

bool PlateSolveControl::canSolveCurrentImage(QString* errorMessage) const
{
    if (errorMessage)
    {
        errorMessage->clear();
    }

    if (!m_viewControl || !m_viewControl->hasImage())
    {
        if (errorMessage)
        {
            *errorMessage = tr("No image is loaded.");
        }
        return false;
    }

    if (m_viewControl->isSequence())
    {
        if (errorMessage)
        {
            *errorMessage = tr("Plate solving currently supports still images, not sequences.");
        }
        return false;
    }

    return true;
}

void PlateSolveControl::refreshForCurrentImage()
{
    if (!m_viewControl)
    {
        return;
    }

    setWindowTitle(tr("Plate Solve <%1>").arg(QString::fromStdString(m_viewControl->getUniqueName())));

    if (!m_isSolving && !m_hasDisplayedSolve)
    {
        clearBanner();
        clearSolvedDisplay();
        populateHintsFromMetadata();
        populateSolvedDisplayFromMetadata();
    }

    QString errorMessage;
    const bool canSolve = canSolveCurrentImage(&errorMessage);
    const bool solverConfigured = isSolverConfigured();
    m_solveButton->setEnabled(canSolve && solverConfigured && !m_isSolving);
    m_writeWcsCheckBox->setEnabled(m_viewControl->hasImage() && !m_isSolving);
    m_resetButton->setEnabled(!m_isSolving);
    updateClearAstrometryButtonState();
    updateSolverConfiguredState();

    if (!canSolve && !m_isSolving)
    {
        setStatusMessage(errorMessage, QStringLiteral("warning"));
    }
    else if (!m_isSolving)
    {
        setStatusMessage(QString());
    }
}

void PlateSolveControl::populateHintsFromMetadata()
{
    if (!m_viewControl)
    {
        return;
    }

    const std::vector<an::CodecCard> cards = currentImageCards();

    // The position the solve itself would be hinted with: the image's first pointing in the order the plate solve
    // reads it, a solution's reference first.
    bool has_position = false;
    if (const an::Result<an::PointingHint> pointing = an::readPointingHint(cards, an::plateSolveHintCards());
        pointing.hasValue())
    {
        applyCoordinateHints(pointing->direction.rightAscensionDegrees, pointing->direction.declinationDegrees);
        has_position = true;
    }
    else
    {
        m_raHoursSpin->setValue(0);
        m_raMinutesSpin->setValue(0);
        m_raSecondsSpin->setValue(0.0);
        m_decHemisphereCombo->setCurrentIndex(0);
        m_decDegreesSpin->setValue(0);
        m_decMinutesSpin->setValue(0);
        m_decSecondsSpin->setValue(0.0);
    }

    const an::OpticalHints optics = an::readOpticalHints(cards);
    m_hintFocalLengthSpin->setValue(optics.focalLengthMillimetres.value_or(0.0));
    m_hintPixelSizeSpin->setValue(optics.pixelSizeMicrometres.value_or(0.0));
    const bool has_scale = optics.focalLengthMillimetres.has_value() && optics.pixelSizeMicrometres.has_value();

    // Auto-expand the hints section when position or scale info is missing
    m_hintsSection->setExpanded(!has_position || !has_scale);

    updateHintWarning();
}

void PlateSolveControl::updateHintWarning()
{
    if (!m_hintWarningLabel || !m_hintFocalLengthSpin || !m_hintPixelSizeSpin)
    {
        return;
    }

    const bool missingFocal = m_hintFocalLengthSpin->value() <= 0.0;
    const bool missingPixel = m_hintPixelSizeSpin->value() <= 0.0;

    QString warningMessage;
    if (missingFocal && missingPixel)
    {
        warningMessage =
            tr("Focal length and pixel size are needed to provide the scale information required for plate solving.");
    }
    else if (missingFocal)
    {
        warningMessage = tr("Focal length is needed to provide the scale information required for plate solving.");
    }
    else if (missingPixel)
    {
        warningMessage = tr("Pixel size is needed to provide the scale information required for plate solving.");
    }

    m_hintWarningLabel->setText(warningMessage);
    m_hintWarningLabel->setVisible(!warningMessage.isEmpty());
    syncFixedWindowSize();
}

void PlateSolveControl::applyCoordinateHints(const double ra_deg, const double dec_deg)
{
    int ra_hours = 0;
    int ra_minutes = 0;
    double ra_seconds = 0.0;
    degreesToRaComponents(ra_deg, ra_hours, ra_minutes, ra_seconds);

    int dec_degrees = 0;
    int dec_minutes = 0;
    double dec_seconds = 0.0;
    bool dec_negative = false;
    degreesToDecComponents(dec_deg, dec_degrees, dec_minutes, dec_seconds, dec_negative);

    m_raHoursSpin->setValue(ra_hours);
    m_raMinutesSpin->setValue(ra_minutes);
    m_raSecondsSpin->setValue(ra_seconds);
    m_decHemisphereCombo->setCurrentIndex(dec_negative ? 1 : 0);
    m_decDegreesSpin->setValue(dec_degrees);
    m_decMinutesSpin->setValue(dec_minutes);
    m_decSecondsSpin->setValue(dec_seconds);
    m_hintsSection->setExpanded(true);
}

void PlateSolveControl::openCatalogSearch()
{
    QString initialQuery;
    for (const an::CodecCard& card : currentImageCards())
    {
        if (card.origin() == an::CodecNamespace::Fits && card.keyword() == "OBJECT")
        {
            initialQuery = sanitizeFitsText(QString::fromStdString(card.value()));
            break;
        }
    }

    CatalogSearchDialog dialog(initialQuery, this);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    const an::ui::CatalogSearchEntry selected = dialog.selectedEntry();
    applyCoordinateHints(selected.raDeg, selected.decDeg);
    clearBanner();
    setStatusMessage(tr("Filled RA/Dec hints from %1").arg(selected.objectName));
    LOG_INFO("Plate solve object search selected '{}' via {} -> RA={:.6f} deg, Dec={:.6f} deg",
             selected.objectName.toStdString(), selected.resolver.toStdString(), selected.raDeg, selected.decDeg);
}

void PlateSolveControl::setBusyState(const bool busy)
{
    m_isSolving = busy;
    m_progress->setVisible(busy);
    if (busy)
    {
        m_progress->setRange(0, 0);
    }
    else
    {
        m_progress->setRange(0, 1);
        m_progress->setValue(0);
    }

    m_solveButton->setEnabled(!busy && m_viewControl && m_viewControl->canPlateSolveCurrentImage() &&
                              isSolverConfigured());
    m_cancelButton->setText(busy ? tr("Cancel") : tr("Close"));
    m_writeWcsCheckBox->setEnabled(!busy && m_viewControl && m_viewControl->hasImage());
    m_hintsSection->setEnabled(!busy);
    m_resetButton->setEnabled(!busy);
    updateClearAstrometryButtonState();
}

void PlateSolveControl::setStatusMessage(const QString& message, const QString& tone)
{
    if (!m_statusLabel)
    {
        return;
    }

    m_statusLabel->setText(message);
    m_statusLabel->setProperty("anStatusTone", tone);
    m_statusLabel->style()->unpolish(m_statusLabel);
    m_statusLabel->style()->polish(m_statusLabel);
    m_statusLabel->update();
}

void PlateSolveControl::showBanner(const QString& message, an::ui::AnInfoBanner::Tone tone)
{
    if (!m_banner)
    {
        return;
    }

    m_banner->setTone(tone);
    m_banner->setMessage(message);
    m_banner->setVisible(!message.trimmed().isEmpty());
}

void PlateSolveControl::clearBanner()
{
    showBanner(QString(), an::ui::AnInfoBanner::Tone::Info);
}

void PlateSolveControl::clearSolvedDisplay()
{
    for (QLabel* label : {m_raLabel, m_decLabel, m_scaleLabel, m_rotationLabel, m_fovLabel, m_solverLabel, m_sipLabel})
    {
        if (label)
        {
            label->setText(QStringLiteral("-"));
        }
    }
}

void PlateSolveControl::populateSolvedDisplayFromMetadata()
{
    if (!m_viewControl || !m_viewControl->hasImage())
    {
        return;
    }

    const auto summary = buildImageAstrometryInfoSummary(m_viewControl->imageSolution(), currentImageCards());
    if (!summary.hasSolution)
    {
        return;
    }

    m_raLabel->setText(summary.fieldCenterRa);
    m_decLabel->setText(summary.fieldCenterDec);
    m_scaleLabel->setText(summary.imageScale);
    m_rotationLabel->setText(summary.rotation);
    m_fovLabel->setText(summary.fieldOfView);
    m_solverLabel->setText(summary.solver);
    m_sipLabel->setText(summary.sip);
}

void PlateSolveControl::startSolve()
{
    QString errorMessage;
    if (!canSolveCurrentImage(&errorMessage))
    {
        showBanner(errorMessage, an::ui::AnInfoBanner::Tone::Warning);
        setStatusMessage(errorMessage, QStringLiteral("warning"));
        return;
    }

    const an::consumer::EngineSession* session = currentSession();
    const std::shared_ptr<const an::consumer::ImageAsset> asset =
        m_viewControl ? m_viewControl->document().asset() : nullptr;
    const std::optional<an::ImageDescriptor> described =
        m_viewControl ? m_viewControl->imageDescriptor() : std::nullopt;
    if (!asset || !described.has_value() || !m_service || session == nullptr)
    {
        return;
    }

    an::ui::PlateSolveRequest request;
    request.imagePath = currentImagePath();
    request.imageWidth = static_cast<int>(described->extent().width());
    request.imageHeight = static_cast<int>(described->extent().height());
    request.workDirectoryRoot = an::ui::ensureTempDirPathOrSystemTemp();
    request.requestTag = QString::fromStdString(m_viewControl->getUniqueName());
    request.allowExternalSolve = true;
    request.forceExternalSolve = false;
    request.ignoreEmbeddedWcs = true;
    if (an::Result<an::consumer::AssetReadLease> lease = asset->tryRead(); lease.hasValue())
    {
        lease->visit([&request](const auto& typed) { request.existingMetadata = typed.metadata(); });
    }

    // The solver reads the image the view shows, written to a staging FITS file on the solve's worker: the exact
    // samples and cards the person sees, whatever the source file held or however it was edited since.
    cleanupActiveInputFile();
    const QString temp_fits = an::ui::createUniquePlateSolveInputPath(request.workDirectoryRoot);
    const an::Result<an::OutputIntent> intent = an::ui::plateSolveInputIntent();
    if (temp_fits.isEmpty() || !intent.hasValue())
    {
        const QString msg = tr("Failed to create a temporary path for plate solving.");
        showBanner(msg, an::ui::AnInfoBanner::Tone::Warning);
        setStatusMessage(msg, QStringLiteral("warning"));
        return;
    }
    request.imagePath = temp_fits;
    m_activeInputPath = temp_fits;
    an::ui::PlateSolveService::PrepareInput prepare = [session, asset, temp_fits, intent = *intent]() {
        const an::Status written = an::ui::writeViewImage(*session, *asset, temp_fits, intent, {});
        if (written.failed())
        {
            return an::Status::failure(
                written.code(), an::StatusDetail{.message = QStringLiteral("Failed to save image for plate solving: %1")
                                                                .arg(QString::fromUtf8(written.message()))
                                                                .toStdString()});
        }
        LOG_INFO("Saved the shown image to temp FITS for plate solving: '{}'", temp_fits.toStdString());
        return an::Status::success();
    };

    // Read user-provided hints from the spinboxes
    const int ra_h = m_raHoursSpin->value();
    const int ra_m = m_raMinutesSpin->value();
    const double ra_s = m_raSecondsSpin->value();
    const int dec_d = m_decDegreesSpin->value();
    const int dec_m = m_decMinutesSpin->value();
    const double dec_s = m_decSecondsSpin->value();
    const bool dec_south = (m_decHemisphereCombo->currentIndex() == 1);

    const bool has_ra = (ra_h > 0 || ra_m > 0 || ra_s > 0.0);
    const bool has_dec = (dec_d > 0 || dec_m > 0 || dec_s > 0.0);
    if (has_ra || has_dec)
    {
        const double ra_deg = (ra_h + ra_m / 60.0 + ra_s / 3600.0) * 15.0;
        double dec_deg = dec_d + dec_m / 60.0 + dec_s / 3600.0;
        if (dec_south)
            dec_deg = -dec_deg;

        request.hintRaDeg = ra_deg;
        request.hintDecDeg = dec_deg;
        LOG_INFO("Plate solve hints: RA={:.6f}\u00b0 Dec={:.6f}\u00b0 ({}h {}m {:.1f}s, {}{}\u00b0 {}' {:.1f}\")",
                 ra_deg, dec_deg, ra_h, ra_m, ra_s, dec_south ? "-" : "+", dec_d, dec_m, dec_s);
    }

    if (m_hintFocalLengthSpin->value() > 0.0)
    {
        request.focalLengthMm = m_hintFocalLengthSpin->value();
    }
    if (m_hintPixelSizeSpin->value() > 0.0)
    {
        request.pixelSizeUm = m_hintPixelSizeSpin->value();
    }

    LOG_INFO("Plate solve request hints: RA={}, Dec={}, focal_mm={}, pixel_um={}",
             request.hintRaDeg.has_value() ? std::to_string(*request.hintRaDeg) : "(none)",
             request.hintDecDeg.has_value() ? std::to_string(*request.hintDecDeg) : "(none)",
             request.focalLengthMm.has_value() ? std::to_string(*request.focalLengthMm) : "(none)",
             request.pixelSizeUm.has_value() ? std::to_string(*request.pixelSizeUm) : "(none)");

    // A solve with no position hint, in the window or in the image's cards, is blind: it is said once the banner the
    // last solve left is cleared, and stays up while the solve runs, until its answer replaces it.
    const bool blindSolve = (!request.hintRaDeg.has_value() || !request.hintDecDeg.has_value()) &&
                            !an::readPointingHint(currentImageCards(), an::plateSolveHintCards()).hasValue();

    ++m_solveGeneration;
    m_activeSolveGeneration = m_solveGeneration;
    m_solveRevision = m_viewControl->document().revision();
    m_hasDisplayedSolve = false;
    clearSolvedDisplay();
    clearBanner();
    if (blindSolve)
    {
        LOG_WARNING("No RA/Dec position hints available - blind solve will be attempted");
        showBanner(tr("No RA/Dec coordinates provided. Blind solving a narrow-field image "
                      "is very slow and may fail. Consider filling in RA and Dec hints, or "
                      "use \u201cFind object\u2026\u201d to resolve the target name."),
                   an::ui::AnInfoBanner::Tone::Warning);
    }
    const an::ui::PlateSolveSettings settings = readSettings();
    LOG_INFO("Starting plate solve for '{}' with default solver '{}'", request.imagePath.toStdString(),
             an::ui::normalizePlateSolveDefaultSolver(settings.defaultSolver).toStdString());
    setStatusMessage(tr("Launching plate solve..."));
    setBusyState(true);
    m_service->solveAsync(request, settings, std::move(prepare));
}

void PlateSolveControl::updateClearAstrometryButtonState()
{
    if (!m_clearAstrometryButton)
    {
        return;
    }

    if (m_isSolving || !m_viewControl)
    {
        m_clearAstrometryButton->setEnabled(false);
        return;
    }

    m_clearAstrometryButton->setEnabled(m_viewControl->imageSolution().has_value());
}

bool PlateSolveControl::isSolverConfigured() const
{
    const auto settings = readSettings();
    return an::ui::hasAnyConfiguredPlateSolveSolver(settings);
}

void PlateSolveControl::updateSolverConfiguredState()
{
    const bool configured = isSolverConfigured();
    const bool showWarning = !configured && !m_isSolving;
    m_solverNotConfiguredBanner->setVisible(showWarning);
    m_configureButton->setVisible(showWarning);
    syncFixedWindowSize();
}

void PlateSolveControl::openSolverSettings()
{
    if (auto* dialogService = currentDialogService())
    {
        dialogService->openSettingsAt(tr("Plate Solving"));
    }

    // Re-check solver availability after the settings dialog closes
    const bool configured = isSolverConfigured();
    updateSolverConfiguredState();

    const bool canSolve = canSolveCurrentImage();
    m_solveButton->setEnabled(canSolve && configured && !m_isSolving);
}

void PlateSolveControl::resetHints()
{
    m_raHoursSpin->setValue(0);
    m_raMinutesSpin->setValue(0);
    m_raSecondsSpin->setValue(0.0);
    m_decHemisphereCombo->setCurrentIndex(0);
    m_decDegreesSpin->setValue(0);
    m_decMinutesSpin->setValue(0);
    m_decSecondsSpin->setValue(0.0);
    m_hintFocalLengthSpin->setValue(0.0);
    m_hintPixelSizeSpin->setValue(0.0);
    m_hintsSection->setExpanded(true);
    updateHintWarning();
}

void PlateSolveControl::clearAstrometry()
{
    if (!m_viewControl || !m_viewControl->hasImage() || !m_viewControl->imageSolution().has_value())
    {
        return;
    }

    int answer = QMessageBox::No;
    process_utils::messageBoxYesNo(this, tr("Clear Astrometry"),
                                   tr("This will remove all astrometry (WCS) data and the plate solver tag from the "
                                      "current image.\n\nDo you want to continue?"),
                                   true, &answer);
    if (answer != QMessageBox::Yes || !m_viewControl)
    {
        return;
    }

    // An edit the history records: a copy of the image with every world-coordinate card and the solver's card
    // withdrawn, shown when it lands.
    QPointer<PlateSolveControl> guard(this);
    const QString path = currentImagePath();
    m_viewControl->editImage(
        "Clear Astrometry", "Removed the astrometric solution",
        [](const an::consumer::EngineSession& session, const an::consumer::AssetWriteLease& image,
           std::stop_token stop) -> an::Result<an::consumer::ImageAsset> {
            an::Result<an::ImageMetadata> metadata =
                std::visit([](const auto& typed) { return an::ui::metadataWithoutPlateSolution(typed.metadata()); },
                           image.image());
            if (!metadata.hasValue())
            {
                return metadata.status();
            }
            return an::ui::copyImageWithMetadata(session, image, std::move(*metadata), std::move(stop));
        },
        [guard, path](const bool shown) {
            if (!guard || !shown)
            {
                return;
            }
            guard->m_hasDisplayedSolve = false;
            guard->clearSolvedDisplay();
            guard->clearBanner();
            guard->setStatusMessage(tr("Astrometry data has been removed."), QStringLiteral("secondary"));
            guard->updateClearAstrometryButtonState();
            LOG_INFO("Astrometry data cleared for '{}'", path.toStdString());
        });
}

void PlateSolveControl::cancelOrClose()
{
    if (m_isSolving)
    {
        if (m_service)
        {
            m_service->cancel();
        }
        setStatusMessage(tr("Cancelling plate solve..."));
        return;
    }

    close();
}

void PlateSolveControl::onSolveFinished(an::ui::PlateSolveResult result)
{
    setBusyState(false);
    cleanupActiveInputFile();
    const uint64_t solved_generation = m_activeSolveGeneration;

    if (result.cancelled)
    {
        clearSolvedDisplay();
        m_hasDisplayedSolve = false;
        LOG_WARNING("Plate solve cancelled");
        setStatusMessage(tr("Plate solve cancelled."), QStringLiteral("warning"));
        showBanner(result.errorMessage.isEmpty() ? tr("Plate solve cancelled.") : result.errorMessage,
                   an::ui::AnInfoBanner::Tone::Warning);
        if (m_closeOnCancelFinish)
        {
            m_closeOnCancelFinish = false;
            close();
        }
        return;
    }

    if (!result.succeeded)
    {
        clearSolvedDisplay();
        m_hasDisplayedSolve = false;
        LOG_ERROR("Plate solve failed: {}", result.errorMessage.toStdString());
        clearBanner();
        setStatusMessage(QString());
        process_utils::notifyErrorMessage(this, tr("Plate Solve Failed"),
                                          result.errorMessage.trimmed().isEmpty() ? tr("The plate solve failed.")
                                                                                  : result.errorMessage);
        return;
    }

    if (!m_viewControl || solved_generation != m_solveGeneration ||
        m_viewControl->document().revision() != m_solveRevision)
    {
        clearSolvedDisplay();
        m_hasDisplayedSolve = false;
        setStatusMessage(tr("Plate solve finished, but the active image changed."), QStringLiteral("warning"));
        showBanner(tr("Plate solve finished for an image that is no longer active. The result was not applied."),
                   an::ui::AnInfoBanner::Tone::Warning);
        return;
    }

    updateSolvedDisplay(result);
    const bool focal_length_updated = applySolvedFocalLength(result);
    m_hasDisplayedSolve = true;
    clearBanner();
    LOG_INFO("Plate solve succeeded with {} in {:.0f} ms", result.solverUsed.toStdString(), result.solveTimeMs);
    QString status_message =
        tr("Solved with %1 in %2 ms").arg(result.solverUsed, QString::number(result.solveTimeMs, 'f', 0));
    if (focal_length_updated && result.derivedFocalLengthMm.has_value())
    {
        status_message +=
            tr(" — focal length updated to %1 mm").arg(QString::number(*result.derivedFocalLengthMm, 'f', 2));
    }
    setStatusMessage(status_message, QStringLiteral("secondary"));

    if (m_writeWcsCheckBox->isChecked())
    {
        writeWcsToMetadata(result);
    }
}

bool PlateSolveControl::applySolvedFocalLength(const an::ui::PlateSolveResult& result)
{
    if (!m_hintFocalLengthSpin || !result.derivedFocalLengthMm.has_value())
    {
        return false;
    }

    const double solved_focal_mm = *result.derivedFocalLengthMm;
    if (solved_focal_mm <= 0.0 || !std::isfinite(solved_focal_mm))
    {
        return false;
    }

    const bool changed = std::abs(m_hintFocalLengthSpin->value() - solved_focal_mm) > 1e-3;
    m_hintFocalLengthSpin->setValue(solved_focal_mm);
    m_hintsSection->setExpanded(true);

    LOG_INFO("Updated plate solve focal length hint to {:.3f} mm from solved scale {:.6f} arcsec/px", solved_focal_mm,
             result.imageScaleArcsecPerPixel);
    return changed;
}

void PlateSolveControl::updateSolvedDisplay(const an::ui::PlateSolveResult& result)
{
    m_raLabel->setText(formatRaSexagesimal(result.fieldCenterRaDeg));
    m_decLabel->setText(formatDecSexagesimal(result.fieldCenterDecDeg));
    m_scaleLabel->setText(formatScale(result.imageScaleArcsecPerPixel));
    m_rotationLabel->setText(formatRotation(result.rotationDeg));
    m_fovLabel->setText(formatFov(result.fovWidthArcmin, result.fovHeightArcmin));
    m_solverLabel->setText(result.solverUsed);

    if (result.solution.has_value() && result.solution->distortion().hasForwardModel())
    {
        m_sipLabel->setText(tr("Yes (A/B order %1/%2)")
                                .arg(result.solution->distortion().forwardX().order)
                                .arg(result.solution->distortion().forwardY().order));
    }
    else
    {
        m_sipLabel->setText(tr("No"));
    }
}

void PlateSolveControl::writeWcsToMetadata(const an::ui::PlateSolveResult& result)
{
    if (!m_viewControl || !m_viewControl->hasImage() || !result.solution.has_value())
    {
        return;
    }

    an::ui::PlateSolutionFacts facts;
    facts.solver = result.solverUsed;
    if (result.derivedFocalLengthMm.has_value() && *result.derivedFocalLengthMm > 0.0)
    {
        facts.focalLengthMillimetres = result.derivedFocalLengthMm;
    }
    if (m_hintPixelSizeSpin && m_hintPixelSizeSpin->value() > 0.0)
    {
        facts.pixelSizeMicrometres = m_hintPixelSizeSpin->value();
    }

    // An edit the history records: a copy of the image carrying the solution in its own cards, shown when it lands.
    QPointer<ImageViewControl> view(m_viewControl);
    const an::WcsSolution solution = *result.solution;
    m_viewControl->editImage(
        "Plate Solve", QStringLiteral("Plate solved with %1").arg(result.solverUsed).toStdString(),
        [solution, facts](const an::consumer::EngineSession& session, const an::consumer::AssetWriteLease& image,
                          std::stop_token stop) -> an::Result<an::consumer::ImageAsset> {
            an::Result<an::ImageMetadata> metadata = std::visit(
                [&](const auto& typed) { return an::ui::metadataWithPlateSolution(typed.metadata(), solution, facts); },
                image.image());
            if (!metadata.hasValue())
            {
                return metadata.status();
            }
            return an::ui::copyImageWithMetadata(session, image, std::move(*metadata), std::move(stop));
        },
        [view](const bool shown) {
            if (view && shown)
            {
                view->runPostPlateSolveStarDetection();
            }
        });
}

} // namespace an::ui
