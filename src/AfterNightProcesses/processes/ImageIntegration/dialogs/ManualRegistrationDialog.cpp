// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ManualRegistrationDialog.h"

#include "controls/ImageViewControl.h"
#include "processes/ImageIntegration/core/TooltipFormatUtils.h"
#include "theme/AnDesignTokens.h"
#include "widgets/ImageViewWidget.h"
#include "widgets/ImageViewWidgetDisplayModule.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHeaderView>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include <verdigris/wobjectimpl.h>

#include <algorithm>
#include <cmath>
#include <functional>

W_OBJECT_IMPL(ManualRegistrationDialog)

namespace
{
struct ManualRegistrationPreviewMarker
{
    QPointF imagePoint;
    QString label;
    QColor color;
};

QString displayNameForPath(const QString& path)
{
    const QFileInfo file_info(path);
    return file_info.fileName().isEmpty() ? path : file_info.fileName();
}

QString formatPoint(const QPointF& point)
{
    return QObject::tr("(%1, %2)").arg(point.x(), 0, 'f', 1).arg(point.y(), 0, 'f', 1);
}

ImageViewControl* createManualRegistrationPreviewControl(QWidget* parent, const char* objectName,
                                                         const QString& accessibleName, const QString& toolTip)
{
    if (!parent)
    {
        return nullptr;
    }

    auto* control = new ImageViewControl(parent);
    control->setObjectName(QString::fromUtf8(objectName));
    control->initialize(objectName ? objectName : "", ViewControlMode::st_preview);
    control->setAccessibleName(accessibleName);
    control->setToolTip(toolTip);
    control->setAutoDebayerCfaImagePreviews(true);
    return control;
}

std::optional<QPointF> mapPreviewClickToImagePoint(ImageViewControl* control, const QPointF& viewPosition)
{
    if (!control)
    {
        return std::nullopt;
    }

    auto* viewWidget = control->getViewWidget();
    const QSize imageSize = viewWidget ? viewWidget->imageSize() : QSize();
    if (!viewWidget || imageSize.isEmpty())
    {
        return std::nullopt;
    }

    const double scale = viewWidget->getCurrentScaleFactor();
    if (!std::isfinite(scale) || scale <= 0.0)
    {
        return std::nullopt;
    }

    const QPointF imagePoint(viewPosition.x() / scale, viewPosition.y() / scale);
    if (imagePoint.x() < 0.0 || imagePoint.y() < 0.0 || imagePoint.x() >= static_cast<double>(imageSize.width()) ||
        imagePoint.y() >= static_cast<double>(imageSize.height()))
    {
        return std::nullopt;
    }

    return imagePoint;
}

class ManualRegistrationMarkerDisplayModule final : public ImageViewWidgetDisplayModule
{
public:
    void setMarkers(const QVector<ManualRegistrationPreviewMarker>& markers)
    {
        m_markers = markers;
        requestUpdate();
    }

    void onDraw(QPainter& painter) override
    {
        if (!m_ViewWidget || m_markers.isEmpty())
        {
            return;
        }

        const double scale = m_ViewWidget->getCurrentScaleFactor();
        if (m_ViewWidget->imageSize().isEmpty() || !std::isfinite(scale) || scale <= 0.0)
        {
            return;
        }

        const auto& tokenSet = an::ui::tokens();
        const auto& colors = tokenSet.colors;
        const auto& spacing = tokenSet.spacing;
        const auto& radius = tokenSet.radius;
        const QFont markerFont = tokenSet.typography.caption;
        const QFontMetrics metrics(markerFont);
        const int markerRadius = (std::max)(1, spacing.xs);
        const int labelPadding = spacing.sm;
        const int labelOffsetX = spacing.sm + spacing.xs;
        const int labelHeight = metrics.height() + spacing.xs * 2;

        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setFont(markerFont);

        for (const auto& marker : m_markers)
        {
            const QPointF widgetPoint(marker.imagePoint.x() * scale, marker.imagePoint.y() * scale);
            if (!m_ViewWidget->rect().contains(widgetPoint.toPoint()))
            {
                continue;
            }

            const QColor markerColor = marker.color.isValid() ? marker.color : colors.accentPrimary;
            painter.setPen(QPen(markerColor, 1.5));
            painter.setBrush(markerColor);
            painter.drawEllipse(widgetPoint, markerRadius, markerRadius);

            if (marker.label.isEmpty())
            {
                continue;
            }

            const int labelWidth = metrics.horizontalAdvance(marker.label) + labelPadding * 2;
            const QRectF labelRect(widgetPoint.x() + labelOffsetX, widgetPoint.y() - labelHeight / 2.0, labelWidth,
                                   labelHeight);

            painter.setPen(Qt::NoPen);
            painter.setBrush(colors.bgCardPrimary);
            painter.drawRoundedRect(labelRect, radius.sm, radius.sm);
            painter.setPen(colors.textPrimary);
            painter.drawText(labelRect.adjusted(labelPadding, 0, -labelPadding, 0), Qt::AlignVCenter | Qt::AlignLeft,
                             marker.label);
        }

        painter.restore();
    }

private:
    void requestUpdate()
    {
        if (m_ViewWidget)
        {
            m_ViewWidget->update();
        }
    }

    QVector<ManualRegistrationPreviewMarker> m_markers;
};

class ManualRegistrationClickFilter final : public QObject
{
public:
    ManualRegistrationClickFilter(ImageViewControl* control, std::function<void(const QPointF&)> callback,
                                  QObject* parent = nullptr)
        : QObject(parent), m_control(control), m_callback(std::move(callback))
    {
    }

protected:
    bool eventFilter(QObject* /*watched*/, QEvent* event) override
    {
        if (event->type() != QEvent::MouseButtonPress)
        {
            return false;
        }

        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() != Qt::LeftButton || !m_control || !m_callback)
        {
            return false;
        }

        const auto imagePoint = mapPreviewClickToImagePoint(m_control, mouseEvent->position());
        if (imagePoint.has_value())
        {
            m_callback(imagePoint.value());
        }

        return false;
    }

private:
    ImageViewControl* m_control = nullptr;
    std::function<void(const QPointF&)> m_callback;
};

} // namespace

class ManualRegistrationPreviewController
{
public:
    ManualRegistrationPreviewController(QWidget* parent, const char* objectName, const QString& accessibleName,
                                        const QString& toolTip, std::function<void(const QPointF&)> clickHandler)
        : m_control(createManualRegistrationPreviewControl(parent, objectName, accessibleName, toolTip))
    {
        if (!m_control)
        {
            return;
        }

        auto* viewWidget = m_control->getViewWidget();
        if (!viewWidget)
        {
            return;
        }

        m_markerModule = std::make_unique<ManualRegistrationMarkerDisplayModule>();
        m_markerModule->attachToViewWidget(viewWidget);
        viewWidget->installEventFilter(
            new ManualRegistrationClickFilter(m_control, std::move(clickHandler), viewWidget));
    }

    [[nodiscard]] QWidget* widget() const
    {
        return m_control;
    }

    void setEnabled(const bool enabled) const
    {
        if (m_control)
        {
            m_control->setEnabled(enabled);
        }
    }

    void loadImage(const QString& path) const
    {
        if (!m_control)
        {
            return;
        }

        if (path.isEmpty())
        {
            m_control->clear();
            return;
        }

        m_control->loadImageFromFile(path.toStdString());
    }

    void clear() const
    {
        if (m_control)
        {
            m_control->clear();
        }
    }

    void setMarkers(const QVector<ManualRegistrationPreviewMarker>& markers) const
    {
        if (m_markerModule)
        {
            m_markerModule->setMarkers(markers);
        }
    }

private:
    ImageViewControl* m_control = nullptr;
    std::unique_ptr<ManualRegistrationMarkerDisplayModule> m_markerModule;
};

ManualRegistrationDialog::ManualRegistrationDialog(const QString& referenceFrame, const QStringList& availableFrames,
                                                   const RegistrationSettings::ManualControlPointMap& existingPoints,
                                                   QWidget* parent)
    : QDialog(parent), m_referenceFrame(referenceFrame.trimmed()), m_availableFrames(availableFrames),
      m_controlPointsByFrame(existingPoints)
{
    setWindowTitle(tr("Manual Registration"));
    setWindowModality(Qt::WindowModal);
    setModal(true);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setSizeGripEnabled(true);
    const auto& spacing = an::ui::tokens().spacing;
    setMinimumSize(spacing.xl * 28, spacing.xl * 18);

    auto* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(spacing.lg, spacing.lg, spacing.lg, spacing.lg);
    main_layout->setSpacing(spacing.md);

    auto* instructions =
        new QLabel(tr("Click a point in the reference image first, then click the matching point in the target frame. "
                      "Each pair is stored per target frame and used for manual N-point registration."),
                   this);
    instructions->setWordWrap(true);
    main_layout->addWidget(instructions);

    auto* header_layout = new QHBoxLayout();
    header_layout->setSpacing(spacing.sm);

    m_referenceLabel = new QLabel(tr("Reference: %1").arg(displayNameForPath(m_referenceFrame)), this);
    header_layout->addWidget(m_referenceLabel, 1);

    auto* target_label = new QLabel(tr("Target frame:"), this);
    header_layout->addWidget(target_label);

    m_targetFrameCombo = new QComboBox(this);
    m_targetFrameCombo->setObjectName(QStringLiteral("manualRegistrationTargetFrameCombo"));
    m_targetFrameCombo->setToolTip(an::ui::imageintegration::formatTooltip(
        tr("Target frame"), {tr("What it does:\nSelects which frame you are currently defining control points for. "
                                "Each target frame gets its own set of reference\u2192target point pairs."),
                             tr("Workflow:\n%1")
                                 .arg(an::ui::imageintegration::tooltipBullets(
                                     {tr("Click a point in the reference image on the left."),
                                      tr("Click the matching point in this target frame on the right."),
                                      tr("Repeat for at least 3 pairs, then switch to the next target frame.")}))}));
    for (const auto& frame : m_availableFrames)
    {
        if (frame.trimmed().isEmpty() || frame == m_referenceFrame)
        {
            continue;
        }
        m_targetFrameCombo->addItem(displayNameForPath(frame), frame);
    }
    header_layout->addWidget(m_targetFrameCombo);
    header_layout->addStretch(1);
    main_layout->addLayout(header_layout);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    m_referencePreview = std::make_unique<ManualRegistrationPreviewController>(
        splitter, "manualRegistrationReferencePreview", tr("Manual registration reference preview"),
        tr("Reference image preview used to place manual registration control points."),
        [this](const QPointF& point) { onReferencePointClicked(point); });
    m_targetPreview = std::make_unique<ManualRegistrationPreviewController>(
        splitter, "manualRegistrationTargetPreview", tr("Manual registration target preview"),
        tr("Target image preview used to place matching manual registration control points."),
        [this](const QPointF& point) { onTargetPointClicked(point); });
    if (m_referencePreview && m_referencePreview->widget())
    {
        splitter->addWidget(m_referencePreview->widget());
    }
    if (m_targetPreview && m_targetPreview->widget())
    {
        splitter->addWidget(m_targetPreview->widget());
    }
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    main_layout->addWidget(splitter, 1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setProperty("anStatusTone", QStringLiteral("secondary"));
    main_layout->addWidget(m_statusLabel);

    auto* button_row = new QHBoxLayout();
    button_row->setSpacing(an::ui::tokens().spacing.sm);

    m_removeSelectedButton = new QPushButton(tr("Remove Selected"), this);
    m_clearFrameButton = new QPushButton(tr("Clear Target Points"), this);
    m_clearPendingButton = new QPushButton(tr("Clear Pending Reference"), this);
    m_clearPendingButton->setEnabled(false);

    m_removeSelectedButton->setObjectName(QStringLiteral("removeSelectedButton"));
    m_clearFrameButton->setObjectName(QStringLiteral("clearTargetPointsButton"));
    m_clearPendingButton->setObjectName(QStringLiteral("clearPendingReferenceButton"));

    m_removeSelectedButton->setProperty("anButtonVariant", QStringLiteral("secondary"));
    m_clearFrameButton->setProperty("anButtonVariant", QStringLiteral("danger"));
    m_clearPendingButton->setProperty("anButtonVariant", QStringLiteral("secondary"));

    m_removeSelectedButton->setToolTip(tr("Remove the selected point pairs from this target frame."));
    m_clearFrameButton->setToolTip(tr("Remove all control point pairs for the current target frame."));
    m_clearPendingButton->setToolTip(tr("Discard the reference click that is waiting for a matching target click."));

    button_row->addWidget(m_removeSelectedButton);
    button_row->addWidget(m_clearFrameButton);
    button_row->addWidget(m_clearPendingButton);
    button_row->addStretch(1);
    main_layout->addLayout(button_row);

    m_pointsTable = new QTableWidget(this);
    m_pointsTable->setObjectName(QStringLiteral("manualRegistrationPointsTable"));
    m_pointsTable->setColumnCount(5);
    m_pointsTable->setHorizontalHeaderLabels({tr("#"), tr("Ref X"), tr("Ref Y"), tr("Target X"), tr("Target Y")});
    m_pointsTable->setToolTip(an::ui::imageintegration::formatTooltip(
        tr("Control point pairs"),
        {tr("What it does:\nLists all reference\u2192target point pairs defined for the current target frame. Each row "
            "represents one matching pair used by the registration transform."),
         tr("Tip: select rows and press Remove Selected to delete incorrect pairs. You need at least 3 pairs for a "
            "valid registration solution.")}));
    m_pointsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pointsTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_pointsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_pointsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    for (int column = 1; column < 5; ++column)
    {
        m_pointsTable->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Stretch);
    }
    main_layout->addWidget(m_pointsTable);

    auto* dialog_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    main_layout->addWidget(dialog_buttons);

    connect(dialog_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(dialog_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_targetFrameCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { onTargetFrameChanged(); });
    connect(m_removeSelectedButton, &QPushButton::clicked, this, [this]() { removeSelectedPoints(); });
    connect(m_clearFrameButton, &QPushButton::clicked, this, [this]() { clearCurrentFramePoints(); });
    connect(m_clearPendingButton, &QPushButton::clicked, this, [this]() {
        m_pendingReferencePoint.reset();
        refreshMarkers();
        updateStatusLabel();
    });

    if (!m_referenceFrame.isEmpty())
    {
        m_referencePreview->loadImage(m_referenceFrame);
    }

    updateTargetFrameUi();
}

ManualRegistrationDialog::~ManualRegistrationDialog() = default;

RegistrationSettings::ManualControlPointMap ManualRegistrationDialog::controlPointsByFrame() const
{
    RegistrationSettings::ManualControlPointMap cleaned;
    for (auto it = m_controlPointsByFrame.cbegin(); it != m_controlPointsByFrame.cend(); ++it)
    {
        if (!it.value().isEmpty())
        {
            cleaned.insert(it.key(), it.value());
        }
    }
    return cleaned;
}

QString ManualRegistrationDialog::currentTargetFrame() const
{
    if (!m_targetFrameCombo || m_targetFrameCombo->currentIndex() < 0)
    {
        return {};
    }
    return m_targetFrameCombo->currentData().toString();
}

void ManualRegistrationDialog::updateTargetFrameUi()
{
    const QString target = currentTargetFrame();
    const bool has_target = !target.isEmpty();

    if (m_referencePreview)
    {
        m_referencePreview->setEnabled(has_target);
    }
    if (m_targetPreview)
    {
        m_targetPreview->setEnabled(has_target);
    }
    m_pointsTable->setEnabled(has_target);
    m_removeSelectedButton->setEnabled(has_target);
    m_clearFrameButton->setEnabled(has_target);

    if (has_target)
    {
        if (m_targetPreview)
        {
            m_targetPreview->loadImage(target);
        }
    }
    else
    {
        if (m_targetPreview)
        {
            m_targetPreview->clear();
        }
    }

    m_pendingReferencePoint.reset();
    m_clearPendingButton->setEnabled(false);
    refreshTable();
    refreshMarkers();
    updateStatusLabel();
}

void ManualRegistrationDialog::refreshTable()
{
    const QString target = currentTargetFrame();
    const auto points = m_controlPointsByFrame.value(target);

    m_pointsTable->setRowCount(points.size());
    for (int row = 0; row < points.size(); ++row)
    {
        const auto& point = points.at(row);
        auto* index_item = new QTableWidgetItem(QString::number(row + 1));
        auto* ref_x_item = new QTableWidgetItem(QString::number(point.referencePoint.x(), 'f', 1));
        auto* ref_y_item = new QTableWidgetItem(QString::number(point.referencePoint.y(), 'f', 1));
        auto* src_x_item = new QTableWidgetItem(QString::number(point.sourcePoint.x(), 'f', 1));
        auto* src_y_item = new QTableWidgetItem(QString::number(point.sourcePoint.y(), 'f', 1));
        index_item->setTextAlignment(Qt::AlignCenter);
        ref_x_item->setTextAlignment(Qt::AlignCenter);
        ref_y_item->setTextAlignment(Qt::AlignCenter);
        src_x_item->setTextAlignment(Qt::AlignCenter);
        src_y_item->setTextAlignment(Qt::AlignCenter);
        m_pointsTable->setItem(row, 0, index_item);
        m_pointsTable->setItem(row, 1, ref_x_item);
        m_pointsTable->setItem(row, 2, ref_y_item);
        m_pointsTable->setItem(row, 3, src_x_item);
        m_pointsTable->setItem(row, 4, src_y_item);
    }
}

void ManualRegistrationDialog::refreshMarkers()
{
    QVector<ManualRegistrationPreviewMarker> reference_markers;
    QVector<ManualRegistrationPreviewMarker> target_markers;

    const auto& colors = an::ui::tokens().colors;
    const QString target = currentTargetFrame();
    const auto points = m_controlPointsByFrame.value(target);

    reference_markers.reserve(points.size() + (m_pendingReferencePoint.has_value() ? 1 : 0));
    target_markers.reserve(points.size());

    for (int i = 0; i < points.size(); ++i)
    {
        const QString label = QString::number(i + 1);
        reference_markers.push_back({points.at(i).referencePoint, label, colors.statusInfo});
        target_markers.push_back({points.at(i).sourcePoint, label, colors.statusSuccess});
    }

    if (m_pendingReferencePoint.has_value())
    {
        reference_markers.push_back({m_pendingReferencePoint.value(), tr("Next"), colors.statusWarning});
    }

    if (m_referencePreview)
    {
        m_referencePreview->setMarkers(reference_markers);
    }
    if (m_targetPreview)
    {
        m_targetPreview->setMarkers(target_markers);
    }
    m_clearPendingButton->setEnabled(m_pendingReferencePoint.has_value());
}

void ManualRegistrationDialog::updateStatusLabel(const QString& message)
{
    if (!message.isEmpty())
    {
        m_statusLabel->setText(message);
        return;
    }

    const QString target = currentTargetFrame();
    if (target.isEmpty())
    {
        m_statusLabel->setText(tr("Select a target frame to start adding manual control points."));
        return;
    }

    const int point_count = m_controlPointsByFrame.value(target).size();
    if (m_pendingReferencePoint.has_value())
    {
        m_statusLabel->setText(tr("Reference point selected at %1. Click the matching point in %2 to complete pair %3.")
                                   .arg(formatPoint(m_pendingReferencePoint.value()), displayNameForPath(target),
                                        QString::number(point_count + 1)));
        return;
    }

    m_statusLabel->setText(
        tr("%1 control point pair(s) stored for %2. Click a new point in the reference image to add another pair.")
            .arg(point_count)
            .arg(displayNameForPath(target)));
}

void ManualRegistrationDialog::addPointPair(const QPointF& sourcePoint, const QPointF& referencePoint)
{
    const QString target = currentTargetFrame();
    if (target.isEmpty())
    {
        return;
    }

    auto& points = m_controlPointsByFrame[target];
    points.push_back({sourcePoint, referencePoint});
    refreshTable();
    refreshMarkers();
    updateStatusLabel();
}

void ManualRegistrationDialog::removeSelectedPoints()
{
    const QString target = currentTargetFrame();
    if (target.isEmpty())
    {
        return;
    }

    auto points = m_controlPointsByFrame.value(target);
    if (points.isEmpty())
    {
        return;
    }
    if (!m_pointsTable->selectionModel())
    {
        return;
    }

    QList<int> rows;
    rows.reserve(m_pointsTable->selectionModel()->selectedRows().size());
    for (const auto& index : m_pointsTable->selectionModel()->selectedRows())
    {
        rows.push_back(index.row());
    }
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());

    for (const int row : rows)
    {
        if (row >= 0 && row < points.size())
        {
            points.removeAt(row);
        }
    }

    if (points.isEmpty())
    {
        m_controlPointsByFrame.remove(target);
    }
    else
    {
        m_controlPointsByFrame.insert(target, points);
    }

    refreshTable();
    refreshMarkers();
    updateStatusLabel();
}

void ManualRegistrationDialog::clearCurrentFramePoints()
{
    const QString target = currentTargetFrame();
    if (target.isEmpty())
    {
        return;
    }

    m_controlPointsByFrame.remove(target);
    refreshTable();
    refreshMarkers();
    updateStatusLabel();
}

void ManualRegistrationDialog::onTargetFrameChanged()
{
    updateTargetFrameUi();
}

void ManualRegistrationDialog::onReferencePointClicked(const QPointF& point)
{
    if (currentTargetFrame().isEmpty())
    {
        updateStatusLabel(tr("Select a target frame before choosing control points."));
        return;
    }

    m_pendingReferencePoint = point;
    refreshMarkers();
    updateStatusLabel();
}

void ManualRegistrationDialog::onTargetPointClicked(const QPointF& point)
{
    if (!m_pendingReferencePoint.has_value())
    {
        updateStatusLabel(tr("Pick a point in the reference image first."));
        return;
    }

    addPointPair(point, m_pendingReferencePoint.value());
    m_pendingReferencePoint.reset();
    refreshMarkers();
    updateStatusLabel();
}
