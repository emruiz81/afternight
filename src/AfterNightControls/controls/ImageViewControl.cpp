// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ImageViewControl.h"
#include "ImageViewControlListener.h"
#include "CalibrationPreviewUtils.h"
#include "ImageViewScrollArea.h"
#include "ImageZoomAndPanController.h"
#include "ToolbarOverflowLayout.h"
#include "controls/ImageViewCompanions.h"
#include "controls/ImageViewFiles.h"
#include "widgets/ImageViewWidget.h"
#include "widgets/ImagePreviewWidget.h"
#include "widgets/waitingspinnerwidget.h"

#include "common.h"

#include "app/FilePolicies.h"
#include "app/GuiThreadInvoke.h"
#include "app/UiStyleTokens.h"
#include "controls/IAppContext.h"
#include "session/CommitTicket.h"
#include "session/IEngineSessionProvider.h"
#include "session/ImageReads.h"
#include "theme/AnDesignTokens.h"

#include "controls/settings/SettingsKeys.h"
#include "controls/settings/SettingsDefaults.h"

#include "controls/AutostretchSettingsControl.h"
#include "controls/StaticPreviewCacheLimits.h"
#include "controls/annotation/AnnotationSettings.h"

#include <AfterNightConsumerRuntime/EngineSession.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/OutputIntent.h>

#include <AfterNightPlatform/Logging.h>

#include <kddockwidgets/core/FloatingWindow.h>

#include <QFontMetrics>
#include <QActionEvent>
#include <QActionGroup>
#include <QAbstractItemModel>
#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QFileInfo>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QShowEvent>
#include <QStyleOptionComboBox>
#include <QStyleOptionToolButton>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QThreadPool>
#include <verdigris/wobjectimpl.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <deque>
#include <list>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace
{

constexpr int kImageModeComboMinWidth = 130;
constexpr int kImageChannelComboMinWidth = 80;
constexpr char kOverflowHiddenProperty[] = "anOverflowHidden";
constexpr char kOverflowWasEnabledProperty[] = "anOverflowWasEnabled";

[[nodiscard]] bool isShortcutActionAvailable(const QAction* action)
{
    return action && action->isVisible() && action->isEnabled();
}

/// The session the view's work runs in, or null in a process that lends none.
[[nodiscard]] const an::consumer::EngineSession* viewSession()
{
    auto* context = an::ui::appContext();
    auto* provider = context != nullptr ? context->getEngineSessionProvider() : nullptr;
    return provider != nullptr ? &provider->engineSession() : nullptr;
}

/// The text a status names for a person: its message, then each cause's.
[[nodiscard]] QString describeStatus(const an::Status& status)
{
    QString text;
    for (an::Status cause = status; cause.failed(); cause = cause.cause())
    {
        if (!text.isEmpty())
        {
            text += QStringLiteral(": ");
        }
        text += QString::fromStdString(std::string(cause.message()));
    }
    return text;
}

/// Takes every attached process out of the view before any of them is closed, so a process that goes on close cannot
/// be reached again through the view.
std::vector<an::ui::IViewAttachment*>
takeAttachedProcessesForClosing(std::map<std::string, an::ui::IViewAttachment*>& attachedProcesses)
{
    std::vector<an::ui::IViewAttachment*> processes;
    processes.reserve(attachedProcesses.size());

    for (const auto& [_, process] : attachedProcesses)
    {
        if (process)
        {
            processes.push_back(process);
        }
    }

    attachedProcesses.clear();
    return processes;
}

void closeAttachedProcesses(std::map<std::string, an::ui::IViewAttachment*>& attachedProcesses)
{
    for (auto* process : takeAttachedProcessesForClosing(attachedProcesses))
    {
        process->detachAndClose();
    }
}

/// Counts one piece of the view's work while it is on its way, and lets it go on any thread.
class OutstandingWork
{
public:
    explicit OutstandingWork(std::shared_ptr<std::atomic<std::uint32_t>> counter) : m_counter(std::move(counter))
    {
        m_counter->fetch_add(1u, std::memory_order_acq_rel);
    }

    OutstandingWork(const OutstandingWork&) = delete;
    OutstandingWork& operator=(const OutstandingWork&) = delete;

    ~OutstandingWork()
    {
        m_counter->fetch_sub(1u, std::memory_order_acq_rel);
    }

private:
    std::shared_ptr<std::atomic<std::uint32_t>> m_counter;
};

/// Logs a view's work that threw on a worker, whose completion is dropped. Any thread.
void logViewWorkThatThrew(const char* what) noexcept
{
    try
    {
        LOG_WARNING("Image view: work on a worker threw and its result was dropped: {}", what);
    }
    catch (...)
    {
        // Nothing is left to tell when the log itself cannot be written.
    }
}

/// Runs a step on a worker and its completion on the GUI thread, for as long as the guarded object lives. The work is
/// counted from the moment it is started until its completion has run or been dropped. A work that throws is logged
/// and its completion dropped: an exception never leaves the worker, where it would end the application.
template <typename ObjectT, typename WorkT, typename DoneT>
void runThenPublish(ObjectT* object, std::shared_ptr<std::atomic<std::uint32_t>> counter, WorkT work, DoneT done)
{
    auto pending = std::make_shared<OutstandingWork>(std::move(counter));
    QPointer<ObjectT> guard(object);
    QThreadPool::globalInstance()->start([guard, pending, work = std::move(work), done = std::move(done)]() mutable {
        std::shared_ptr<decltype(work())> result;
        try
        {
            result = std::make_shared<decltype(work())>(work());
        }
        catch (const std::exception& error)
        {
            logViewWorkThatThrew(error.what());
            return;
        }
        catch (...)
        {
            logViewWorkThatThrew("an exception of no standard type");
            return;
        }
        QCoreApplication* application = QCoreApplication::instance();
        if (application == nullptr)
        {
            return;
        }
        QMetaObject::invokeMethod(
            application,
            [guard, pending, result, done = std::move(done)]() mutable {
                if (guard)
                {
                    done(*guard, std::move(*result));
                }
            },
            Qt::QueuedConnection);
    });
}

void applyImageViewScrollAreaBackground(ImageViewScrollArea* scrollArea, const bool darkMode)
{
    if (!scrollArea)
    {
        return;
    }

    const QColor windowColor = an::ui::imageViewScrollAreaWindowColor(darkMode);

    const auto applyRoles = [&windowColor](QPalette& pal, const QPalette::ColorGroup group) {
        pal.setColor(group, QPalette::Window, windowColor);
        pal.setColor(group, QPalette::Base, windowColor);
        pal.setColor(group, QPalette::Dark, windowColor);
    };

    scrollArea->setBackgroundRole(QPalette::Dark);
    QPalette scrollPal = scrollArea->palette();
    applyRoles(scrollPal, QPalette::Active);
    applyRoles(scrollPal, QPalette::Inactive);
    applyRoles(scrollPal, QPalette::Disabled);
    scrollArea->setPalette(scrollPal);
    scrollArea->setAutoFillBackground(true);

    if (QWidget* viewport = scrollArea->viewport())
    {
        viewport->setBackgroundRole(QPalette::Dark);
        QPalette viewportPal = viewport->palette();
        applyRoles(viewportPal, QPalette::Active);
        applyRoles(viewportPal, QPalette::Inactive);
        applyRoles(viewportPal, QPalette::Disabled);
        viewport->setPalette(viewportPal);
        viewport->setAutoFillBackground(true);
    }
}

void activateImageViewLayoutBeforeFit(QWidget* widget)
{
    for (QWidget* current = widget; current; current = current->parentWidget())
    {
        current->updateGeometry();
        if (auto* layout = current->layout())
        {
            layout->activate();
        }
    }
}

void applyImageViewToolbarBackground(QToolBar* toolbar, const bool darkMode, const bool useCardSurfaceTint)
{
    if (!toolbar)
    {
        return;
    }

    toolbar->setProperty("anUseCardBackground", useCardSurfaceTint);

    if (!useCardSurfaceTint)
    {
        toolbar->setAutoFillBackground(false);
        if (auto* style = toolbar->style())
        {
            style->unpolish(toolbar);
            style->polish(toolbar);
        }
        toolbar->update();
        return;
    }

    const QColor toolbarColor = an::ui::imageViewCardSurfaceTintColor(darkMode);
    QPalette toolbarPalette = toolbar->palette();
    toolbarPalette.setColor(QPalette::Active, QPalette::Window, toolbarColor);
    toolbarPalette.setColor(QPalette::Inactive, QPalette::Window, toolbarColor);
    toolbarPalette.setColor(QPalette::Disabled, QPalette::Window, toolbarColor);
    toolbarPalette.setColor(QPalette::Active, QPalette::Button, toolbarColor);
    toolbarPalette.setColor(QPalette::Inactive, QPalette::Button, toolbarColor);
    toolbarPalette.setColor(QPalette::Disabled, QPalette::Button, toolbarColor);
    toolbar->setPalette(toolbarPalette);
    // Let the proxy style own the toolbar surface paint to avoid double-fill seams.
    toolbar->setAutoFillBackground(false);

    if (auto* style = toolbar->style())
    {
        style->unpolish(toolbar);
        style->polish(toolbar);
    }

    toolbar->update();
}

void updateComboBoxWidthToContents(QComboBox* comboBox, const int extraPadding = 0, const int minimumWidthFloor = 0,
                                   const bool fixedWidth = false)
{
    if (!comboBox || !comboBox->style())
    {
        return;
    }

    int maxWidth = 0;
    QStyleOptionComboBox comboOption;
    comboOption.initFrom(comboBox);
    comboOption.editable = comboBox->isEditable();
    comboOption.iconSize = comboBox->iconSize();

    const QFontMetrics metrics(comboBox->font());
    for (int i = 0; i < comboBox->count(); ++i)
    {
        comboOption.currentText = comboBox->itemText(i);
        comboOption.currentIcon = comboBox->itemIcon(i);
        const QSize contentSize(metrics.horizontalAdvance(comboOption.currentText), metrics.height());
        const int itemWidth =
            comboBox->style()->sizeFromContents(QStyle::CT_ComboBox, &comboOption, contentSize, comboBox).width();
        maxWidth = (std::max)(maxWidth, itemWidth);
    }

    if (maxWidth == 0)
    {
        maxWidth = comboBox->sizeHint().width();
    }

    const int resolvedWidth = (std::max)(maxWidth + extraPadding, minimumWidthFloor);
    if (fixedWidth)
    {
        comboBox->setMinimumWidth(0);
        comboBox->setMaximumWidth(QWIDGETSIZE_MAX);
        comboBox->setFixedWidth(resolvedWidth);
    }
    else
    {
        comboBox->setMaximumWidth(QWIDGETSIZE_MAX);
        comboBox->setMinimumWidth(resolvedWidth);
    }
}

struct ToolbarOverflowCandidate
{
    QAction* action = nullptr;
    QWidget* widget = nullptr;
    int width = 0;
};

int resolveToolbarActionWidth(QToolBar* toolbar, QAction* action, QWidget* widget)
{
    if (!toolbar || !action)
    {
        return 0;
    }

    int width = 0;
    if (widget)
    {
        width = widget->sizeHint().width();
        if (width <= 0)
        {
            width = widget->minimumSizeHint().width();
        }
    }

    if (width <= 0)
    {
        width = toolbar->actionGeometry(action).width();
    }

    if (width <= 0 && action->isSeparator())
    {
        width = toolbar->style()->pixelMetric(QStyle::PM_ToolBarSeparatorExtent, nullptr, toolbar);
    }

    if (width <= 0)
    {
        const int iconWidth = toolbar->iconSize().width() > 0
                                  ? toolbar->iconSize().width()
                                  : toolbar->style()->pixelMetric(QStyle::PM_ToolBarIconSize, nullptr, toolbar);
        width = (std::max)(24, iconWidth + 12);
    }

    return (std::max)(0, width);
}

bool isToolbarSpacerWidget(const QWidget* widget)
{
    return widget && widget->property("anToolbarSpacer").toBool();
}

QString overflowComboLabel(const QComboBox* comboBox)
{
    if (!comboBox)
    {
        return QStringLiteral("Options");
    }

    const QString accessibleName = comboBox->accessibleName().trimmed();
    if (!accessibleName.isEmpty())
    {
        return accessibleName;
    }

    const QString toolTip = comboBox->toolTip().trimmed();
    if (!toolTip.isEmpty())
    {
        return toolTip;
    }

    return QStringLiteral("Options");
}

class OverflowToolButton final : public QToolButton
{
public:
    using QToolButton::QToolButton;

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QStyleOptionToolButton option;
        initStyleOption(&option);
        option.features &= ~QStyleOptionToolButton::HasMenu;
        option.features &= ~QStyleOptionToolButton::MenuButtonPopup;
        option.subControls = QStyle::SC_ToolButton;

        QPainter painter(this);
        style()->drawPrimitive(QStyle::PE_PanelButtonTool, &option, &painter, this);
        style()->drawControl(QStyle::CE_ToolButtonLabel, &option, &painter, this);
    }
};

void hideDefaultToolbarExtensionButton(QToolBar* toolbar, const QIcon& fallbackIcon = QIcon())
{
    if (!toolbar)
    {
        return;
    }

    if (auto* extensionButton = toolbar->findChild<QToolButton*>(QStringLiteral("qt_toolbar_ext_button")))
    {
        if (!fallbackIcon.isNull())
        {
            extensionButton->setIcon(fallbackIcon);
            extensionButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
        }

        // Never let the built-in extension button steal clicks from the custom overflow button.
        extensionButton->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        extensionButton->hide();
    }
}

// A toolbar action the overflow hides reads as disabled, whatever it was set to, so the overflow records each action's
// state when it hides it. These two keep that record current and read through it.

/// Enables or disables a toolbar action, keeping the state the overflow recorded for it current while it is hidden.
void setToolbarActionEnabled(QAction* action, const bool enabled)
{
    if (!action)
    {
        return;
    }

    action->setEnabled(enabled);
    if (action->property(kOverflowHiddenProperty).toBool())
    {
        action->setProperty(kOverflowWasEnabledProperty, enabled);
    }
}

/// Whether a toolbar action is enabled: the state the overflow recorded for it while it is hidden, else its own.
[[nodiscard]] bool isToolbarActionEnabled(const QAction* action)
{
    if (!action)
    {
        return false;
    }

    if (action->property(kOverflowHiddenProperty).toBool())
    {
        const QVariant recorded = action->property(kOverflowWasEnabledProperty);
        if (recorded.isValid())
        {
            return recorded.toBool();
        }
    }
    return action->isEnabled();
}

bool resolveDarkMode(const QPalette& palette)
{
    return an::ui::styling::isDarkPalette(palette);
}

/// The key a static preview's image is kept under: the file, the calibration it was read through, and whether a mosaic
/// was demosaiced. How the view stretches is not part of it: a presentation is keyed by the image's revision.
struct StaticPreviewCacheKey
{
    QString path;
    QString calibrationKey;
    bool autoDebayerCfa = false;
    // The RAW settings the preview was read under, so a preview read before a change is read again under the new ones.
    std::optional<an::ui::RawOpenSettings> raw;

    bool operator==(const StaticPreviewCacheKey& other) const = default;
};

/// The settings a RAW policy was built from, or nothing for a read under none.
std::optional<an::ui::RawOpenSettings> rawSettingsOf(const std::shared_ptr<const an::ui::RawOpenPolicy>& policy)
{
    return policy ? std::optional<an::ui::RawOpenSettings>(policy->settings()) : std::nullopt;
}

/// A static preview's image as it was read, and its statistics when they were measured.
struct StaticPreviewCachedImage
{
    std::shared_ptr<const an::consumer::ImageAsset> asset;
    std::optional<an::ui::ViewImageStatistics> statistics;
};

/// The static previews read most recently. Used on the GUI thread.
class StaticPreviewCache
{
public:
    explicit StaticPreviewCache(const size_t maxSize = an::ui::kMaxPreviewCacheEntries) : m_maxSize(maxSize) {}

    std::optional<StaticPreviewCachedImage> get(const StaticPreviewCacheKey& key)
    {
        for (auto entry = m_order.begin(); entry != m_order.end(); ++entry)
        {
            if (entry->first == key)
            {
                m_order.splice(m_order.begin(), m_order, entry);
                return m_order.front().second;
            }
        }
        return std::nullopt;
    }

    void put(const StaticPreviewCacheKey& key, StaticPreviewCachedImage item)
    {
        m_order.remove_if([&key](const auto& entry) { return entry.first == key; });
        while (m_order.size() >= m_maxSize && !m_order.empty())
        {
            m_order.pop_back();
        }
        if (m_maxSize > 0u)
        {
            m_order.emplace_front(key, std::move(item));
        }
    }

    void setStatistics(const std::shared_ptr<const an::consumer::ImageAsset>& asset,
                       const an::ui::ViewImageStatistics& statistics)
    {
        for (auto& entry : m_order)
        {
            if (entry.second.asset == asset)
            {
                entry.second.statistics = statistics;
            }
        }
    }

    void clear()
    {
        m_order.clear();
    }

private:
    size_t m_maxSize = 0;
    std::list<std::pair<StaticPreviewCacheKey, StaticPreviewCachedImage>> m_order;
};

// ---------------------------------------------------------------------------
// StatsOverlayWidget
// ---------------------------------------------------------------------------
// A themed, token-driven overlay card that displays per-channel aggregate
// statistics for the active st_preview image.  It replaces the former
// QLabel + QFrame::StyledPanel approach, which fought with AnProxyStyle and
// produced inconsistent borders and fonts.
//
// Rules followed:
//   - No setStyleSheet / QSS
//   - No hardcoded colors, spacing, or fonts
//   - an::ui::tokens() called inside paintEvent() so theme switches apply
//     automatically without re-construction
// ---------------------------------------------------------------------------
class StatsOverlayWidget : public QWidget
{
public:
    explicit StatsOverlayWidget(QWidget* parent = nullptr) : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        hide();
    }

    void setStats(const std::optional<an::ui::ViewImageStatistics>& stats)
    {
        m_rows.clear();
        setAccessibleDescription(QString());

        if (!stats.has_value() || stats->channels == 0u)
        {
            hide();
            return;
        }

        double minValue = std::numeric_limits<double>::infinity();
        double maxValue = -std::numeric_limits<double>::infinity();
        double meanSum = 0.0;
        double medianSum = 0.0;
        double stdDevSum = 0.0;
        size_t valid = 0;

        for (std::uint32_t channel = 0u; channel < stats->channels; ++channel)
        {
            const an::ui::ViewChannelStatistics& ch = stats->channel[channel];
            if (!std::isfinite(ch.mean))
                continue;
            minValue = (std::min)(minValue, ch.minimum);
            maxValue = (std::max)(maxValue, ch.maximum);
            meanSum += ch.mean;
            medianSum += ch.median;
            stdDevSum += ch.standardDeviation;
            ++valid;
        }

        if (valid == 0)
        {
            hide();
            return;
        }

        const double inv = 1.0 / static_cast<double>(valid);
        auto fmt = [](double v) { return QString::number(v, 'f', 4); };

        m_rows = {
            {tr("Min"), fmt(minValue)},           {tr("Max"), fmt(maxValue)},
            {tr("Mean"), fmt(meanSum * inv)},     {tr("Median"), fmt(medianSum * inv)},
            {tr("StdDev"), fmt(stdDevSum * inv)},
        };

        // The card paints its rows itself, so what it says is also its accessible description.
        QStringList described;
        for (const auto& [label, value] : m_rows)
        {
            described.push_back(label + QStringLiteral(" ") + value);
        }
        setAccessibleName(tr("Image statistics"));
        setAccessibleDescription(described.join(QStringLiteral(", ")));

        updateGeometryFromRows();
        show();
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        if (m_rows.empty())
            return;

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const auto& t = an::ui::tokens();
        const auto& c = t.colors;

        // Semi-transparent card background
        QColor bg = c.bgCardPrimary;
        bg.setAlpha(220);

        QColor border = c.borderSubtle;
        border.setAlpha(160);

        const QRectF bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        painter.setPen(QPen(border, 1));
        painter.setBrush(bg);
        painter.drawRoundedRect(bounds, t.radius.md, t.radius.md);

        // Typography
        QFont labelFont = t.typography.caption;
        QFont valueFont = t.typography.monospace;
        valueFont.setPointSize(t.typography.caption.pointSize());

        const QFontMetrics labelFm(labelFont);
        const QFontMetrics valueFm(valueFont);

        const int rowH = (std::max)(labelFm.height(), valueFm.height()) + t.spacing.xs;
        const int padX = t.spacing.sm;
        const int padY = t.spacing.sm;
        const int labelW = m_labelColumnWidth;
        const int valueX = padX + labelW + t.spacing.sm;

        int y = padY;
        for (const auto& [label, value] : m_rows)
        {
            const QRect labelRect(padX, y, labelW, rowH);
            const QRect valueRect(valueX, y, width() - valueX - padX, rowH);

            painter.setFont(labelFont);
            painter.setPen(c.textSecondary);
            painter.drawText(labelRect, Qt::AlignRight | Qt::AlignVCenter, label);

            painter.setFont(valueFont);
            painter.setPen(c.textPrimary);
            painter.drawText(valueRect, Qt::AlignLeft | Qt::AlignVCenter, value);

            y += rowH;
        }
    }

private:
    void updateGeometryFromRows()
    {
        if (m_rows.empty())
        {
            setFixedSize(0, 0);
            return;
        }

        const auto& t = an::ui::tokens();
        QFont labelFont = t.typography.caption;
        QFont valueFont = t.typography.monospace;
        valueFont.setPointSize(t.typography.caption.pointSize());

        const QFontMetrics labelFm(labelFont);
        const QFontMetrics valueFm(valueFont);

        int maxLabel = 0;
        int maxValue = 0;
        for (const auto& [label, value] : m_rows)
        {
            maxLabel = (std::max)(maxLabel, labelFm.horizontalAdvance(label));
            maxValue = (std::max)(maxValue, valueFm.horizontalAdvance(value));
        }

        m_labelColumnWidth = maxLabel;

        const int rowH = (std::max)(labelFm.height(), valueFm.height()) + t.spacing.xs;
        const int padX = t.spacing.sm;
        const int padY = t.spacing.sm;
        const int totalW = padX + maxLabel + t.spacing.sm + maxValue + padX;
        const int totalH = padY + rowH * static_cast<int>(m_rows.size()) + padY;

        setFixedSize(totalW, totalH);
    }

    std::vector<std::pair<QString, QString>> m_rows;
    int m_labelColumnWidth = 0;
};

} // namespace

#define CHECK_IF_CONTROL_IS_CLOSING                                 \
    if (m_impl->m_ControlIsClosing.load(std::memory_order_acquire)) \
        return;

const QString RT_PREVIEW_SHOW_ORIGINAL_LABEL = "ORIGINAL";
const QString RT_PREVIEW_SHOW_PREVIEW_LABEL = "PREVIEW";

const QString IMAGE_VIEW_MODE_LINEAR = "Linear";
const QString IMAGE_VIEW_MODE_AUTOSTRETCH = "Autostretch";

namespace
{
/// The demosaic method the product's one demosaic setting names now, read on the GUI thread.
[[nodiscard]] an::ui::DemosaicChoice demosaicSettingChoice()
{
    QString setting = QString::fromLatin1(SettingsDefaults::RAW_DEMOSAIC_ALGORITHM);
    if (auto* settings = an::ui::appContext() ? an::ui::appContext()->getGlobalSettings() : nullptr)
    {
        setting = settings->select(SettingsKeys::RAW_DEMOSAIC_ALGORITHM, setting);
    }
    return an::ui::demosaicMethodForSetting(setting);
}

[[nodiscard]] QString imageViewModeTextFor(const ImageViewMode mode)
{
    switch (mode)
    {
    case ImageViewMode::AUTOSTRETCH_MODE:
        return IMAGE_VIEW_MODE_AUTOSTRETCH;
    case ImageViewMode::LINEAR_MODE:
    default:
        return IMAGE_VIEW_MODE_LINEAR;
    }
}
} // namespace

const QString IMAGE_VIEW_CHANNEL_GRAYSCALE = "Grayscale";
const QString IMAGE_VIEW_CHANNEL_RGB = "RGB";
const QString IMAGE_VIEW_CHANNEL_RED = "Red";
const QString IMAGE_VIEW_CHANNEL_GREEN = "Green";
const QString IMAGE_VIEW_CHANNEL_BLUE = "Blue";
const QString IMAGE_VIEW_CHANNEL_CIE_L = "CIE L* (Lightness)";
const QString IMAGE_VIEW_CHANNEL_CIE_A = "CIE a*";
const QString IMAGE_VIEW_CHANNEL_CIE_B = "CIE b*";
const QString IMAGE_VIEW_CHANNEL_CIE_AB = "CIE a*=R / b*=G (Chrominance)";
const QString IMAGE_VIEW_CHANNEL_CIE_C = "CIE c*";
const QString IMAGE_VIEW_CHANNEL_CIE_H = "CIE h*";
const QString IMAGE_VIEW_CHANNEL_CIE_X = "CIE X";
const QString IMAGE_VIEW_CHANNEL_CIE_Y = "CIE Y (Luminance)";
const QString IMAGE_VIEW_CHANNEL_CIE_Z = "CIE Z";
const QString IMAGE_VIEW_CHANNEL_HSL_HUE = "HSL Hue";
const QString IMAGE_VIEW_CHANNEL_HSL_SATURATION = "HSL Saturation";
const QString IMAGE_VIEW_CHANNEL_HSL_LIGHTNESS = "HSL Lightness";
const QString IMAGE_VIEW_CHANNEL_HSV_HUE = "HSV Hue";
const QString IMAGE_VIEW_CHANNEL_HSV_SATURATION = "HSV Saturation";
const QString IMAGE_VIEW_CHANNEL_HSV_VALUE = "HSV Value";
const QString IMAGE_VIEW_CHANNEL_HSI_HUE = "HSI Hue";
const QString IMAGE_VIEW_CHANNEL_HSI_SATURATION = "HSI Saturation";
const QString IMAGE_VIEW_CHANNEL_HSI_INTENSITY = "HSI Intensity";
const QString IMAGE_VIEW_CHANNEL_YCBCR_LUMA = "YCbCr Luma";
const QString IMAGE_VIEW_CHANNEL_YCBCR_BLUE_CHROMA = "YCbCr Blue Chroma";
const QString IMAGE_VIEW_CHANNEL_YCBCR_RED_CHROMA = "YCbCr Red Chroma";

/// One read of a file the view asked for: the ticket that decides whether it may be shown, and what it reads through.
struct ImageViewControl::ReadJob
{
    an::ui::CommitTicket ticket;
    QString path;
    bool preview = false;
    QString calibrationKey;
    bool autoDebayerCfa = false;
    // The RAW policy the read was asked under: kept until it lands, because the read borrows its registry.
    std::shared_ptr<const an::ui::RawOpenPolicy> raw;
};

/// One edit of the view's image: what it is recorded as in the history, and how it computes the new image from the one
/// its writer holds.
struct ImageViewControl::EditJob
{
    std::string stepName;
    std::string stepDescription;
    const char* removedSolutionNote = nullptr;
    ImageViewControl::EditCompute compute;
    std::function<void(bool)> done;
    // The other images the edit reads, leased with the document's writer before it asks to be admitted.
    std::shared_ptr<an::ui::ImageReads> reads;
    // Whether the view says a failed computation itself: an edit of its own, which no caller of its own says for it.
    bool saysItsFailure = false;
};

namespace
{

/// Says a failure of an edit under the edit's name, as a process says a failure of its own run under its title.
void sayEditFailure(QWidget* view, const std::string& stepName, const QString& text)
{
    if (auto* dialogs = an::ui::appContext() ? an::ui::appContext()->getDialogService() : nullptr)
    {
        dialogs->notifyErrorMessageAsync(view, QString::fromStdString(stepName), text);
    }
}

} // namespace

struct ImageViewControl::Impl
{
    // The document the view shows. Shared so that work on its way keeps the record it reads alive; the view is its only
    // owner otherwise, and closes it before it lets it go.
    std::shared_ptr<an::ui::ImageDocument> m_Document = std::make_shared<an::ui::ImageDocument>();
    std::shared_ptr<const an::consumer::ImageAsset> m_AlternateAsset;

    bool m_SequenceAutoDebayer = false;

    ImageViewWidget* m_ImageViewWidget = nullptr;

    an::ui::IIconProviderPtr m_IconProvider;

    QAction* m_AutostretchSettingsAction = nullptr;
    QAction* m_UndoAction = nullptr;
    QAction* m_RedoAction = nullptr;
    QAction* m_HistoryAction = nullptr;

    QAction* m_FitToWindowAction = nullptr;
    QAction* m_ZoomInAction = nullptr;
    QAction* m_ZoomOutAction = nullptr;
    QAction* m_Zoom100Action = nullptr;
    QAction* m_MaximizeAction = nullptr;

    bool m_IsMaximized = false;

    QAction* m_StatisticsAction = nullptr;
    QAction* m_HistogramAction = nullptr;
    QAction* m_EXIFViewerAction = nullptr;
    QAction* m_FITSViewerAction = nullptr;
    QAction* m_ImageSummaryAction = nullptr;

    QAction* m_AstrometryToggleAction = nullptr;
    QAction* m_AstrometrySettingsAction = nullptr;
    QAction* m_AstrometryInfoAction = nullptr;
    QAction* m_PlateSolveToolbarAction = nullptr;
    QAction* m_AnnotateImageToolbarAction = nullptr;
    QMenu* m_AstrometrySettingsMenu = nullptr;
    QAction* m_AstrometryGridMenuAction = nullptr;
    QAction* m_AstrometryAnnotationsMenuAction = nullptr;
    QAction* m_AstrometryStarLabelsMenuAction = nullptr;
    QAction* m_AstrometrySipMenuAction = nullptr;
    QAction* m_AstrometryDetectedStarsMenuAction = nullptr;
    QMenu* m_AstrometryDetailSubMenu = nullptr;
    QMenu* m_AstrometryLabelsSubMenu = nullptr;
    QMenu* m_AstrometryPaletteSubMenu = nullptr;
    QMenu* m_AstrometryTextSizeSubMenu = nullptr;

    /// What the live annotations name and how they look, as the menu chose and the settings keep them.
    an::ui::AnnotationViewChoices m_AnnotationChoices;

    QAction* m_RTPreviewTakeSnapshotActionSpacer = nullptr;
    QAction* m_RTPreviewTakeSnapshotAction = nullptr;

    QAction* m_RTPreviewShowOriginalAction = nullptr;
    QAction* m_RTPreviewAreaAction = nullptr;
    QAction* m_RTPreviewAutostretchSeparatorAction = nullptr;
    QAction* m_ImageViewModeComboBoxAction = nullptr;
    QAction* m_CalibrationPreviewAction = nullptr;
    QAction* m_StatsOverlayToggleAction = nullptr;
    /// The toolbar's choices of how the image is presented - the channel, the display mode, the stretch's settings,
    /// the statistics - and the separators between them, which a preview whose owner decides them does not offer.
    std::vector<QPointer<QAction>> m_PresentationChoiceActions;
    bool m_PresentationChoicesOffered = true;
    ViewControlTextOverlay* m_ViewControlTextOverlay = nullptr;
    StatsOverlayWidget* m_StaticPreviewStatsOverlay = nullptr;
    bool m_StaticPreviewStatsOverlayVisible = true;
    WaitingSpinnerWidget* m_StaticPreviewLoadingSpinner = nullptr;
    QCheckBox* m_chkRTPreviewArea = nullptr;
    QCheckBox* m_chkCalibrationPreview = nullptr;

    std::string m_UniqueName;

    QComboBox* m_imageViewChannelComboBox = nullptr;
    QComboBox* m_imageViewModeComboBox = nullptr;

    std::atomic_size_t m_LastImageChannels = 0;

    std::vector<ImageViewControlListener*> m_listeners;
    std::shared_mutex m_listeners_mutex;

    std::atomic_bool m_ControlIsClosing = false;

    std::unique_ptr<an::ui::IViewHistory> m_History;

    QVBoxLayout* m_CanvasLayout = nullptr;
    ImageViewScrollArea* m_ScrollArea = nullptr;

    std::unique_ptr<ImageZoomAndPanController> m_ImageZoomAndPanController;

    ViewControlMode m_ViewControlMode = ViewControlMode::standard;

    ImageViewControl* m_CurrentMask = nullptr;

    bool m_MaskEnabled = false;
    bool m_ShowMask = true;
    bool m_InvertMask = false;

    QString m_AlternateRTPreviewLabel1;
    QString m_AlternateRTPreviewLabel2;

    bool m_RTPreviewShowOriginalLabelEnabled = false;

    MaskRenderingMode m_MaskRenderingMode = MaskRenderingMode::red_mode;

    std::vector<QPointer<ImageViewControl>> m_masked_clients;

    std::atomic<ImageCompositionMode> m_ImaveCompositionMode = ImageCompositionMode::rgb_mode;

    std::map<std::string, an::ui::IViewAttachment*> m_AttachedProcessesByName;

    std::unique_ptr<an::ui::IViewSequencePlayer> m_SequencePlayer;

    std::atomic_bool m_IsLockedForProcessing = false;

    NoImagePreviewWidget* m_NoImagePreviewWidget = nullptr;

    bool m_PendingChanges = true;

    std::atomic_bool m_CloseRequested = false;

    QToolBar* m_ToolBar = nullptr;

    bool m_IsFloating = false;

    bool m_ShowAutostretchBanner = true;

    /// Whether the view's Autostretch Settings window is open.
    bool m_AutostretchSettingsOpen = false;
    bool m_AutoDebayerCfaImagePreviews = false;
    an::ui::CalibrationPreviewConfig m_CalibrationPreviewConfig;
    StaticPreviewCache m_StaticPreviewCache;
    std::shared_ptr<an::ui::ViewMasterCache> m_StaticPreviewMasterCache =
        std::make_shared<an::ui::ViewMasterCache>(an::ui::kMaxPreviewMasterCacheEntries);
    std::optional<an::ui::ViewImageStatistics> m_StaticPreviewOverlayStats;
    QString m_StaticPreviewCurrentPath;
    bool m_StaticPreviewAutoFitNextImage = false;
    bool m_MeasuresPreviewStatistics = true;

    // The view's work: the read it shows next, the statistics of the preview it shows, the stars of its overlay, and
    // its edits one at a time.
    an::ui::RequestTracker m_Reads;
    an::ui::RequestTracker m_Statistics;
    an::ui::RequestTracker m_Detections;
    an::ui::RequestTracker m_Edits;
    std::deque<EditJob> m_PendingEdits;
    bool m_EditRunning = false;
    std::stop_source m_Closing;
    std::shared_ptr<std::atomic<std::uint32_t>> m_Outstanding = std::make_shared<std::atomic<std::uint32_t>>(0u);
    ImageViewWorkTelemetry m_Telemetry;

    std::optional<an::WcsSolution> m_CelestialCoordinateSystem;
    std::shared_ptr<const an::ui::ViewOverlayStars> m_OverlayStars;
    std::unique_ptr<ImageViewWidgetDisplayModule> m_AstrometryStarDisplayModule;
    std::unique_ptr<ImageViewWidgetDisplayModule> m_CoordinateGridDisplayModule;
    std::unique_ptr<ImageViewWidgetDisplayModule> m_DistortionMapDisplayModule;
    /// The catalogue annotations: the deep-sky objects and the bright stars, each a layer of the one overlay.
    std::unique_ptr<ImageViewWidgetDisplayModule> m_CatalogAnnotationDisplayModule;

    /// Draws the catalogue annotations' layers the menu checks while the astrometry overlays are shown, and nothing
    /// when neither is checked.
    void showCatalogAnnotations(const bool shown, const bool deepSky, const bool stars) const
    {
        if (m_CatalogAnnotationDisplayModule)
        {
            an::ui::setAstrometryOverlayLayers(*m_CatalogAnnotationDisplayModule, deepSky, stars);
            m_CatalogAnnotationDisplayModule->enableDisplayModule(shown && (deepSky || stars));
        }
    }

    /// Hands what the live annotations name and how they look to the overlays that draw them: the catalogue
    /// annotations, and the grid its palette.
    void showAnnotationChoices() const
    {
        for (const std::unique_ptr<ImageViewWidgetDisplayModule>* overlay :
             {&m_CoordinateGridDisplayModule, &m_CatalogAnnotationDisplayModule})
        {
            if (*overlay)
            {
                an::ui::setAstrometryOverlayChoices(**overlay, m_AnnotationChoices);
            }
        }
    }

    [[nodiscard]] bool hasCalibrationInput() const
    {
        return an::ui::detail::calibrationPreviewHasInput(m_CalibrationPreviewConfig);
    }

    [[nodiscard]] bool isCalibrationPreviewEnabled() const
    {
        return an::ui::detail::calibrationPreviewIsEnabled(
            m_CalibrationPreviewConfig, m_chkCalibrationPreview && m_chkCalibrationPreview->isChecked());
    }

    [[nodiscard]] QString buildCalibrationPreviewKey() const
    {
        return an::ui::detail::buildCalibrationPreviewKey(
            m_CalibrationPreviewConfig, m_chkCalibrationPreview && m_chkCalibrationPreview->isChecked());
    }

    [[nodiscard]] std::optional<an::ui::ViewCalibration> buildCalibrationPreview() const
    {
        if (!isCalibrationPreviewEnabled())
        {
            return std::nullopt;
        }

        an::ui::ViewCalibration calibration;
        calibration.biasPath = m_CalibrationPreviewConfig.biasPath;
        calibration.darkPath = m_CalibrationPreviewConfig.darkPath;
        calibration.flatPath = m_CalibrationPreviewConfig.flatPath;
        calibration.subtractBiasFromDark = m_CalibrationPreviewConfig.subtractBiasFromDark;
        calibration.settings = m_CalibrationPreviewConfig.settings;
        return calibration;
    }

    [[nodiscard]] bool hasImage() const
    {
        return m_Document && m_Document->valid() && m_Document->isOpen();
    }

    [[nodiscard]] std::optional<an::ImageDescriptor> descriptor() const
    {
        return hasImage() ? m_Document->descriptor() : std::nullopt;
    }

    [[nodiscard]] std::uint32_t channelCount() const
    {
        const std::optional<an::ImageDescriptor> described = descriptor();
        return described.has_value() ? described->extent().channelCount() : 0u;
    }

    [[nodiscard]] std::optional<an::ui::DocumentSequence> sequence() const
    {
        return hasImage() ? m_Document->sequence() : std::nullopt;
    }

    static QString formatCalibrationMasterLabel(const QString& path)
    {
        if (path.isEmpty())
        {
            return ImageViewControl::tr("(not used)");
        }

        const QFileInfo fileInfo(path);
        const QString fileName = fileInfo.fileName();
        return fileName.isEmpty() ? path : fileName;
    }

    [[nodiscard]] QString buildCalibrationToolTip() const
    {
        QString tip =
            ImageViewControl::tr("<b>Calibrated (preview)</b><br>"
                                 "Shows the selected light after applying calibration using the currently matched "
                                 "master frames (bias/dark/flat). "
                                 "This affects <i>only the preview</i>; source frames are not modified.<br><br>"
                                 "<b>Order</b>: Calibration -> Statistics/Histogram -> Autostretch (if enabled).");

        const bool hasInputs = hasCalibrationInput();
        const bool available = m_CalibrationPreviewConfig.available && hasInputs;
        if (!available)
        {
            QString reason = m_CalibrationPreviewConfig.tooltip.trimmed();
            if (reason.isEmpty())
            {
                reason = hasInputs
                             ? ImageViewControl::tr("Calibration preview is not available for the current selection.")
                             : ImageViewControl::tr("No matching master frames are available for this light.");
            }

            tip += ImageViewControl::tr("<br><br><b>Unavailable:</b> %1").arg(reason.toHtmlEscaped());
            tip += ImageViewControl::tr("<br><br>Tip: Build/select the relevant masters (bias/dark/flat) or adjust "
                                        "grouping/calibration settings.");
            return tip;
        }

        tip += ImageViewControl::tr("<br><br><b>Masters used</b>:<br>"
                                    "Bias: <tt>%1</tt><br>"
                                    "Dark: <tt>%2</tt><br>"
                                    "Flat: <tt>%3</tt>")
                   .arg(formatCalibrationMasterLabel(m_CalibrationPreviewConfig.biasPath).toHtmlEscaped(),
                        formatCalibrationMasterLabel(m_CalibrationPreviewConfig.darkPath).toHtmlEscaped(),
                        formatCalibrationMasterLabel(m_CalibrationPreviewConfig.flatPath).toHtmlEscaped());

        if (m_chkCalibrationPreview)
        {
            tip += m_chkCalibrationPreview->isChecked()
                       ? ImageViewControl::tr("<br><br>Status: <b>On</b> (preview is calibrated).")
                       : ImageViewControl::tr("<br><br>Status: <b>Off</b> (preview shows the raw light).");
        }
        return tip;
    }

    void updateCalibrationToolTip()
    {
        if (m_chkCalibrationPreview)
        {
            m_chkCalibrationPreview->setToolTip(buildCalibrationToolTip());
        }
    }

    /// Whether the Autostretch Settings window can be opened: its settings only change what the view shows while it
    /// shows the image stretched, and one window is open at a time.
    [[nodiscard]] bool autostretchSettingsAvailable() const
    {
        return m_PresentationChoicesOffered && !m_AutostretchSettingsOpen && m_ImageViewWidget &&
               m_ImageViewWidget->getViewMode() == ImageViewMode::AUTOSTRETCH_MODE;
    }

    /// Offers the Autostretch Settings button as `autostretchSettingsAvailable` says, and the display mode unless the
    /// window is open.
    void refreshAutostretchSettingsAvailability()
    {
        setToolbarActionEnabled(m_AutostretchSettingsAction, autostretchSettingsAvailable());
        if (m_imageViewModeComboBox)
        {
            m_imageViewModeComboBox->setEnabled(!m_AutostretchSettingsOpen);
        }
    }

    void layoutStaticPreviewStatsOverlay()
    {
        if (!m_StaticPreviewStatsOverlay || !m_ScrollArea || !m_StaticPreviewStatsOverlay->isVisible())
        {
            return;
        }

        QWidget* viewport = m_ScrollArea->viewport();
        if (!viewport)
        {
            return;
        }

        constexpr int kOverlayMargin = 15;
        const QSize overlaySize = m_StaticPreviewStatsOverlay->size();
        QRect targetRect((std::max)(kOverlayMargin, viewport->width() - overlaySize.width() - kOverlayMargin),
                         kOverlayMargin, overlaySize.width(), overlaySize.height());

        if (m_ViewControlTextOverlay && m_ViewControlTextOverlay->isEnabled() && m_ViewControlTextOverlay->isVisible())
        {
            const QRect bannerRect = m_ViewControlTextOverlay->geometry();
            if (bannerRect.intersects(targetRect))
            {
                targetRect.moveTop(bannerRect.bottom() + kOverlayMargin);
            }
        }

        const int maxLeft = (std::max)(kOverlayMargin, viewport->width() - overlaySize.width() - kOverlayMargin);
        const int maxTop = (std::max)(kOverlayMargin, viewport->height() - overlaySize.height() - kOverlayMargin);
        targetRect.moveLeft(std::clamp(targetRect.left(), kOverlayMargin, maxLeft));
        targetRect.moveTop(std::clamp(targetRect.top(), kOverlayMargin, maxTop));

        m_StaticPreviewStatsOverlay->move(targetRect.topLeft());
        m_StaticPreviewStatsOverlay->raise();
    }

    void updateStaticPreviewStatsOverlay(const std::optional<an::ui::ViewImageStatistics>& stats)
    {
        m_StaticPreviewOverlayStats = stats;

        if (!m_StaticPreviewStatsOverlay)
        {
            return;
        }

        const bool shouldShow =
            m_ViewControlMode == ViewControlMode::st_preview && hasImage() && m_StaticPreviewStatsOverlayVisible;

        if (!shouldShow)
        {
            m_StaticPreviewStatsOverlay->hide();
            return;
        }

        m_StaticPreviewStatsOverlay->setStats(stats);
        if (m_StaticPreviewStatsOverlay->isVisible())
        {
            layoutStaticPreviewStatsOverlay();
        }
    }
};

W_OBJECT_IMPL(ImageViewControl)

ImageViewControl::ImageViewControl(QWidget* parent, Qt::WindowFlags f)
    : DockableControl(DockableControlType::IMAGE_VIEW_CONTROL, parent, f), m_impl(std::make_unique<Impl>())
{
}

KDDockWidgets::QtWidgets::DockWidget* ImageViewControl::getDockWidget()
{
    auto* context = an::ui::appContext();
    auto* viewControlManager = context ? context->getViewControlManager() : nullptr;
    if (!viewControlManager || !viewControlManager->supportsDockWidgets())
    {
        return nullptr;
    }

    if (!m_DockWidget)
    {
        auto title = QString::fromUtf8(getUniqueName());

        KDDockWidgets::DockWidgetOptions options = KDDockWidgets::DockWidgetOption_DeleteOnClose;
        m_DockWidget = new CustomDockWidget(title, options);
        m_DockWidget->setWidget(this);
        m_DockWidget->setTitle(title);

        this->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        this->setMinimumSize(QSize(100, 100));
        m_DockWidget->setMinimumSize(QSize(100, 100));

        connect(reinterpret_cast<CustomDockWidget*>(m_DockWidget), &CustomDockWidget::closeRequested, this,
                &ImageViewControl::closeRequested);
        connect(m_DockWidget, &KDDockWidgets::QtWidgets::DockWidget::isCurrentTabChanged, this,
                &ImageViewControl::isCurrentTabChanged);
        connect(m_DockWidget, &KDDockWidgets::QtWidgets::DockWidget::isFocusedChanged, this,
                &ImageViewControl::isFocusedChanged);
        connect(m_DockWidget, &KDDockWidgets::QtWidgets::DockWidget::isFloatingChanged, this,
                &ImageViewControl::isFloatingChanged);
        connect(m_DockWidget, &KDDockWidgets::QtWidgets::DockWidget::guestViewChanged, this,
                &ImageViewControl::guestViewChanged);
    }

    return m_DockWidget;
}

void ImageViewControl::initialize(const std::string& uniqueName, const ViewControlMode mode)
{
    auto* context = an::ui::appContext();
    auto* viewControlManager = context ? context->getViewControlManager() : nullptr;
    const bool supportsDockWidgets = viewControlManager && viewControlManager->supportsDockWidgets();

    m_impl->m_IconProvider = context ? context->getIconProvider() : nullptr;
    m_impl->m_UniqueName = uniqueName;
    const bool darkMode = resolveDarkMode(palette());

    m_impl->m_ViewControlMode = mode;

    if (auto* sbs = context ? context->getStatusBarService() : nullptr)
    {
        connect(this, &ImageViewControl::celestialCoordsChanged, this,
                [sbs](double ra_deg, double dec_deg) { sbs->updateImageCelestialCoordsInStatusBar(ra_deg, dec_deg); });
        connect(this, &ImageViewControl::celestialCoordsCleared, this,
                [sbs]() { sbs->clearImageCelestialCoordsInStatusBar(); });
    }

    this->setAttribute(Qt::WA_DeleteOnClose);

    const auto& iconManager = m_impl->m_IconProvider;

    this->setWindowIcon(iconManager->getIcon("galaxy"));
    // Compact-mode measurement: an embedded ImageViewControl in st_preview mode
    // was measured at runtime to occupy ~230px wide x ~170px tall (toolbar + image area)
    // when its QSplitter half is at the minimum. Two such controls fit side-by-side inside
    // an 800x600 dialog with the toolbar at ~28px, leaving ~540px for image area per panel.
    // No toolbar chrome removal is required; the 240x180 floor is sufficient for
    // ComparisonViewWidget. Standard (non-embedded) controls retain the 600x400 minimum.
    if (mode == ViewControlMode::st_preview)
    {
        this->setMinimumWidth(240);
        this->setMinimumHeight(180);
    }
    else
    {
        this->setMinimumWidth(600);
        this->setMinimumHeight(400);
    }
    this->resize(800, 600);

    bool astrometryOverlayEnabled = SettingsDefaults::PLATESOLVE_ASTROMETRY_OVERLAY_ENABLED;
    bool coordinateGridEnabled = SettingsDefaults::PLATESOLVE_COORDINATE_GRID_ENABLED;
    bool distortionMapEnabled = SettingsDefaults::PLATESOLVE_DISTORTION_MAP_ENABLED;
    bool detectedStarsEnabled = SettingsDefaults::PLATESOLVE_DETECTED_STARS_ENABLED;
    bool catalogAnnotationsEnabled = SettingsDefaults::PLATESOLVE_CATALOG_ANNOTATIONS_ENABLED;
    bool starLabelsEnabled = SettingsDefaults::PLATESOLVE_STAR_LABELS_ENABLED;
    an::ui::AnnotationViewChoices annotationChoices;
    auto* settings = context ? context->getGlobalSettings() : nullptr;
    if (settings)
    {
        // Master astrometry toggle always starts unchecked; sub-toggles persist.
        // astrometryOverlayEnabled intentionally not restored from settings.
        coordinateGridEnabled =
            settings->select(SettingsKeys::PLATESOLVE_COORDINATE_GRID_ENABLED,
                             SettingsDefaults::PLATESOLVE_COORDINATE_GRID_ENABLED ? "1" : "0") == "1";
        distortionMapEnabled = settings->select(SettingsKeys::PLATESOLVE_DISTORTION_MAP_ENABLED,
                                                SettingsDefaults::PLATESOLVE_DISTORTION_MAP_ENABLED ? "1" : "0") == "1";
        detectedStarsEnabled = settings->select(SettingsKeys::PLATESOLVE_DETECTED_STARS_ENABLED,
                                                SettingsDefaults::PLATESOLVE_DETECTED_STARS_ENABLED ? "1" : "0") == "1";
        catalogAnnotationsEnabled =
            settings->select(SettingsKeys::PLATESOLVE_CATALOG_ANNOTATIONS_ENABLED,
                             SettingsDefaults::PLATESOLVE_CATALOG_ANNOTATIONS_ENABLED ? "1" : "0") == "1";
        starLabelsEnabled = settings->select(SettingsKeys::PLATESOLVE_STAR_LABELS_ENABLED,
                                             SettingsDefaults::PLATESOLVE_STAR_LABELS_ENABLED ? "1" : "0") == "1";
        if (const std::optional<QString> stored = settings->selectOptional(SettingsKeys::ANNOTATION_VIEW_CHOICES))
        {
            annotationChoices = an::ui::annotationViewChoicesFromStored(*stored);
        }
        else if (const std::optional<QString> limit =
                     settings->selectOptional(SettingsKeys::PLATESOLVE_CATALOG_MAGNITUDE_LIMIT))
        {
            // A view that kept a magnitude limit before it offered levels starts at the level naming about as much.
            bool ok = false;
            const double magnitude = limit->toDouble(&ok);
            if (ok)
            {
                annotationChoices.detail = an::ui::annotationDetailOfMagnitudeLimit(magnitude);
            }
        }
    }
    m_impl->m_AnnotationChoices = annotationChoices;
    // Create the toolbar
    m_impl->m_ToolBar = new CustomToolBar(this);
    m_impl->m_ToolBar->setObjectName(QStringLiteral("anImageViewToolbar"));
    const bool useCardSurfaceTintToolbar = (mode != ViewControlMode::standard);
    m_impl->m_ToolBar->setProperty("anUseCardBackground", useCardSurfaceTintToolbar);
    if (auto* customToolBar = dynamic_cast<CustomToolBar*>(m_impl->m_ToolBar))
    {
        customToolBar->setOverflowIconColors(iconManager->getColorNormal(), iconManager->getColorDisabled());
    }

    applyImageViewToolbarBackground(m_impl->m_ToolBar, darkMode, useCardSurfaceTintToolbar);

    // Add the actions

    m_impl->m_FitToWindowAction = m_impl->m_ToolBar->addAction(iconManager->getIcon("zoom_fit"), "Fit To Window", this,
                                                               &ImageViewControl::adjustImageToWindow);
    m_impl->m_FitToWindowAction->setToolTip("Fit To Window (Ctrl+Shift+F)");
    m_impl->m_ZoomInAction =
        m_impl->m_ToolBar->addAction(iconManager->getIcon("zoom_in"), "Zoom In", this, &ImageViewControl::zoomIn);
    m_impl->m_ZoomOutAction =
        m_impl->m_ToolBar->addAction(iconManager->getIcon("zoom_out"), "Zoom Out", this, &ImageViewControl::zoomOut);
    m_impl->m_Zoom100Action =
        m_impl->m_ToolBar->addAction(iconManager->getIcon("zoom_100"), "Zoom 100%", this, &ImageViewControl::zoom100);

    if (mode == ViewControlMode::standard && supportsDockWidgets)
    {
        m_impl->m_MaximizeAction = m_impl->m_ToolBar->addAction(
            iconManager->getIcon("fullscreen"), "Maximize image view", this, &ImageViewControl::switchMaximized);
        m_impl->m_MaximizeAction->setToolTip("Maximize image view (F11)");
    }

    m_impl->m_PresentationChoiceActions.emplace_back(m_impl->m_ToolBar->addSeparator());

    // Create the image view channel combobox
    m_impl->m_imageViewChannelComboBox = new QComboBox();
    m_impl->m_imageViewChannelComboBox->setAccessibleName(QStringLiteral("View Channel"));
    m_impl->m_imageViewChannelComboBox->setToolTip(QStringLiteral("View Channel"));

    m_impl->m_imageViewChannelComboBox->setMaxVisibleItems(22);
    m_impl->m_imageViewChannelComboBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_impl->m_imageViewChannelComboBox->setMinimumContentsLength(9);
    m_impl->m_imageViewChannelComboBox->setMinimumWidth(kImageChannelComboMinWidth);

    m_impl->m_PresentationChoiceActions.emplace_back(m_impl->m_ToolBar->addWidget(m_impl->m_imageViewChannelComboBox));

    QObject::connect(m_impl->m_imageViewChannelComboBox, &QComboBox::currentTextChanged, this,
                     &ImageViewControl::on_imageViewChannelComboBox_currentTextChanged);

    m_impl->m_PresentationChoiceActions.emplace_back(m_impl->m_ToolBar->addSeparator());

    // Create the image view mode combobox
    if (mode == ViewControlMode::standard)
    {
        m_impl->m_ViewControlTextOverlay = new ViewControlTextOverlay();
        m_impl->m_ViewControlTextOverlay->move(15, 15);
        m_impl->m_ViewControlTextOverlay->setText("AUTOSTRETCH", an::ui::imageViewOverlayAutostretchColor(darkMode));
        m_impl->m_ViewControlTextOverlay->setEnabled(false);

        m_impl->m_imageViewModeComboBox = new QComboBox();
        m_impl->m_imageViewModeComboBox->setAccessibleName(QStringLiteral("Display Mode"));
        m_impl->m_imageViewModeComboBox->setToolTip(QStringLiteral("Display Mode"));

        m_impl->m_ToolBar->addWidget(m_impl->m_imageViewModeComboBox);

        QObject::connect(m_impl->m_imageViewModeComboBox, &QComboBox::currentTextChanged, this,
                         &ImageViewControl::on_imageViewModeComboBox_currentTextChanged);
        m_impl->m_imageViewModeComboBox->setEnabled(true);

        m_impl->m_imageViewModeComboBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        m_impl->m_imageViewModeComboBox->setMinimumContentsLength(12);
        m_impl->m_imageViewModeComboBox->setMinimumWidth(kImageModeComboMinWidth);

        m_impl->m_AutostretchSettingsAction = m_impl->m_ToolBar->addAction(
            iconManager->getIcon("settings"), "Autostretch Settings", this, &ImageViewControl::autostretchSettings);
        m_impl->m_AutostretchSettingsAction->setToolTip("Autostretch Settings (Ctrl+Shift+A)");
        m_impl->refreshAutostretchSettingsAvailability();
        m_impl->m_ToolBar->addSeparator();

        m_impl->m_UndoAction =
            m_impl->m_ToolBar->addAction(iconManager->getIcon("undo"), "Undo", this, &ImageViewControl::undo);
        m_impl->m_RedoAction =
            m_impl->m_ToolBar->addAction(iconManager->getIcon("redo"), "Redo", this, &ImageViewControl::redo);
        m_impl->m_HistoryAction = m_impl->m_ToolBar->addAction(iconManager->getIcon("history"), "Image History", this,
                                                               &ImageViewControl::history);
        m_impl->m_HistoryAction->setToolTip("Image History (Ctrl+Shift+Z)");

        m_impl->m_ToolBar->addSeparator();

        m_impl->m_StatisticsAction = m_impl->m_ToolBar->addAction(iconManager->getIcon("stats"), "Statistics", this,
                                                                  &ImageViewControl::executeStatistics);
        m_impl->m_StatisticsAction->setToolTip("Statistics (Ctrl+Shift+T)");
        m_impl->m_HistogramAction = m_impl->m_ToolBar->addAction(iconManager->getIcon("histogram"), "Histogram", this,
                                                                 &ImageViewControl::executeHistogram);
        m_impl->m_HistogramAction->setToolTip("Histogram (Ctrl+H)");
        m_impl->m_EXIFViewerAction = m_impl->m_ToolBar->addAction(iconManager->getIcon("camera"), "EXIF Viewer", this,
                                                                  &ImageViewControl::executeEXIFViewer);
        m_impl->m_EXIFViewerAction->setToolTip("EXIF Viewer (Ctrl+Shift+X)");
        m_impl->m_FITSViewerAction = m_impl->m_ToolBar->addAction(iconManager->getIcon("header"), "FITS Header", this,
                                                                  &ImageViewControl::executeFITSHeaderViewer);
        m_impl->m_FITSViewerAction->setToolTip("FITS Header (Ctrl+Shift+H)");
        m_impl->m_ImageSummaryAction = m_impl->m_ToolBar->addAction(iconManager->getIcon("summary"), "Summary", this,
                                                                    &ImageViewControl::executeSummaryViewer);
        m_impl->m_ImageSummaryAction->setToolTip("Summary (Ctrl+Shift+M)");

        m_impl->m_ToolBar->addSeparator();

        // Astrometry overlay toggle (master switch)
        m_impl->m_AstrometryToggleAction =
            m_impl->m_ToolBar->addAction(iconManager->getIcon("star"), tr("Show Astrometry"));
        m_impl->m_AstrometryToggleAction->setCheckable(true);
        m_impl->m_AstrometryToggleAction->setChecked(astrometryOverlayEnabled);
        m_impl->m_AstrometryToggleAction->setToolTip(tr("Toggle astrometry overlays on solved images (Ctrl+Shift+W)"));

        // Astrometry settings button with popup menu
        m_impl->m_AstrometrySettingsMenu = new QMenu(this);

        m_impl->m_AstrometryGridMenuAction = m_impl->m_AstrometrySettingsMenu->addAction(tr("Equatorial Grid\tCtrl+1"));
        m_impl->m_AstrometryGridMenuAction->setCheckable(true);
        m_impl->m_AstrometryGridMenuAction->setChecked(coordinateGridEnabled);

        m_impl->m_AstrometryAnnotationsMenuAction =
            m_impl->m_AstrometrySettingsMenu->addAction(tr("Deep-Sky Annotations\tCtrl+2"));
        m_impl->m_AstrometryAnnotationsMenuAction->setCheckable(true);
        m_impl->m_AstrometryAnnotationsMenuAction->setChecked(catalogAnnotationsEnabled);

        m_impl->m_AstrometryStarLabelsMenuAction =
            m_impl->m_AstrometrySettingsMenu->addAction(tr("Star Labels\tCtrl+3"));
        m_impl->m_AstrometryStarLabelsMenuAction->setCheckable(true);
        m_impl->m_AstrometryStarLabelsMenuAction->setChecked(starLabelsEnabled);

        m_impl->m_AstrometrySipMenuAction =
            m_impl->m_AstrometrySettingsMenu->addAction(tr("SIP Distortion Map\tCtrl+5"));
        m_impl->m_AstrometrySipMenuAction->setCheckable(true);
        m_impl->m_AstrometrySipMenuAction->setChecked(distortionMapEnabled);

        m_impl->m_AstrometryDetectedStarsMenuAction =
            m_impl->m_AstrometrySettingsMenu->addAction(tr("Detected Stars\tCtrl+4"));
        m_impl->m_AstrometryDetectedStarsMenuAction->setCheckable(true);
        m_impl->m_AstrometryDetectedStarsMenuAction->setChecked(detectedStarsEnabled);

        m_impl->m_AstrometrySettingsMenu->addSeparator();

        // What the annotations name and how they look: an annotated picture's choices, named as Annotate Image names
        // them, each submenu a group of exclusive items.
        const auto addChoices = [this](const QString& title, const QString& name,
                                       const std::vector<std::tuple<QString, int, QString>>& items, const int chosen) {
            QMenu* menu = m_impl->m_AstrometrySettingsMenu->addMenu(title);
            menu->setObjectName(name);
            menu->setToolTipsVisible(true);
            auto* group = new QActionGroup(menu);
            group->setExclusive(true);
            for (const auto& [label, value, tip] : items)
            {
                QAction* action = menu->addAction(label);
                action->setCheckable(true);
                action->setData(value);
                action->setToolTip(tip);
                action->setChecked(value == chosen);
                group->addAction(action);
            }
            return menu;
        };
        std::vector<std::tuple<QString, int, QString>> details;
        for (const an::ui::AnnotationDetail detail : an::ui::annotationDetails())
        {
            details.emplace_back(an::ui::annotationDetailName(detail), static_cast<int>(detail),
                                 an::ui::annotationDetailDescription(detail));
        }
        m_impl->m_AstrometryDetailSubMenu = addChoices(tr("Level of Detail"), QStringLiteral("astrometryDetailMenu"),
                                                       details, static_cast<int>(annotationChoices.detail));
        m_impl->m_AstrometryLabelsSubMenu =
            addChoices(tr("Labels"), QStringLiteral("astrometryLabelsMenu"),
                       {{tr("Designation"), static_cast<int>(an::ui::AnnotationLabelText::Designation),
                         tr("An object's designation, or its name when it has none: M 42, α Lyr")},
                        {tr("Name"), static_cast<int>(an::ui::AnnotationLabelText::Name),
                         tr("An object's common name, or its designation when it has none: Orion Nebula, Vega")},
                        {tr("Designation and Name"), static_cast<int>(an::ui::AnnotationLabelText::DesignationAndName),
                         tr("An object's designation, with its common name on a second line")}},
                       static_cast<int>(annotationChoices.labelText));
        std::vector<std::tuple<QString, int, QString>> palettes;
        for (const an::ui::AnnotationPalette palette : an::ui::annotationPalettes())
        {
            palettes.emplace_back(an::ui::annotationPaletteName(palette), static_cast<int>(palette), QString());
        }
        m_impl->m_AstrometryPaletteSubMenu = addChoices(tr("Palette"), QStringLiteral("astrometryPaletteMenu"),
                                                        palettes, static_cast<int>(annotationChoices.palette));
        const auto textSizeTip = [](const an::ui::AnnotationViewTextSize size) {
            switch (size)
            {
            case an::ui::AnnotationViewTextSize::Small:
                return tr("Labels as large as the interface's captions");
            case an::ui::AnnotationViewTextSize::Medium:
                break;
            case an::ui::AnnotationViewTextSize::Large:
                return tr("Labels half as large again as the interface's captions");
            }
            return tr("Labels a quarter larger than the interface's captions");
        };
        std::vector<std::tuple<QString, int, QString>> textSizes;
        for (const an::ui::AnnotationViewTextSize size : an::ui::annotationViewTextSizes())
        {
            textSizes.emplace_back(an::ui::annotationViewTextSizeName(size), static_cast<int>(size), textSizeTip(size));
        }
        m_impl->m_AstrometryTextSizeSubMenu = addChoices(tr("Text Size"), QStringLiteral("astrometryTextSizeMenu"),
                                                         textSizes, static_cast<int>(annotationChoices.textSize));

        m_impl->m_AstrometrySettingsAction =
            m_impl->m_ToolBar->addAction(iconManager->getIcon("settings"), tr("Astrometry Settings"));
        m_impl->m_AstrometrySettingsAction->setToolTip(tr("Configure astrometry overlay display options"));
        m_impl->m_AstrometrySettingsAction->setEnabled(astrometryOverlayEnabled);

        // Astrometry info button
        m_impl->m_AstrometryInfoAction =
            m_impl->m_ToolBar->addAction(iconManager->getIcon("info"), tr("Astrometry Info"));
        m_impl->m_AstrometryInfoAction->setToolTip(tr("Show astrometry solution details (Ctrl+Shift+P)"));

        // Annotated picture button (shown only for a solved image)
        m_impl->m_AnnotateImageToolbarAction =
            m_impl->m_ToolBar->addAction(iconManager->getIcon("panorama"), tr("Annotate Image"));
        m_impl->m_AnnotateImageToolbarAction->setToolTip(
            tr("Export a picture of the solved image with its objects, stars and coordinates named (Ctrl+Shift+L)"));

        // Plate solve button (shown only when no astrometry)
        m_impl->m_PlateSolveToolbarAction =
            m_impl->m_ToolBar->addAction(iconManager->getIcon("global"), tr("Plate Solve"));
        m_impl->m_PlateSolveToolbarAction->setToolTip(tr("Solve the image astrometrically (Ctrl+P)"));

        m_impl->m_ToolBar->addSeparator();
    }
    else if (mode == ViewControlMode::rt_preview)
    {
        m_impl->m_ViewControlTextOverlay = new ViewControlTextOverlay();
        setRTPreviewShowOriginalLabel(false);

        m_impl->m_ViewControlTextOverlay->move(15, 15);

        m_impl->m_RTPreviewShowOriginalAction =
            m_impl->m_ToolBar->addAction(iconManager->getIcon("toggle_on"), "Switch image", this,
                                         &ImageViewControl::on_rtpreview_switch_images_clicked);
        m_impl->m_RTPreviewShowOriginalAction->setToolTip("Switch image (Space / Mouse Right-Click / Ctrl+Shift+O)");

        m_impl->m_chkRTPreviewArea = new QCheckBox(tr("Preview area"));
        m_impl->m_chkRTPreviewArea->setObjectName(QStringLiteral("chkRTPreviewArea"));
        m_impl->m_chkRTPreviewArea->setToolTip(tr("Limit real-time preview processing to a selected area"));
        m_impl->m_RTPreviewAreaAction = m_impl->m_ToolBar->addWidget(m_impl->m_chkRTPreviewArea);

        QWidget* spacerWidget = new QWidget(this);
        spacerWidget->setProperty("anToolbarSpacer", true);
        spacerWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        spacerWidget->setVisible(true);

        m_impl->m_RTPreviewTakeSnapshotActionSpacer = m_impl->m_ToolBar->addWidget(spacerWidget);
        m_impl->m_RTPreviewTakeSnapshotAction =
            m_impl->m_ToolBar->addAction(iconManager->getIcon("camera"), "Take snapshot", this,
                                         &ImageViewControl::on_rtpreview_take_snapshot_clicked);
        m_impl->m_RTPreviewTakeSnapshotAction->setToolTip("Take snapshot (Ctrl+Shift+K)");
    }
    else if (mode == ViewControlMode::st_preview)
    {
        m_impl->m_ViewControlTextOverlay = new ViewControlTextOverlay();
        m_impl->m_ViewControlTextOverlay->move(15, 15);
        m_impl->m_ViewControlTextOverlay->setText("AUTOSTRETCH", an::ui::imageViewOverlayAutostretchColor(darkMode));
        m_impl->m_ViewControlTextOverlay->setEnabled(false);

        m_impl->m_chkCalibrationPreview = new QCheckBox(tr("Calibrated"));
        m_impl->m_chkCalibrationPreview->setObjectName(QStringLiteral("chkCalibrationPreview"));
        m_impl->m_chkCalibrationPreview->setVisible(false);
        m_impl->m_chkCalibrationPreview->setEnabled(false);
        QObject::connect(m_impl->m_chkCalibrationPreview, &QCheckBox::toggled, this, [this](bool) {
            m_impl->updateCalibrationToolTip();
            restartStaticPreviewLoad();
        });
        m_impl->m_CalibrationPreviewAction = m_impl->m_ToolBar->addWidget(m_impl->m_chkCalibrationPreview);
        if (m_impl->m_CalibrationPreviewAction)
        {
            // The toolbar hands the toggle its action's enabled state at every change of the action, and an action is
            // enabled again whenever it is shown unless it was disabled explicitly: the toggle's state is the action's.
            m_impl->m_CalibrationPreviewAction->setEnabled(false);
            m_impl->m_CalibrationPreviewAction->setVisible(false);
        }

        m_impl->m_imageViewModeComboBox = new QComboBox();
        m_impl->m_imageViewModeComboBox->setAccessibleName(QStringLiteral("Display Mode"));
        m_impl->m_imageViewModeComboBox->setToolTip(QStringLiteral("Display Mode"));

        m_impl->m_PresentationChoiceActions.emplace_back(m_impl->m_ToolBar->addWidget(m_impl->m_imageViewModeComboBox));

        QObject::connect(m_impl->m_imageViewModeComboBox, &QComboBox::currentTextChanged, this,
                         &ImageViewControl::on_imageViewModeComboBox_currentTextChanged);
        m_impl->m_imageViewModeComboBox->setEnabled(true);

        m_impl->m_imageViewModeComboBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        m_impl->m_imageViewModeComboBox->setMinimumContentsLength(12);
        m_impl->m_imageViewModeComboBox->setMinimumWidth(kImageModeComboMinWidth);

        m_impl->m_AutostretchSettingsAction = m_impl->m_ToolBar->addAction(
            iconManager->getIcon("settings"), "Autostretch Settings", this, &ImageViewControl::autostretchSettings);
        m_impl->m_AutostretchSettingsAction->setToolTip(tr("Autostretch Settings (Ctrl+Shift+A)"));
        m_impl->m_PresentationChoiceActions.emplace_back(m_impl->m_AutostretchSettingsAction);
        m_impl->refreshAutostretchSettingsAvailability();

        m_impl->m_StatsOverlayToggleAction = m_impl->m_ToolBar->addAction(
            iconManager->getIcon("stats"), tr("Show statistics"), this, [this](bool checked) {
                m_impl->m_StaticPreviewStatsOverlayVisible = checked;
                m_impl->updateStaticPreviewStatsOverlay(m_impl->m_StaticPreviewOverlayStats);
            });
        m_impl->m_StatsOverlayToggleAction->setCheckable(true);
        m_impl->m_StatsOverlayToggleAction->setChecked(true);
        m_impl->m_StatsOverlayToggleAction->setToolTip(tr("Show / hide image statistics overlay"));
        m_impl->m_PresentationChoiceActions.emplace_back(m_impl->m_StatsOverlayToggleAction);

        m_impl->updateCalibrationToolTip();
    }

    m_impl->m_CanvasLayout = new QVBoxLayout(this);
    setLayout(m_impl->m_CanvasLayout);

    m_impl->m_NoImagePreviewWidget = new NoImagePreviewWidget(nullptr);

    m_impl->m_ScrollArea = new ImageViewScrollArea();

    m_impl->m_ScrollArea->setViewControl(this);

    if (m_impl->m_ViewControlMode == ViewControlMode::st_preview)
    {
        m_impl->m_CanvasLayout->addWidget(m_impl->m_ToolBar);
    }
    else
    {
        m_impl->m_CanvasLayout->setMenuBar(m_impl->m_ToolBar);
    }
    m_impl->m_CanvasLayout->addWidget(m_impl->m_ScrollArea);
    m_impl->m_CanvasLayout->addWidget(m_impl->m_NoImagePreviewWidget);

    m_impl->m_CanvasLayout->setContentsMargins(QMargins(0, 0, 0, 0));
    m_impl->m_CanvasLayout->setSpacing(0);

    m_impl->m_ImageViewWidget = new ImageViewWidget(this);
    m_impl->m_ImageViewWidget->installEventFilter(this);
    m_impl->m_ImageViewWidget->setBackgroundRole(QPalette::Base);
    m_impl->m_ImageViewWidget->enablePerformanceMode(m_impl->m_ViewControlMode == ViewControlMode::rt_preview);

    applyImageViewScrollAreaBackground(m_impl->m_ScrollArea, darkMode);

    m_impl->m_ScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_impl->m_ScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_impl->m_ScrollArea->setContentsMargins(QMargins(0, 0, 0, 0));
    m_impl->m_ScrollArea->setWidgetResizable(true);
    m_impl->m_ScrollArea->setAlignment(Qt::AlignCenter);

    m_impl->m_ScrollArea->setWidget(m_impl->m_ImageViewWidget);
    if (m_impl->m_ViewControlMode == ViewControlMode::st_preview)
    {
        m_impl->m_StaticPreviewStatsOverlay = new StatsOverlayWidget(m_impl->m_ScrollArea->viewport());
        m_impl->m_StaticPreviewStatsOverlay->setObjectName(QStringLiteral("staticPreviewStatsOverlay"));

        m_impl->m_StaticPreviewLoadingSpinner = new WaitingSpinnerWidget(m_impl->m_ScrollArea, true /*centerOnParent*/,
                                                                         false /*disableParentWhenSpinning*/);
        m_impl->m_StaticPreviewLoadingSpinner->setColor(an::ui::tokens().colors.accentPrimary);

        if (QWidget* viewport = m_impl->m_ScrollArea->viewport())
        {
            viewport->installEventFilter(this);
        }
    }
    if (m_impl->m_ViewControlMode == ViewControlMode::standard)
    {
        const auto attachOverlay = [this](std::unique_ptr<ImageViewWidgetDisplayModule>& slot,
                                          const an::ui::AstrometryOverlay overlay, const bool enabled) {
            slot = an::ui::createAstrometryOverlay(overlay, *this);
            if (slot)
            {
                slot->attachToViewWidget(m_impl->m_ImageViewWidget);
                slot->enableDisplayModule(enabled);
            }
        };
        attachOverlay(m_impl->m_AstrometryStarDisplayModule, an::ui::AstrometryOverlay::DetectedStars,
                      astrometryOverlayEnabled && detectedStarsEnabled);
        attachOverlay(m_impl->m_CoordinateGridDisplayModule, an::ui::AstrometryOverlay::CoordinateGrid,
                      astrometryOverlayEnabled && coordinateGridEnabled);
        attachOverlay(m_impl->m_DistortionMapDisplayModule, an::ui::AstrometryOverlay::DistortionMap,
                      astrometryOverlayEnabled && distortionMapEnabled);
        attachOverlay(m_impl->m_CatalogAnnotationDisplayModule, an::ui::AstrometryOverlay::CatalogAnnotations, false);
        m_impl->showAnnotationChoices();
        m_impl->showCatalogAnnotations(astrometryOverlayEnabled, catalogAnnotationsEnabled, starLabelsEnabled);
    }
    const auto persistBoolSetting = [this](const QString& key, const bool enabled) {
        if (auto* persistence = an::ui::appContext()->getGlobalSettings())
        {
            persistence->upsert(key, enabled ? QStringLiteral("1") : QStringLiteral("0"));
        }
    };

    const auto persistStringSetting = [this](const QString& key, const QString& value) {
        if (auto* persistence = an::ui::appContext()->getGlobalSettings())
        {
            persistence->upsert(key, value);
        }
    };

    // Astrometry master toggle
    if (m_impl->m_AstrometryToggleAction)
    {
        QObject::connect(m_impl->m_AstrometryToggleAction, &QAction::toggled, this,
                         [this, persistBoolSetting](bool enabled) {
                             if (m_impl->m_AstrometrySettingsAction)
                             {
                                 m_impl->m_AstrometrySettingsAction->setEnabled(enabled);
                             }

                             const bool gridChecked =
                                 m_impl->m_AstrometryGridMenuAction && m_impl->m_AstrometryGridMenuAction->isChecked();
                             const bool annotationsChecked = m_impl->m_AstrometryAnnotationsMenuAction &&
                                                             m_impl->m_AstrometryAnnotationsMenuAction->isChecked();
                             const bool starLabelsChecked = m_impl->m_AstrometryStarLabelsMenuAction &&
                                                            m_impl->m_AstrometryStarLabelsMenuAction->isChecked();
                             const bool sipChecked =
                                 m_impl->m_AstrometrySipMenuAction && m_impl->m_AstrometrySipMenuAction->isChecked();
                             const bool starsChecked = m_impl->m_AstrometryDetectedStarsMenuAction &&
                                                       m_impl->m_AstrometryDetectedStarsMenuAction->isChecked();

                             if (m_impl->m_CoordinateGridDisplayModule)
                             {
                                 m_impl->m_CoordinateGridDisplayModule->enableDisplayModule(enabled && gridChecked);
                             }
                             if (m_impl->m_DistortionMapDisplayModule)
                             {
                                 m_impl->m_DistortionMapDisplayModule->enableDisplayModule(enabled && sipChecked);
                             }
                             m_impl->showCatalogAnnotations(enabled, annotationsChecked, starLabelsChecked);
                             if (m_impl->m_AstrometryStarDisplayModule)
                             {
                                 m_impl->m_AstrometryStarDisplayModule->enableDisplayModule(enabled && starsChecked);
                             }
                             // Master toggle state intentionally not persisted; starts unchecked per session.
                             if (m_impl->m_ImageViewWidget)
                             {
                                 m_impl->m_ImageViewWidget->update();
                             }
                         });
    }

    // Astrometry settings button opens popup menu
    if (m_impl->m_AstrometrySettingsAction && m_impl->m_AstrometrySettingsMenu)
    {
        QObject::connect(m_impl->m_AstrometrySettingsAction, &QAction::triggered, this, [this]() {
            auto* toolButton =
                qobject_cast<QToolButton*>(m_impl->m_ToolBar->widgetForAction(m_impl->m_AstrometrySettingsAction));
            if (toolButton && toolButton->isVisible())
            {
                m_impl->m_AstrometrySettingsMenu->popup(toolButton->mapToGlobal(QPoint(0, toolButton->height())));
            }
            else
            {
                m_impl->m_AstrometrySettingsMenu->popup(QCursor::pos());
            }
        });
    }

    // Grid menu action
    if (m_impl->m_AstrometryGridMenuAction)
    {
        QObject::connect(m_impl->m_AstrometryGridMenuAction, &QAction::toggled, this,
                         [this, persistBoolSetting](bool enabled) {
                             const bool masterEnabled =
                                 m_impl->m_AstrometryToggleAction && m_impl->m_AstrometryToggleAction->isChecked();
                             if (m_impl->m_CoordinateGridDisplayModule)
                             {
                                 m_impl->m_CoordinateGridDisplayModule->enableDisplayModule(masterEnabled && enabled);
                             }
                             persistBoolSetting(SettingsKeys::PLATESOLVE_COORDINATE_GRID_ENABLED, enabled);
                             if (m_impl->m_ImageViewWidget)
                             {
                                 m_impl->m_ImageViewWidget->update();
                             }
                         });
    }

    // Annotations menu action
    if (m_impl->m_AstrometryAnnotationsMenuAction)
    {
        QObject::connect(m_impl->m_AstrometryAnnotationsMenuAction, &QAction::toggled, this,
                         [this, persistBoolSetting](bool enabled) {
                             const bool masterEnabled =
                                 m_impl->m_AstrometryToggleAction && m_impl->m_AstrometryToggleAction->isChecked();
                             const bool starLabelsChecked = m_impl->m_AstrometryStarLabelsMenuAction &&
                                                            m_impl->m_AstrometryStarLabelsMenuAction->isChecked();
                             m_impl->showCatalogAnnotations(masterEnabled, enabled, starLabelsChecked);
                             persistBoolSetting(SettingsKeys::PLATESOLVE_CATALOG_ANNOTATIONS_ENABLED, enabled);
                             if (m_impl->m_ImageViewWidget)
                             {
                                 m_impl->m_ImageViewWidget->update();
                             }
                         });
    }

    if (m_impl->m_AstrometryStarLabelsMenuAction)
    {
        QObject::connect(m_impl->m_AstrometryStarLabelsMenuAction, &QAction::toggled, this,
                         [this, persistBoolSetting](bool enabled) {
                             const bool masterEnabled =
                                 m_impl->m_AstrometryToggleAction && m_impl->m_AstrometryToggleAction->isChecked();
                             const bool annotationsChecked = m_impl->m_AstrometryAnnotationsMenuAction &&
                                                             m_impl->m_AstrometryAnnotationsMenuAction->isChecked();
                             m_impl->showCatalogAnnotations(masterEnabled, annotationsChecked, enabled);
                             persistBoolSetting(SettingsKeys::PLATESOLVE_STAR_LABELS_ENABLED, enabled);
                             if (m_impl->m_ImageViewWidget)
                             {
                                 m_impl->m_ImageViewWidget->update();
                             }
                         });
    }

    // SIP distortion menu action
    if (m_impl->m_AstrometrySipMenuAction)
    {
        QObject::connect(m_impl->m_AstrometrySipMenuAction, &QAction::toggled, this,
                         [this, persistBoolSetting](bool enabled) {
                             const bool masterEnabled =
                                 m_impl->m_AstrometryToggleAction && m_impl->m_AstrometryToggleAction->isChecked();
                             if (m_impl->m_DistortionMapDisplayModule)
                             {
                                 m_impl->m_DistortionMapDisplayModule->enableDisplayModule(masterEnabled && enabled);
                             }
                             persistBoolSetting(SettingsKeys::PLATESOLVE_DISTORTION_MAP_ENABLED, enabled);
                             if (m_impl->m_ImageViewWidget)
                             {
                                 m_impl->m_ImageViewWidget->update();
                             }
                         });
    }

    if (m_impl->m_AstrometryDetectedStarsMenuAction)
    {
        QObject::connect(m_impl->m_AstrometryDetectedStarsMenuAction, &QAction::toggled, this,
                         [this, persistBoolSetting](bool enabled) {
                             const bool masterEnabled =
                                 m_impl->m_AstrometryToggleAction && m_impl->m_AstrometryToggleAction->isChecked();
                             if (m_impl->m_AstrometryStarDisplayModule)
                             {
                                 m_impl->m_AstrometryStarDisplayModule->enableDisplayModule(masterEnabled && enabled);
                             }
                             persistBoolSetting(SettingsKeys::PLATESOLVE_DETECTED_STARS_ENABLED, enabled);
                             if (m_impl->m_ImageViewWidget)
                             {
                                 m_impl->m_ImageViewWidget->update();
                             }
                         });
    }

    // The level of detail, the labels, the palette and the text size: each choice is handed to the overlays and kept.
    const auto connectChoices = [this, persistStringSetting](QMenu* menu,
                                                             void (*choose)(an::ui::AnnotationViewChoices&, int)) {
        if (menu == nullptr)
        {
            return;
        }
        QObject::connect(menu, &QMenu::triggered, this, [this, persistStringSetting, choose](QAction* action) {
            bool ok = false;
            const int value = action != nullptr ? action->data().toInt(&ok) : 0;
            if (!ok)
            {
                return;
            }
            choose(m_impl->m_AnnotationChoices, value);
            m_impl->showAnnotationChoices();
            persistStringSetting(SettingsKeys::ANNOTATION_VIEW_CHOICES,
                                 an::ui::storedAnnotationViewChoices(m_impl->m_AnnotationChoices));
            if (m_impl->m_ImageViewWidget)
            {
                m_impl->m_ImageViewWidget->update();
            }
        });
    };
    connectChoices(m_impl->m_AstrometryDetailSubMenu, [](an::ui::AnnotationViewChoices& choices, const int value) {
        choices.detail = static_cast<an::ui::AnnotationDetail>(value);
    });
    connectChoices(m_impl->m_AstrometryLabelsSubMenu, [](an::ui::AnnotationViewChoices& choices, const int value) {
        choices.labelText = static_cast<an::ui::AnnotationLabelText>(value);
    });
    connectChoices(m_impl->m_AstrometryPaletteSubMenu, [](an::ui::AnnotationViewChoices& choices, const int value) {
        choices.palette = static_cast<an::ui::AnnotationPalette>(value);
    });
    connectChoices(m_impl->m_AstrometryTextSizeSubMenu, [](an::ui::AnnotationViewChoices& choices, const int value) {
        choices.textSize = static_cast<an::ui::AnnotationViewTextSize>(value);
    });

    // Astrometry info button
    if (m_impl->m_AstrometryInfoAction)
    {
        QObject::connect(m_impl->m_AstrometryInfoAction, &QAction::triggered, this,
                         &ImageViewControl::executeAstrometryInfoViewer);
    }

    // Plate solve toolbar button
    if (m_impl->m_PlateSolveToolbarAction)
    {
        QObject::connect(m_impl->m_PlateSolveToolbarAction, &QAction::triggered, this,
                         &ImageViewControl::executePlateSolve);
    }

    if (m_impl->m_AnnotateImageToolbarAction)
    {
        QObject::connect(m_impl->m_AnnotateImageToolbarAction, &QAction::triggered, this,
                         &ImageViewControl::executeAnnotateImage);
    }

    m_impl->m_ScrollArea->setOverlayWidget(m_impl->m_ViewControlTextOverlay);

    if (m_impl->m_ViewControlMode == ViewControlMode::standard)
    {
        an::ui::appContext()->getViewControlManager()->addViewControl(this);
    }

    if (m_impl->m_ViewControlMode == ViewControlMode::standard)
    {
        m_impl->m_History = an::ui::createViewHistory(*this);
        upcateImageHistory();
        updateImageActions();
    }
    else
    {
        m_impl->m_History.reset();
    }

    m_impl->m_ImageZoomAndPanController = std::make_unique<ImageZoomAndPanController>(m_impl->m_ScrollArea);
    if (auto* customToolBar = dynamic_cast<CustomToolBar*>(m_impl->m_ToolBar))
    {
        customToolBar->refreshOverflow();
    }

    if (m_impl->m_ViewControlMode == ViewControlMode::st_preview)
    {
        setPreviewContentVisible(false);
    }
}

void ImageViewControl::initializeNewImageHistory(const std::string& historyDesc)
{
    if (m_impl->m_ViewControlMode == ViewControlMode::standard && m_impl->m_History)
    {
        m_impl->m_History->registerInitialStep(historyDesc);
    }
}

void ImageViewControl::loadImageFromFile(const std::string& fileName)
{
    CHECK_IF_CONTROL_IS_CLOSING;

    const QString path = QString::fromUtf8(fileName);
    if (m_impl->m_ViewControlMode == ViewControlMode::st_preview)
    {
        const bool pathChanged = (path != m_impl->m_StaticPreviewCurrentPath);
        m_impl->m_StaticPreviewCurrentPath = path;
        m_impl->m_StaticPreviewAutoFitNextImage = !path.isEmpty() && (pathChanged || !m_impl->hasImage());

        if (path.isEmpty())
        {
            clear();
            return;
        }

        m_impl->updateStaticPreviewStatsOverlay(std::nullopt);
        if (!m_impl->hasImage())
        {
            setPreviewContentVisible(false);
        }
        else if (m_impl->m_StaticPreviewLoadingSpinner)
        {
            m_impl->m_StaticPreviewLoadingSpinner->start();
        }
    }

    requestRead(path);
}

void ImageViewControl::requestRead(const QString& path)
{
    const an::consumer::EngineSession* session = viewSession();
    ReadJob job;
    job.ticket = m_impl->m_Reads.issueUnbound();
    job.path = path;
    job.preview = m_impl->m_ViewControlMode == ViewControlMode::st_preview;
    ++m_impl->m_Telemetry.readsRequested;
    if (session == nullptr)
    {
        failRead(path, an::Status::failure(an::ErrorCode::NotInitialized, "no engine session is lent to the view"));
        return;
    }

    an::ui::ViewReadRequest request;
    request.path = path;
    // A camera file is read under the RAW policy published when the read is asked for, and keeps it until it lands.
    if (auto* context = an::ui::appContext())
    {
        if (an::ui::IRawOpenPolicyService* raw = context->getRawOpenPolicyService())
        {
            job.raw = raw->rawOpenPolicy();
        }
    }
    if (job.raw)
    {
        request.registry = &job.raw->registry();
        request.raw = job.raw->treatment();
    }
    if (job.preview)
    {
        request.calibration = m_impl->buildCalibrationPreview();
        job.calibrationKey = m_impl->buildCalibrationPreviewKey();
        job.autoDebayerCfa = m_impl->m_AutoDebayerCfaImagePreviews;
        // A preview of a mosaic shows it demosaiced by the fast method when the list asks for colour.
        if (job.autoDebayerCfa)
        {
            request.demosaic = an::DemosaicMethod::Fast;
        }

        if (const auto cached = m_impl->m_StaticPreviewCache.get(
                StaticPreviewCacheKey{path, job.calibrationKey, job.autoDebayerCfa, rawSettingsOf(job.raw)});
            cached.has_value())
        {
            // A preview read before is shown again from a copy of what was read, so the cache keeps its own.
            std::shared_ptr<const an::consumer::ImageAsset> kept = cached->asset;
            std::optional<an::ui::ViewImageStatistics> statistics = cached->statistics;
            auto shared = std::make_shared<const ReadJob>(std::move(job));
            runThenPublish(
                this, m_impl->m_Outstanding,
                [kept, stop = shared->ticket.stopToken()]() {
                    return an::ui::viewWorkOutcome([&]() { return kept->snapshot(stop); });
                },
                [shared, statistics](ImageViewControl& view, an::Result<an::consumer::ImageAsset>&& copy) {
                    if (statistics.has_value())
                    {
                        view.m_impl->m_StaticPreviewOverlayStats = statistics;
                    }
                    view.commitRead(*shared, std::move(copy), false);
                });
            return;
        }
    }
    else
    {
        // A standard view reads the mosaic as recorded and asks whether to demosaic it.
        request.demosaic.reset();
    }

    auto shared = std::make_shared<const ReadJob>(std::move(job));
    std::shared_ptr<an::ui::ViewMasterCache> masters = m_impl->m_StaticPreviewMasterCache;
    runThenPublish(
        this, m_impl->m_Outstanding,
        [session, request, masters, policy = shared->raw, stop = shared->ticket.stopToken()]() {
            // The policy is held for the read, whose request borrows its registry. A read that throws ends as a read
            // that failed: its spinner stops and its listeners are told, as for any refused read.
            (void)policy;
            return an::ui::viewWorkOutcome(
                [&]() { return an::ui::readViewImage(*session, request, stop, masters.get()); });
        },
        [shared](ImageViewControl& view, an::Result<an::ui::ViewReadResult>&& read) {
            if (!read.hasValue())
            {
                view.commitRead(*shared, read.status(), false);
                return;
            }
            const bool mosaic = read->mosaic;
            view.commitRead(*shared, std::move(read->asset), mosaic);
        });
}

void ImageViewControl::commitRead(const ReadJob& job, an::Result<an::consumer::ImageAsset>&& read, const bool mosaic)
{
    if (job.ticket.verdict() != an::ui::CommitVerdict::Commit || m_impl->m_ControlIsClosing.load())
    {
        // A read superseded by a later one, or of a view that is closing, is never shown.
        ++m_impl->m_Telemetry.readsDiscarded;
        return;
    }
    if (!read.hasValue())
    {
        failRead(job.path, read.status());
        return;
    }

    if (job.preview)
    {
        if (m_impl->m_StaticPreviewLoadingSpinner)
        {
            m_impl->m_StaticPreviewLoadingSpinner->stop();
        }
        showReadImage(std::move(*read), job.path);
        // A preview's document is never written, so the cache keeps the asset it shows and a later visit shows a copy.
        if (m_impl->hasImage())
        {
            m_impl->m_StaticPreviewCache.put(
                StaticPreviewCacheKey{job.path, job.calibrationKey, job.autoDebayerCfa, rawSettingsOf(job.raw)},
                StaticPreviewCachedImage{m_impl->m_Document->asset(), m_impl->m_StaticPreviewOverlayStats});
        }
        if (m_impl->m_StaticPreviewAutoFitNextImage)
        {
            fitToWindow();
            m_impl->m_StaticPreviewAutoFitNextImage = false;
        }
        return;
    }

    if (mosaic)
    {
        offerDemosaic(std::move(*read), job.path);
        return;
    }
    showReadImage(std::move(*read), job.path);
}

void ImageViewControl::offerDemosaic(an::consumer::ImageAsset&& mosaic, const QString& path)
{
    int answer = QMessageBox::No;
    if (auto* dialogs = an::ui::appContext() ? an::ui::appContext()->getDialogService() : nullptr)
    {
        QString text = tr("The image appears to have a bayer matrix\n\n");
        text.append(tr("Do you want to automatically debayer it before opening? (If no, you can always debayer it by "
                       "using the Debayer process)"));
        dialogs->messageBoxYesNo(nullptr, tr("Debayer Image"), text, false, &answer);
    }
    if (answer != QMessageBox::Yes)
    {
        showReadImage(std::move(mosaic), path);
        return;
    }

    // The one demosaic setting decides the method, as it does everywhere a mosaic is opened.
    const an::ui::DemosaicChoice choice = demosaicSettingChoice();

    const an::consumer::EngineSession* session = viewSession();
    if (session == nullptr)
    {
        showReadImage(std::move(mosaic), path);
        return;
    }
    ReadJob job;
    job.ticket = m_impl->m_Reads.issueUnbound();
    job.path = path;
    auto shared = std::make_shared<const ReadJob>(std::move(job));
    auto held = std::make_shared<an::consumer::ImageAsset>(std::move(mosaic));
    runThenPublish(
        this, m_impl->m_Outstanding,
        [session, held, method = choice.method, stop = shared->ticket.stopToken()]() {
            return an::ui::demosaicViewImage(*session, *held, method, stop);
        },
        [shared](ImageViewControl& view, an::Result<an::consumer::ImageAsset>&& colour) {
            view.commitRead(*shared, std::move(colour), false);
        });
}

void ImageViewControl::showReadImage(an::consumer::ImageAsset&& asset, const QString& path)
{
    const QFileInfo file(path);
    an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(
        std::move(asset), an::ui::DocumentNames{.displayName = file.completeBaseName(), .sourcePath = path});
    if (!document.hasValue())
    {
        failRead(path, document.status());
        return;
    }
    ++m_impl->m_Telemetry.readsCommitted;
    showDocumentInternal(std::make_shared<an::ui::ImageDocument>(std::move(*document)));
    notifyImageLoaded();
}

void ImageViewControl::failRead(const QString& path, const an::Status& status)
{
    ++m_impl->m_Telemetry.readsFailed;
    if (m_impl->m_StaticPreviewLoadingSpinner)
    {
        m_impl->m_StaticPreviewLoadingSpinner->stop();
    }

    const QString message = tr("'%1' could not be read: %2").arg(path, describeStatus(status));
    LOG_WARNING("Image view: {}", message.toStdString());

    if (m_impl->m_ViewControlMode == ViewControlMode::st_preview)
    {
        m_impl->m_StaticPreviewAutoFitNextImage = false;
        m_impl->updateStaticPreviewStatsOverlay(std::nullopt);
        showDocumentInternal(std::make_shared<an::ui::ImageDocument>());
    }

    std::vector<ImageViewControlListener*> listenersCopy;
    {
        std::shared_lock lock(m_impl->m_listeners_mutex);
        listenersCopy = m_impl->m_listeners;
    }

    for (auto& listener : listenersCopy)
    {
        listener->onImageLoadFailed(this, message);
    }
}

void ImageViewControl::clear()
{
    CHECK_IF_CONTROL_IS_CLOSING;

    m_impl->m_Reads.cancel();
    m_impl->m_Statistics.cancel();
    m_impl->m_StaticPreviewCurrentPath.clear();
    m_impl->m_StaticPreviewAutoFitNextImage = false;
    if (m_impl->m_StaticPreviewLoadingSpinner)
        m_impl->m_StaticPreviewLoadingSpinner->stop();
    m_impl->updateStaticPreviewStatsOverlay(std::nullopt);
    showDocumentInternal(std::make_shared<an::ui::ImageDocument>());
}

void ImageViewControl::setCalibrationPreviewConfig(const an::ui::CalibrationPreviewConfig& config)
{
    m_impl->m_CalibrationPreviewConfig = config;
    if (m_impl->m_chkCalibrationPreview)
    {
        const bool enabled = config.available && m_impl->hasCalibrationInput();
        if (m_impl->m_CalibrationPreviewAction)
        {
            m_impl->m_CalibrationPreviewAction->setEnabled(enabled);
        }
        m_impl->m_chkCalibrationPreview->setEnabled(enabled);
        m_impl->updateCalibrationToolTip();
        if (!m_impl->m_chkCalibrationPreview->isEnabled() && m_impl->m_chkCalibrationPreview->isChecked())
        {
            m_impl->m_chkCalibrationPreview->setChecked(false);
        }
    }

    if (auto* customToolBar = dynamic_cast<CustomToolBar*>(m_impl->m_ToolBar))
    {
        customToolBar->refreshOverflow();
    }
}

void ImageViewControl::setCalibrationPreviewVisible(bool visible)
{
    if (m_impl->m_chkCalibrationPreview)
    {
        m_impl->m_chkCalibrationPreview->setVisible(visible);
        if (m_impl->m_CalibrationPreviewAction)
        {
            m_impl->m_CalibrationPreviewAction->setVisible(visible);
        }
        if (auto* customToolBar = dynamic_cast<CustomToolBar*>(m_impl->m_ToolBar))
        {
            customToolBar->refreshOverflow();
        }
    }
}

bool ImageViewControl::isCalibrationPreviewEnabled() const
{
    return m_impl->isCalibrationPreviewEnabled();
}

void ImageViewControl::clearCalibrationCaches()
{
    m_impl->m_StaticPreviewCache.clear();
    if (m_impl->m_StaticPreviewMasterCache)
    {
        m_impl->m_StaticPreviewMasterCache->clear();
    }
}

void ImageViewControl::setAutoDebayerCfaImagePreviews(const bool enabled)
{
    if (m_impl->m_AutoDebayerCfaImagePreviews == enabled)
    {
        return;
    }

    m_impl->m_AutoDebayerCfaImagePreviews = enabled;
    m_impl->m_StaticPreviewCache.clear();
    m_impl->m_Reads.cancel();
}

bool ImageViewControl::isAutoDebayerCfaImagePreviewsEnabled() const
{
    return m_impl->m_AutoDebayerCfaImagePreviews;
}

an::Status ImageViewControl::saveImageToFile(const std::string& fileName, bool async)
{
    if (m_impl->m_ControlIsClosing.load(std::memory_order_acquire))
    {
        return an::Status::failure(an::ErrorCode::InvalidState, "the view is closing");
    }
    const std::shared_ptr<an::ui::ImageDocument> document = m_impl->m_Document;
    const an::consumer::EngineSession* session = viewSession();
    auto* context = an::ui::appContext();
    auto* intents = context != nullptr ? context->getOutputIntentProvider() : nullptr;
    if (!m_impl->hasImage() || session == nullptr || intents == nullptr)
    {
        ++m_impl->m_Telemetry.writesFailed;
        return an::Status::failure(an::ErrorCode::NotInitialized, "the view has no image, session or export settings");
    }

    const QString path = QString::fromUtf8(fileName);
    const an::CodecFormat container = an::codecFormatFromPath(fileName);
    const std::optional<an::ImageDescriptor> described = document->descriptor();
    if (!described.has_value())
    {
        ++m_impl->m_Telemetry.writesFailed;
        return an::Status::failure(an::ErrorCode::NotInitialized, "the view's image holds nothing");
    }
    an::Result<an::OutputIntent> intent = intents->buildOutputIntent(*described, container);
    if (!intent.hasValue())
    {
        ++m_impl->m_Telemetry.writesFailed;
        return intent.status();
    }

    // What is written is the revision held when the write started; the pending mark is cleared only if the document
    // still holds it when the write is done.
    const an::ui::DocumentRevision written = document->revision();
    const std::shared_ptr<const an::consumer::ImageAsset> asset = document->asset();
    const an::Status status = an::ui::writeViewImage(*session, *asset, path, *intent, m_impl->m_Closing.get_token());
    if (status.failed())
    {
        ++m_impl->m_Telemetry.writesFailed;
        LOG_WARNING("Image view: '{}' could not be written: {}", fileName, describeStatus(status).toStdString());
        return status;
    }
    ++m_impl->m_Telemetry.writesCompleted;

    QPointer<ImageViewControl> guard(this);
    auto saved = [guard, document, written, path]() {
        if (!guard || guard->m_impl->m_ControlIsClosing.load())
        {
            return;
        }
        if (guard->m_impl->m_Document == document && document->revision() == written)
        {
            an::ui::DocumentNames names = document->names();
            names.sourcePath = path;
            document->setNames(std::move(names));
            guard->notifyImageSaved();
        }
        else
        {
            LOG_INFO("Image view: '{}' changed while it was written; its changes stay pending", guard->getUniqueName());
        }
    };
    if (async || QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, std::move(saved), Qt::QueuedConnection);
    }
    else
    {
        saved();
    }
    return an::Status::success();
}

bool ImageViewControl::saveSequenceToFile(const std::string& fileName, bool async)
{
    if (m_impl->m_ControlIsClosing.load(std::memory_order_acquire) || !m_impl->m_SequencePlayer)
    {
        return false;
    }

    const an::Status saved = m_impl->m_SequencePlayer->saveTo(QString::fromUtf8(fileName));
    if (saved.failed())
    {
        LOG_WARNING("Image view: the sequence of '{}' could not be written: {}", getUniqueName(),
                    describeStatus(saved).toStdString());
        return false;
    }

    if (async)
    {
        notifyImageSavedAsync();
    }
    else
    {
        notifyImageSaved();
    }

    return true;
}

void ImageViewControl::loadImageFromOtherViewControl(ImageViewControl* other)
{
    CHECK_IF_CONTROL_IS_CLOSING;

    if (!other || !other->hasImage())
    {
        return;
    }

    // The copy is taken under a read lease on a worker, so a writer of the other view is waited for there and never
    // on the GUI thread.
    const std::shared_ptr<an::ui::ImageDocument> source = other->m_impl->m_Document;
    std::shared_ptr<const an::ui::ViewOverlayStars> stars = other->m_impl->m_OverlayStars;
    ReadJob job;
    job.ticket = m_impl->m_Reads.issueUnbound();
    job.path = source->names().displayName;
    ++m_impl->m_Telemetry.readsRequested;
    auto shared = std::make_shared<const ReadJob>(std::move(job));
    runThenPublish(
        this, m_impl->m_Outstanding, [source, stop = shared->ticket.stopToken()]() { return source->snapshot(stop); },
        [shared, stars](ImageViewControl& view, an::Result<an::consumer::ImageAsset>&& copy) {
            if (shared->ticket.verdict() != an::ui::CommitVerdict::Commit || view.m_impl->m_ControlIsClosing.load())
            {
                ++view.m_impl->m_Telemetry.readsDiscarded;
                return;
            }
            if (!copy.hasValue())
            {
                view.failRead(shared->path, copy.status());
                return;
            }
            // The copy is the view's own: shown under the view's name, with no file behind it.
            an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(
                std::move(*copy), an::ui::DocumentNames{.displayName = QString::fromUtf8(view.getUniqueName())});
            if (!document.hasValue())
            {
                view.failRead(shared->path, document.status());
                return;
            }
            ++view.m_impl->m_Telemetry.readsCommitted;
            view.showDocumentInternal(std::make_shared<an::ui::ImageDocument>(std::move(*document)));
            view.m_impl->m_OverlayStars = stars;
            if (view.m_impl->m_AstrometryStarDisplayModule)
            {
                an::ui::setAstrometryOverlayStars(*view.m_impl->m_AstrometryStarDisplayModule, stars);
            }
            view.notifyImageLoaded();
        });
}

void ImageViewControl::setDocument(an::ui::ImageDocument&& document)
{
    CHECK_IF_CONTROL_IS_CLOSING;

    m_impl->m_Reads.cancel();
    const std::optional<an::ImageDescriptor> described = document.descriptor();
    const an::consumer::EngineSession* session = viewSession();
    if (m_impl->m_AutoDebayerCfaImagePreviews && described.has_value() && described->isMosaiced() && session)
    {
        // A preview that shows colour shows a mosaic demosaiced by the fast method, as it does for one it reads.
        ReadJob job;
        job.ticket = m_impl->m_Reads.issueUnbound();
        job.path = document.names().sourcePath;
        ++m_impl->m_Telemetry.readsRequested;
        auto shared = std::make_shared<const ReadJob>(std::move(job));
        auto held = std::make_shared<an::ui::ImageDocument>(std::move(document));
        runThenPublish(
            this, m_impl->m_Outstanding,
            [session, held, stop = shared->ticket.stopToken()]() {
                return an::ui::demosaicViewImage(*session, *held->asset(), an::DemosaicMethod::Fast, stop);
            },
            [shared, held](ImageViewControl& view, an::Result<an::consumer::ImageAsset>&& colour) {
                if (shared->ticket.verdict() != an::ui::CommitVerdict::Commit || view.m_impl->m_ControlIsClosing.load())
                {
                    ++view.m_impl->m_Telemetry.readsDiscarded;
                    return;
                }
                if (!colour.hasValue())
                {
                    view.failRead(shared->path, colour.status());
                    return;
                }
                an::Result<an::ui::ImageDocument> shown =
                    an::ui::ImageDocument::create(std::move(*colour), held->names());
                held->close();
                if (!shown.hasValue())
                {
                    view.failRead(shared->path, shown.status());
                    return;
                }
                ++view.m_impl->m_Telemetry.readsCommitted;
                view.showDocumentInternal(std::make_shared<an::ui::ImageDocument>(std::move(*shown)));
                view.notifyImageLoaded();
            });
        return;
    }
    showDocumentInternal(std::make_shared<an::ui::ImageDocument>(std::move(document)));
    notifyImageLoaded();
}

void ImageViewControl::setSequenceDocument(an::ui::ImageDocument&& document, const bool autoDebayer)
{
    CHECK_IF_CONTROL_IS_CLOSING;

    m_impl->m_Reads.cancel();
    m_impl->m_SequenceAutoDebayer = autoDebayer;
    // A sequence that states no demosaic method takes the one demosaic setting now, on the GUI thread, so every worker
    // that demosaics one of its frames - the player's, a process's, the history's - reads the same method and no
    // setting.
    if (std::optional<an::ui::DocumentSequence> sequence = document.sequence();
        sequence.has_value() && !sequence->demosaic.has_value())
    {
        sequence->demosaic = demosaicSettingChoice().method;
        document.setSequence(std::move(sequence));
    }
    // The document carries the sequence as a value and the frame as its own asset, so nothing of the caller's sequence
    // is shared with the view.
    showDocumentInternal(std::make_shared<an::ui::ImageDocument>(std::move(document)));
    if (m_impl->sequence().has_value() && !m_impl->m_SequencePlayer)
    {
        m_impl->m_SequencePlayer = an::ui::createViewSequencePlayer(*this);
        if (m_impl->m_SequencePlayer && m_impl->m_SequencePlayer->widget())
        {
            m_impl->m_CanvasLayout->addWidget(m_impl->m_SequencePlayer->widget());
        }
    }

    notifyImageLoaded();
}

void ImageViewControl::showDocumentInternal(std::shared_ptr<an::ui::ImageDocument> document)
{
    invalidateAstrometryStarDetection(true);

    // Work begun on the image the view showed is refused from now on, and the document it showed is let go closed.
    m_impl->m_Statistics.cancel();
    m_impl->m_Edits.cancel();
    m_impl->m_PendingEdits.clear();
    const std::shared_ptr<an::ui::ImageDocument> previous = std::exchange(m_impl->m_Document, std::move(document));
    if (previous && previous != m_impl->m_Document)
    {
        previous->close();
    }

    if (m_impl->hasImage())
    {
        an::ui::DocumentNames names = m_impl->m_Document->names();
        if (names.displayName.isEmpty())
        {
            names.displayName = QString::fromUtf8(m_impl->m_UniqueName);
            m_impl->m_Document->setNames(std::move(names));
        }
        setPreviewContentVisible(true);
        updateSupportedImageChannels(true);
    }
    else
    {
        setPreviewContentVisible(false);
        updateSupportedImageChannels(true);
        if (m_impl->m_ImageViewWidget)
        {
            m_impl->m_ImageViewWidget->imageUpdated(false, false);
        }
    }
}

const an::ui::ImageDocument& ImageViewControl::document() const
{
    return *m_impl->m_Document;
}

an::ui::ImageDocument& ImageViewControl::document()
{
    return *m_impl->m_Document;
}

bool ImageViewControl::hasImage() const
{
    return m_impl->hasImage();
}

std::optional<an::ImageDescriptor> ImageViewControl::imageDescriptor() const
{
    return m_impl->descriptor();
}

void ImageViewControl::replaceImage(an::consumer::ImageAsset&& asset, const bool disablePendingChangesCheck,
                                    std::function<void(const an::Status&)> done)
{
    replaceImageWithPresentationAnalysis(std::move(asset), std::nullopt, disablePendingChangesCheck, std::move(done));
}

void ImageViewControl::replaceImageWithPresentationAnalysis(an::consumer::ImageAsset&& asset,
                                                            std::optional<an::DisplayPresentationAnalysis> analysis,
                                                            const bool disablePendingChangesCheck,
                                                            std::function<void(const an::Status&)> done)
{
    const std::shared_ptr<an::ui::ImageDocument> document = m_impl->m_Document;
    auto finish = [done = std::move(done)](const an::Status& status) {
        if (done)
        {
            done(status);
        }
    };
    if (m_impl->m_ControlIsClosing.load() || !m_impl->hasImage())
    {
        finish(an::Status::failure(an::ErrorCode::InvalidState, "the view shows no open document"));
        return;
    }

    // At once, whatever reads the document: what reads the image shown goes on reading it. Only a writer of the
    // document holds a replacement back, and then a worker waits for the writer and the GUI thread hears of it.
    const an::Status replaced = document->tryReplace(std::move(asset));
    if (replaced.ok())
    {
        notifyImageUpdatedImpl(std::move(analysis), disablePendingChangesCheck, ImageUpdateKind::PixelDataChanged);
        finish(replaced);
        return;
    }
    if (replaced.code() != an::ErrorCode::Busy)
    {
        finish(replaced);
        return;
    }

    auto held = std::make_shared<an::consumer::ImageAsset>(std::move(asset));
    runThenPublish(
        this, m_impl->m_Outstanding,
        [document, held, stop = m_impl->m_Closing.get_token()]() -> an::Status {
            an::Result<an::ui::ImageDocumentWriter> writer = document->write(stop);
            if (!writer.hasValue())
            {
                return writer.status();
            }
            return writer->replace(std::move(*held));
        },
        [document, analysis, disablePendingChangesCheck, finish](ImageViewControl& view, an::Status&& replaced) {
            if (replaced.ok() && view.m_impl->m_Document == document && !view.m_impl->m_ControlIsClosing.load())
            {
                view.notifyImageUpdatedImpl(analysis, disablePendingChangesCheck, ImageUpdateKind::PixelDataChanged);
            }
            finish(replaced);
        });
}

void ImageViewControl::fitWindowToImage()
{
    CHECK_IF_CONTROL_IS_CLOSING;

    if (windowState() & Qt::WindowMaximized)
    {
        return;
    }

    auto imgSize = m_impl->m_ImageViewWidget->rect();
    auto scrollAreaSize = m_impl->m_ScrollArea->rect();
    auto winSize = this->rect().size();

    auto sizeOffset = imgSize.size() - scrollAreaSize.size();

    winSize += sizeOffset + QSize(2, 2);

    bool minimumWidtAdjusted = false;
    if (winSize.width() < 600)
    {
        int diff = 600 - winSize.width();
        winSize.setWidth(600);
        winSize.setHeight(winSize.height() + diff);
        minimumWidtAdjusted = true;
    }

    this->resize(winSize);

    if (minimumWidtAdjusted)
    {
        fitToWindow();
    }
}

void ImageViewControl::executeStatistics()
{
    CHECK_IF_CONTROL_IS_CLOSING;

    (void)an::ui::openViewTool(an::ui::ViewTool::Statistics, *this);
}

void ImageViewControl::executeHistogram()
{
    CHECK_IF_CONTROL_IS_CLOSING;

    (void)an::ui::openViewTool(an::ui::ViewTool::Histogram, *this);
}

void ImageViewControl::executeFITSHeaderViewer()
{
    CHECK_IF_CONTROL_IS_CLOSING;

    (void)an::ui::openViewTool(an::ui::ViewTool::FitsHeader, *this);
}

void ImageViewControl::executeAstrometryInfoViewer()
{
    CHECK_IF_CONTROL_IS_CLOSING;

    (void)an::ui::openViewTool(an::ui::ViewTool::AstrometryInfo, *this);
}

void ImageViewControl::executeEXIFViewer()
{
    CHECK_IF_CONTROL_IS_CLOSING;

    (void)an::ui::openViewTool(an::ui::ViewTool::Exif, *this);
}

void ImageViewControl::executeSummaryViewer()
{
    CHECK_IF_CONTROL_IS_CLOSING;

    (void)an::ui::openViewTool(an::ui::ViewTool::Summary, *this);
}

void ImageViewControl::executePlateSolve()
{
    CHECK_IF_CONTROL_IS_CLOSING;

    (void)an::ui::openViewTool(an::ui::ViewTool::PlateSolve, *this);
}

void ImageViewControl::executeAnnotateImage()
{
    CHECK_IF_CONTROL_IS_CLOSING;

    (void)an::ui::openViewTool(an::ui::ViewTool::AnnotateImage, *this);
}

void ImageViewControl::adjustImageToWindow()
{
    CHECK_IF_CONTROL_IS_CLOSING;

    activateImageViewLayoutBeforeFit(this);
    m_impl->m_ImageViewWidget->fitInView();
}

void ImageViewControl::switchMaximized()
{
    auto* context = an::ui::appContext();
    auto* viewControlManager = context ? context->getViewControlManager() : nullptr;
    if (!m_impl->m_MaximizeAction || !viewControlManager || !viewControlManager->supportsDockWidgets())
    {
        return;
    }

    if (m_impl->m_IsMaximized)
    {
        m_impl->m_IsMaximized = false;

        m_impl->m_MaximizeAction->setIcon(context->getIconProvider()->getIcon("fullscreen"));
        m_impl->m_MaximizeAction->setToolTip("Maximize image view");
        // Restore last container state

        viewControlManager->restoreViewControl(this);
    }
    else
    {
        m_impl->m_MaximizeAction->setIcon(context->getIconProvider()->getIcon("fullscreen_exit"));
        m_impl->m_MaximizeAction->setToolTip("Restore image view");

        viewControlManager->maximizeViewControl(this);

        m_impl->m_IsMaximized = true;
    }
}

bool ImageViewControl::isMaximized() const
{
    return m_impl->m_IsMaximized;
}

void ImageViewControl::switchMaximizedExternal()
{
    if (!m_impl->m_MaximizeAction)
    {
        return;
    }

    if (m_impl->m_IsMaximized)
    {
        m_impl->m_MaximizeAction->setIcon(an::ui::appContext()->getIconProvider()->getIcon("fullscreen"));
        m_impl->m_MaximizeAction->setToolTip("Maximize image view");

        m_impl->m_IsMaximized = false;
    }
    else
    {
        m_impl->m_MaximizeAction->setIcon(an::ui::appContext()->getIconProvider()->getIcon("fullscreen_exit"));
        m_impl->m_MaximizeAction->setToolTip("Restore image view");

        m_impl->m_IsMaximized = true;
    }
}

void ImageViewControl::displayDefaultViewChannel()
{
    CHECK_IF_CONTROL_IS_CLOSING;

    if (!m_impl->hasImage())
    {
        return;
    }

    m_impl->m_ImageViewWidget->displayMainImage();
}

void ImageViewControl::displayViewChannel(const ImageCompositionMode channel)
{
    CHECK_IF_CONTROL_IS_CLOSING;

    if (!m_impl->hasImage())
    {
        return;
    }

    m_impl->m_ImaveCompositionMode.store(channel, std::memory_order_relaxed);
    // The widget presents the channel and, under a multiply, the mask over what it shows.
    m_impl->m_ImageViewWidget->displayChannel(an::ui::viewChannelForComposition(channel));
}

ImageCompositionMode ImageViewControl::displayedComposition() const
{
    return m_impl->m_ImaveCompositionMode.load(std::memory_order_relaxed);
}

void ImageViewControl::setUniqueName(const std::string& name)
{
    CHECK_IF_CONTROL_IS_CLOSING;

    bool renamed = false;
    if (m_impl->m_UniqueName != name)
    {
        m_impl->m_UniqueName = name;
        renamed = true;
    }

    if (m_impl->m_ViewControlMode == ViewControlMode::standard)
    {
        an::ui::appContext()->getViewControlManager()->renameViewControl(this);
    }

    if (renamed)
    {
        std::string prefix;
        if (m_impl->m_ViewControlMode == ViewControlMode::rt_preview)
        {
            prefix = "** RT PREVIEW ** ";
        }

        auto title = QString::fromUtf8(prefix + m_impl->m_UniqueName);

        if (m_impl->m_ViewControlMode == ViewControlMode::standard && hasPendingChanges())
        {
            title += " *";
        }

        setWindowTitle(title);

        if (m_DockWidget)
        {
            m_DockWidget->setTitle(title);
        }
        notifyImageRenamed();
    }
}

std::string ImageViewControl::getUniqueName() const
{
    return m_impl->m_UniqueName;
}

std::string ImageViewControl::getFileName() const
{
    if (const std::optional<an::ui::DocumentSequence> sequence = m_impl->sequence(); sequence.has_value())
    {
        return sequence->sourcePath.toStdString();
    }
    if (m_impl->hasImage())
    {
        return m_impl->m_Document->names().sourcePath.toStdString();
    }

    return std::string();
}

std::shared_ptr<const an::ui::ViewOverlayStars> ImageViewControl::overlayStars() const
{
    return m_impl->m_OverlayStars;
}

const std::optional<an::WcsSolution>& ImageViewControl::imageSolution() const
{
    return m_impl->m_CelestialCoordinateSystem;
}

void ImageViewControl::setAlternateAsset(std::shared_ptr<const an::consumer::ImageAsset> asset,
                                         bool notifyImageViewWidget)
{
    m_impl->m_AlternateAsset = std::move(asset);
    if (notifyImageViewWidget)
    {
        m_impl->m_ImageViewWidget->onImageUpdated(this);
    }
}

void ImageViewControl::clearAlternateImage()
{
    m_impl->m_AlternateAsset.reset();
}

bool ImageViewControl::hasAlternateImage() const
{
    return m_impl->m_AlternateAsset != nullptr;
}

std::shared_ptr<const an::consumer::ImageAsset> ImageViewControl::alternateAsset() const
{
    return m_impl->m_AlternateAsset;
}

void ImageViewControl::subscribeListener(ImageViewControlListener* listener)
{
    std::unique_lock lock(m_impl->m_listeners_mutex);
    if (m_impl->m_ControlIsClosing.load(std::memory_order_acquire))
        return;
    m_impl->m_listeners.push_back(listener);
}

void ImageViewControl::unsubscribeListener(ImageViewControlListener* listener)
{
    std::unique_lock lock(m_impl->m_listeners_mutex);
    m_impl->m_listeners.erase(std::remove(m_impl->m_listeners.begin(), m_impl->m_listeners.end(), listener),
                              m_impl->m_listeners.end());
}

void ImageViewControl::notifyImageLoaded()
{
    CHECK_IF_CONTROL_IS_CLOSING;

    refreshCelestialCoordinateSystemCache();
    updateSupportedImageChannels();

    std::vector<ImageViewControlListener*> listenersCopy;
    {
        std::shared_lock lock(m_impl->m_listeners_mutex);
        listenersCopy = m_impl->m_listeners;
    }

    for (auto& listener : listenersCopy)
    {
        listener->onPreImageLoaded(this);
    }

    for (auto& listener : listenersCopy)
    {
        listener->onImageLoaded(this);
    }

    for (auto& listener : listenersCopy)
    {
        listener->onPostImageLoaded(this);
    }

    if (m_impl->m_ViewControlMode == ViewControlMode::standard)
    {
        updateImageActions();
        if (auto* statusBar = an::ui::appContext()->getStatusBarService())
        {
            statusBar->updateImageInfoInStatusBar();
        }
    }

    auto composition_mode = m_impl->m_ImaveCompositionMode.load(std::memory_order_relaxed);
    if (m_impl->channelCount() != 3u)
    {
        composition_mode = ImageCompositionMode::rgb_mode;
    }
    displayViewChannel(composition_mode);
    if (m_impl->m_StaticPreviewOverlayStats.has_value() || m_impl->m_ViewControlMode != ViewControlMode::st_preview)
    {
        m_impl->updateStaticPreviewStatsOverlay(m_impl->m_StaticPreviewOverlayStats);
    }
    else
    {
        requestPreviewStatistics();
    }

    // Update cursor coordinates after display refresh so the scale factor is current.
    updateCursorCelestialCoords();
}

void ImageViewControl::requestPreviewStatistics()
{
    // The owner's own choice, and the presentation choices being offered at all: neither is the other's to change.
    if (!m_impl->m_MeasuresPreviewStatistics || !m_impl->m_PresentationChoicesOffered)
    {
        return;
    }
    const an::consumer::EngineSession* session = viewSession();
    if (m_impl->m_ViewControlMode != ViewControlMode::st_preview || session == nullptr || !m_impl->hasImage())
    {
        m_impl->updateStaticPreviewStatsOverlay(std::nullopt);
        return;
    }
    an::Result<an::ui::CommitTicket> ticket = m_impl->m_Statistics.issue(*m_impl->m_Document);
    if (!ticket.hasValue())
    {
        return;
    }
    auto issued = std::make_shared<const an::ui::CommitTicket>(std::move(*ticket));
    const std::shared_ptr<const an::consumer::ImageAsset> asset = m_impl->m_Document->asset();
    runThenPublish(
        this, m_impl->m_Outstanding,
        [session, asset, stop = issued->stopToken()]() { return an::ui::measureViewImage(*session, *asset, stop); },
        [issued, asset](ImageViewControl& view, an::Result<an::ui::ViewImageStatistics>&& measured) {
            if (issued->verdict() != an::ui::CommitVerdict::Commit || !measured.hasValue())
            {
                return;
            }
            view.m_impl->m_StaticPreviewCache.setStatistics(asset, *measured);
            view.m_impl->updateStaticPreviewStatsOverlay(*measured);
        });
}

void ImageViewControl::setMeasuresPreviewStatistics(const bool measures)
{
    m_impl->m_MeasuresPreviewStatistics = measures;
    if (!measures)
    {
        m_impl->m_Statistics.cancel();
    }
}

bool ImageViewControl::measuresPreviewStatistics() const
{
    return m_impl->m_MeasuresPreviewStatistics;
}

void ImageViewControl::setPresentationChoicesOffered(const bool offered)
{
    CHECK_IF_CONTROL_IS_CLOSING;

    m_impl->m_PresentationChoicesOffered = offered;
    for (const QPointer<QAction>& action : m_impl->m_PresentationChoiceActions)
    {
        if (action)
        {
            // Hidden here rather than by the overflow, whose next pass would show it again.
            action->setProperty(kOverflowHiddenProperty, false);
            action->setVisible(offered);
        }
    }
    m_impl->refreshAutostretchSettingsAvailability();
    // Whether the preview measures statistics and whether it shows the banner over an automatic stretch are its
    // owner's own choices (`setMeasuresPreviewStatistics`, `setShowAutostretchBanner`). Taking the presentation
    // choices away stops both; offering them again leaves each as its owner set it.
    if (!offered)
    {
        m_impl->m_Statistics.cancel();
    }
    m_impl->m_StaticPreviewStatsOverlayVisible =
        offered && (m_impl->m_StatsOverlayToggleAction == nullptr || m_impl->m_StatsOverlayToggleAction->isChecked());
    m_impl->updateStaticPreviewStatsOverlay(offered ? m_impl->m_StaticPreviewOverlayStats : std::nullopt);
    enableAutostretchBanner(offered && m_impl->m_ShowAutostretchBanner && m_impl->m_ImageViewWidget != nullptr &&
                            m_impl->m_ImageViewWidget->getViewMode() == ImageViewMode::AUTOSTRETCH_MODE);
    if (auto* customToolBar = dynamic_cast<CustomToolBar*>(m_impl->m_ToolBar))
    {
        customToolBar->refreshOverflow();
    }
}

void ImageViewControl::showPreviewStatistics(const std::optional<an::ui::ViewImageStatistics>& statistics)
{
    CHECK_IF_CONTROL_IS_CLOSING;

    m_impl->updateStaticPreviewStatsOverlay(statistics);
}

std::optional<an::ui::ViewImageStatistics> ImageViewControl::previewStatistics() const
{
    return m_impl->m_StaticPreviewOverlayStats;
}

void ImageViewControl::notifyImageSaved()
{
    CHECK_IF_CONTROL_IS_CLOSING;

    std::vector<ImageViewControlListener*> listenersCopy;
    {
        std::shared_lock lock(m_impl->m_listeners_mutex);
        listenersCopy = m_impl->m_listeners;
    }

    for (auto& listener : listenersCopy)
    {
        listener->onPreImageSaved(this);
    }

    for (auto& listener : listenersCopy)
    {
        listener->onImageSaved(this);
    }

    for (auto& listener : listenersCopy)
    {
        listener->onPostImageSaved(this);
    }

    if (m_impl->m_ViewControlMode == ViewControlMode::standard)
    {
        resetPendingChanges();

        // A save can make an image a file for the first time: the menus offer what a file has.
        if (auto* context = an::ui::appContext())
        {
            if (auto* actionStateManager = context->getActionStateManager())
            {
                actionStateManager->updateActiveImageActions();
            }
        }
    }
}

void ImageViewControl::notifyImageSavedAsync()
{
    QPointer<ImageViewControl> guard(this);
    QMetaObject::invokeMethod(
        this,
        [guard]() {
            if (guard && !guard->isCloseRequested())
            {
                guard->notifyImageSaved();
            }
        },
        Qt::QueuedConnection);
}

void ImageViewControl::notifyImageUpdated(bool disablePendingChangesCheck, ImageUpdateKind updateKind)
{
    notifyImageUpdatedImpl(std::nullopt, disablePendingChangesCheck, updateKind);
}

void ImageViewControl::notifyImageUpdatedWithPresentationAnalysis(
    std::optional<an::DisplayPresentationAnalysis> analysis, bool disablePendingChangesCheck,
    ImageUpdateKind updateKind)
{
    notifyImageUpdatedImpl(std::move(analysis), disablePendingChangesCheck, updateKind);
}

void ImageViewControl::notifyImageUpdatedImpl(std::optional<an::DisplayPresentationAnalysis> analysis,
                                              bool disablePendingChangesCheck, ImageUpdateKind updateKind)
{
    CHECK_IF_CONTROL_IS_CLOSING;

    if (QThread::currentThread() != thread())
    {
        // Every listener and the widget are the GUI thread's; a worker's notification is queued there.
        QPointer<ImageViewControl> guard(this);
        QMetaObject::invokeMethod(
            this,
            [guard, analysis = std::move(analysis), disablePendingChangesCheck, updateKind]() mutable {
                if (guard)
                {
                    guard->notifyImageUpdatedImpl(std::move(analysis), disablePendingChangesCheck, updateKind);
                }
            },
            Qt::QueuedConnection);
        return;
    }

    if (updateKind == ImageUpdateKind::PixelDataChanged && m_impl->m_ViewControlMode != ViewControlMode::rt_preview)
    {
        invalidateAstrometryStarDetection(true);
    }

    m_impl->m_ImageViewWidget->setPrecomputedAnalysis(std::move(analysis));

    // A real-time preview's frames keep the source's metadata, so its coordinates are not read again per frame.
    if (m_impl->m_ViewControlMode != ViewControlMode::rt_preview)
    {
        const bool solvedBefore = m_impl->m_CelestialCoordinateSystem.has_value();
        refreshCelestialCoordinateSystemCache();
        // What the menus offer of an image follows whether it states a solution - Annotate Image is offered for one
        // that does - so a view that gains or loses its solution, as a plate solve or an edit makes it, has the
        // application's actions read again. The view's own toolbar is refreshed with every update.
        if (m_impl->m_ViewControlMode == ViewControlMode::standard &&
            solvedBefore != m_impl->m_CelestialCoordinateSystem.has_value())
        {
            if (auto* context = an::ui::appContext())
            {
                if (auto* actionStateManager = context->getActionStateManager())
                {
                    actionStateManager->updateActiveImageActions();
                }
            }
        }
    }

    updateSupportedImageChannels();

    if (m_impl->m_ViewControlMode == ViewControlMode::standard)
    {
        notifySourceMaskUpdated(false);
    }

    std::vector<ImageViewControlListener*> listenersCopy;
    {
        std::shared_lock lock(m_impl->m_listeners_mutex);
        listenersCopy = m_impl->m_listeners;
    }

    for (auto& listener : listenersCopy)
    {
        listener->onPreImageUpdated(this);
    }

    for (auto& listener : listenersCopy)
    {
        listener->onImageUpdated(this);
    }

    for (auto& listener : listenersCopy)
    {
        listener->onPostImageUpdated(this);
    }

    if (m_impl->m_ViewControlMode == ViewControlMode::standard)
    {
        if (auto* manager = an::ui::appContext()->getViewControlManager())
        {
            manager->notifyViewControlUpdated(this);
        }

        updateImageActions();
        if (auto* statusBar = an::ui::appContext()->getStatusBarService())
        {
            statusBar->updateImageInfoInStatusBar();
        }

        if (!disablePendingChangesCheck)
        {
            markPendingChanges();
        }
    }

    auto composition_mode = m_impl->m_ImaveCompositionMode.load(std::memory_order_relaxed);
    if (m_impl->channelCount() != 3u)
    {
        composition_mode = ImageCompositionMode::rgb_mode;
    }
    displayViewChannel(composition_mode);

    if (m_impl->m_ViewControlMode == ViewControlMode::st_preview)
    {
        requestPreviewStatistics();
    }

    // Update cursor coordinates after display refresh so the scale factor is current.
    if (m_impl->m_ViewControlMode != ViewControlMode::rt_preview)
    {
        updateCursorCelestialCoords();
    }

    if (m_impl->m_ViewControlMode == ViewControlMode::standard)
    {
        // notifySourceMaskUpdated could make the client control remove the mask, so the clients are copied first.
        auto maskedClientsCopy = m_impl->m_masked_clients;
        for (auto& ctrl : maskedClientsCopy)
        {
            if (ctrl)
                ctrl->notifySourceMaskUpdated(true);
        }
    }
}

void ImageViewControl::notifyImageUpdatedAsync(bool disablePendingChangesCheck, const std::function<void()>& callback,
                                               ImageUpdateKind updateKind)
{
    CHECK_IF_CONTROL_IS_CLOSING;

    QPointer<ImageViewControl> guard(this);
    QMetaObject::invokeMethod(
        this,
        [guard, disablePendingChangesCheck, callback, updateKind]() {
            if (!guard || guard->isCloseRequested())
            {
                return;
            }
            guard->notifyImageUpdated(disablePendingChangesCheck, updateKind);
            if (guard && !guard->isCloseRequested() && callback)
            {
                callback();
            }
        },
        Qt::QueuedConnection);
}

void ImageViewControl::fitToWindowAsync()
{
    QPointer<ImageViewControl> guard(this);
    QMetaObject::invokeMethod(
        this,
        [guard]() {
            if (guard && !guard->isCloseRequested())
            {
                guard->fitToWindow();
            }
        },
        Qt::QueuedConnection);
}

void ImageViewControl::notifyImageRepaintAsync()
{
    CHECK_IF_CONTROL_IS_CLOSING;

    QPointer<ImageViewWidget> guard(m_impl->m_ImageViewWidget);
    QMetaObject::invokeMethod(
        this,
        [guard]() {
            if (guard)
            {
                guard->update();
            }
        },
        Qt::QueuedConnection);
}

void ImageViewControl::notifyImageRenamed()
{
    CHECK_IF_CONTROL_IS_CLOSING;

    if (m_impl->hasImage())
    {
        an::ui::DocumentNames names = m_impl->m_Document->names();
        names.displayName = QString::fromUtf8(m_impl->m_UniqueName);
        m_impl->m_Document->setNames(std::move(names));
    }

    std::vector<ImageViewControlListener*> listenersCopy;
    {
        std::shared_lock lock(m_impl->m_listeners_mutex);
        listenersCopy = m_impl->m_listeners;
    }

    for (auto& listener : listenersCopy)
    {
        listener->onPreImageRenamed(this);
    }

    for (auto& listener : listenersCopy)
    {
        listener->onImageRenamed(this);
    }

    for (auto& listener : listenersCopy)
    {
        listener->onPostImageRenamed(this);
    }
}

void ImageViewControl::notifyImageViewScaled()
{
    if (m_impl->m_ViewControlMode == ViewControlMode::standard)
    {
        an::ui::appContext()->getStatusBarService()->updateImageZoomInfoInStatusBar();
    }

    std::vector<ImageViewControlListener*> listenersCopy;
    {
        std::shared_lock lock(m_impl->m_listeners_mutex);
        listenersCopy = m_impl->m_listeners;
    }

    for (auto& listener : listenersCopy)
    {
        listener->onImageViewScaled(this);
    }
}

void ImageViewControl::notifyMaskUpdated()
{
    std::shared_lock lock(m_impl->m_listeners_mutex);
    auto listenersCopy = m_impl->m_listeners;
    lock.unlock();
    for (auto& listener : listenersCopy)
    {
        listener->onMaskUpdated(this);
    }
}

void ImageViewControl::notifyAlternateImageEnabled()
{
    std::shared_lock lock(m_impl->m_listeners_mutex);
    auto listenersCopy = m_impl->m_listeners;
    lock.unlock();
    for (auto& listener : listenersCopy)
    {
        listener->onAlternateImageEnabled(this);
    }
}

void ImageViewControl::notifyAlternateImageDisabled()
{
    std::shared_lock lock(m_impl->m_listeners_mutex);
    auto listenersCopy = m_impl->m_listeners;
    lock.unlock();
    for (auto& listener : listenersCopy)
    {
        listener->onAlternateImageDisabled(this);
    }
}

void ImageViewControl::invalidateAstrometryStarDetection(const bool clearStars)
{
    m_impl->m_Detections.cancel();
    if (clearStars)
    {
        m_impl->m_OverlayStars.reset();
        if (m_impl->m_AstrometryStarDisplayModule)
        {
            an::ui::setAstrometryOverlayStars(*m_impl->m_AstrometryStarDisplayModule, nullptr);
        }
        if (m_impl->m_ImageViewWidget)
        {
            m_impl->m_ImageViewWidget->update();
        }
    }
}

void ImageViewControl::restartStaticPreviewLoad()
{
    if (m_impl->m_ViewControlMode != ViewControlMode::st_preview || m_impl->m_StaticPreviewCurrentPath.isEmpty())
    {
        return;
    }

    loadImageFromFile(m_impl->m_StaticPreviewCurrentPath.toStdString());
}

void ImageViewControl::setPreviewContentVisible(const bool hasContent)
{
    const bool preserveToolbarWhenEmpty = m_impl->m_ViewControlMode == ViewControlMode::st_preview;

    // SAFE_INVOKE_METHOD_IN_GUI_THREAD_BLOCK is used defensively here.
    // All current callers run on the main thread (directly or via Qt::QueuedConnection
    // callbacks).  The macro handles the rare future path where this might be called
    // from a worker without requiring further changes at all call sites.
    SAFE_INVOKE_METHOD_IN_GUI_THREAD_BLOCK(m_impl->m_ToolBar, setVisible, hasContent || preserveToolbarWhenEmpty);
    SAFE_INVOKE_METHOD_IN_GUI_THREAD_BLOCK(m_impl->m_ScrollArea, setVisible, hasContent);
    SAFE_INVOKE_METHOD_IN_GUI_THREAD_BLOCK(m_impl->m_NoImagePreviewWidget, setVisible, !hasContent);
}

void ImageViewControl::runPostPlateSolveStarDetection()
{
    CHECK_IF_CONTROL_IS_CLOSING;

    refreshCelestialCoordinateSystemCache();
    const an::consumer::EngineSession* session = viewSession();
    if (!m_impl->hasImage() || !m_impl->m_CelestialCoordinateSystem.has_value() || session == nullptr)
    {
        invalidateAstrometryStarDetection(true);
        return;
    }

    an::Result<an::ui::CommitTicket> ticket = m_impl->m_Detections.issue(*m_impl->m_Document);
    if (!ticket.hasValue())
    {
        invalidateAstrometryStarDetection(true);
        return;
    }
    auto issued = std::make_shared<const an::ui::CommitTicket>(std::move(*ticket));
    const std::shared_ptr<const an::consumer::ImageAsset> asset = m_impl->m_Document->asset();
    const an::WcsSolution solution = *m_impl->m_CelestialCoordinateSystem;
    runThenPublish(
        this, m_impl->m_Outstanding,
        [session, asset, solution, stop = issued->stopToken()]() {
            return an::ui::detectViewOverlayStars(*session, *asset, solution, stop);
        },
        [issued, solution](ImageViewControl& view, an::Result<an::StarDetectionChannelResult>&& detected) {
            // Stars of an image the view no longer shows, or of a request superseded, are never drawn.
            if (issued->verdict() != an::ui::CommitVerdict::Commit)
            {
                return;
            }
            if (!detected.hasValue())
            {
                LOG_WARNING("Post plate-solve star detection of '{}' failed: {}", view.getUniqueName(),
                            describeStatus(detected.status()).toStdString());
                view.invalidateAstrometryStarDetection(true);
                return;
            }
            auto stars = std::make_shared<an::ui::ViewOverlayStars>();
            stars->detection = std::move(*detected);
            stars->solution = solution;
            LOG_INFO("Post plate-solve star detection: {} stars detected on channel {}",
                     stars->detection.detection.stars.size(), stars->detection.channel);
            if (stars->detection.detection.stars.empty())
            {
                view.invalidateAstrometryStarDetection(true);
                return;
            }
            view.m_impl->m_OverlayStars = std::move(stars);
            if (view.m_impl->m_AstrometryStarDisplayModule)
            {
                an::ui::setAstrometryOverlayStars(*view.m_impl->m_AstrometryStarDisplayModule,
                                                  view.m_impl->m_OverlayStars);
            }
            if (view.m_impl->m_ImageViewWidget)
            {
                view.m_impl->m_ImageViewWidget->update();
            }
        });
}

void ImageViewControl::refreshCelestialCoordinateSystemCache()
{
    m_impl->m_CelestialCoordinateSystem.reset();

    if (!m_impl->m_ImageViewWidget)
    {
        return;
    }

    // The solution is read from the image's own cards under a lease that never waits; an image a writer holds keeps
    // no coordinates until its next update.
    if (m_impl->hasImage())
    {
        if (an::Result<an::consumer::AssetReadLease> lease = m_impl->m_Document->tryRead(); lease.hasValue())
        {
            lease->visit([&](const auto& typed) {
                const std::optional<an::ImageDescriptor> described = typed.descriptor();
                if (described.has_value())
                {
                    m_impl->m_CelestialCoordinateSystem = an::ui::viewImageSolution(
                        typed.metadata(), described->extent().width(), described->extent().height());
                }
            });
        }
    }

    m_impl->m_ImageViewWidget->setMouseTracking(m_impl->m_CelestialCoordinateSystem.has_value());
    clearHoveredCelestialCoords();
}

void ImageViewControl::updateCursorCelestialCoords()
{
    if (!m_impl->m_ImageViewWidget || !m_impl->m_CelestialCoordinateSystem.has_value())
    {
        return;
    }

    const std::optional<an::ImageDescriptor> described = m_impl->descriptor();
    if (!described.has_value())
    {
        return;
    }

    const QPoint global_pos = QCursor::pos();
    const QPoint widget_pos = m_impl->m_ImageViewWidget->mapFromGlobal(global_pos);
    if (m_impl->m_ImageViewWidget->rect().contains(widget_pos))
    {
        const double scale = m_impl->m_ImageViewWidget->getCurrentScaleFactor();
        if (std::isfinite(scale) && scale > 0.0)
        {
            const double pixel_x = static_cast<double>(widget_pos.x()) / scale;
            const double pixel_y = static_cast<double>(widget_pos.y()) / scale;
            if (pixel_x >= 0.0 && pixel_y >= 0.0 && pixel_x < static_cast<double>(described->extent().width()) &&
                pixel_y < static_cast<double>(described->extent().height()))
            {
                updateHoveredCelestialCoordsForImagePixel(pixel_x, pixel_y);
                return;
            }
        }
    }
    clearHoveredCelestialCoords();
}

void ImageViewControl::updateHoveredCelestialCoordsForImagePixel(const double pixel_x, const double pixel_y)
{
    if (m_impl->m_ViewControlMode != ViewControlMode::standard || !m_impl->m_CelestialCoordinateSystem.has_value())
    {
        clearHoveredCelestialCoords();
        return;
    }

    const an::Result<an::SkyCoordinate> sky =
        m_impl->m_CelestialCoordinateSystem->pixelToWorld(an::PixelCoordinate{.x = pixel_x, .y = pixel_y});
    if (!sky.hasValue())
    {
        clearHoveredCelestialCoords();
        return;
    }

    celestialCoordsChanged(sky->rightAscensionDegrees, sky->declinationDegrees);
}

void ImageViewControl::clearHoveredCelestialCoords()
{
    if (m_impl->m_ViewControlMode == ViewControlMode::standard)
    {
        celestialCoordsCleared();
    }
}

void ImageViewControl::enableAction(ImageViewControlAction action, bool enable)
{
    CHECK_IF_CONTROL_IS_CLOSING;

    auto* asm_ = an::ui::appContext()->getActionStateManager();
    if (!asm_)
        return;

    switch (action)
    {
    case ImageViewControlAction::execute_statistics: {
        setToolbarActionEnabled(m_impl->m_StatisticsAction, enable);
        asm_->setCustomImageActionEnabled(this, GlobalViewControlActions::STATISTICS_ACTION, enable);
        break;
    }
    case ImageViewControlAction::execute_histogram: {
        setToolbarActionEnabled(m_impl->m_HistogramAction, enable);
        asm_->setCustomImageActionEnabled(this, GlobalViewControlActions::HISTOGRAM_ACTION, enable);
        break;
    }
    case ImageViewControlAction::execute_fits_header: {
        asm_->setCustomImageActionEnabled(this, GlobalViewControlActions::FITS_HEADER_ACTION, enable);
        break;
    }
    case ImageViewControlAction::execute_exif: {
        asm_->setCustomImageActionEnabled(this, GlobalViewControlActions::EXIF_ACTION, enable);
        break;
    }
    case ImageViewControlAction::execute_summary: {
        asm_->setCustomImageActionEnabled(this, GlobalViewControlActions::SUMMARY_ACTION, enable);
        break;
    }
    default: {
        break;
    }
    }
}

an::ui::IViewHistory* ImageViewControl::viewHistory() const
{
    return m_impl->m_History.get();
}

void ImageViewControl::setActiveControl()
{
    if (m_impl->m_ViewControlMode == ViewControlMode::standard)
    {
        an::ui::appContext()->getViewRegistry()->setActiveImageViewControl(this);
    }
}

void ImageViewControl::refreshTheme()
{
    const auto& iconManager = m_impl->m_IconProvider;
    if (auto* customToolBar = dynamic_cast<CustomToolBar*>(m_impl->m_ToolBar))
    {
        customToolBar->setOverflowIconColors(iconManager->getColorNormal(), iconManager->getColorDisabled());
        customToolBar->refreshOverflow();
    }

    // Refresh toolbar action icons
    if (m_impl->m_FitToWindowAction)
    {
        m_impl->m_FitToWindowAction->setIcon(iconManager->getIcon("zoom_fit"));
    }
    if (m_impl->m_ZoomInAction)
    {
        m_impl->m_ZoomInAction->setIcon(iconManager->getIcon("zoom_in"));
    }
    if (m_impl->m_ZoomOutAction)
    {
        m_impl->m_ZoomOutAction->setIcon(iconManager->getIcon("zoom_out"));
    }
    if (m_impl->m_Zoom100Action)
    {
        m_impl->m_Zoom100Action->setIcon(iconManager->getIcon("zoom_100"));
    }
    if (m_impl->m_MaximizeAction)
    {
        if (m_impl->m_IsMaximized)
        {
            m_impl->m_MaximizeAction->setIcon(iconManager->getIcon("fullscreen_exit"));
        }
        else
        {
            m_impl->m_MaximizeAction->setIcon(iconManager->getIcon("fullscreen"));
        }
    }
    if (m_impl->m_AutostretchSettingsAction)
    {
        m_impl->m_AutostretchSettingsAction->setIcon(iconManager->getIcon("settings"));
    }
    if (m_impl->m_UndoAction)
    {
        m_impl->m_UndoAction->setIcon(iconManager->getIcon("undo"));
    }
    if (m_impl->m_RedoAction)
    {
        m_impl->m_RedoAction->setIcon(iconManager->getIcon("redo"));
    }
    if (m_impl->m_HistoryAction)
    {
        m_impl->m_HistoryAction->setIcon(iconManager->getIcon("history"));
    }
    if (m_impl->m_StatisticsAction)
    {
        m_impl->m_StatisticsAction->setIcon(iconManager->getIcon("stats"));
    }
    if (m_impl->m_StatsOverlayToggleAction)
    {
        m_impl->m_StatsOverlayToggleAction->setIcon(iconManager->getIcon("stats"));
    }
    if (m_impl->m_HistogramAction)
    {
        m_impl->m_HistogramAction->setIcon(iconManager->getIcon("histogram"));
    }
    if (m_impl->m_EXIFViewerAction)
    {
        m_impl->m_EXIFViewerAction->setIcon(iconManager->getIcon("camera"));
    }
    if (m_impl->m_FITSViewerAction)
    {
        m_impl->m_FITSViewerAction->setIcon(iconManager->getIcon("header"));
    }
    if (m_impl->m_ImageSummaryAction)
    {
        m_impl->m_ImageSummaryAction->setIcon(iconManager->getIcon("summary"));
    }
    if (m_impl->m_AstrometryToggleAction)
    {
        m_impl->m_AstrometryToggleAction->setIcon(iconManager->getIcon("star"));
    }
    if (m_impl->m_AstrometrySettingsAction)
    {
        m_impl->m_AstrometrySettingsAction->setIcon(iconManager->getIcon("settings"));
    }
    if (m_impl->m_AstrometryInfoAction)
    {
        m_impl->m_AstrometryInfoAction->setIcon(iconManager->getIcon("info"));
    }
    if (m_impl->m_PlateSolveToolbarAction)
    {
        m_impl->m_PlateSolveToolbarAction->setIcon(iconManager->getIcon("search"));
    }
    if (m_impl->m_AnnotateImageToolbarAction)
    {
        m_impl->m_AnnotateImageToolbarAction->setIcon(iconManager->getIcon("panorama"));
    }
    if (m_impl->m_RTPreviewShowOriginalAction)
    {
        if (m_impl->m_RTPreviewShowOriginalLabelEnabled)
        {
            m_impl->m_RTPreviewShowOriginalAction->setIcon(iconManager->getIcon("toggle_off"));
        }
        else
        {
            m_impl->m_RTPreviewShowOriginalAction->setIcon(iconManager->getIcon("toggle_on"));
        }
    }
    if (m_impl->m_RTPreviewTakeSnapshotAction)
    {
        m_impl->m_RTPreviewTakeSnapshotAction->setIcon(iconManager->getIcon("camera"));
    }

    // Refresh scroll area background based on theme
    if (m_impl->m_ScrollArea)
    {
        applyImageViewScrollAreaBackground(m_impl->m_ScrollArea, resolveDarkMode(palette()));
    }

    if (m_impl->m_ToolBar)
    {
        const bool useCardSurfaceTintToolbar = (m_impl->m_ViewControlMode != ViewControlMode::standard);
        m_impl->m_ToolBar->setProperty("anUseCardBackground", useCardSurfaceTintToolbar);
        applyImageViewToolbarBackground(m_impl->m_ToolBar, resolveDarkMode(palette()), useCardSurfaceTintToolbar);
    }

    // Toolbar background is handled by the proxy style
    // No QSS needed for toolbar refresh

    // Refresh dock tab icon based on whether this is an image or sequence
    if (m_DockWidget)
    {
        if (isSequence())
        {
            m_DockWidget->setIcon(iconManager->getIcon("film"));
        }
        else
        {
            m_DockWidget->setIcon(iconManager->getIcon("picture"));
        }
    }

    if (m_impl->m_ViewControlMode != ViewControlMode::rt_preview && isAutoStretchEnabled())
    {
        updateAutostretchBannerText();
    }
    m_impl->updateStaticPreviewStatsOverlay(m_impl->m_StaticPreviewOverlayStats);

    // ComboBox item icons are cached per item; force a rebuild so theme icon colors refresh.
    if (m_impl->m_imageViewModeComboBox || m_impl->m_imageViewChannelComboBox)
    {
        updateSupportedImageChannels(true);
    }
}

void ImageViewControl::refreshZoomSettings()
{
    if (m_impl->m_ImageZoomAndPanController)
    {
        m_impl->m_ImageZoomAndPanController->refreshZoomSettings();
    }
}

void ImageViewControl::horizontalMirror()
{
    orientImage(static_cast<int>(an::ImageOrientation::MirrorHorizontal), "Image Horizontal Mirror", std::string(),
                "horizontal mirror");
}

void ImageViewControl::verticalMirror()
{
    orientImage(static_cast<int>(an::ImageOrientation::MirrorVertical), "Image Vertical Mirror", std::string(),
                "vertical mirror");
}

void ImageViewControl::rotate180Degree()
{
    orientImage(static_cast<int>(an::ImageOrientation::Rotate180), "Image Rotation", "180 degrees",
                "180-degree rotation");
}

void ImageViewControl::rotate90DegreeRight()
{
    orientImage(static_cast<int>(an::ImageOrientation::Rotate90Clockwise), "Image Rotation", "90 degrees right",
                "90-degree-right rotation");
}

void ImageViewControl::rotate90DegreeLeft()
{
    orientImage(static_cast<int>(an::ImageOrientation::Rotate270Clockwise), "Image Rotation", "90 degrees left",
                "90-degree-left rotation");
}

void ImageViewControl::orientImage(const int orientation, const std::string& stepName,
                                   const std::string& stepDescription, const char* removedSolutionNote)
{
    CHECK_IF_CONTROL_IS_CLOSING;

    if (!m_impl->hasImage())
    {
        return;
    }

    EditJob edit;
    edit.stepName = stepName;
    edit.stepDescription = stepDescription;
    edit.removedSolutionNote = removedSolutionNote;
    edit.saysItsFailure = true;
    edit.compute = [orientation = static_cast<an::ImageOrientation>(orientation)](
                       const an::consumer::EngineSession& session, const an::consumer::AssetWriteLease& image,
                       std::stop_token stop) {
        return an::ui::orientViewImage(session, image.image(), image.validity(), orientation, std::move(stop));
    };
    queueEdit(std::move(edit));
}

void ImageViewControl::editImage(std::string stepName, std::string stepDescription, EditCompute compute,
                                 std::function<void(bool shown)> done, std::shared_ptr<an::ui::ImageReads> reads)
{
    if (m_impl->m_ControlIsClosing.load() || !m_impl->hasImage() || !compute)
    {
        if (done)
        {
            done(false);
        }
        return;
    }
    EditJob edit;
    edit.stepName = std::move(stepName);
    edit.stepDescription = std::move(stepDescription);
    edit.compute = std::move(compute);
    edit.done = std::move(done);
    edit.reads = std::move(reads);
    queueEdit(std::move(edit));
}

void ImageViewControl::queueEdit(EditJob job)
{
    // Edits run one at a time and in the order they were asked for: each computes from what the one before published.
    m_impl->m_PendingEdits.push_back(std::move(job));
    if (!m_impl->m_EditRunning)
    {
        startNextEdit();
    }
}

void ImageViewControl::startNextEdit()
{
    m_impl->m_EditRunning = false;
    const auto dropPending = [this]() {
        std::deque<EditJob> dropped = std::exchange(m_impl->m_PendingEdits, {});
        for (EditJob& job : dropped)
        {
            if (job.done)
            {
                job.done(false);
            }
        }
    };
    if (m_impl->m_PendingEdits.empty() || m_impl->m_ControlIsClosing.load() || !m_impl->hasImage())
    {
        dropPending();
        return;
    }
    const an::consumer::EngineSession* session = viewSession();
    an::Result<an::ui::CommitTicket> ticket = m_impl->m_Edits.issue(*m_impl->m_Document);
    if (session == nullptr || !ticket.hasValue())
    {
        dropPending();
        return;
    }
    m_impl->m_EditRunning = true;
    auto edit = std::make_shared<EditJob>(std::move(m_impl->m_PendingEdits.front()));
    m_impl->m_PendingEdits.pop_front();
    auto issued = std::make_shared<const an::ui::CommitTicket>(std::move(*ticket));
    const std::shared_ptr<an::ui::ImageDocument> document = m_impl->m_Document;

    // The worker takes the document's writer, so nothing else changes the image between what the edit reads and what
    // it publishes; the GUI thread records the step and then publishes, or lets the writer go with nothing changed.
    struct Computed
    {
        an::Result<an::ui::ImageDocumentWriter> writer =
            an::Status::failure(an::ErrorCode::Internal, "the edit did not run");
        an::Result<an::consumer::ImageAsset> result = an::Status::failure(an::ErrorCode::Internal, "no result");
        // What the history secured for the step on the worker, held until the step is recorded or the edit let go.
        std::shared_ptr<void> preparation;
        // Whether the failure is the history's refusal to keep the step, which the history says itself.
        bool refusedByHistory = false;
    };
    // The history waits for its writer on this worker, never on the GUI thread: what the edit replaces is kept and the
    // result's copy admitted before the result is handed back.
    an::ui::IViewHistory::EditPreparation prepare;
    if (m_impl->m_History)
    {
        prepare = m_impl->m_History->editPreparation(edit->stepName);
    }
    runThenPublish(
        this, m_impl->m_Outstanding,
        [session, document, edit, prepare, stop = issued->stopToken()]() {
            auto computed = std::make_shared<Computed>();
            const auto takeWriter = [&]() -> an::Status {
                computed->writer = document->write(stop);
                return computed->writer.hasValue() ? an::Status::success() : computed->writer.status();
            };
            // The other images the edit reads are leased with the writer, in one order of identity, so two edits that
            // each read the other's image cannot each hold what the other waits for.
            const an::Status taken =
                edit->reads
                    ? edit->reads->take(stop, document->asset() ? document->asset()->identity() : 0u, takeWriter)
                    : takeWriter();
            if (taken.failed())
            {
                computed->result = taken;
                return computed;
            }
            // What the edit or the history does outside its own guard and throws - out of memory, most likely - ends
            // the edit as a failure its completion says, so whatever waits on the edit is told it ended.
            const auto failureOf = [](const char* what) {
                return an::Status::failure(an::ErrorCode::Internal, std::string("the edit failed: ") + what);
            };
            try
            {
                computed->result = edit->compute(*session, computed->writer->asset(), stop);
            }
            catch (const std::exception& error)
            {
                computed->result = failureOf(error.what());
            }
            catch (...)
            {
                computed->result = failureOf("an exception of no standard type");
            }
            if (edit->reads)
            {
                edit->reads->release();
            }
            if (prepare && computed->result.hasValue())
            {
                an::Result<std::shared_ptr<void>> prepared = an::Status::failure(an::ErrorCode::Internal, "unprepared");
                try
                {
                    prepared = prepare(computed->writer->asset(), *computed->result, stop);
                }
                catch (const std::exception& error)
                {
                    prepared = failureOf(error.what());
                }
                catch (...)
                {
                    prepared = failureOf("an exception of no standard type");
                }
                if (prepared.hasValue())
                {
                    computed->preparation = std::move(*prepared);
                }
                else
                {
                    computed->result = prepared.status();
                    computed->refusedByHistory = true;
                }
            }
            return computed;
        },
        [issued, edit, document](ImageViewControl& view, std::shared_ptr<Computed>&& computed) {
            const bool current =
                issued->verdict() == an::ui::CommitVerdict::Commit && view.m_impl->m_Document == document;
            if (!current || !computed->writer.hasValue() || !computed->result.hasValue())
            {
                if (current && !computed->result.hasValue() &&
                    computed->result.status().code() != an::ErrorCode::Cancelled)
                {
                    LOG_WARNING("Image view: '{}' of '{}' failed: {}", edit->stepName, view.getUniqueName(),
                                describeStatus(computed->result.status()).toStdString());
                    if (edit->saysItsFailure && !computed->refusedByHistory)
                    {
                        sayEditFailure(&view, edit->stepName, describeStatus(computed->result.status()));
                    }
                }
                ++view.m_impl->m_Telemetry.editsDiscarded;
                computed.reset();
                if (edit->done)
                {
                    edit->done(false);
                }
                view.startNextEdit();
                return;
            }
            // The step is recorded before the view shows it; a step the history refuses is not shown.
            if (view.m_impl->m_History && !view.m_impl->m_History->registerEditBeforePublishing(
                                              edit->stepName, edit->stepDescription, *computed->result))
            {
                ++view.m_impl->m_Telemetry.editsDiscarded;
                computed.reset();
                if (edit->done)
                {
                    edit->done(false);
                }
                view.startNextEdit();
                return;
            }
            const bool hadSolution = view.m_impl->m_CelestialCoordinateSystem.has_value();
            const an::Status replaced = computed->writer->replace(std::move(*computed->result));
            computed->writer->release();
            computed.reset();
            if (replaced.failed())
            {
                LOG_WARNING("Image view: '{}' of '{}' could not be shown: {}", edit->stepName, view.getUniqueName(),
                            describeStatus(replaced).toStdString());
                // The history holds a step the view could not show, which no caller can know: the view says it.
                sayEditFailure(&view, edit->stepName,
                               QStringLiteral("The result could not be shown: ") + describeStatus(replaced));
                ++view.m_impl->m_Telemetry.editsDiscarded;
                if (edit->done)
                {
                    edit->done(false);
                }
                view.startNextEdit();
                return;
            }
            ++view.m_impl->m_Telemetry.editsCommitted;
            if (edit->done)
            {
                edit->done(true);
            }
            view.notifyImageUpdated();
            if (hadSolution && edit->removedSolutionNote != nullptr)
            {
                LOG_WARNING("Astrometric solution removed after {} of '{}'. Plate solve again to restore coordinates.",
                            edit->removedSolutionNote, view.getUniqueName());
            }
            view.startNextEdit();
        });
}

void ImageViewControl::zoomIn()
{
    CHECK_IF_CONTROL_IS_CLOSING;

    m_impl->m_ImageViewWidget->zoomIn();
}

void ImageViewControl::zoomOut()
{
    CHECK_IF_CONTROL_IS_CLOSING;

    m_impl->m_ImageViewWidget->zoomOut();
}

void ImageViewControl::zoom100()
{
    CHECK_IF_CONTROL_IS_CLOSING;

    m_impl->m_ImageViewWidget->zoom100();
}

void ImageViewControl::fitToWindow()
{
    CHECK_IF_CONTROL_IS_CLOSING;

    activateImageViewLayoutBeforeFit(this);
    m_impl->m_ImageViewWidget->fitInView();
}

void ImageViewControl::adjustWindow()
{
    fitWindowToImage();
}

void ImageViewControl::zoomAndCenterOn(int x, int y, float scaleFactor)
{
    m_impl->m_ImageViewWidget->scaleImageView(scaleFactor, true);
    const double actualScaleFactor = m_impl->m_ImageViewWidget->getCurrentScaleFactor();

    auto scrollArea = m_impl->m_ScrollArea->rect();
    int offsetx = static_cast<int>(std::lround(x * actualScaleFactor - scrollArea.center().x()));
    int offsety = static_cast<int>(std::lround(y * actualScaleFactor - scrollArea.center().y()));

    m_impl->m_ScrollArea->horizontalScrollBar()->setValue(offsetx);
    m_impl->m_ScrollArea->verticalScrollBar()->setValue(offsety);
}

float ImageViewControl::getCurrentZoom()
{
    return m_impl->m_ImageViewWidget->getCurrentZoom();
}

QPoint ImageViewControl::getViewportOffset() const
{
    if (!m_impl->m_ScrollArea)
    {
        return QPoint();
    }

    return QPoint(m_impl->m_ScrollArea->horizontalScrollBar()->value(),
                  m_impl->m_ScrollArea->verticalScrollBar()->value());
}

void ImageViewControl::setViewportOffset(const QPoint& offset)
{
    if (!m_impl->m_ScrollArea)
    {
        return;
    }

    m_impl->m_ScrollArea->horizontalScrollBar()->setValue(offset.x());
    m_impl->m_ScrollArea->verticalScrollBar()->setValue(offset.y());
}

ViewControlMode ImageViewControl::getViewControlMode() const
{
    return m_impl->m_ViewControlMode;
}

ImageViewScrollArea* ImageViewControl::getScrollArea() const
{
    return m_impl->m_ScrollArea;
}

ImageViewWidget* ImageViewControl::getViewWidget() const
{
    return m_impl->m_ImageViewWidget;
}

bool ImageViewControl::isMasked() const
{
    return m_impl->m_CurrentMask != nullptr;
}

namespace
{

/// Why a mask cannot be shown over an image, or nothing when it can: a mask has the image's width and height, and one
/// channel, or three over a colour image.
[[nodiscard]] std::optional<QString> maskIncompatibility(const std::optional<an::ImageDescriptor>& mask,
                                                         const std::optional<an::ImageDescriptor>& image)
{
    if (!mask.has_value() || !image.has_value())
    {
        return QObject::tr("the mask or the image holds nothing");
    }
    if (mask->extent().width() != image->extent().width() || mask->extent().height() != image->extent().height())
    {
        return QObject::tr("the mask is %1 x %2 pixels and the image %3 x %4")
            .arg(mask->extent().width())
            .arg(mask->extent().height())
            .arg(image->extent().width())
            .arg(image->extent().height());
    }
    const std::uint32_t maskChannels = mask->extent().channelCount();
    const std::uint32_t imageChannels = image->extent().channelCount();
    if (maskChannels != 1u && maskChannels != 3u)
    {
        return QObject::tr("a mask has one channel or three, and this one has %1").arg(maskChannels);
    }
    if (maskChannels == 3u && imageChannels < 3u)
    {
        return QObject::tr("a colour mask cannot mask a grey image");
    }
    return std::nullopt;
}

} // namespace

void ImageViewControl::setCurrentMask(ImageViewControl* mask)
{
    if (m_impl->m_CurrentMask == mask)
        return;

    if (mask != nullptr)
    {
        // A four-channel image is not a mask: its alpha would be tinted as a fourth layer.
        if (const std::optional<QString> reason = maskIncompatibility(mask->imageDescriptor(), imageDescriptor()))
        {
            if (auto* dialogs = an::ui::appContext() ? an::ui::appContext()->getDialogService() : nullptr)
            {
                dialogs->notifyWarnMessage(
                    this, tr("Mask Reference"),
                    tr("'%1' cannot be the mask of '%2': %3")
                        .arg(QString::fromUtf8(mask->getUniqueName()), QString::fromUtf8(getUniqueName()), *reason));
            }
            return;
        }
    }

    if (m_impl->m_CurrentMask)
    {
        auto& v = m_impl->m_CurrentMask->m_impl->m_masked_clients;
        v.erase(std::remove_if(v.begin(), v.end(),
                               [this](const QPointer<ImageViewControl>& p) { return p.isNull() || p.data() == this; }),
                v.end());
    }

    m_impl->m_CurrentMask = mask;

    if (m_impl->m_CurrentMask)
    {
        m_impl->m_CurrentMask->m_impl->m_masked_clients.push_back(QPointer<ImageViewControl>(this));
        m_impl->m_InvertMask = false;
        m_impl->m_MaskEnabled = true;
        m_impl->m_ShowMask = true;
    }
    else
    {
        m_impl->m_InvertMask = false;
        m_impl->m_MaskEnabled = false;
        m_impl->m_ShowMask = false;
    }

    notifyMaskUpdated();
}

ImageViewControl* ImageViewControl::getCurrentMask() const
{
    return m_impl->m_CurrentMask;
}

void ImageViewControl::removeCurrentMask()
{
    if (m_impl->m_CurrentMask)
    {
        auto& v = m_impl->m_CurrentMask->m_impl->m_masked_clients;
        v.erase(std::remove_if(v.begin(), v.end(),
                               [this](const QPointer<ImageViewControl>& p) { return p.isNull() || p.data() == this; }),
                v.end());

        m_impl->m_CurrentMask = nullptr;

        m_impl->m_InvertMask = false;
        m_impl->m_MaskEnabled = false;
        m_impl->m_ShowMask = false;

        m_impl->m_MaskRenderingMode = MaskRenderingMode::red_mode;

        notifyMaskUpdated();

        if (auto* actions = an::ui::appContext() ? an::ui::appContext()->getActionStateManager() : nullptr)
        {
            actions->updateActiveImageActions();
        }
    }
}

void ImageViewControl::setShowMask(const bool show)
{
    m_impl->m_ShowMask = show;
    notifyMaskUpdated();
}

void ImageViewControl::setEnableMask(const bool enable)
{
    m_impl->m_MaskEnabled = enable;
    notifyMaskUpdated();
}

bool ImageViewControl::isMaskEnabled() const
{
    return m_impl->m_MaskEnabled;
}

void ImageViewControl::setInvertMask(const bool invert)
{
    m_impl->m_InvertMask = invert;
    notifyMaskUpdated();
}

bool ImageViewControl::isMaskInverted() const
{
    return m_impl->m_InvertMask;
}

void ImageViewControl::setMaskRenderingMode(const MaskRenderingMode mode)
{
    if (m_impl->m_MaskRenderingMode != mode)
    {
        m_impl->m_MaskRenderingMode = mode;
        notifyMaskUpdated();
    }
}

MaskRenderingMode ImageViewControl::getMaskRenderingMode() const
{
    return m_impl->m_MaskRenderingMode;
}

void ImageViewControl::notifySourceMaskUpdated(const bool fromMaskProvider)
{
    if (!m_impl->m_CurrentMask)
        return;

    if (const std::optional<QString> reason =
            maskIncompatibility(m_impl->m_CurrentMask->imageDescriptor(), imageDescriptor()))
    {
        std::string message = fmt::format(
            "This update made the mask '{}' incompatible with the image '{}'. Removing the mask reference. ",
            m_impl->m_CurrentMask->getUniqueName(), getUniqueName());
        if (auto* dialogs = an::ui::appContext() ? an::ui::appContext()->getDialogService() : nullptr)
        {
            dialogs->notifyWarnMessage(this, tr("Mask Reference"), QString::fromUtf8(message));
        }
        removeCurrentMask();
        return;
    }

    // A change of the mask's own image, or of this image under a multiply, is a new layer: the multiply shows the
    // image the view shows times the mask, so it follows either.
    if (fromMaskProvider || m_impl->m_MaskRenderingMode == MaskRenderingMode::multiply_mode)
    {
        notifyMaskUpdated();
    }
}

bool ImageViewControl::hasProcessAttached(const std::string& processName) const
{
    return m_impl->m_AttachedProcessesByName.find(processName) != m_impl->m_AttachedProcessesByName.end();
}

size_t ImageViewControl::numOfAttachedProcesses() const
{
    return m_impl->m_AttachedProcessesByName.size();
}

bool ImageViewControl::hasProcessWithExclusiveAccessAttached() const
{
    for (auto& it : m_impl->m_AttachedProcessesByName)
    {
        if (it.second->requiresExclusiveAccess())
            return true;
    }

    return false;
}

void ImageViewControl::attachProcess(an::ui::IViewAttachment& process)
{
    m_impl->m_AttachedProcessesByName[process.attachmentName()] = &process;
}

void ImageViewControl::detachProcess(an::ui::IViewAttachment& process)
{
    auto it = m_impl->m_AttachedProcessesByName.find(process.attachmentName());
    if (it != m_impl->m_AttachedProcessesByName.end() && it->second == &process)
    {
        m_impl->m_AttachedProcessesByName.erase(it);
    }
}

void ImageViewControl::disableExclusiveRTPreviews(const an::ui::IViewAttachment* requester)
{
    for (auto& it : m_impl->m_AttachedProcessesByName)
    {
        an::ui::IViewAttachment* process = it.second;
        if (process == nullptr || process == requester || !process->showsRealtimePreview())
        {
            continue;
        }

        if (process->requiresExclusivePreviewAccess())
        {
            process->disableRealtimePreview();
        }
    }
}

void ImageViewControl::disableAllRTPreviews()
{
    for (auto& it : m_impl->m_AttachedProcessesByName)
    {
        if (it.second != nullptr && it.second->showsRealtimePreview())
        {
            it.second->disableRealtimePreview();
        }
    }
}

void ImageViewControl::setRTPreviewShowOriginalLabel(bool val)
{
    if (m_impl->m_ViewControlMode == ViewControlMode::rt_preview)
    {
        const bool darkMode = resolveDarkMode(palette());
        const QColor originalColor = an::ui::imageViewOverlayOriginalColor(darkMode);
        const QColor previewColor = an::ui::imageViewOverlayAutostretchColor(darkMode);
        m_impl->m_RTPreviewShowOriginalLabelEnabled = val;

        if (val)
        {
            if (!m_impl->m_AlternateRTPreviewLabel2.isEmpty())
            {
                m_impl->m_ViewControlTextOverlay->setText(m_impl->m_AlternateRTPreviewLabel2, originalColor);
            }
            else
            {
                m_impl->m_ViewControlTextOverlay->setText(RT_PREVIEW_SHOW_ORIGINAL_LABEL, originalColor);
            }
        }
        else
        {
            if (!m_impl->m_AlternateRTPreviewLabel1.isEmpty())
            {
                m_impl->m_ViewControlTextOverlay->setText(m_impl->m_AlternateRTPreviewLabel1, previewColor);
            }
            else
            {
                m_impl->m_ViewControlTextOverlay->setText(RT_PREVIEW_SHOW_PREVIEW_LABEL, previewColor);
            }
        }

        m_impl->m_ViewControlTextOverlay->update();
    }
}

void ImageViewControl::setRTAlternatePreviewLabels(const QString& label1, const QString& label2)
{
    m_impl->m_AlternateRTPreviewLabel1 = label1;
    m_impl->m_AlternateRTPreviewLabel2 = label2;
    setRTPreviewShowOriginalLabel(m_impl->m_RTPreviewShowOriginalLabelEnabled);
}

void ImageViewControl::setRTPreviewLabelEnabled(bool enabled)
{
    if (m_impl->m_ViewControlTextOverlay)
    {
        m_impl->m_ViewControlTextOverlay->setEnabled(enabled);
        m_impl->m_ScrollArea->refreshOverlayWidget();
    }
}

void ImageViewControl::enableAutostretchBanner(bool enabled)
{
    if (m_impl->m_ViewControlMode == ViewControlMode::rt_preview)
    {
        // In RT preview mode the overlay is used to show PREVIEW/ORIGINAL, not the autostretch banner.
        return;
    }

    if (m_impl->m_ViewControlTextOverlay)
    {
        if (enabled)
        {
            updateAutostretchBannerText();
        }
        m_impl->m_ViewControlTextOverlay->setEnabled(enabled);
        m_impl->m_ScrollArea->refreshOverlayWidget();
        m_impl->layoutStaticPreviewStatsOverlay();
    }
}

void ImageViewControl::updateAutostretchBannerText()
{
    if (m_impl->m_ViewControlMode == ViewControlMode::rt_preview || !m_impl->m_ViewControlTextOverlay)
    {
        return;
    }

    if (m_impl->m_ImageViewWidget && m_impl->m_ImageViewWidget->getViewMode() != ImageViewMode::AUTOSTRETCH_MODE)
    {
        return;
    }

    const bool darkMode = resolveDarkMode(palette());
    m_impl->m_ViewControlTextOverlay->setText("AUTOSTRETCH", an::ui::imageViewOverlayAutostretchColor(darkMode));
    m_impl->layoutStaticPreviewStatsOverlay();
}

void ImageViewControl::enableRTPreviewAutostretchControls(bool enabled)
{
    if (m_impl->m_ViewControlMode != ViewControlMode::rt_preview)
    {
        return;
    }

    if (!m_impl->m_ToolBar)
    {
        return;
    }

    if (!enabled)
    {
        if (m_impl->m_imageViewModeComboBox)
        {
            m_impl->m_imageViewModeComboBox->setVisible(false);
        }

        if (m_impl->m_ImageViewModeComboBoxAction)
        {
            m_impl->m_ImageViewModeComboBoxAction->setVisible(false);
        }

        if (m_impl->m_AutostretchSettingsAction)
        {
            m_impl->m_AutostretchSettingsAction->setVisible(false);
        }

        if (m_impl->m_RTPreviewAutostretchSeparatorAction)
        {
            m_impl->m_RTPreviewAutostretchSeparatorAction->setVisible(false);
        }

        if (auto* customToolBar = dynamic_cast<CustomToolBar*>(m_impl->m_ToolBar))
        {
            customToolBar->refreshOverflow();
        }

        return;
    }

    const auto& iconManager = m_impl->m_IconProvider;

    if (!m_impl->m_imageViewModeComboBox)
    {
        m_impl->m_imageViewModeComboBox = new QComboBox();
        m_impl->m_imageViewModeComboBox->setAccessibleName(QStringLiteral("Display Mode"));
        m_impl->m_imageViewModeComboBox->setToolTip(QStringLiteral("Display Mode"));
        m_impl->m_imageViewModeComboBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        m_impl->m_imageViewModeComboBox->setMinimumContentsLength(12);
        m_impl->m_imageViewModeComboBox->setMinimumWidth(kImageModeComboMinWidth);

        m_impl->m_imageViewModeComboBox->blockSignals(true);
        m_impl->m_imageViewModeComboBox->addItem(iconManager->getIcon("image_linear"), IMAGE_VIEW_MODE_LINEAR);
        m_impl->m_imageViewModeComboBox->setItemData(0, "Linear mode", Qt::ToolTipRole);
        m_impl->m_imageViewModeComboBox->addItem(iconManager->getIcon("image_filled"), IMAGE_VIEW_MODE_AUTOSTRETCH);
        m_impl->m_imageViewModeComboBox->setItemData(1, "Autostretch mode", Qt::ToolTipRole);
        m_impl->m_imageViewModeComboBox->blockSignals(false);
        updateComboBoxWidthToContents(m_impl->m_imageViewModeComboBox, 8, kImageModeComboMinWidth, true);

        // Insert before the RT preview toggle action so it shows next to the other view controls.
        if (m_impl->m_RTPreviewShowOriginalAction)
        {
            m_impl->m_ImageViewModeComboBoxAction =
                m_impl->m_ToolBar->insertWidget(m_impl->m_RTPreviewShowOriginalAction, m_impl->m_imageViewModeComboBox);
        }
        else
        {
            m_impl->m_ImageViewModeComboBoxAction = m_impl->m_ToolBar->addWidget(m_impl->m_imageViewModeComboBox);
        }

        QObject::connect(m_impl->m_imageViewModeComboBox, &QComboBox::currentTextChanged, this,
                         &ImageViewControl::on_imageViewModeComboBox_currentTextChanged);
    }

    if (!m_impl->m_AutostretchSettingsAction)
    {
        m_impl->m_AutostretchSettingsAction =
            new QAction(iconManager->getIcon("settings"), "Autostretch Settings", this);
        QObject::connect(m_impl->m_AutostretchSettingsAction, &QAction::triggered, this,
                         &ImageViewControl::autostretchSettings);

        if (m_impl->m_RTPreviewShowOriginalAction)
        {
            m_impl->m_ToolBar->insertAction(m_impl->m_RTPreviewShowOriginalAction, m_impl->m_AutostretchSettingsAction);
            m_impl->m_RTPreviewAutostretchSeparatorAction =
                m_impl->m_ToolBar->insertSeparator(m_impl->m_RTPreviewShowOriginalAction);
        }
        else
        {
            m_impl->m_ToolBar->addAction(m_impl->m_AutostretchSettingsAction);
            m_impl->m_RTPreviewAutostretchSeparatorAction = m_impl->m_ToolBar->addSeparator();
        }
        m_impl->refreshAutostretchSettingsAvailability();
    }

    if (m_impl->m_imageViewModeComboBox)
    {
        m_impl->m_imageViewModeComboBox->setVisible(true);
    }

    if (m_impl->m_ImageViewModeComboBoxAction)
    {
        m_impl->m_ImageViewModeComboBoxAction->setVisible(true);
    }

    if (m_impl->m_AutostretchSettingsAction)
    {
        m_impl->m_AutostretchSettingsAction->setVisible(true);
    }

    if (m_impl->m_RTPreviewAutostretchSeparatorAction)
    {
        m_impl->m_RTPreviewAutostretchSeparatorAction->setVisible(true);
    }

    // Force enabled/disabled state to match the current view mode.
    on_imageViewModeComboBox_currentTextChanged(m_impl->m_imageViewModeComboBox->currentText());

    if (auto* customToolBar = dynamic_cast<CustomToolBar*>(m_impl->m_ToolBar))
    {
        customToolBar->refreshOverflow();
    }
}

void ImageViewControl::setShowAutostretchBanner(bool show)
{
    m_impl->m_ShowAutostretchBanner = show;
    enableAutostretchBanner(show && m_impl->m_PresentationChoicesOffered);
}

bool ImageViewControl::isShowAutostretchBannerEnabled()
{
    return m_impl->m_ShowAutostretchBanner;
}

bool ImageViewControl::isShowMaskEnabled() const
{
    return m_impl->m_ShowMask;
}

void ImageViewControl::deepClose()
{
    if (isMaximized())
    {
        switchMaximized();
    }

    if (m_DockWidget)
    {
        reinterpret_cast<CustomDockWidget*>(m_DockWidget)->forceClose();
    }

    close();
}

void ImageViewControl::updateImageActions()
{
    const bool hasImage = m_impl->hasImage();
    const bool hasAstrometry = hasImage && m_impl->m_CelestialCoordinateSystem.has_value();
    const bool isSeq = isSequence();

    if (!m_impl->m_StatisticsAction)
    {
        return;
    }

    if (!hasImage)
    {
        setToolbarActionEnabled(m_impl->m_StatisticsAction, false);
        setToolbarActionEnabled(m_impl->m_HistogramAction, false);
        setToolbarActionEnabled(m_impl->m_FITSViewerAction, false);
        setToolbarActionEnabled(m_impl->m_EXIFViewerAction, false);
        setToolbarActionEnabled(m_impl->m_ImageSummaryAction, false);
    }
    else
    {
        // A header or an EXIF record is what the file stated, carried as the image's preserved cards. The EXIF window
        // is offered when it has a field to show, from whichever raster container stated it - the rule its rows read -
        // and never for a container's own entries alone.
        bool hasHeader = false;
        bool hasExif = false;
        if (an::Result<an::consumer::AssetReadLease> lease = m_impl->m_Document->tryRead(); lease.hasValue())
        {
            lease->visit([&](const auto& typed) {
                const an::CodecMetadata& codec = typed.metadata().codec();
                hasHeader = std::any_of(codec.cards().begin(), codec.cards().end(), [](const an::CodecCard& card) {
                    return card.origin() == an::CodecNamespace::Fits;
                });
                hasExif = statesShownExifField(codec);
            });
        }
        setToolbarActionEnabled(m_impl->m_StatisticsAction, true);
        setToolbarActionEnabled(m_impl->m_HistogramAction, true);
        setToolbarActionEnabled(m_impl->m_FITSViewerAction, hasHeader);
        setToolbarActionEnabled(m_impl->m_EXIFViewerAction, hasExif);
        setToolbarActionEnabled(m_impl->m_ImageSummaryAction, true);
    }

    // Astrometry toolbar buttons visibility and enablement
    if (m_impl->m_AstrometryToggleAction)
    {
        m_impl->m_AstrometryToggleAction->setVisible(hasAstrometry && !isSeq);
        m_impl->m_AstrometryToggleAction->setEnabled(hasAstrometry && !isSeq);
    }
    if (m_impl->m_AstrometrySettingsAction)
    {
        m_impl->m_AstrometrySettingsAction->setVisible(hasAstrometry && !isSeq);
        m_impl->m_AstrometrySettingsAction->setEnabled(hasAstrometry && !isSeq && m_impl->m_AstrometryToggleAction &&
                                                       m_impl->m_AstrometryToggleAction->isChecked());
    }
    if (m_impl->m_AstrometryInfoAction)
    {
        m_impl->m_AstrometryInfoAction->setVisible(!isSeq);
        m_impl->m_AstrometryInfoAction->setEnabled(hasAstrometry);
    }
    if (m_impl->m_PlateSolveToolbarAction)
    {
        m_impl->m_PlateSolveToolbarAction->setVisible(hasImage && !isSeq);
        m_impl->m_PlateSolveToolbarAction->setEnabled(canPlateSolveCurrentImage());
    }
    if (m_impl->m_AnnotateImageToolbarAction)
    {
        m_impl->m_AnnotateImageToolbarAction->setVisible(hasAstrometry && !isSeq);
        m_impl->m_AnnotateImageToolbarAction->setEnabled(hasAstrometry && !isSeq);
    }

    if (auto* customToolBar = dynamic_cast<CustomToolBar*>(m_impl->m_ToolBar))
    {
        customToolBar->refreshOverflow();
    }
}

void ImageViewControl::upcateImageHistory()
{
    if (!m_impl->m_UndoAction || !m_impl->m_RedoAction)
    {
        return;
    }

    an::ui::IViewHistory* history = m_impl->m_History.get();
    m_impl->m_UndoAction->setText("Undo");
    if (history != nullptr && history->hasUndo())
    {
        m_impl->m_UndoAction->setEnabled(true);
        const std::string name = history->previousStepName();
        if (!name.empty())
        {
            m_impl->m_UndoAction->setText(QString::fromUtf8(fmt::format("Undo <{}>", name)));
        }
    }
    else
    {
        m_impl->m_UndoAction->setEnabled(false);
    }

    m_impl->m_RedoAction->setText("Redo");
    if (history != nullptr && history->hasRedo())
    {
        m_impl->m_RedoAction->setEnabled(true);
        const std::string name = history->nextStepName();
        if (!name.empty())
        {
            m_impl->m_RedoAction->setText(QString::fromUtf8(fmt::format("Redo <{}>", name)));
        }
    }
    else
    {
        m_impl->m_RedoAction->setEnabled(false);
    }
}

bool ImageViewControl::isSequence() const
{
    return m_impl->sequence().has_value();
}

bool ImageViewControl::isSERSequence() const
{
    const std::optional<an::ui::DocumentSequence> sequence = m_impl->sequence();
    return sequence.has_value() && sequence->descriptor.format == an::SequenceFormat::Ser;
}

bool ImageViewControl::isImageListSequence() const
{
    const std::optional<an::ui::DocumentSequence> sequence = m_impl->sequence();
    return sequence.has_value() && sequence->descriptor.format == an::SequenceFormat::ImageList;
}

bool ImageViewControl::isVideoSequence() const
{
    const std::optional<an::ui::DocumentSequence> sequence = m_impl->sequence();
    return sequence.has_value() && (sequence->descriptor.format == an::SequenceFormat::Mp4 ||
                                    sequence->descriptor.format == an::SequenceFormat::Avi ||
                                    sequence->descriptor.format == an::SequenceFormat::Video);
}

bool ImageViewControl::isFITSSequence() const
{
    const std::optional<an::ui::DocumentSequence> sequence = m_impl->sequence();
    return sequence.has_value() && sequence->descriptor.format == an::SequenceFormat::Fits;
}

bool ImageViewControl::isSequenceAutodebayerEnabled() const
{
    return m_impl->m_SequenceAutoDebayer;
}

bool ImageViewControl::isSequencePlaying() const
{
    return m_impl->m_SequencePlayer && m_impl->m_SequencePlayer->isPlaying();
}

void ImageViewControl::pauseSequence()
{
    if (m_impl->m_SequencePlayer)
    {
        m_impl->m_SequencePlayer->pause();
    }
}

void ImageViewControl::resumeSequence()
{
    if (m_impl->m_SequencePlayer)
    {
        m_impl->m_SequencePlayer->resume();
    }
}

size_t ImageViewControl::getSequenceCurrentFrame()
{
    if (m_impl->m_SequencePlayer)
    {
        if (getSequenceNumOfFrames() == 0u)
        {
            return 0;
        }

        return static_cast<size_t>(m_impl->m_SequencePlayer->currentFrame()) + 1u;
    }

    return 0;
}

size_t ImageViewControl::getSequenceCurrentFrameIndex()
{
    if (m_impl->m_SequencePlayer)
    {
        return static_cast<size_t>(m_impl->m_SequencePlayer->currentFrame());
    }
    if (const std::optional<an::ui::DocumentSequence> sequence = m_impl->sequence(); sequence.has_value())
    {
        return static_cast<size_t>(sequence->currentFrame);
    }

    return 0;
}

size_t ImageViewControl::getSequenceNumOfFrames()
{
    const std::optional<an::ui::DocumentSequence> sequence = m_impl->sequence();
    if (!sequence.has_value())
    {
        return 0;
    }
    if (sequence->selectedFrames.has_value())
    {
        return sequence->selectedFrames->size();
    }
    return static_cast<size_t>(sequence->descriptor.frameCount.value_or(0u));
}

an::ui::IViewSequencePlayer* ImageViewControl::sequencePlayer() const
{
    return m_impl->m_SequencePlayer.get();
}

bool ImageViewControl::isCFA() const
{
    const std::optional<an::ImageDescriptor> described = m_impl->descriptor();
    if (!described.has_value())
    {
        return false;
    }
    if (isSequence())
    {
        return described->isMosaiced() && !isSequenceAutodebayerEnabled();
    }
    return described->isMosaiced();
}

bool ImageViewControl::isMono() const
{
    return m_impl->channelCount() == 1u;
}

bool ImageViewControl::tryRequestLockForProcessing()
{
    // One compare-exchange decides between two concurrent callers: a check and then a store let both through.
    bool expected = false;
    if (!m_impl->m_IsLockedForProcessing.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
    {
        return false;
    }

    if (isSequence() && m_impl->m_SequencePlayer)
    {
        an::ui::IViewSequencePlayer* player = m_impl->m_SequencePlayer.get();
        QPointer<ImageViewControl> guard(this);
        auto hold = [guard, player]() {
            if (guard && guard->m_impl->m_SequencePlayer.get() == player)
            {
                player->pause();
                player->setEnabled(false);
            }
        };
        SAFE_INVOKE_LAMBDA_IN_GUI_THREAD(this, hold);
    }

    return true;
}

bool ImageViewControl::isLockedForProcessing()
{
    return m_impl->m_IsLockedForProcessing.load(std::memory_order_acquire);
}

void ImageViewControl::releaseLockForProcessing()
{
    m_impl->m_IsLockedForProcessing.store(false, std::memory_order_release);

    if (isSequence() && m_impl->m_SequencePlayer)
    {
        an::ui::IViewSequencePlayer* player = m_impl->m_SequencePlayer.get();
        QPointer<ImageViewControl> guard(this);
        auto release = [guard, player]() {
            if (guard && guard->m_impl->m_SequencePlayer.get() == player)
            {
                player->setEnabled(true);
            }
        };
        SAFE_INVOKE_LAMBDA_IN_GUI_THREAD(this, release);
    }
}

void ImageViewControl::setRTPreviewTakeSnapshotActionEnabled(bool enabled)
{
    if (m_impl->m_ViewControlMode == ViewControlMode::rt_preview && m_impl->m_RTPreviewTakeSnapshotAction)
    {
        m_impl->m_RTPreviewTakeSnapshotAction->setEnabled(enabled);
    }
}

void ImageViewControl::setRTPreviewAreaActionEnabled(bool enabled)
{
    if (m_impl->m_ViewControlMode == ViewControlMode::rt_preview && m_impl->m_chkRTPreviewArea)
    {
        m_impl->m_chkRTPreviewArea->setEnabled(enabled);
        m_impl->m_chkRTPreviewArea->setVisible(enabled);
        if (m_impl->m_RTPreviewAreaAction)
        {
            m_impl->m_RTPreviewAreaAction->setVisible(enabled);
        }

        if (auto* customToolBar = dynamic_cast<CustomToolBar*>(m_impl->m_ToolBar))
        {
            customToolBar->refreshOverflow();
        }
    }
}

QCheckBox* ImageViewControl::getRTPreviewAreaCheckBox() const
{
    if (m_impl->m_ViewControlMode != ViewControlMode::rt_preview)
    {
        return nullptr;
    }

    return m_impl->m_chkRTPreviewArea;
}

void ImageViewControl::setAutostretchSettingsOpen(bool open)
{
    m_impl->m_AutostretchSettingsOpen = open;
    m_impl->refreshAutostretchSettingsAvailability();

    if (auto* customToolBar = dynamic_cast<CustomToolBar*>(m_impl->m_ToolBar))
    {
        customToolBar->refreshOverflow();
    }
}

void ImageViewControl::setViewMode(ImageViewMode mode)
{
    if (!m_impl->m_imageViewModeComboBox)
    {
        if (m_impl->m_ImageViewWidget)
        {
            m_impl->m_ImageViewWidget->setViewMode(mode);
        }
        m_impl->refreshAutostretchSettingsAvailability();
        return;
    }

    if (m_impl->m_imageViewModeComboBox->findText(IMAGE_VIEW_MODE_LINEAR) == -1)
    {
        const QSignalBlocker blocker(m_impl->m_imageViewModeComboBox);
        m_impl->m_imageViewModeComboBox->addItem(m_impl->m_IconProvider->getIcon("image_linear"),
                                                 IMAGE_VIEW_MODE_LINEAR);
        m_impl->m_imageViewModeComboBox->setItemData(0, "Linear mode", Qt::ToolTipRole);
        m_impl->m_imageViewModeComboBox->addItem(m_impl->m_IconProvider->getIcon("image_filled"),
                                                 IMAGE_VIEW_MODE_AUTOSTRETCH);
        m_impl->m_imageViewModeComboBox->setItemData(1, "Autostretch mode", Qt::ToolTipRole);
        updateComboBoxWidthToContents(m_impl->m_imageViewModeComboBox, 8, kImageModeComboMinWidth, true);
    }

    const QString targetMode = imageViewModeTextFor(mode);

    if (m_impl->m_imageViewModeComboBox->currentText() == targetMode)
    {
        on_imageViewModeComboBox_currentTextChanged(targetMode);
        return;
    }

    m_impl->m_imageViewModeComboBox->setCurrentText(targetMode);
}

void ImageViewControl::forceLinearMode()
{
    setViewMode(ImageViewMode::LINEAR_MODE);
}

void ImageViewControl::redrawImage()
{
    displayImageChannelFromSelection(m_impl->m_imageViewChannelComboBox->currentIndex());
}

bool ImageViewControl::hasPendingChanges()
{
    return m_impl->m_PendingChanges;
}

bool ImageViewControl::hasProcessAttached()
{
    return !m_impl->m_AttachedProcessesByName.empty();
}

void ImageViewControl::markPendingChanges()
{
    m_impl->m_PendingChanges = true;

    if (m_impl->m_ViewControlMode == ViewControlMode::standard)
    {
        auto title = QString::fromUtf8(m_impl->m_UniqueName);
        title += " *";

        setWindowTitle(title);

        if (m_DockWidget)
        {
            m_DockWidget->setTitle(title);
        }
    }
}

void ImageViewControl::resetPendingChanges()
{
    m_impl->m_PendingChanges = false;

    if (m_impl->m_ViewControlMode == ViewControlMode::standard)
    {
        auto title = QString::fromUtf8(m_impl->m_UniqueName);
        // setWindowTitle(title);

        if (m_DockWidget)
        {
            m_DockWidget->setTitle(title);
        }
    }
}

bool ImageViewControl::checkPendingChangesAndAskUserBeforeClosing()
{
    if (hasPendingChanges())
    {
        // Check if user wants confirmation dialogs for unsaved changes
        bool confirmCloseUnsaved = true; // default to true
        auto* settings = an::ui::appContext()->getGlobalSettings();
        if (settings)
        {
            QString confirmStr = settings->select(SettingsKeys::GENERAL_CONFIRM_CLOSE_UNSAVED, "1");
            confirmCloseUnsaved = (confirmStr == "1");
        }

        // If confirmation is disabled, allow closing without prompting
        if (!confirmCloseUnsaved)
        {
            return true;
        }

        int ret;
        an::ui::appContext()->getDialogService()->messageBoxYesNo(
            this, "Close image with unsaved changes",
            QString::fromUtf8(
                fmt::format("The image '{}' has unsaved changes, are you sure you want to close it?", getUniqueName())),
            true, &ret);

        if (ret == QMessageBox::Yes)
        {
            return true;
        }
        else
        {
            return false;
        }
    }

    // No pending changes, we are ok to close it
    return true;
}

bool ImageViewControl::checkProcessAttachedAndAskUserBeforeClosing()
{
    if (hasProcessAttached())
    {
        std::string message = fmt::format(
            "There are some processes attached the image '{}' that will be closed. \nCurrently attached processes: ",
            getUniqueName());

        for (auto& it : m_impl->m_AttachedProcessesByName)
        {
            message += it.first + "; ";
        }

        message += "\n\nAre you sure you want to close it?";

        int ret;
        an::ui::appContext()->getDialogService()->messageBoxYesNo(this, "Close image with process attached",
                                                                  QString::fromUtf8(message), true, &ret);

        if (ret == QMessageBox::Yes)
        {
            closeAttachedProcesses(m_impl->m_AttachedProcessesByName);

            return true;
        }
        else
        {
            return false;
        }
    }

    return true;
}

void ImageViewControl::closeRequested()
{
    if (checkPendingChangesAndAskUserBeforeClosing() && checkProcessAttachedAndAskUserBeforeClosing())
    {
        m_impl->m_CloseRequested = true;
        // Stop any playing sequence before sending the close event
        if (m_impl->m_SequencePlayer)
        {
            m_impl->m_SequencePlayer->stop();
        }

        if (m_DockWidget)
        {
            reinterpret_cast<CustomDockWidget*>(m_DockWidget)->forceClose();
        }
        close();
    }
}

bool ImageViewControl::isCloseRequested()
{
    return m_impl->m_CloseRequested;
}

void ImageViewControl::showViewControlBusyWarning(const QString& title, bool async)
{
    QString type = "image";
    if (isSequence())
    {
        type = "sequence";
    }

    if (async)
    {
        an::ui::appContext()->getDialogService()->notifyWarnMessageAsync(
            nullptr, title,
            QString(
                "Another process is executing for this %1, please cancel that job first before running this process")
                .arg(type));
    }
    else
    {
        an::ui::appContext()->getDialogService()->notifyWarnMessage(
            nullptr, title,
            QString(
                "Another process is executing for this %1, please cancel that job first before running this process")
                .arg(type));
    }
}

bool ImageViewControl::isAutoStretchEnabled()
{
    if (m_impl->m_ImageViewWidget)
    {
        return m_impl->m_ImageViewWidget->getViewMode() == ImageViewMode::AUTOSTRETCH_MODE;
    }

    return false;
}

AutostretchParams ImageViewControl::getAutostretchSettings()
{
    return m_impl->m_ImageViewWidget->getAutostretchSettings();
}

bool ImageViewControl::isInFloatingWindow()
{
    updateFloatingStatus();
    return m_impl->m_IsFloating;
}

bool ImageViewControl::isStatisticsActionEnabled() const
{
    return isToolbarActionEnabled(m_impl->m_StatisticsAction);
}

bool ImageViewControl::isHistogramActionEnabled() const
{
    return isToolbarActionEnabled(m_impl->m_HistogramAction);
}

bool ImageViewControl::isFitsHeaderActionEnabled() const
{
    return isToolbarActionEnabled(m_impl->m_FITSViewerAction);
}

bool ImageViewControl::isExifActionEnabled() const
{
    return isToolbarActionEnabled(m_impl->m_EXIFViewerAction);
}

bool ImageViewControl::canPlateSolveCurrentImage() const
{
    return m_impl->hasImage() && !isSequence();
}

bool ImageViewControl::canAnnotateCurrentImage() const
{
    return canPlateSolveCurrentImage() && imageSolution().has_value();
}

ImageViewWorkTelemetry ImageViewControl::workTelemetry() const
{
    return m_impl->m_Telemetry;
}

bool ImageViewControl::isWorkIdle() const
{
    return m_impl->m_Outstanding->load(std::memory_order_acquire) == 0u && !m_impl->m_EditRunning &&
           (m_impl->m_ImageViewWidget == nullptr || m_impl->m_ImageViewWidget->isPresentationIdle());
}

ImageViewControl::~ImageViewControl()
{
    // Work still on its way is refused from here: its completions find the view gone, and a worker waiting for the
    // document's writer stops waiting.
    m_impl->m_ControlIsClosing = true;
    m_impl->m_Closing.request_stop();
    m_impl->m_Reads.cancel();
    m_impl->m_Statistics.cancel();
    m_impl->m_Detections.cancel();
    m_impl->m_Edits.cancel();
    if (m_impl->m_Document)
    {
        m_impl->m_Document->close();
    }

    // Use exclusive lock to prevent new subscriptions during destruction.
    std::unique_lock lock(m_impl->m_listeners_mutex);
    auto listenersCopy = m_impl->m_listeners;
    m_impl->m_listeners.clear();
    lock.unlock();
    for (auto& listener : listenersCopy)
    {
        listener->onImageViewControlDestroyed(this);
    }

    // The overlays draw on the widget and the history reaches the view: both go before the widget does.
    m_impl->m_AstrometryStarDisplayModule.reset();
    m_impl->m_CoordinateGridDisplayModule.reset();
    m_impl->m_DistortionMapDisplayModule.reset();
    m_impl->m_CatalogAnnotationDisplayModule.reset();
    m_impl->m_History.reset();
    if (m_impl->m_SequencePlayer)
    {
        m_impl->m_SequencePlayer->shutdown();
        m_impl->m_SequencePlayer.reset();
    }
    // m_impl->m_ViewControlTextOverlay, m_impl->m_ScrollArea, and m_impl->m_NoImagePreviewWidget are all re-parented
    // to Qt parent widgets and will be deleted by Qt's object tree. Do not delete manually.
}

void ImageViewControl::undo()
{
    if (m_impl->m_History)
    {
        m_impl->m_History->undo();
    }
}

void ImageViewControl::redo()
{
    if (m_impl->m_History)
    {
        m_impl->m_History->redo();
    }
}

void ImageViewControl::history()
{
    if (m_impl->m_History)
    {
        m_impl->m_History->showHistory();
    }
}

void ImageViewControl::closeEvent(QCloseEvent* closeEvent)
{
    // Close all child popup dialogs (Histogram, Statistics, FITS viewer, etc.)
    // before this control goes away.  They hold a pointer back to this control
    // via ImageViewControlListener, so closing them now lets them clean up
    // safely.  Each dialog has WA_DeleteOnClose, so close() also deletes it.
    const auto childDialogs = findChildren<QDialog*>(QString(), Qt::FindDirectChildrenOnly);
    for (auto* dialog : childDialogs)
    {
        dialog->close();
    }

    if (isMaximized())
    {
        switchMaximized();
    }

    if (!m_impl->m_masked_clients.empty())
    {
        // Purge expired QPointer entries before iterating.
        std::erase_if(m_impl->m_masked_clients, [](const QPointer<ImageViewControl>& p) { return p.isNull(); });

        if (!m_impl->m_masked_clients.empty())
        {
            std::string message = fmt::format(
                "This image is being used as a mask in other images. The mask references are being removed");
            if (auto* dialogs = an::ui::appContext() ? an::ui::appContext()->getDialogService() : nullptr)
            {
                dialogs->notifyWarnMessage(this, tr("Mask Reference"), QString::fromUtf8(message));
            }
        }

        // removeCurrentMask could make the client control to remove the mask,
        // that would produce an error if we are using m_impl->m_masked_clients
        auto maskedClientsCopy = m_impl->m_masked_clients;
        for (auto& ctrl : maskedClientsCopy)
        {
            if (ctrl)
                ctrl->removeCurrentMask();
        }
    }

    removeCurrentMask();

    if (!m_impl->m_AttachedProcessesByName.empty())
    {
        closeAttachedProcesses(m_impl->m_AttachedProcessesByName);
    }

    // Closing comes before draining: every result still on its way is refused from now on, and the document refuses
    // its writer, while the work already reading it finishes on its own.
    m_impl->m_ControlIsClosing = true;
    m_impl->m_Closing.request_stop();
    m_impl->m_Reads.cancel();
    m_impl->m_Statistics.cancel();
    m_impl->m_Detections.cancel();
    m_impl->m_Edits.cancel();
    m_impl->m_PendingEdits.clear();
    if (m_impl->m_Document)
    {
        m_impl->m_Document->close();
    }

    if (m_impl->m_ViewControlMode == ViewControlMode::standard)
    {
        if (auto* manager = an::ui::appContext() ? an::ui::appContext()->getViewControlManager() : nullptr)
        {
            manager->removeViewControl(this, false);
        }
    }

    // A sequence's source is let go so its temporary files can be removed.
    if (m_impl->m_SequencePlayer)
    {
        m_impl->m_SequencePlayer->shutdown();
    }
}

void ImageViewControl::focusInEvent(QFocusEvent* focusInEvent)
{
    CHECK_IF_CONTROL_IS_CLOSING;

    if (m_impl->m_ViewControlMode == ViewControlMode::standard)
    {
        an::ui::appContext()->getViewRegistry()->setActiveImageViewControl(this);
    }

    QWidget::focusInEvent(focusInEvent);
}

void ImageViewControl::on_imageViewChannelComboBox_currentTextChanged(const QString& text)
{
    CHECK_IF_CONTROL_IS_CLOSING;

    (void)text;
    displayImageChannelFromSelection(m_impl->m_imageViewChannelComboBox->currentIndex());
}

void ImageViewControl::on_imageViewModeComboBox_currentTextChanged(const QString& text)
{
    CHECK_IF_CONTROL_IS_CLOSING;

    if (text == IMAGE_VIEW_MODE_LINEAR)
    {
        m_impl->m_ImageViewWidget->setViewMode(ImageViewMode::LINEAR_MODE);
        m_impl->refreshAutostretchSettingsAvailability();
        enableAutostretchBanner(false);
    }
    else if (text == IMAGE_VIEW_MODE_AUTOSTRETCH)
    {
        m_impl->m_ImageViewWidget->setViewMode(ImageViewMode::AUTOSTRETCH_MODE);
        m_impl->refreshAutostretchSettingsAvailability();
        enableAutostretchBanner(m_impl->m_ShowAutostretchBanner && m_impl->m_PresentationChoicesOffered);
    }

    if (auto* customToolBar = dynamic_cast<CustomToolBar*>(m_impl->m_ToolBar))
    {
        customToolBar->refreshOverflow();
    }

    // The presentation is keyed by the image's revision and the stretch, so switching the stretch re-presents the
    // image the view holds and never reads the file again.
    displayImageChannelFromSelection(m_impl->m_imageViewChannelComboBox->currentIndex());
}

void ImageViewControl::on_rtpreview_switch_images_clicked()
{
    if (m_impl->m_ImageViewWidget->isAlternateImageRenderEnabled())
    {
        m_impl->m_ImageViewWidget->enableAlternateImageRendering(false);
        m_impl->m_RTPreviewShowOriginalAction->setIcon(m_impl->m_IconProvider->getIcon("toggle_on"));
    }
    else
    {
        if (m_impl->m_AlternateAsset)
        {
            m_impl->m_ImageViewWidget->enableAlternateImageRendering(true);
            m_impl->m_RTPreviewShowOriginalAction->setIcon(m_impl->m_IconProvider->getIcon("toggle_off"));
        }
        else
        {
            m_impl->m_ImageViewWidget->enableAlternateImageRendering(false);
            m_impl->m_RTPreviewShowOriginalAction->setIcon(m_impl->m_IconProvider->getIcon("toggle_on"));
        }
    }
}

void ImageViewControl::on_rtpreview_take_snapshot_clicked()
{
    // We could just create a new view control from this one, but RT preview images are all cut to 8 bit to save memory.
    // Snapshots should have the original image depth, that's why we reschedule an execution by sending a signal to the
    // parent process
    std::shared_lock lock(m_impl->m_listeners_mutex);
    auto listenersCopy = m_impl->m_listeners;
    lock.unlock();
    for (auto& listener : listenersCopy)
    {
        listener->onRTTakeSnapshot(this);
    }
}

void ImageViewControl::isCurrentTabChanged(bool focused)
{
    updateFloatingStatus();
    updateTabBarHighlight(focused);

    if (focused)
    {
        an::ui::appContext()->getViewRegistry()->setActiveImageViewControl(this);
    }
}

void ImageViewControl::isFocusedChanged(bool focused)
{
    updateFloatingStatus();

    if (focused)
    {
        an::ui::appContext()->getViewRegistry()->setActiveImageViewControl(this);
    }
}

void ImageViewControl::isFloatingChanged(bool floating)
{
    updateFloatingStatus();
}

void ImageViewControl::guestViewChanged()
{
    updateFloatingStatus();
}

void ImageViewControl::autostretchSettings()
{
    if (!m_impl->autostretchSettingsAvailable())
    {
        return;
    }

    AutostretchSettingsControl* control = new AutostretchSettingsControl(this);

    control->setColorCalibrationControlsEnabled(!isSequence());

    control->show();
}

void ImageViewControl::updateSupportedImageChannels(bool forceUpdate)
{
    CHECK_IF_CONTROL_IS_CLOSING;

    if (QThread::currentThread() != qApp->thread())
    {
        QPointer<ImageViewControl> guard(this);
        auto update_channels = [guard, forceUpdate]() {
            if (guard)
            {
                guard->updateSupportedImageChannels(forceUpdate);
            }
        };
        SAFE_INVOKE_LAMBDA_IN_GUI_THREAD(this, update_channels);
        return;
    }

    const size_t current_channel_count = m_impl->channelCount();
    if (!forceUpdate && m_impl->m_LastImageChannels.load(std::memory_order_relaxed) == current_channel_count)
    {
        return;
    }

    const size_t previous_channel_count = m_impl->m_LastImageChannels.load(std::memory_order_relaxed);
    bool channel_count_changed = false;

    // Rebuilding the combo contents is internal state sync, not a user gesture: the signals are blocked so that the
    // rebuild does not present the image once per item.
    const QSignalBlocker channelSignalsBlocker(m_impl->m_imageViewChannelComboBox);
    std::optional<QSignalBlocker> modeSignalsBlocker;
    if (m_impl->m_ViewControlMode != ViewControlMode::rt_preview && m_impl->m_imageViewModeComboBox)
    {
        modeSignalsBlocker.emplace(m_impl->m_imageViewModeComboBox);
    }

    const QVariant currentViewChannel = m_impl->m_imageViewChannelComboBox->currentData();
    QString currentViewMode;

    if (m_impl->m_ViewControlMode != ViewControlMode::rt_preview)
    {
        if (m_impl->m_ImageViewWidget)
        {
            currentViewMode = imageViewModeTextFor(m_impl->m_ImageViewWidget->getViewMode());
        }
        else if (m_impl->m_imageViewModeComboBox)
        {
            currentViewMode = m_impl->m_imageViewModeComboBox->currentText();
        }
    }

    m_impl->m_imageViewChannelComboBox->clear();

    if (m_impl->m_ViewControlMode != ViewControlMode::rt_preview && m_impl->m_imageViewModeComboBox)
    {
        m_impl->m_imageViewModeComboBox->clear();
    }

    if (m_impl->hasImage())
    {
        const auto& iconManager = m_impl->m_IconProvider;

        if (m_impl->m_ViewControlMode != ViewControlMode::rt_preview && m_impl->m_imageViewModeComboBox)
        {
            m_impl->m_imageViewModeComboBox->addItem(iconManager->getIcon("image_linear"), IMAGE_VIEW_MODE_LINEAR);
            m_impl->m_imageViewModeComboBox->setItemData(0, "Linear mode", Qt::ToolTipRole);
        }

        // Each entry carries the channel it shows, so the selection is read by value and never by its label.
        auto addChannel = [this](const QString& icon, const QString& label, const ImageCompositionMode mode) {
            const QVariant data(static_cast<int>(mode));
            if (icon.isEmpty())
            {
                m_impl->m_imageViewChannelComboBox->addItem(label, data);
            }
            else
            {
                m_impl->m_imageViewChannelComboBox->addItem(m_impl->m_IconProvider->getIcon(icon), label, data);
            }
        };

        if (current_channel_count < 3u)
        {
            addChannel(QString(), IMAGE_VIEW_CHANNEL_GRAYSCALE, ImageCompositionMode::rgb_mode);
            m_impl->m_imageViewChannelComboBox->setEnabled(false);
        }
        else
        {
            m_impl->m_imageViewChannelComboBox->setEnabled(true);
            addChannel(QStringLiteral("rgb"), IMAGE_VIEW_CHANNEL_RGB, ImageCompositionMode::rgb_mode);
            addChannel(QStringLiteral("red_circle"), IMAGE_VIEW_CHANNEL_RED, ImageCompositionMode::r_mode);
            addChannel(QStringLiteral("green_circle"), IMAGE_VIEW_CHANNEL_GREEN, ImageCompositionMode::g_mode);
            addChannel(QStringLiteral("blue_circle"), IMAGE_VIEW_CHANNEL_BLUE, ImageCompositionMode::b_mode);
            m_impl->m_imageViewChannelComboBox->insertSeparator(4);
            addChannel(QStringLiteral("cielab"), IMAGE_VIEW_CHANNEL_CIE_L, ImageCompositionMode::ciel_mode);
            addChannel(QStringLiteral("cielab"), IMAGE_VIEW_CHANNEL_CIE_A, ImageCompositionMode::ciea_mode);
            addChannel(QStringLiteral("cielab"), IMAGE_VIEW_CHANNEL_CIE_B, ImageCompositionMode::cieb_mode);
            addChannel(QStringLiteral("cielab"), IMAGE_VIEW_CHANNEL_CIE_AB, ImageCompositionMode::cieab_mode);
            addChannel(QStringLiteral("cielab"), IMAGE_VIEW_CHANNEL_CIE_C, ImageCompositionMode::ciec_mode);
            addChannel(QStringLiteral("cielab"), IMAGE_VIEW_CHANNEL_CIE_H, ImageCompositionMode::cieh_mode);
            m_impl->m_imageViewChannelComboBox->insertSeparator(11);
            addChannel(QStringLiteral("cielab"), IMAGE_VIEW_CHANNEL_CIE_X, ImageCompositionMode::ciex_mode);
            addChannel(QStringLiteral("cielab"), IMAGE_VIEW_CHANNEL_CIE_Y, ImageCompositionMode::ciey_mode);
            addChannel(QStringLiteral("cielab"), IMAGE_VIEW_CHANNEL_CIE_Z, ImageCompositionMode::ciez_mode);
            m_impl->m_imageViewChannelComboBox->insertSeparator(15);
            addChannel(QStringLiteral("hsl"), IMAGE_VIEW_CHANNEL_HSL_HUE, ImageCompositionMode::hsl_h_mode);
            addChannel(QStringLiteral("hsl"), IMAGE_VIEW_CHANNEL_HSL_SATURATION, ImageCompositionMode::hsl_s_mode);
            addChannel(QStringLiteral("hsl"), IMAGE_VIEW_CHANNEL_HSL_LIGHTNESS, ImageCompositionMode::hsl_l_mode);
            m_impl->m_imageViewChannelComboBox->insertSeparator(19);
            addChannel(QStringLiteral("hsv"), IMAGE_VIEW_CHANNEL_HSV_HUE, ImageCompositionMode::hsv_h_mode);
            addChannel(QStringLiteral("hsv"), IMAGE_VIEW_CHANNEL_HSV_SATURATION, ImageCompositionMode::hsv_s_mode);
            addChannel(QStringLiteral("hsv"), IMAGE_VIEW_CHANNEL_HSV_VALUE, ImageCompositionMode::hsv_v_mode);
            m_impl->m_imageViewChannelComboBox->insertSeparator(23);
            addChannel(QStringLiteral("hsv"), IMAGE_VIEW_CHANNEL_HSI_HUE, ImageCompositionMode::hsi_h_mode);
            addChannel(QStringLiteral("hsv"), IMAGE_VIEW_CHANNEL_HSI_SATURATION, ImageCompositionMode::hsi_s_mode);
            addChannel(QStringLiteral("hsv"), IMAGE_VIEW_CHANNEL_HSI_INTENSITY, ImageCompositionMode::hsi_i_mode);
            m_impl->m_imageViewChannelComboBox->insertSeparator(27);
            addChannel(QStringLiteral("cielab"), IMAGE_VIEW_CHANNEL_YCBCR_LUMA, ImageCompositionMode::ycbcr_y_mode);
            addChannel(QStringLiteral("cielab"), IMAGE_VIEW_CHANNEL_YCBCR_BLUE_CHROMA,
                       ImageCompositionMode::ycbcr_cb_mode);
            addChannel(QStringLiteral("cielab"), IMAGE_VIEW_CHANNEL_YCBCR_RED_CHROMA,
                       ImageCompositionMode::ycbcr_cr_mode);
        }

        if (m_impl->m_ViewControlMode != ViewControlMode::rt_preview && m_impl->m_imageViewModeComboBox)
        {
            m_impl->m_imageViewModeComboBox->addItem(iconManager->getIcon("image_filled"), IMAGE_VIEW_MODE_AUTOSTRETCH);
            m_impl->m_imageViewModeComboBox->setItemData(1, "Autostretch mode", Qt::ToolTipRole);
        }

        if (currentViewChannel.isValid())
        {
            const int idx = m_impl->m_imageViewChannelComboBox->findData(currentViewChannel);
            if (idx != -1)
            {
                m_impl->m_imageViewChannelComboBox->setCurrentIndex(idx);
            }
        }

        if (!currentViewMode.isEmpty() && m_impl->m_ViewControlMode != ViewControlMode::rt_preview &&
            m_impl->m_imageViewModeComboBox)
        {
            const int idx = m_impl->m_imageViewModeComboBox->findText(currentViewMode);
            if (idx != -1)
            {
                m_impl->m_imageViewModeComboBox->setCurrentIndex(idx);
            }
        }

        // The mode was restored with its signals blocked, so the settings button is offered for it here.
        m_impl->refreshAutostretchSettingsAvailability();
        m_impl->m_LastImageChannels.store(current_channel_count, std::memory_order_relaxed);
        channel_count_changed = previous_channel_count != current_channel_count;
    }
    else
    {
        m_impl->m_LastImageChannels.store(0u, std::memory_order_relaxed);
    }

    m_impl->m_imageViewChannelComboBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    updateComboBoxWidthToContents(m_impl->m_imageViewChannelComboBox, 8, kImageChannelComboMinWidth);

    if (m_impl->m_ViewControlMode != ViewControlMode::rt_preview && m_impl->m_imageViewModeComboBox)
    {
        m_impl->m_imageViewModeComboBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        updateComboBoxWidthToContents(m_impl->m_imageViewModeComboBox, 8, kImageModeComboMinWidth, true);
    }

    if (channel_count_changed && m_impl->m_ViewControlMode == ViewControlMode::standard)
    {
        if (auto* context = an::ui::appContext())
        {
            if (auto* actionStateManager = context->getActionStateManager())
            {
                actionStateManager->updateActiveImageActions();
            }
        }
    }
}

void ImageViewControl::notifyImageDisplayChannelChangedListeners(const ImageCompositionMode mode)
{
    std::shared_lock lock(m_impl->m_listeners_mutex);
    auto listenersCopy = m_impl->m_listeners;
    lock.unlock();
    for (auto* listener : listenersCopy)
    {
        listener->onImageDisplayChannelChanged(this, static_cast<int>(mode));
    }
}

void ImageViewControl::displayImageChannelFromSelection(const int index)
{
    CHECK_IF_CONTROL_IS_CLOSING;

    ImageCompositionMode mode = ImageCompositionMode::rgb_mode;
    const QVariant data = index >= 0 ? m_impl->m_imageViewChannelComboBox->itemData(index) : QVariant();
    if (data.isValid())
    {
        mode = static_cast<ImageCompositionMode>(data.toInt());
    }

    displayViewChannel(mode);
    notifyImageDisplayChannelChangedListeners(mode);
}

void ImageViewControl::keyPressEvent(QKeyEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier)
    {
        event->accept();
        if (event->key() == Qt::Key_Z)
        {
            undo();
        }
        else if (event->key() == Qt::Key_Y)
        {
            redo();
        }
    }

    QWidget::keyPressEvent(event);
}

bool ImageViewControl::eventFilter(QObject* watched, QEvent* event)
{
    if (m_impl->m_ViewControlMode == ViewControlMode::st_preview && m_impl->m_ScrollArea &&
        watched == m_impl->m_ScrollArea->viewport())
    {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Show)
        {
            m_impl->layoutStaticPreviewStatsOverlay();
        }

        return QWidget::eventFilter(watched, event);
    }

    if (watched != m_impl->m_ImageViewWidget)
        return QWidget::eventFilter(watched, event);

    const auto type = event->type();
    if (type != QEvent::KeyPress && type != QEvent::KeyRelease)
        return QWidget::eventFilter(watched, event);

    auto* keyEvent = static_cast<QKeyEvent*>(event);
    if (keyEvent->isAutoRepeat())
        return QWidget::eventFilter(watched, event);

    const auto key = keyEvent->key();
    const bool ctrl = (keyEvent->modifiers() & Qt::ControlModifier) != 0;
    const bool shift = (keyEvent->modifiers() & Qt::ShiftModifier) != 0;

    // Space — hold to peek at original (RT preview only, both press and release toggle)
    if (key == Qt::Key_Space && m_impl->m_ViewControlMode == ViewControlMode::rt_preview)
    {
        m_impl->m_ImageViewWidget->enableAlternateImageRendering(
            !m_impl->m_ImageViewWidget->isAlternateImageRenderEnabled());
        return true;
    }

    // All remaining shortcuts are press-only
    if (type == QEvent::KeyRelease)
        return QWidget::eventFilter(watched, event);

    if (!ctrl && !shift)
    {
        if (key == Qt::Key_F11)
        {
            switchMaximized();
            return true;
        }
    }
    else if (ctrl && !shift)
    {
        switch (key)
        {
        case Qt::Key_H:
            if (isShortcutActionAvailable(m_impl->m_HistogramAction))
                executeHistogram();
            return true;
        case Qt::Key_P:
            if (isShortcutActionAvailable(m_impl->m_PlateSolveToolbarAction))
                executePlateSolve();
            return true;
        case Qt::Key_1:
            if (isShortcutActionAvailable(m_impl->m_AstrometrySettingsAction) && m_impl->m_AstrometryGridMenuAction)
                m_impl->m_AstrometryGridMenuAction->toggle();
            return true;
        case Qt::Key_2:
            if (isShortcutActionAvailable(m_impl->m_AstrometrySettingsAction) &&
                m_impl->m_AstrometryAnnotationsMenuAction)
                m_impl->m_AstrometryAnnotationsMenuAction->toggle();
            return true;
        case Qt::Key_3:
            if (isShortcutActionAvailable(m_impl->m_AstrometrySettingsAction) &&
                m_impl->m_AstrometryStarLabelsMenuAction)
                m_impl->m_AstrometryStarLabelsMenuAction->toggle();
            return true;
        case Qt::Key_4:
            if (isShortcutActionAvailable(m_impl->m_AstrometrySettingsAction) &&
                m_impl->m_AstrometryDetectedStarsMenuAction)
                m_impl->m_AstrometryDetectedStarsMenuAction->toggle();
            return true;
        case Qt::Key_5:
            if (isShortcutActionAvailable(m_impl->m_AstrometrySettingsAction) && m_impl->m_AstrometrySipMenuAction)
                m_impl->m_AstrometrySipMenuAction->toggle();
            return true;
        default:
            break;
        }
    }
    else if (ctrl && shift)
    {
        switch (key)
        {
        case Qt::Key_F:
            adjustImageToWindow();
            return true;
        case Qt::Key_H:
            executeFITSHeaderViewer();
            return true;
        case Qt::Key_T:
            if (isShortcutActionAvailable(m_impl->m_StatisticsAction))
                executeStatistics();
            return true;
        case Qt::Key_X:
            executeEXIFViewer();
            return true;
        case Qt::Key_M:
            if (isShortcutActionAvailable(m_impl->m_ImageSummaryAction))
                executeSummaryViewer();
            return true;
        case Qt::Key_Z:
            history();
            return true;
        case Qt::Key_A:
            autostretchSettings();
            return true;
        case Qt::Key_P:
            if (isShortcutActionAvailable(m_impl->m_AstrometryInfoAction))
                executeAstrometryInfoViewer();
            return true;
        case Qt::Key_W:
            if (isShortcutActionAvailable(m_impl->m_AstrometryToggleAction))
                m_impl->m_AstrometryToggleAction->toggle();
            return true;
        case Qt::Key_L:
            if (isShortcutActionAvailable(m_impl->m_AnnotateImageToolbarAction))
                executeAnnotateImage();
            return true;
        case Qt::Key_K:
            if (m_impl->m_ViewControlMode == ViewControlMode::rt_preview)
            {
                on_rtpreview_take_snapshot_clicked();
                return true;
            }
            break;
        case Qt::Key_O:
            if (m_impl->m_ViewControlMode == ViewControlMode::rt_preview)
            {
                on_rtpreview_switch_images_clicked();
                return true;
            }
            break;
        default:
            break;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void ImageViewControl::contextMenuEvent(QContextMenuEvent* contextMenuEvent)
{
    CHECK_IF_CONTROL_IS_CLOSING;

    if (m_impl->m_ViewControlMode == ViewControlMode::standard && m_impl->m_ImageViewWidget->isContextMenuSupported())
    {
        an::ui::appContext()->getViewControlManager()->createContextMenuForViewControl(this, contextMenuEvent);
    }
}

void ImageViewControl::mousePressEvent(QMouseEvent* event)
{
    QWidget::mousePressEvent(event);

    if (isMaximized())
    {
        setFocus();
    }

    if (getViewControlMode() == ViewControlMode::standard)
    {
        an::ui::appContext()->getViewRegistry()->setActiveImageViewControl(this);
    }
}

void ImageViewControl::mouseReleaseEvent(QMouseEvent* event)
{
    QWidget::mouseReleaseEvent(event);
}

void ImageViewControl::mouseDoubleClickEvent(QMouseEvent* event)
{
    QWidget::mouseDoubleClickEvent(event);

    if (isMaximized())
    {
        setFocus();
    }

    if (getViewControlMode() == ViewControlMode::standard)
    {
        an::ui::appContext()->getViewRegistry()->setActiveImageViewControl(this);
    }
}

void ImageViewControl::updateFloatingStatus()
{
    auto* context = an::ui::appContext();
    auto* viewControlManager = context ? context->getViewControlManager() : nullptr;
    const bool supportsDockWidgets = viewControlManager && viewControlManager->supportsDockWidgets();

    if (supportsDockWidgets && m_DockWidget)
    {
        m_impl->m_IsFloating = !m_DockWidget->dockWidget()->isInMainWindow();
    }
    else
    {
        m_impl->m_IsFloating = false;
    }

    if (m_impl->m_MaximizeAction)
    {
        m_impl->m_MaximizeAction->setEnabled(supportsDockWidgets && !m_impl->m_IsFloating);
    }
}

ViewControlTextOverlay::ViewControlTextOverlay(QWidget* parent, Qt::WindowFlags f) : QWidget(parent, f)
{
    // this->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    this->setAttribute(Qt::WA_TranslucentBackground);
    this->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void ViewControlTextOverlay::setText(const QString& text, const QColor& color)
{
    m_Text = text;
    m_Color = color;

    const QFont bannerFont = an::ui::tokens().typography.heading1;
    QFontMetrics metrics(bannerFont);
    const QRect textRect = metrics.boundingRect(m_Text);
    constexpr int kPaddingX = 16;
    constexpr int kPaddingY = 10;
    const QSize newSize(textRect.size().width() + kPaddingX * 2, textRect.size().height() + kPaddingY * 2);
    setFixedSize(newSize);

    update();
}

void ViewControlTextOverlay::paintEvent(QPaintEvent* ev)
{
    QWidget::paintEvent(ev);

    QPainter painter(this);
    const bool darkMode = an::ui::styling::isDarkPalette(palette());
    painter.fillRect(rect(), an::ui::imageViewOverlayBackdropColor(darkMode));

    QPen pen = painter.pen();

    pen.setColor(m_Color);
    pen.setWidth(3);
    painter.setPen(pen);

    painter.setFont(an::ui::tokens().typography.heading1);

    painter.drawText(rect(), Qt::AlignCenter, m_Text);
}

CustomToolBar::CustomToolBar(ImageViewControl* vControl) : QToolBar(vControl), m_ImageViewControl(vControl)
{
    m_OverflowButton = new OverflowToolButton(this);
    m_OverflowButton->setObjectName(QStringLiteral("imageViewToolbarOverflowButton"));
    m_OverflowButton->setAutoRaise(true);
    m_OverflowButton->setPopupMode(QToolButton::InstantPopup);
    m_OverflowButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_OverflowButton->setText(QStringLiteral("More"));
    m_OverflowButton->setToolTip(QStringLiteral("More tools"));

    m_OverflowMenu = new QMenu(m_OverflowButton);
    m_OverflowButton->setMenu(m_OverflowMenu);
    QObject::connect(m_OverflowMenu, &QMenu::aboutToShow, this, &CustomToolBar::rebuildOverflowMenu);

    m_OverflowAction = addWidget(m_OverflowButton);
    if (m_OverflowAction)
    {
        m_OverflowAction->setVisible(false);
    }

    hideDefaultToolbarExtensionButton(this);
}

void CustomToolBar::setOverflowIconColors(const QColor& normalColor, const QColor& disabledColor)
{
    if (m_OverflowButton)
    {
        auto iconProvider = an::ui::appContext()->getIconProvider();
        m_OverflowButton->setIcon(iconProvider->getIcon("more_vert"));
        hideDefaultToolbarExtensionButton(this, m_OverflowButton->icon());
    }
}

void CustomToolBar::refreshOverflow()
{
    queueOverflowRefresh();
}

void CustomToolBar::resizeEvent(QResizeEvent* event)
{
    QToolBar::resizeEvent(event);
    hideDefaultToolbarExtensionButton(this);
    queueOverflowRefresh();
}

void CustomToolBar::showEvent(QShowEvent* event)
{
    QToolBar::showEvent(event);
    hideDefaultToolbarExtensionButton(this);
    queueOverflowRefresh();
}

void CustomToolBar::actionEvent(QActionEvent* event)
{
    QToolBar::actionEvent(event);
    hideDefaultToolbarExtensionButton(this);

    if (!event || m_ReorderingOverflowAction)
    {
        return;
    }

    if (event->type() == QEvent::ActionAdded)
    {
        QAction* action = event->action();
        if (action && !action->property("anOverflowRefreshConnected").toBool())
        {
            QObject::connect(action, &QAction::changed, this, [this]() { queueOverflowRefresh(); });
            action->setProperty("anOverflowRefreshConnected", true);
        }
    }

    if (event->action() != m_OverflowAction)
    {
        ensureOverflowActionIsLast();
    }

    queueOverflowRefresh();
}

void CustomToolBar::mousePressEvent(QMouseEvent* event)
{
    QToolBar::mousePressEvent(event);

    if (m_ImageViewControl)
    {
        m_ImageViewControl->setFocus();
    }
}

void CustomToolBar::ensureOverflowActionIsLast()
{
    if (!m_OverflowAction || m_ReorderingOverflowAction)
    {
        return;
    }

    const QList<QAction*> currentActions = actions();
    if (!currentActions.isEmpty() && currentActions.back() == m_OverflowAction)
    {
        return;
    }

    m_ReorderingOverflowAction = true;
    removeAction(m_OverflowAction);
    addAction(m_OverflowAction);
    m_ReorderingOverflowAction = false;
}

void CustomToolBar::queueOverflowRefresh()
{
    if (m_OverflowRefreshPending || m_UpdatingOverflowLayout)
    {
        return;
    }

    m_OverflowRefreshPending = true;
    QPointer<CustomToolBar> self(this);
    QTimer::singleShot(0, this, [self]() {
        if (!self)
        {
            return;
        }

        self->m_OverflowRefreshPending = false;
        self->updateOverflowLayout();
    });
}

void CustomToolBar::updateOverflowLayout()
{
    if (m_UpdatingOverflowLayout || !m_OverflowButton || !m_OverflowMenu || !m_OverflowAction)
    {
        return;
    }

    m_UpdatingOverflowLayout = true;
    ensureOverflowActionIsLast();

    // Restore actions that were hidden by the previous overflow pass.
    for (QAction* action : actions())
    {
        if (!action)
        {
            continue;
        }

        if (action->property(kOverflowHiddenProperty).toBool())
        {
            action->setProperty(kOverflowHiddenProperty, false);
            action->setProperty(kOverflowWasEnabledProperty, QVariant());
            action->setVisible(true);
            if (QWidget* widget = widgetForAction(action))
            {
                widget->setVisible(true);
            }
        }
    }

    m_OverflowedActions.clear();

    // Activate layout after restoring hidden actions so widgets have up-to-date
    // geometry before we measure their widths.
    if (layout())
    {
        layout()->activate();
    }

    // Account for spacing between toolbar items that sizeHint() does not include.
    int itemSpacing = layout() ? layout()->spacing() : 0;
    if (itemSpacing < 0)
    {
        itemSpacing = style()->pixelMetric(QStyle::PM_ToolBarItemSpacing, nullptr, this);
    }
    itemSpacing = (std::max)(0, itemSpacing);

    std::vector<ToolbarOverflowCandidate> candidates;
    candidates.reserve(actions().size());

    for (QAction* action : actions())
    {
        if (!action || action == m_OverflowAction || !action->isVisible())
        {
            continue;
        }

        QWidget* widget = widgetForAction(action);
        if (widget)
        {
            widget->setVisible(true);
        }

        candidates.push_back({action, widget, resolveToolbarActionWidth(this, action, widget) + itemSpacing});
    }

    if (candidates.empty())
    {
        m_OverflowAction->setVisible(false);
        m_OverflowButton->setVisible(false);
        m_OverflowMenu->clear();
        m_UpdatingOverflowLayout = false;
        return;
    }

    std::vector<int> widths;
    widths.reserve(candidates.size());
    for (const auto& candidate : candidates)
    {
        widths.push_back(candidate.width);
    }

    const int availableWidth = (std::max)(0, contentsRect().width());
    const int overflowButtonWidth = resolveToolbarActionWidth(this, m_OverflowAction, m_OverflowButton);

    int overflowIndex = an::ui::controls::firstOverflowIndex(widths, availableWidth, overflowButtonWidth);
    if (overflowIndex < 0)
    {
        m_OverflowAction->setVisible(false);
        m_OverflowButton->setVisible(false);
        m_OverflowMenu->clear();
        m_UpdatingOverflowLayout = false;
        return;
    }

    while (overflowIndex > 0 && candidates[overflowIndex - 1].action->isSeparator())
    {
        --overflowIndex;
    }

    for (int i = overflowIndex; i < static_cast<int>(candidates.size()); ++i)
    {
        auto& candidate = candidates[static_cast<size_t>(i)];
        candidate.action->setProperty(kOverflowWasEnabledProperty, candidate.action->isEnabled());
        candidate.action->setProperty(kOverflowHiddenProperty, true);
        candidate.action->setVisible(false);
        m_OverflowedActions.append(candidate.action);
    }

    rebuildOverflowMenu();

    const bool hasOverflowEntries = !m_OverflowMenu->actions().isEmpty();
    m_OverflowAction->setVisible(hasOverflowEntries);
    m_OverflowButton->setVisible(hasOverflowEntries);
    hideDefaultToolbarExtensionButton(this);

    m_UpdatingOverflowLayout = false;
}

void CustomToolBar::rebuildOverflowMenu()
{
    if (!m_OverflowMenu)
    {
        return;
    }

    m_OverflowMenu->clear();

    bool pendingSeparator = false;
    for (QAction* action : m_OverflowedActions)
    {
        if (!action)
        {
            continue;
        }

        QWidget* actionWidget = widgetForAction(action);
        if (isToolbarSpacerWidget(actionWidget))
        {
            continue;
        }

        if (action->isSeparator())
        {
            pendingSeparator = !m_OverflowMenu->actions().isEmpty();
            continue;
        }

        if (pendingSeparator)
        {
            m_OverflowMenu->addSeparator();
            pendingSeparator = false;
        }

        if (auto* comboBox = qobject_cast<QComboBox*>(actionWidget))
        {
            const QVariant preservedEnabled = action->property(kOverflowWasEnabledProperty);
            const bool comboWasEnabled = preservedEnabled.isValid() ? preservedEnabled.toBool() : comboBox->isEnabled();

            const QString prefix = overflowComboLabel(comboBox);
            const int currentIndex = comboBox->currentIndex();
            const QIcon currentIcon = currentIndex >= 0 ? comboBox->itemIcon(currentIndex) : QIcon();
            QMenu* comboMenu =
                m_OverflowMenu->addMenu(currentIcon, QStringLiteral("%1: %2").arg(prefix, comboBox->currentText()));
            comboMenu->setEnabled(comboWasEnabled);

            for (int i = 0; i < comboBox->count(); ++i)
            {
                QAction* itemAction = comboMenu->addAction(comboBox->itemIcon(i), comboBox->itemText(i));
                itemAction->setCheckable(true);
                itemAction->setChecked(i == comboBox->currentIndex());

                bool itemEnabled = comboWasEnabled;
                if (itemEnabled && comboBox->model())
                {
                    const QModelIndex idx =
                        comboBox->model()->index(i, comboBox->modelColumn(), comboBox->rootModelIndex());
                    itemEnabled = comboBox->model()->flags(idx).testFlag(Qt::ItemIsEnabled);
                }
                itemAction->setEnabled(itemEnabled);

                QObject::connect(itemAction, &QAction::triggered, comboBox,
                                 [comboBox, i]() { comboBox->setCurrentIndex(i); });
            }

            continue;
        }

        if (auto* checkBox = qobject_cast<QCheckBox*>(actionWidget))
        {
            const QString label = checkBox->text().trimmed();
            if (label.isEmpty())
            {
                continue;
            }

            // Hiding the action for the overflow disabled the widget it holds, so the menu offers the state the action
            // had before it was hidden, as it does for a combo box.
            const QVariant preservedEnabled = action->property(kOverflowWasEnabledProperty);
            const bool wasEnabled = preservedEnabled.isValid() ? preservedEnabled.toBool() : checkBox->isEnabled();
            QAction* overflowItem = m_OverflowMenu->addAction(label);
            overflowItem->setEnabled(wasEnabled);
            overflowItem->setCheckable(true);
            overflowItem->setChecked(checkBox->isChecked());
            overflowItem->setToolTip(checkBox->toolTip());

            QObject::connect(overflowItem, &QAction::triggered, checkBox, [checkBox, wasEnabled](bool checked) {
                if (!wasEnabled)
                {
                    return;
                }

                checkBox->setChecked(checked);
            });

            continue;
        }

        if (action->text().isEmpty() && action->icon().isNull())
        {
            continue;
        }

        QAction* overflowItem = m_OverflowMenu->addAction(action->icon(), action->text());
        const QVariant preservedEnabled = action->property(kOverflowWasEnabledProperty);
        const bool wasEnabled = preservedEnabled.isValid() ? preservedEnabled.toBool() : action->isEnabled();
        overflowItem->setEnabled(wasEnabled);
        overflowItem->setCheckable(action->isCheckable());
        if (action->isCheckable())
        {
            overflowItem->setChecked(action->isChecked());
        }

        QObject::connect(overflowItem, &QAction::triggered, action, [action, wasEnabled](bool checked) {
            if (!wasEnabled)
            {
                return;
            }

            const bool temporarilyEnable = !action->isEnabled();
            if (temporarilyEnable)
            {
                action->setEnabled(true);
            }

            if (action->isCheckable())
            {
                action->setChecked(checked);
            }
            action->trigger();

            if (temporarilyEnable)
            {
                action->setEnabled(false);
            }
        });
    }
}
