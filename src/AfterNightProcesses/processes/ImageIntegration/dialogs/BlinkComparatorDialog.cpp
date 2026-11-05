// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "BlinkComparatorDialog.h"
#include "app/ApplicationStorage.h"
#include "components/AnCardWidget.h"
#include "controls/ImageViewControl.h"
#include "processes/ImageIntegration/ImageIntegrationProcessShared.h"
#include "processes/ImageIntegration/model/FrameId.h"
#include "theme/AnDesignTokens.h"
#include "widgets/ComparisonViewWidget.h"
#include "widgets/ImageViewWidget.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecMetadataProjection.h>
#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/ImageMetadata.h>

#include <AfterNightPlatform/Logging.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QAbstractItemView>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QTimer>
#include <QCoreApplication>
#include <QCloseEvent>
#include <QBrush>
#include <QColor>
#include <QDateTime>
#include <QFileInfo>
#include <QKeyEvent>
#include <QListWidget>
#include <QMap>
#include <QMouseEvent>
#include <QPalette>
#include <QSet>
#include <QSettings>
#include <QHeaderView>
#include <QScopedValueRollback>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QSignalBlocker>
#include <QStyle>
#include <QTableWidget>
#include <QVariantMap>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(BlinkComparatorDialog)

namespace
{

enum HistoryTableColumn
{
    HistoryRunColumn,
    HistoryExecutedColumn,
    HistoryIntegrationTimeColumn,
    HistoryFramesColumn,
    HistoryFilterColumn,
    HistoryObjectColumn,
    HistoryResolutionColumn,
    HistoryPathColumn,
    HistoryColumnCount
};

enum SourceFrameTableColumn
{
    SourceFrameFileColumn,
    SourceFrameFilterColumn,
    SourceFrameResolutionColumn,
    SourceFrameExposureColumn,
    SourceFrameTemperatureColumn,
    SourceFrameObjectColumn,
    SourceFrameCameraColumn,
    SourceFramePathColumn,
    SourceFrameColumnCount
};

[[nodiscard]] QString historyRowText(const QVariantMap& row, const QString& key)
{
    return row.value(key).toString().trimmed();
}

[[nodiscard]] QString normalizedMetadataText(QString value)
{
    value = value.trimmed();
    const QString upper = value.toUpper();
    if (value.isEmpty() || value == QStringLiteral("-") || upper == QStringLiteral("N/A") ||
        upper == QStringLiteral("NOT AVAILABLE"))
    {
        return {};
    }
    return value;
}

/// The text a decoder stated, or else the text the cards state.
[[nodiscard]] QString statedOrCarded(const std::string& stated, const std::string& carded)
{
    return normalizedMetadataText(QString::fromStdString(stated.empty() ? carded : stated));
}

[[nodiscard]] QString historyExecutedText(const QVariantMap& row)
{
    const QString text = historyRowText(row, QStringLiteral("executedAtText"));
    if (!text.isEmpty())
    {
        return text;
    }

    const QDateTime executedAt = row.value(QStringLiteral("executedAt")).toDateTime();
    return executedAt.isValid() ? executedAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")) : QString();
}

[[nodiscard]] QString formatSecondsText(const double seconds)
{
    if (!std::isfinite(seconds) || seconds <= 0.0)
    {
        return {};
    }

    const int totalSeconds = static_cast<int>(std::round(seconds));
    if (totalSeconds < 60)
    {
        return BlinkComparatorDialog::tr("%1s").arg(totalSeconds);
    }

    const int minutes = totalSeconds / 60;
    const int remainingSeconds = totalSeconds % 60;
    if (minutes < 60)
    {
        return remainingSeconds > 0 ? BlinkComparatorDialog::tr("%1m %2s").arg(minutes).arg(remainingSeconds)
                                    : BlinkComparatorDialog::tr("%1m").arg(minutes);
    }

    const int hours = minutes / 60;
    const int remainingMinutes = minutes % 60;
    return remainingMinutes > 0 ? BlinkComparatorDialog::tr("%1h %2m").arg(hours).arg(remainingMinutes)
                                : BlinkComparatorDialog::tr("%1h").arg(hours);
}

[[nodiscard]] QString formatExposureSecondsText(const double seconds)
{
    if (!std::isfinite(seconds) || seconds <= 0.0)
    {
        return {};
    }

    const double rounded = std::round(seconds);
    if (std::abs(seconds - rounded) < 0.001)
    {
        return BlinkComparatorDialog::tr("%1s").arg(static_cast<int>(rounded));
    }
    return BlinkComparatorDialog::tr("%1s").arg(QString::number(seconds, 'f', 2));
}

[[nodiscard]] Qt::CaseSensitivity pathCaseSensitivity()
{
#if defined(Q_OS_WIN) || defined(Q_OS_DARWIN)
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

[[nodiscard]] QString cleanAbsolutePath(const QString& path)
{
    const QString cleanPath = path.trimmed();
    if (cleanPath.isEmpty())
    {
        return {};
    }

    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(cleanPath).absoluteFilePath()));
}

[[nodiscard]] QString cleanAbsoluteDirectoryPath(const QString& path)
{
    const QString absolutePath = cleanAbsolutePath(path);
    return absolutePath.isEmpty() ? QString() : QFileInfo(absolutePath).absolutePath();
}

[[nodiscard]] bool isPathUnderDirectory(const QString& absolutePath, const QString& directoryPath)
{
    if (absolutePath.isEmpty() || directoryPath.isEmpty())
    {
        return false;
    }

    const Qt::CaseSensitivity sensitivity = pathCaseSensitivity();
    if (absolutePath.compare(directoryPath, sensitivity) == 0)
    {
        return true;
    }

    const QString directoryPrefix =
        directoryPath.endsWith(QLatin1Char('/')) ? directoryPath : directoryPath + QLatin1Char('/');
    return absolutePath.startsWith(directoryPrefix, sensitivity);
}

[[nodiscard]] QString parentDirectoryPath(const QString& directoryPath)
{
    const int lastSeparator = directoryPath.lastIndexOf(QLatin1Char('/'));
    if (lastSeparator <= 0)
    {
        return {};
    }

    const QString parent = directoryPath.left(lastSeparator);
    return parent.endsWith(QLatin1Char(':')) ? QString() : parent;
}

[[nodiscard]] QString commonDisplayRootForPaths(const QStringList& paths)
{
    QString root;
    for (const QString& path : paths)
    {
        const QString absolutePath = cleanAbsolutePath(path);
        if (absolutePath.isEmpty())
        {
            continue;
        }

        if (root.isEmpty())
        {
            root = cleanAbsoluteDirectoryPath(path);
        }

        while (!root.isEmpty() && !isPathUnderDirectory(absolutePath, root))
        {
            root = parentDirectoryPath(root);
        }

        if (root.isEmpty())
        {
            return {};
        }
    }

    return root;
}

[[nodiscard]] QString relativeDisplayPath(const QString& path, const QStringList& allPaths)
{
    const QString absolutePath = cleanAbsolutePath(path);
    const QString root = commonDisplayRootForPaths(allPaths);
    if (!absolutePath.isEmpty() && !root.isEmpty() && isPathUnderDirectory(absolutePath, root))
    {
        const QString relativePath =
            QDir::cleanPath(QDir::fromNativeSeparators(QDir(root).relativeFilePath(absolutePath)));
        if (!relativePath.isEmpty() && !relativePath.startsWith(QStringLiteral("..")) &&
            !QDir::isAbsolutePath(relativePath))
        {
            return relativePath;
        }
    }

    const QString filename = QFileInfo(path.trimmed()).fileName();
    return filename.isEmpty() ? path.trimmed() : filename;
}

[[nodiscard]] QString historyIntegrationTimeText(const QVariantMap& row)
{
    const QString text = historyRowText(row, QStringLiteral("integrationTimeText"));
    if (!text.isEmpty())
    {
        return text;
    }

    bool ok = false;
    const double integrationSeconds = row.value(QStringLiteral("integrationSeconds")).toDouble(&ok);
    return ok ? formatSecondsText(integrationSeconds) : QString();
}

[[nodiscard]] QString historyFrameCountText(const QVariantMap& row)
{
    const QVariant frameCount = row.value(QStringLiteral("frameCount"));
    if (!frameCount.isValid() || frameCount.toInt() <= 0)
    {
        return {};
    }
    return QString::number(frameCount.toInt());
}

[[nodiscard]] QTableWidgetItem* createHistoryTableItem(const QString& text, const QString& tooltip = {})
{
    auto* item = new QTableWidgetItem(text);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    if (!tooltip.trimmed().isEmpty())
    {
        item->setToolTip(tooltip);
    }
    else if (!text.trimmed().isEmpty())
    {
        item->setToolTip(text);
    }
    return item;
}

[[nodiscard]] QString contextLineKey(const QString& line)
{
    const int separator = line.indexOf(QLatin1Char(':'));
    const QString key = separator >= 0 ? line.left(separator) : line;
    return key.trimmed().toCaseFolded();
}

[[nodiscard]] QString normalizedContextLineValue(const QString& line)
{
    return line.trimmed().simplified().toCaseFolded();
}

[[nodiscard]] QMap<QString, QString> contextLinesByKey(const QString& context)
{
    QMap<QString, QString> lines;
    const QStringList rawLines = context.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& rawLine : rawLines)
    {
        const QString line = rawLine.trimmed();
        const QString key = contextLineKey(line);
        if (!key.isEmpty())
        {
            lines.insert(key, normalizedContextLineValue(line));
        }
    }
    return lines;
}

[[nodiscard]] QSet<QString> differingContextKeys(const QString& leftContext, const QString& rightContext)
{
    const QMap<QString, QString> leftLines = contextLinesByKey(leftContext);
    const QMap<QString, QString> rightLines = contextLinesByKey(rightContext);

    QSet<QString> keys;
    for (auto it = leftLines.cbegin(); it != leftLines.cend(); ++it)
    {
        keys.insert(it.key());
    }
    for (auto it = rightLines.cbegin(); it != rightLines.cend(); ++it)
    {
        keys.insert(it.key());
    }

    QSet<QString> differing;
    for (const QString& key : keys)
    {
        if (leftLines.value(key) != rightLines.value(key))
        {
            differing.insert(key);
        }
    }
    return differing;
}

[[nodiscard]] QString contextDifferenceStyle()
{
    const auto& tokenSet = an::ui::tokens();
    const QColor background = an::ui::compositeOver(an::ui::semanticColor(an::ui::SemanticColorRole::WarningBackground),
                                                    tokenSet.colors.bgCardSecondary);
    const QColor foreground = an::ui::semanticColor(an::ui::SemanticColorRole::WarningText);
    return QStringLiteral("background-color:%1; color:%2;")
        .arg(background.name(QColor::HexRgb), foreground.name(QColor::HexRgb));
}

[[nodiscard]] QString formatContextForDisplay(const QString& context, const QSet<QString>& highlightedKeys)
{
    QStringList lines;
    const QStringList rawLines = context.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    lines.reserve(rawLines.size());
    const QString differenceStyle = highlightedKeys.isEmpty() ? QString() : contextDifferenceStyle();

    for (const QString& rawLine : rawLines)
    {
        const QString line = rawLine.trimmed();
        QString escaped = line.toHtmlEscaped();
        if (highlightedKeys.contains(contextLineKey(line)))
        {
            escaped = QStringLiteral("<span style=\"%1\">%2</span>").arg(differenceStyle, escaped);
        }
        lines << escaped;
    }

    return lines.join(QStringLiteral("<br/>"));
}

[[nodiscard]] QString windowSettingsGroup()
{
    return QStringLiteral("ImageIntegration/BlinkComparatorDialog");
}

[[nodiscard]] QSettings windowSettings()
{
    return an::ui::applicationSettings();
}

} // namespace

BlinkComparatorDialog::BlinkComparatorDialog(const QStringList& files, QWidget* parent, bool enableImageLoading)
    : BlinkComparatorDialog(files, {}, {}, parent, enableImageLoading)
{
}

BlinkComparatorDialog::BlinkComparatorDialog(const QStringList& files, const QStringList& labels, QWidget* parent,
                                             bool enableImageLoading)
    : BlinkComparatorDialog(files, labels, {}, parent, enableImageLoading)
{
}

BlinkComparatorDialog::BlinkComparatorDialog(const QStringList& files, const QStringList& labels,
                                             const QStringList& contexts, QWidget* parent, bool enableImageLoading)
    : QDialog(parent), m_files(files), m_labels(labels), m_contexts(contexts), m_enableImageLoading(enableImageLoading)
{
    normalizeLabels();
    normalizeContexts();

    setWindowTitle(tr("Blink Comparator"));
    setWindowModality(Qt::WindowModal);
    setModal(true);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    setSizeGripEnabled(true);
    const auto& spacing = an::ui::tokens().spacing;
    setMinimumSize(spacing.xl * 26, spacing.xl * 16);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(spacing.lg, spacing.lg, spacing.lg, spacing.lg);
    mainLayout->setSpacing(spacing.md);

    auto* imageLabelLayout = new QHBoxLayout();
    imageLabelLayout->setSpacing(spacing.sm);

    const auto applyImageLabelStyle = [](QLabel* label) {
        if (!label)
        {
            return;
        }

        const auto& tokenSet = an::ui::tokens();
        label->setFont(tokenSet.typography.caption);
        QPalette palette = label->palette();
        palette.setColor(QPalette::WindowText, tokenSet.colors.textSecondary);
        label->setPalette(palette);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        label->setWordWrap(true);
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    };

    m_leftImageLabel = new QLabel(this);
    m_leftImageLabel->setObjectName(QStringLiteral("leftImageLabel"));
    applyImageLabelStyle(m_leftImageLabel);
    imageLabelLayout->addWidget(m_leftImageLabel, 1);

    m_rightImageLabel = new QLabel(this);
    m_rightImageLabel->setObjectName(QStringLiteral("rightImageLabel"));
    applyImageLabelStyle(m_rightImageLabel);
    m_rightImageLabel->setVisible(false);
    imageLabelLayout->addWidget(m_rightImageLabel, 1);

    mainLayout->addLayout(imageLabelLayout);

    const auto applyContextLabelStyle = [](QLabel* label) {
        if (!label)
        {
            return;
        }

        const auto& tokenSet = an::ui::tokens();
        label->setFont(tokenSet.typography.caption);
        QPalette palette = label->palette();
        palette.setColor(QPalette::WindowText, tokenSet.colors.textSecondary);
        label->setPalette(palette);
        label->setTextFormat(Qt::RichText);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        label->setWordWrap(true);
        label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    };

    auto* contextLayout = new QHBoxLayout();
    contextLayout->setSpacing(spacing.sm);

    const auto createContextCard = [this, &applyContextLabelStyle](const QString& objectName, const QString& title,
                                                                   const QString& labelObjectName, QLabel** outLabel) {
        auto* card = new an::ui::AnCardWidget(title, this);
        card->setObjectName(objectName);
        card->setProperty("anCardCompact", true);

        auto* bodyLayout = new QVBoxLayout();
        auto* label = new QLabel(card->contentWidget());
        label->setObjectName(labelObjectName);
        applyContextLabelStyle(label);
        bodyLayout->addWidget(label);
        card->setContentLayout(bodyLayout);
        card->refreshTheme();
        card->setVisible(false);

        if (outLabel)
        {
            *outLabel = label;
        }
        return card;
    };

    m_leftContextCard = createContextCard(QStringLiteral("leftContextCard"), tr("Left Master"),
                                          QStringLiteral("leftContextLabel"), &m_leftContextLabel);
    contextLayout->addWidget(m_leftContextCard, 1);

    m_rightContextCard = createContextCard(QStringLiteral("rightContextCard"), tr("Right Master"),
                                           QStringLiteral("rightContextLabel"), &m_rightContextLabel);
    contextLayout->addWidget(m_rightContextCard, 1);

    mainLayout->addLayout(contextLayout);

    m_comparisonView = new an::ui::ComparisonViewWidget(this);
    m_comparisonView->setObjectName(QStringLiteral("comparisonView"));
    m_comparisonView->setMinimumSize(spacing.xl * 16, spacing.xl * 10);
    mainLayout->addWidget(m_comparisonView, 1);

    // Playback Controls
    auto* controlsLayout = new QHBoxLayout();
    controlsLayout->setSpacing(spacing.sm);

    m_prevButton = new QPushButton(tr("Previous"), this);
    m_prevButton->setObjectName(QStringLiteral("prevButton"));
    m_prevButton->setToolTip(tr("Previous image (Left Arrow)"));
    connect(m_prevButton, &QPushButton::clicked, this, &BlinkComparatorDialog::showPreviousImage);
    controlsLayout->addWidget(m_prevButton);

    m_playButton = new QPushButton(tr("Play"), this);
    m_playButton->setObjectName(QStringLiteral("playButton"));
    m_playButton->setToolTip(tr("Play/Pause (Space)"));
    connect(m_playButton, &QPushButton::clicked, this, &BlinkComparatorDialog::togglePlayPause);
    controlsLayout->addWidget(m_playButton);

    m_nextButton = new QPushButton(tr("Next"), this);
    m_nextButton->setObjectName(QStringLiteral("nextButton"));
    m_nextButton->setToolTip(tr("Next image (Right Arrow)"));
    connect(m_nextButton, &QPushButton::clicked, this, &BlinkComparatorDialog::showNextImage);
    controlsLayout->addWidget(m_nextButton);

    controlsLayout->addSpacing(spacing.lg);

    auto* slowLabel = new QLabel(tr("Slow"), this);
    controlsLayout->addWidget(slowLabel);

    m_speedSlider = new QSlider(Qt::Horizontal, this);
    m_speedSlider->setObjectName(QStringLiteral("speedSlider"));
    m_speedSlider->setRange(0, 100); // Use normalized 0-100 scale
    m_speedSlider->setValue(intervalToSliderValue(m_intervalMs));
    m_speedSlider->setToolTip(tr("Adjust playback speed"));
    connect(m_speedSlider, &QSlider::valueChanged, this, &BlinkComparatorDialog::onSpeedChanged);
    controlsLayout->addWidget(m_speedSlider);

    auto* fastLabel = new QLabel(tr("Fast"), this);
    controlsLayout->addWidget(fastLabel);

    controlsLayout->addSpacing(spacing.lg);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("statusLabel"));
    controlsLayout->addWidget(m_statusLabel);

    mainLayout->addLayout(controlsLayout);

    // Thumbnail Strip
    auto* thumbnailList = new QListWidget(this);
    m_thumbnailStrip = thumbnailList;
    thumbnailList->setObjectName(QStringLiteral("thumbnailStrip"));
    thumbnailList->setMinimumHeight(spacing.xl * 4);
    thumbnailList->setFlow(QListWidget::LeftToRight);
    thumbnailList->setViewMode(QListWidget::IconMode);
    thumbnailList->setSpacing(spacing.xs);
    refreshThumbnailLabels();
    mainLayout->addWidget(m_thumbnailStrip);

    m_historyTable = new QTableWidget(this);
    m_historyTable->setObjectName(QStringLiteral("historyTable"));
    m_historyTable->setColumnCount(HistoryColumnCount);
    m_historyTable->setHorizontalHeaderLabels({tr("Run"), tr("Executed"), tr("Integration Time"), tr("Frames"),
                                               tr("Filter"), tr("Object"), tr("Resolution"), tr("Path")});
    m_historyTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_historyTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_historyTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_historyTable->setAlternatingRowColors(true);
    m_historyTable->setMinimumHeight(spacing.xl * 5);
    m_historyTable->verticalHeader()->setVisible(false);
    m_historyTable->horizontalHeader()->setStretchLastSection(true);
    m_historyTable->setToolTip(tr("Select a history row to compare it with the pinned reference."));
    m_historyTable->setVisible(false);
    m_historyTable->viewport()->installEventFilter(this);
    connect(m_historyTable, &QTableWidget::currentCellChanged, this, [this](int currentRow, int, int, int) {
        if (m_syncingHistoryTable || currentRow < 0 || currentRow >= m_files.size())
        {
            return;
        }

        if (usesHistoryComparisonSelection())
        {
            if (currentRow == m_historyReferenceIndex)
            {
                QTimer::singleShot(0, this, [this]() { updateHistoryTableSelection(); });
                return;
            }
            setHistoryTargetIndex(currentRow);
            return;
        }

        QTimer::singleShot(0, this, [this]() { updateHistoryTableSelection(); });
    });
    connect(m_historyTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (usesHistoryComparisonSelection())
        {
            setHistoryTargetIndex(row);
            return;
        }
        setCurrentFrameIndexFromTable(row);
    });
    mainLayout->addWidget(m_historyTable);

    m_historyRows = buildSourceFrameRows();
    populateHistoryTable();
    if (m_historyTable)
    {
        m_historyTable->setVisible(!m_historyRows.isEmpty());
    }

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &BlinkComparatorDialog::showNextImage);

    restoreWindowSize();

    if (!m_files.isEmpty())
    {
        updateImage();
    }
    if (!isVisible())
    {
        restoreWindowSize();
    }
}

BlinkComparatorDialog::~BlinkComparatorDialog()
{
    rememberWindowSize();

    QCoreApplication::removePostedEvents(this, QEvent::MetaCall);

    if (m_timer)
    {
        m_timer->stop();
    }
}

// --- Comparator methods ---

void BlinkComparatorDialog::setMode(BlinkMode mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;

    an::ui::ComparisonViewMode comparisonMode = an::ui::ComparisonViewMode::Single;
    switch (m_mode)
    {
    case BlinkMode::Single:
        comparisonMode = an::ui::ComparisonViewMode::Single;
        break;
    case BlinkMode::SideBySide:
        comparisonMode = an::ui::ComparisonViewMode::SideBySide;
        break;
    case BlinkMode::Overlay:
        comparisonMode = an::ui::ComparisonViewMode::Overlay;
        break;
    case BlinkMode::Difference:
        comparisonMode = an::ui::ComparisonViewMode::Difference;
        break;
    }

    if (m_comparisonView)
    {
        m_comparisonView->setMode(comparisonMode);
    }
    updateImage();
    if (!isVisible() && usesHistoryComparisonSelection())
    {
        restoreWindowSize();
    }
}

ImageViewControl* BlinkComparatorDialog::leftPanel() const
{
    return m_comparisonView ? m_comparisonView->leftControl() : nullptr;
}

ImageViewControl* BlinkComparatorDialog::rightPanel() const
{
    return m_comparisonView ? m_comparisonView->rightControl() : nullptr;
}

an::ui::ComparisonViewWidget* BlinkComparatorDialog::comparisonView() const
{
    return m_comparisonView;
}

QString BlinkComparatorDialog::canonicalPathForIndex(int index) const
{
    if (index < 0 || index >= m_files.size())
    {
        return {};
    }

    const QString path = m_files.at(index);
    return path.trimmed().isEmpty() ? QString() : FrameId(path).canonicalPath();
}

QString BlinkComparatorDialog::currentCanonicalPath() const
{
    return canonicalPathForIndex(m_currentIndex);
}

QString BlinkComparatorDialog::displayLabelForIndex(int index) const
{
    if (index < 0 || index >= m_files.size())
    {
        return {};
    }

    if (index < m_labels.size())
    {
        const QString label = m_labels.at(index).trimmed();
        if (!label.isEmpty())
        {
            return label;
        }
    }

    return QFileInfo(m_files.at(index)).fileName();
}

QString BlinkComparatorDialog::displayHeaderForIndex(int index) const
{
    const QString label = displayLabelForIndex(index);
    if (index < 0 || index >= m_files.size() || m_historyRows.isEmpty())
    {
        return label;
    }

    const QString path = relativeDisplayPath(m_files.at(index), m_files);
    if (path.trimmed().isEmpty() || label == path)
    {
        return label;
    }

    return QStringLiteral("%1\n%2").arg(label, path);
}

QString BlinkComparatorDialog::displayContextForIndex(int index) const
{
    if (index < 0 || index >= m_contexts.size())
    {
        return {};
    }

    return m_contexts.at(index).trimmed();
}

bool BlinkComparatorDialog::usesHistoryComparisonSelection() const
{
    return m_showingExecutionHistoryRows && !m_historyRows.isEmpty() && m_historyReferenceIndex >= 0 &&
           m_historyReferenceIndex < m_files.size();
}

int BlinkComparatorDialog::activeRightIndex() const
{
    if (usesHistoryComparisonSelection())
    {
        return (m_historyTargetIndex >= 0 && m_historyTargetIndex < m_files.size() &&
                m_historyTargetIndex != m_historyReferenceIndex)
                   ? m_historyTargetIndex
                   : -1;
    }

    if (m_files.size() <= 1)
    {
        return -1;
    }
    return (m_currentIndex + 1) % m_files.size();
}

QVariantList BlinkComparatorDialog::buildSourceFrameRows() const
{
    QVariantList rows;
    rows.reserve(m_files.size());

    // The headers are read on the GUI thread, so the operation is taken only when the session has a thread free now;
    // a busy session leaves the rows with their names and paths alone.
    std::optional<an::Result<an::consumer::OperationScope>> scope;
    if (const an::consumer::EngineSession* session = ImageIntegration::detail::applicationEngineSession())
    {
        an::Result<an::consumer::OperationScope> admitted = session->tryAdmit(
            an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch, .maxConcurrency = 1u});
        if (admitted.hasValue())
        {
            scope.emplace(std::move(admitted));
        }
        else
        {
            LOG_DEBUG("Blink comparator reads no headers: {}", admitted.status().message());
        }
    }

    for (int index = 0; index < m_files.size(); ++index)
    {
        const QString path = m_files.at(index).trimmed();
        const QFileInfo info(path);

        QVariantMap row;
        row.insert(QStringLiteral("label"), displayLabelForIndex(index));
        row.insert(QStringLiteral("filename"),
                   info.fileName().isEmpty() ? displayLabelForIndex(index) : info.fileName());
        row.insert(QStringLiteral("path"), path);

        if (!info.exists() || !info.isFile() || info.size() <= 0 ||
            !ImageIntegration::detail::isReadableImagePath(path) || !scope.has_value())
        {
            rows.append(row);
            continue;
        }

        const an::Result<an::CodecImageInfo> probed =
            an::probeImage((*scope)->engine(), (*scope)->execution(),
                           an::CodecReadRequest{.path = path.toUtf8().toStdString(), .validatePayload = false});
        if (!probed.hasValue())
        {
            LOG_DEBUG("Blink comparator could not read the header of {}: {}", path.toStdString(),
                      probed.status().message());
            rows.append(row);
            continue;
        }

        const std::uint32_t width = probed->descriptor.extent().width();
        const std::uint32_t height = probed->descriptor.extent().height();
        if (width > 0 && height > 0)
        {
            row.insert(
                QStringLiteral("resolution"),
                QStringLiteral("%1x%2").arg(static_cast<qulonglong>(width)).arg(static_cast<qulonglong>(height)));
        }

        // A camera RAW probe states the typed record and no card; FITS, XISF and TIFF state cards. A figure is read
        // from the record the decoder stated, and from the cards where the record says nothing.
        const an::AcquisitionMetadata& stated = probed->metadata.acquisition();
        const an::AcquisitionProjection projected = an::projectAcquisition(probed->metadata.codec());
        const an::AcquisitionMetadataRequest& carded = projected.acquisition;

        const std::optional<double> exposure =
            stated.exposureSeconds().has_value() ? stated.exposureSeconds() : carded.exposureSeconds;
        row.insert(QStringLiteral("exposure"), exposure.has_value() ? formatExposureSecondsText(*exposure) : QString());

        const std::optional<double> temperature = stated.sensorTemperatureCelsius().has_value()
                                                      ? stated.sensorTemperatureCelsius()
                                                      : carded.sensorTemperatureCelsius;
        row.insert(QStringLiteral("temperature"), temperature.has_value() && std::isfinite(*temperature)
                                                      ? tr("%1 C").arg(QString::number(*temperature, 'f', 1))
                                                      : QString());

        row.insert(QStringLiteral("filter"), statedOrCarded(stated.filterName(), carded.filterName));
        row.insert(QStringLiteral("object"), statedOrCarded(stated.objectName(), carded.objectName));
        row.insert(QStringLiteral("camera"), statedOrCarded(stated.instrumentName(), carded.instrumentName));

        rows.append(row);
    }

    return rows;
}

void BlinkComparatorDialog::normalizeLabels()
{
    while (m_labels.size() > m_files.size())
    {
        m_labels.removeLast();
    }

    for (int i = 0; i < m_files.size(); ++i)
    {
        const QString fallback = QFileInfo(m_files.at(i)).fileName();
        if (i >= m_labels.size())
        {
            m_labels.append(fallback);
        }
        else if (m_labels[i].trimmed().isEmpty())
        {
            m_labels[i] = fallback;
        }
    }
}

void BlinkComparatorDialog::normalizeContexts()
{
    while (m_contexts.size() > m_files.size())
    {
        m_contexts.removeLast();
    }

    while (m_contexts.size() < m_files.size())
    {
        m_contexts.append(QString());
    }

    for (QString& context : m_contexts)
    {
        context = context.trimmed();
    }
}

void BlinkComparatorDialog::refreshThumbnailLabels()
{
    auto* thumbnailList = qobject_cast<QListWidget*>(m_thumbnailStrip);
    if (!thumbnailList)
    {
        return;
    }

    thumbnailList->clear();
    for (int i = 0; i < m_files.size(); ++i)
    {
        auto* item = new QListWidgetItem(displayLabelForIndex(i), thumbnailList);
        item->setToolTip(m_files.at(i));
    }
}

void BlinkComparatorDialog::updateImageLabels(int rightIndex)
{
    const QString leftLabel = displayLabelForIndex(m_currentIndex);
    const bool compareMode = m_mode != BlinkMode::Single && rightIndex >= 0 && rightIndex < m_files.size();
    const QString rightLabel = compareMode ? displayLabelForIndex(rightIndex) : QString();
    const bool historyHeaders = m_showingExecutionHistoryRows && !m_historyRows.isEmpty();

    if (m_leftImageLabel)
    {
        m_leftImageLabel->setText(historyHeaders ? displayHeaderForIndex(m_currentIndex) : leftLabel);
        m_leftImageLabel->setToolTip(m_currentIndex >= 0 && m_currentIndex < m_files.size() ? m_files.at(m_currentIndex)
                                                                                            : QString());
    }

    if (m_rightImageLabel)
    {
        m_rightImageLabel->setVisible(compareMode);
        m_rightImageLabel->setText(historyHeaders ? displayHeaderForIndex(rightIndex) : rightLabel);
        m_rightImageLabel->setToolTip(compareMode ? m_files.at(rightIndex) : QString());
    }

    if (m_comparisonView && m_comparisonView->leftControl())
    {
        m_comparisonView->leftControl()->setUniqueName(leftLabel.toStdString());
    }
    if (m_comparisonView && m_comparisonView->rightControl())
    {
        m_comparisonView->rightControl()->setUniqueName((compareMode ? rightLabel : QString()).toStdString());
    }

    updateContextCards(compareMode ? rightIndex : -1);
}

void BlinkComparatorDialog::updateContextCards(int rightIndex)
{
    const QString leftContext = displayContextForIndex(m_currentIndex);
    const bool compareMode = m_mode != BlinkMode::Single && rightIndex >= 0 && rightIndex < m_files.size();
    const QString rightContext = compareMode ? displayContextForIndex(rightIndex) : QString();
    const QSet<QString> highlightedKeys =
        compareMode ? differingContextKeys(leftContext, rightContext) : QSet<QString>();

    if (m_leftContextCard)
    {
        m_leftContextCard->setVisible(!leftContext.isEmpty());
    }
    if (m_leftContextLabel)
    {
        m_leftContextLabel->setText(formatContextForDisplay(leftContext, highlightedKeys));
        m_leftContextLabel->setToolTip(leftContext);
    }

    if (m_rightContextCard)
    {
        m_rightContextCard->setVisible(compareMode && !rightContext.isEmpty());
    }
    if (m_rightContextLabel)
    {
        m_rightContextLabel->setText(formatContextForDisplay(rightContext, highlightedKeys));
        m_rightContextLabel->setToolTip(rightContext);
    }
}

void BlinkComparatorDialog::restoreWindowSize()
{
    const auto& spacing = an::ui::tokens().spacing;
    const QSize defaultSize(spacing.xl * 40, spacing.xl * 30);

    QSettings settings = windowSettings();
    settings.beginGroup(windowSettingsGroup());
    const QSize savedSize = settings.value(QStringLiteral("size")).toSize();
    settings.endGroup();

    resize((savedSize.isValid() ? savedSize : defaultSize).expandedTo(minimumSize()));
}

void BlinkComparatorDialog::rememberWindowSize() const
{
    if (isMinimized())
    {
        return;
    }

    const QSize windowSize = isMaximized() && normalGeometry().isValid()
                                 ? normalGeometry().size()
                                 : (m_lastVisibleWindowSize.isValid() ? m_lastVisibleWindowSize : size());
    if (!windowSize.isValid() || windowSize.isEmpty())
    {
        return;
    }

    QSettings settings = windowSettings();
    settings.beginGroup(windowSettingsGroup());
    settings.setValue(QStringLiteral("size"), windowSize);
    settings.endGroup();
    settings.sync();
}

void BlinkComparatorDialog::setOpacity(double opacity)
{
    m_overlayOpacity = std::clamp(opacity, 0.0, 1.0);
    if (m_comparisonView)
    {
        m_comparisonView->setOverlayOpacity(m_overlayOpacity);
    }
}

void BlinkComparatorDialog::excludeCurrentFrame()
{
    const QString canonicalPath = currentCanonicalPath();
    if (!canonicalPath.isEmpty())
    {
        m_excludedPaths.insert(canonicalPath);
    }
}

bool BlinkComparatorDialog::isExcluded(int index) const
{
    const QString canonicalPath = canonicalPathForIndex(index);
    return !canonicalPath.isEmpty() && m_excludedPaths.contains(canonicalPath);
}

bool BlinkComparatorDialog::supportsSortCriteria(SortCriteria criteria) const
{
    return criteria == SortCriteria::Filename;
}

void BlinkComparatorDialog::sortBy(SortCriteria criteria, Qt::SortOrder order)
{
    if (!supportsSortCriteria(criteria))
    {
        LOG_DEBUG("BlinkComparatorDialog::sortBy: requested sort criterion is unavailable without frame metrics");
        return;
    }

    const QString currentPath = currentCanonicalPath();
    const QString referencePath =
        usesHistoryComparisonSelection() ? canonicalPathForIndex(m_historyReferenceIndex) : QString();
    const QString targetPath =
        usesHistoryComparisonSelection() ? canonicalPathForIndex(m_historyTargetIndex) : QString();

    struct SortEntry
    {
        QString path;
        QString label;
        QString context;
        QVariant historyRow;
    };

    std::vector<SortEntry> entries;
    entries.reserve(static_cast<size_t>(m_files.size()));
    for (int i = 0; i < m_files.size(); ++i)
    {
        entries.push_back({m_files.at(i), displayLabelForIndex(i), displayContextForIndex(i),
                           i < m_historyRows.size() ? m_historyRows.at(i) : QVariant()});
    }

    std::sort(entries.begin(), entries.end(), [order](const auto& a, const auto& b) {
        const QString nameA = QFileInfo(a.path).fileName();
        const QString nameB = QFileInfo(b.path).fileName();
        return (order == Qt::AscendingOrder) ? (nameA < nameB) : (nameA > nameB);
    });

    m_files.clear();
    m_labels.clear();
    m_contexts.clear();
    m_historyRows.clear();
    for (const auto& entry : entries)
    {
        m_files.append(entry.path);
        m_labels.append(entry.label);
        m_contexts.append(entry.context);
        if (entry.historyRow.isValid())
        {
            m_historyRows.append(entry.historyRow);
        }
    }

    m_currentIndex = 0;
    m_historyReferenceIndex = referencePath.isEmpty() ? -1 : 0;
    m_historyTargetIndex = targetPath.isEmpty() ? -1 : 0;
    if (!currentPath.isEmpty())
    {
        for (int i = 0; i < m_files.size(); ++i)
        {
            if (canonicalPathForIndex(i) == currentPath)
            {
                m_currentIndex = i;
                break;
            }
        }
    }
    if (!referencePath.isEmpty())
    {
        for (int i = 0; i < m_files.size(); ++i)
        {
            if (canonicalPathForIndex(i) == referencePath)
            {
                m_historyReferenceIndex = i;
                m_currentIndex = i;
                break;
            }
        }
    }
    if (!targetPath.isEmpty())
    {
        for (int i = 0; i < m_files.size(); ++i)
        {
            if (canonicalPathForIndex(i) == targetPath)
            {
                m_historyTargetIndex = i;
                break;
            }
        }
    }
    if (usesHistoryComparisonSelection() && (m_historyTargetIndex < 0 || m_historyTargetIndex >= m_files.size() ||
                                             m_historyTargetIndex == m_historyReferenceIndex))
    {
        for (int i = 0; i < m_files.size(); ++i)
        {
            if (i != m_historyReferenceIndex)
            {
                m_historyTargetIndex = i;
                break;
            }
        }
    }

    refreshThumbnailLabels();
    populateHistoryTable();
    updateImage();
}

int BlinkComparatorDialog::ratingFor(int index) const
{
    const QString canonicalPath = canonicalPathForIndex(index);
    return canonicalPath.isEmpty() ? 0 : m_ratingsByPath.value(canonicalPath, 0);
}

BlinkComparatorResult BlinkComparatorDialog::comparisonResult() const
{
    BlinkComparatorResult result;
    QSet<QString> seenPaths;

    for (int i = 0; i < m_files.size(); ++i)
    {
        const QString canonicalPath = canonicalPathForIndex(i);
        if (canonicalPath.isEmpty() || !m_excludedPaths.contains(canonicalPath) || seenPaths.contains(canonicalPath))
        {
            continue;
        }

        seenPaths.insert(canonicalPath);
        result.excludedPaths.append(canonicalPath);
    }

    return result;
}

void BlinkComparatorDialog::setHistoryRows(const QVariantList& rows)
{
    m_showingExecutionHistoryRows = !rows.isEmpty();
    m_historyRows = m_showingExecutionHistoryRows ? rows : buildSourceFrameRows();
    if (!m_showingExecutionHistoryRows || m_historyRows.isEmpty() || m_files.isEmpty())
    {
        m_historyReferenceIndex = -1;
        m_historyTargetIndex = -1;
    }
    else
    {
        m_historyReferenceIndex = 0;
        m_currentIndex = m_historyReferenceIndex;
        m_historyTargetIndex = -1;
        for (int i = 0; i < m_files.size(); ++i)
        {
            if (i != m_historyReferenceIndex)
            {
                m_historyTargetIndex = i;
                break;
            }
        }
    }

    populateHistoryTable();

    const bool showHistoryTable = !m_historyRows.isEmpty();
    if (m_thumbnailStrip)
    {
        m_thumbnailStrip->setVisible(!m_showingExecutionHistoryRows);
    }
    if (m_historyTable)
    {
        m_historyTable->setVisible(showHistoryTable);
    }

    updateHistoryTableSelection();
    if (!m_files.isEmpty())
    {
        updateImage();
    }
    if (!isVisible())
    {
        restoreWindowSize();
    }
}

void BlinkComparatorDialog::populateHistoryTable()
{
    if (!m_historyTable)
    {
        return;
    }

    QSignalBlocker blocker(m_historyTable);
    m_historyTable->clearContents();
    m_historyTable->setRowCount(m_historyRows.size());

    if (m_showingExecutionHistoryRows)
    {
        m_historyTable->setColumnCount(HistoryColumnCount);
        m_historyTable->setHorizontalHeaderLabels({tr("Run"), tr("Executed"), tr("Integration Time"), tr("Frames"),
                                                   tr("Filter"), tr("Object"), tr("Resolution"), tr("Path")});
    }
    else
    {
        m_historyTable->setColumnCount(SourceFrameColumnCount);
        m_historyTable->setHorizontalHeaderLabels({tr("File"), tr("Filter"), tr("Resolution"), tr("Exposure"),
                                                   tr("Temp"), tr("Object"), tr("Camera"), tr("Path")});
    }

    for (int rowIndex = 0; rowIndex < m_historyRows.size(); ++rowIndex)
    {
        QVariantMap row = m_historyRows.at(rowIndex).toMap();
        if (row.value(QStringLiteral("label")).toString().trimmed().isEmpty() && rowIndex < m_labels.size())
        {
            row.insert(QStringLiteral("label"), displayLabelForIndex(rowIndex));
        }
        if (row.value(QStringLiteral("path")).toString().trimmed().isEmpty() && rowIndex < m_files.size())
        {
            row.insert(QStringLiteral("path"), m_files.at(rowIndex));
        }

        const QString path = historyRowText(row, QStringLiteral("path"));
        if (m_showingExecutionHistoryRows)
        {
            m_historyTable->setItem(rowIndex, HistoryRunColumn,
                                    createHistoryTableItem(historyRowText(row, QStringLiteral("label")), path));
            m_historyTable->setItem(rowIndex, HistoryExecutedColumn, createHistoryTableItem(historyExecutedText(row)));
            m_historyTable->setItem(rowIndex, HistoryIntegrationTimeColumn,
                                    createHistoryTableItem(historyIntegrationTimeText(row)));
            m_historyTable->setItem(rowIndex, HistoryFramesColumn, createHistoryTableItem(historyFrameCountText(row)));
            m_historyTable->setItem(rowIndex, HistoryFilterColumn,
                                    createHistoryTableItem(historyRowText(row, QStringLiteral("filter"))));
            m_historyTable->setItem(rowIndex, HistoryObjectColumn,
                                    createHistoryTableItem(historyRowText(row, QStringLiteral("object"))));
            m_historyTable->setItem(rowIndex, HistoryResolutionColumn,
                                    createHistoryTableItem(historyRowText(row, QStringLiteral("resolution"))));
            m_historyTable->setItem(rowIndex, HistoryPathColumn, createHistoryTableItem(path, path));
            continue;
        }

        const QString filename = historyRowText(row, QStringLiteral("filename"));
        m_historyTable->setItem(
            rowIndex, SourceFrameFileColumn,
            createHistoryTableItem(filename.isEmpty() ? displayLabelForIndex(rowIndex) : filename, path));
        m_historyTable->setItem(rowIndex, SourceFrameFilterColumn,
                                createHistoryTableItem(historyRowText(row, QStringLiteral("filter"))));
        m_historyTable->setItem(rowIndex, SourceFrameResolutionColumn,
                                createHistoryTableItem(historyRowText(row, QStringLiteral("resolution"))));
        m_historyTable->setItem(rowIndex, SourceFrameExposureColumn,
                                createHistoryTableItem(historyRowText(row, QStringLiteral("exposure"))));
        m_historyTable->setItem(rowIndex, SourceFrameTemperatureColumn,
                                createHistoryTableItem(historyRowText(row, QStringLiteral("temperature"))));
        m_historyTable->setItem(rowIndex, SourceFrameObjectColumn,
                                createHistoryTableItem(historyRowText(row, QStringLiteral("object"))));
        m_historyTable->setItem(rowIndex, SourceFrameCameraColumn,
                                createHistoryTableItem(historyRowText(row, QStringLiteral("camera"))));
        m_historyTable->setItem(rowIndex, SourceFramePathColumn, createHistoryTableItem(path, path));
    }

    m_historyTable->resizeColumnsToContents();
    for (int column = 0; column < m_historyTable->columnCount(); ++column)
    {
        m_historyTable->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    }
    const int stretchColumn =
        m_showingExecutionHistoryRows ? static_cast<int>(HistoryPathColumn) : static_cast<int>(SourceFramePathColumn);
    m_historyTable->horizontalHeader()->setSectionResizeMode(stretchColumn, QHeaderView::Stretch);
    applyHistoryTableHighlights();
}

void BlinkComparatorDialog::updateHistoryTableSelection()
{
    if (!m_historyTable)
    {
        return;
    }

    applyHistoryTableHighlights();

    if (!m_historyTable->isVisible())
    {
        return;
    }

    const int selectedRow = usesHistoryComparisonSelection() ? m_historyTargetIndex : m_currentIndex;
    if (selectedRow < 0 || selectedRow >= m_historyTable->rowCount())
    {
        QSignalBlocker blocker(m_historyTable);
        m_historyTable->clearSelection();
        return;
    }

    QSignalBlocker blocker(m_historyTable);
    QScopedValueRollback<bool> guard(m_syncingHistoryTable, true);
    m_historyTable->selectRow(selectedRow);
    m_historyTable->setCurrentCell(selectedRow, 0);
    applyHistoryTableHighlights();
}

void BlinkComparatorDialog::applyHistoryTableHighlights()
{
    if (!m_historyTable)
    {
        return;
    }

    const auto& tokenSet = an::ui::tokens();
    QColor targetBackground = tokenSet.colors.accentPrimary;
    targetBackground.setAlpha(an::ui::isDarkTheme() ? 86 : 56);
    targetBackground = an::ui::compositeOver(targetBackground, m_historyTable->palette().color(QPalette::Base));
    const QColor targetText = an::ui::bestContrastTextColor(
        targetBackground, tokenSet.colors.textAccent, tokenSet.colors.textOnAccent, tokenSet.colors.textPrimary, 4.5);

    for (int row = 0; row < m_historyTable->rowCount(); ++row)
    {
        const bool isReference = usesHistoryComparisonSelection() && row == m_historyReferenceIndex;
        const bool isTarget = usesHistoryComparisonSelection() && row == m_historyTargetIndex;
        const bool isCurrentFrame = !usesHistoryComparisonSelection() && row == m_currentIndex;

        for (int column = 0; column < m_historyTable->columnCount(); ++column)
        {
            QTableWidgetItem* item = m_historyTable->item(row, column);
            if (!item)
            {
                continue;
            }

            Qt::ItemFlags flags = Qt::ItemIsEnabled;
            if (!isReference)
            {
                flags |= Qt::ItemIsSelectable;
            }
            item->setFlags(flags);
            item->setData(Qt::BackgroundRole, QVariant());
            item->setData(Qt::ForegroundRole, QVariant());

            if (isReference)
            {
                item->setBackground(tokenSet.colors.bgSurface);
                item->setForeground(tokenSet.colors.textTertiary);
            }
            else if (isTarget || isCurrentFrame)
            {
                item->setBackground(targetBackground);
                item->setForeground(targetText);
            }
        }
    }
}

QWidget* BlinkComparatorDialog::thumbnailStrip() const
{
    return m_thumbnailStrip;
}

// --- End of comparator methods ---

bool BlinkComparatorDialog::isPlaying() const
{
    return m_timer && m_timer->isActive();
}

bool BlinkComparatorDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (m_historyTable && watched == m_historyTable->viewport() && event->type() == QEvent::MouseButtonDblClick &&
        !m_historyRows.isEmpty())
    {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton)
        {
            const int row = m_historyTable->rowAt(mouseEvent->pos().y());
            if (usesHistoryComparisonSelection())
            {
                setHistoryTargetIndex(row);
            }
            else
            {
                setCurrentFrameIndexFromTable(row);
            }
            return true;
        }
    }

    return QDialog::eventFilter(watched, event);
}

void BlinkComparatorDialog::closeEvent(QCloseEvent* event)
{
    rememberWindowSize();
    QDialog::closeEvent(event);
}

void BlinkComparatorDialog::done(int result)
{
    rememberWindowSize();
    QDialog::done(result);
}

void BlinkComparatorDialog::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    if (isVisible() && !isMinimized() && event && event->size().isValid() && !event->size().isEmpty())
    {
        m_lastVisibleWindowSize = event->size();
    }
}

void BlinkComparatorDialog::keyPressEvent(QKeyEvent* event)
{
    switch (event->key())
    {
    case Qt::Key_Space:
        togglePlayPause();
        event->accept();
        return;
    case Qt::Key_Left:
        showPreviousImage();
        event->accept();
        return;
    case Qt::Key_Right:
        showNextImage();
        event->accept();
        return;
    case Qt::Key_Escape:
        close();
        event->accept();
        return;
    case Qt::Key_X:
        excludeCurrentFrame();
        event->accept();
        return;
    }

    if (event->key() >= Qt::Key_1 && event->key() <= Qt::Key_9)
    {
        int rating = event->key() - Qt::Key_0;
        const QString canonicalPath = currentCanonicalPath();
        if (!canonicalPath.isEmpty())
        {
            m_ratingsByPath.insert(canonicalPath, rating);
        }
        event->accept();
        return;
    }

    QDialog::keyPressEvent(event);
}

void BlinkComparatorDialog::showNextImage()
{
    if (m_files.isEmpty())
        return;
    if (usesHistoryComparisonSelection())
    {
        advanceHistoryTarget(1);
        return;
    }
    m_currentIndex = (m_currentIndex + 1) % m_files.size();
    updateImage();
}

void BlinkComparatorDialog::showPreviousImage()
{
    if (m_files.isEmpty())
        return;
    if (usesHistoryComparisonSelection())
    {
        advanceHistoryTarget(-1);
        return;
    }
    m_currentIndex = (m_currentIndex - 1 + m_files.size()) % m_files.size();
    updateImage();
}

void BlinkComparatorDialog::advanceHistoryTarget(int step)
{
    if (!usesHistoryComparisonSelection() || m_files.size() <= 1 || step == 0)
    {
        return;
    }

    int nextIndex = (m_historyTargetIndex >= 0 && m_historyTargetIndex < m_files.size()) ? m_historyTargetIndex
                                                                                         : m_historyReferenceIndex;
    for (int attempts = 0; attempts < m_files.size(); ++attempts)
    {
        nextIndex = (nextIndex + step + m_files.size()) % m_files.size();
        if (nextIndex != m_historyReferenceIndex)
        {
            setHistoryTargetIndex(nextIndex);
            return;
        }
    }
}

void BlinkComparatorDialog::setHistoryTargetIndex(int index)
{
    if (!usesHistoryComparisonSelection() || index < 0 || index >= m_files.size() || index == m_historyReferenceIndex ||
        index == m_historyTargetIndex)
    {
        return;
    }

    m_historyTargetIndex = index;
    updateImage();
}

void BlinkComparatorDialog::setCurrentFrameIndexFromTable(int index)
{
    if (usesHistoryComparisonSelection() || index < 0 || index >= m_files.size() || index == m_currentIndex)
    {
        return;
    }

    m_currentIndex = index;
    updateImage();
}

void BlinkComparatorDialog::updateImage()
{
    if (m_files.isEmpty())
        return;

    // Ensure index is valid
    if (m_currentIndex < 0)
        m_currentIndex = 0;
    if (m_currentIndex >= m_files.size())
        m_currentIndex = m_files.size() - 1;

    if (usesHistoryComparisonSelection())
    {
        m_currentIndex = m_historyReferenceIndex;
        if (m_historyTargetIndex < 0 || m_historyTargetIndex >= m_files.size() ||
            m_historyTargetIndex == m_historyReferenceIndex)
        {
            m_historyTargetIndex = -1;
            for (int i = 0; i < m_files.size(); ++i)
            {
                if (i != m_historyReferenceIndex)
                {
                    m_historyTargetIndex = i;
                    break;
                }
            }
        }
    }

    QString path = m_files[m_currentIndex];
    const int rightIndex = activeRightIndex();
    const bool compareMode =
        m_mode != BlinkMode::Single && m_comparisonView && rightIndex >= 0 && rightIndex < m_files.size();
    updateImageLabels(compareMode ? rightIndex : -1);
    updateHistoryTableSelection();

    if (m_enableImageLoading && m_comparisonView)
    {
        m_comparisonView->loadLeftFromFile(path.toStdString());
    }

    if (compareMode)
    {
        if (m_enableImageLoading)
        {
            m_comparisonView->loadRightFromFile(m_files[rightIndex].toStdString());
        }
        m_statusLabel->setText(tr("%1 / %2: %3 vs %4")
                                   .arg(m_currentIndex + 1)
                                   .arg(m_files.size())
                                   .arg(displayLabelForIndex(m_currentIndex), displayLabelForIndex(rightIndex)));
        m_statusLabel->setToolTip(QStringLiteral("%1\n%2").arg(m_files[m_currentIndex], m_files[rightIndex]));
        return;
    }

    m_statusLabel->setText(
        tr("%1 / %2: %3").arg(m_currentIndex + 1).arg(m_files.size()).arg(displayLabelForIndex(m_currentIndex)));
    m_statusLabel->setToolTip(path);
}

void BlinkComparatorDialog::togglePlayPause()
{
    if (m_timer->isActive())
    {
        m_timer->stop();
        m_playButton->setText(tr("Play"));
    }
    else
    {
        m_timer->start(m_intervalMs);
        m_playButton->setText(tr("Pause"));
    }
}

void BlinkComparatorDialog::onSpeedChanged(int sliderValue)
{
    m_intervalMs = sliderValueToInterval(sliderValue);
    if (m_timer->isActive())
    {
        m_timer->setInterval(m_intervalMs);
    }
}

int BlinkComparatorDialog::sliderValueToInterval(int sliderValue) const
{
    // Slider 0 = slow (max interval), slider 100 = fast (min interval)
    // Use exponential mapping for more natural feel
    double normalized = static_cast<double>(sliderValue) / 100.0;
    // Invert: higher slider value = faster = lower interval
    double inverted = 1.0 - normalized;
    // Exponential curve for smoother speed changes
    double intervalRange = kMaxInterval - kMinInterval;
    return static_cast<int>(kMinInterval + inverted * inverted * intervalRange);
}

int BlinkComparatorDialog::intervalToSliderValue(int intervalMs) const
{
    // Reverse the mapping
    double intervalRange = kMaxInterval - kMinInterval;
    double inverted = std::sqrt(static_cast<double>(intervalMs - kMinInterval) / intervalRange);
    double normalized = 1.0 - inverted;
    return static_cast<int>(normalized * 100.0);
}
