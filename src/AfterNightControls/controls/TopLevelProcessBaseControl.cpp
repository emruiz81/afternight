// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "TopLevelProcessBaseControl.h"
#include "controls/IAppContext.h"
#include "app/ModernizationFeatureFlags.h"
#include "controls/GlobalSettingsPersistence.h"
#include "controls/ImageViewControl.h"
#include "theme/AnDesignTokens.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"
#include "controls/ProcessButtonIcons.h"

#include <QDialogButtonBox>
#include <QFrame>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QLayoutItem>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QScreen>
#include <QShowEvent>
#include <QStyle>
#include <QThreadPool>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>
#include <cmath>

namespace
{
constexpr auto PROCESS_DESCRIPTION_PROPERTY = "anProcessDescription";
constexpr auto PROCESS_MODERN_SHELL_ENABLED_PROPERTY = "anModernProcessShellEnabled";
constexpr auto PROCESS_WINDOW_SIZE_MULTIPLIER_X_PROPERTY = "anProcessWindowSizeMultiplierX";
constexpr auto PROCESS_WINDOW_SIZE_MULTIPLIER_Y_PROPERTY = "anProcessWindowSizeMultiplierY";
constexpr auto PROCESS_WINDOW_SIZE_BASELINE_PROPERTY = "anProcessWindowSizeBaseline";
constexpr double PROCESS_WINDOW_SIZE_MULTIPLIER_DEFAULT = 1.0;

constexpr auto PROCESS_HEADER_OBJECT_NAME = "anProcessShellHeader";
constexpr auto PROCESS_HEADER_TITLE_OBJECT_NAME = "anProcessShellHeaderTitle";
constexpr auto PROCESS_HEADER_DESCRIPTION_OBJECT_NAME = "anProcessShellHeaderDescription";
constexpr auto PROCESS_BANNER_OBJECT_NAME = "anProcessShellBanner";
constexpr auto PROCESS_BANNER_MESSAGE_OBJECT_NAME = "anProcessShellBannerMessage";
constexpr auto PROCESS_BANNER_PROGRESS_OBJECT_NAME = "anProcessShellBannerProgress";
constexpr auto PROCESS_ACTION_BAR_PROPERTY = "anProcessActionBar";
constexpr auto PROCESS_CARD_OBJECT_NAME = "anProcessShellCard";
constexpr auto PROCESS_CONTENT_OBJECT_NAME = "anProcessShellContent";

QSize sanitizeMinSize(const QSize& size)
{
    constexpr int kSafeWidgetMax = (std::min)(QWIDGETSIZE_MAX - 1, 1000000);
    const int width = std::clamp(size.width(), 0, kSafeWidgetMax);
    const int height = std::clamp(size.height(), 0, kSafeWidgetMax);
    return QSize(width, height);
}

double sanitizeWindowSizeMultiplier(const double multiplier)
{
    if (!std::isfinite(multiplier))
    {
        return PROCESS_WINDOW_SIZE_MULTIPLIER_DEFAULT;
    }

    return (std::max)(PROCESS_WINDOW_SIZE_MULTIPLIER_DEFAULT, multiplier);
}

QSize applySizeMultipliers(const QSize& size, const double widthMultiplier, const double heightMultiplier)
{
    if (!size.isValid())
    {
        return size;
    }

    const double effectiveWidthMultiplier = sanitizeWindowSizeMultiplier(widthMultiplier);
    const double effectiveHeightMultiplier = sanitizeWindowSizeMultiplier(heightMultiplier);
    if (effectiveWidthMultiplier <= PROCESS_WINDOW_SIZE_MULTIPLIER_DEFAULT &&
        effectiveHeightMultiplier <= PROCESS_WINDOW_SIZE_MULTIPLIER_DEFAULT)
    {
        return size;
    }

    const int scaledWidth = static_cast<int>(std::ceil(size.width() * effectiveWidthMultiplier));
    const int scaledHeight = static_cast<int>(std::ceil(size.height() * effectiveHeightMultiplier));
    return QSize(scaledWidth, scaledHeight);
}

bool isModernShellEnabled(const QWidget* widget)
{
    if (!widget)
    {
        return false;
    }

    const QVariant modernShellProperty = widget->property(PROCESS_MODERN_SHELL_ENABLED_PROPERTY);
    return !modernShellProperty.isValid() || modernShellProperty.toBool();
}

bool shouldRememberWindowGeometry()
{
    auto* settings = an::ui::appContext()->getGlobalSettings();
    if (settings)
    {
        QString rememberSizeStr = settings->select(SettingsKeys::INTERFACE_REMEMBER_WINDOW_SIZE,
                                                   SettingsDefaults::INTERFACE_REMEMBER_WINDOW_SIZE ? "1" : "0");
        return (rememberSizeStr == "1");
    }
    return SettingsDefaults::INTERFACE_REMEMBER_WINDOW_SIZE;
}

void applyButtonRoleProperty(QDialogButtonBox* buttonBox, const QDialogButtonBox::StandardButton standardButton,
                             const char* role)
{
    if (!buttonBox || !role)
    {
        return;
    }

    if (auto* button = buttonBox->button(standardButton))
    {
        button->setProperty("anProcessButtonRole", QString::fromUtf8(role));
        if (auto* style = button->style())
        {
            style->unpolish(button);
            style->polish(button);
        }
        button->update();
    }
}

void applyProcessButtonRoles(QWidget* root)
{
    if (!root)
    {
        return;
    }

    const auto buttonBoxes = root->findChildren<QDialogButtonBox*>();
    for (auto* buttonBox : buttonBoxes)
    {
        applyButtonRoleProperty(buttonBox, QDialogButtonBox::Apply, "primary");
        applyButtonRoleProperty(buttonBox, QDialogButtonBox::Ok, "primary");
        applyButtonRoleProperty(buttonBox, QDialogButtonBox::Yes, "primary");
        applyButtonRoleProperty(buttonBox, QDialogButtonBox::Close, "secondary");
        applyButtonRoleProperty(buttonBox, QDialogButtonBox::Cancel, "secondary");
        applyButtonRoleProperty(buttonBox, QDialogButtonBox::No, "secondary");
        applyButtonRoleProperty(buttonBox, QDialogButtonBox::Reset, "neutral");
    }
}

void applyProcessButtonIcons(QWidget* root)
{
    if (!root)
    {
        return;
    }

    auto iconProvider = an::ui::appContext()->getIconProvider();
    if (!iconProvider)
    {
        return;
    }

    const auto buttonBoxes = root->findChildren<QDialogButtonBox*>();
    for (auto* buttonBox : buttonBoxes)
    {
        process_utils::applyDialogButtonBoxIcons(buttonBox, iconProvider.get());
    }
}

void moveLayoutItems(QLayout* sourceLayout, QBoxLayout* targetLayout)
{
    if (!sourceLayout || !targetLayout)
    {
        return;
    }

    // Capture stretch factors before moving items so they can be restored
    // on the target layout (crucial for QHBoxLayout left/right splits).
    auto* sourceBox = qobject_cast<QBoxLayout*>(sourceLayout);
    const int itemCount = sourceLayout->count();
    QVector<int> stretches(itemCount, 0);
    if (sourceBox)
    {
        for (int i = 0; i < itemCount; ++i)
        {
            stretches[i] = sourceBox->stretch(i);
        }
    }

    while (QLayoutItem* item = sourceLayout->takeAt(0))
    {
        if (QWidget* widget = item->widget())
        {
            targetLayout->addWidget(widget);
            delete item;
        }
        else if (QLayout* nestedLayout = item->layout())
        {
            targetLayout->addLayout(nestedLayout);
        }
        else
        {
            targetLayout->addItem(item);
        }
    }

    // Restore stretch factors on the target layout.
    const int targetCount = targetLayout->count();
    const int stretchCount = static_cast<int>(stretches.size());
    for (int i = 0; i < (std::min)(targetCount, stretchCount); ++i)
    {
        targetLayout->setStretch(i, stretches[i]);
    }
}

/// Recursively search @p layout for a QDialogButtonBox, remove it from
/// whatever (possibly nested) layout owns it, and return the pointer.
/// Returns nullptr when no QDialogButtonBox is found.
QDialogButtonBox* extractDialogButtonBox(QLayout* layout)
{
    if (!layout)
    {
        return nullptr;
    }

    for (int i = layout->count() - 1; i >= 0; --i)
    {
        QLayoutItem* item = layout->itemAt(i);
        if (!item)
        {
            continue;
        }

        if (auto* widget = item->widget())
        {
            if (auto* btnBox = qobject_cast<QDialogButtonBox*>(widget))
            {
                layout->removeWidget(btnBox);
                return btnBox;
            }

            if (auto* widgetLayout = widget->layout())
            {
                if (auto* found = extractDialogButtonBox(widgetLayout))
                {
                    return found;
                }
            }
        }
        else if (auto* childLayout = item->layout())
        {
            if (auto* found = extractDialogButtonBox(childLayout))
            {
                return found;
            }
        }
    }
    return nullptr;
}

QDialogButtonBox* extractDialogButtonBox(QWidget* root)
{
    if (!root)
    {
        return nullptr;
    }

    const auto buttonBoxes = root->findChildren<QDialogButtonBox*>();
    for (auto it = buttonBoxes.crbegin(); it != buttonBoxes.crend(); ++it)
    {
        auto* buttonBox = *it;
        if (!buttonBox)
        {
            continue;
        }

        if (auto* parentWidget = buttonBox->parentWidget())
        {
            if (auto* parentLayout = parentWidget->layout())
            {
                parentLayout->removeWidget(buttonBox);
            }
        }

        return buttonBox;
    }

    return nullptr;
}

QWidget* extractProcessActionBar(QLayout* layout)
{
    if (!layout)
    {
        return nullptr;
    }

    for (int i = layout->count() - 1; i >= 0; --i)
    {
        QLayoutItem* item = layout->itemAt(i);
        if (!item)
        {
            continue;
        }

        if (auto* widget = item->widget())
        {
            if (widget->property(PROCESS_ACTION_BAR_PROPERTY).toBool())
            {
                layout->removeWidget(widget);
                return widget;
            }

            if (auto* widgetLayout = widget->layout())
            {
                if (auto* found = extractProcessActionBar(widgetLayout))
                {
                    return found;
                }
            }
        }
        else if (auto* childLayout = item->layout())
        {
            if (auto* found = extractProcessActionBar(childLayout))
            {
                return found;
            }
        }
    }

    return nullptr;
}

QWidget* extractProcessActionBar(QWidget* root)
{
    if (!root)
    {
        return nullptr;
    }

    const auto actionBars = root->findChildren<QWidget*>(QString(), Qt::FindChildrenRecursively);
    for (QWidget* actionBar : actionBars)
    {
        if (!actionBar || !actionBar->property(PROCESS_ACTION_BAR_PROPERTY).toBool())
        {
            continue;
        }

        if (auto* parentWidget = actionBar->parentWidget())
        {
            if (auto* parentLayout = parentWidget->layout())
            {
                parentLayout->removeWidget(actionBar);
            }
        }
        return actionBar;
    }

    return nullptr;
}

void applyFlatGroupBoxVariantStyles(QWidget* root)
{
    if (!root)
    {
        return;
    }

    const auto groupBoxes = root->findChildren<QGroupBox*>();
    for (QGroupBox* groupBox : groupBoxes)
    {
        if (!groupBox || !groupBox->isFlat())
        {
            continue;
        }

        if (groupBox->property("anGroupVariant").toString() == QStringLiteral("flat"))
        {
            continue;
        }

        groupBox->setProperty("anGroupVariant", QStringLiteral("flat"));

        if (auto* style = groupBox->style())
        {
            style->unpolish(groupBox);
            style->polish(groupBox);
        }
        groupBox->update();
    }
}

QString tonePropertyValue(const TopLevelProcessBaseControl::ProcessMessageTone tone)
{
    switch (tone)
    {
    case TopLevelProcessBaseControl::ProcessMessageTone::Warning:
        return QStringLiteral("warning");
    case TopLevelProcessBaseControl::ProcessMessageTone::Error:
        return QStringLiteral("error");
    case TopLevelProcessBaseControl::ProcessMessageTone::Info:
    default:
        return QStringLiteral("info");
    }
}

QWidget* findShellCard(QWidget* root)
{
    return root ? root->findChild<QWidget*>(QString::fromUtf8(PROCESS_CARD_OBJECT_NAME)) : nullptr;
}

QLabel* findHeaderDescriptionLabel(QWidget* root)
{
    return root ? root->findChild<QLabel*>(QString::fromUtf8(PROCESS_HEADER_DESCRIPTION_OBJECT_NAME)) : nullptr;
}

QWidget* findInfoBanner(QWidget* root)
{
    return root ? root->findChild<QWidget*>(QString::fromUtf8(PROCESS_BANNER_OBJECT_NAME)) : nullptr;
}

QLabel* findInfoBannerMessageLabel(QWidget* root)
{
    return root ? root->findChild<QLabel*>(QString::fromUtf8(PROCESS_BANNER_MESSAGE_OBJECT_NAME)) : nullptr;
}

QProgressBar* findInfoBannerProgressBar(QWidget* root)
{
    return root ? root->findChild<QProgressBar*>(QString::fromUtf8(PROCESS_BANNER_PROGRESS_OBJECT_NAME)) : nullptr;
}

void fitFreeformButtonWidths(QWidget* root)
{
    if (!root)
    {
        return;
    }

    const auto buttons = root->findChildren<QPushButton*>();
    for (QPushButton* button : buttons)
    {
        if (!button)
        {
            continue;
        }

        if (qobject_cast<QDialogButtonBox*>(button->parentWidget()))
        {
            continue;
        }

        QWidget* parent = button->parentWidget();
        if (!parent || parent->layout())
        {
            continue;
        }

        button->ensurePolished();
        const int requiredWidth = (std::max)(button->sizeHint().width(), button->minimumSizeHint().width());
        if (requiredWidth <= button->width())
        {
            continue;
        }

        QRect buttonGeometry = button->geometry();
        const int rightEdge = buttonGeometry.right();
        buttonGeometry.setWidth(requiredWidth);
        buttonGeometry.moveRight(rightEdge);
        if (buttonGeometry.left() < 0)
        {
            buttonGeometry.moveLeft(0);
        }
        button->setGeometry(buttonGeometry);
    }
}

void ensureModernShellSize(QWidget* root, const double widthMultiplier, const double heightMultiplier)
{
    if (!root)
    {
        return;
    }

    if (!an::ui::modernization::ModernizationFeatureFlags::uiTokensEnabled())
    {
        return;
    }

    if (!findShellCard(root))
    {
        return;
    }

    QLayout* rootLayout = root->layout();
    if (!rootLayout)
    {
        return;
    }

    // Use the layout's sizeHint as the primary source of the required
    // content size, but fall back to minimumSize() which is more reliably
    // populated for geometry-based children (no layout, but minimumSize
    // explicitly set).
    QSize requiredSize = rootLayout->sizeHint();
    const QSize layoutMinSize = rootLayout->minimumSize();
    if (layoutMinSize.isValid())
    {
        requiredSize = requiredSize.isValid() ? requiredSize.expandedTo(layoutMinSize) : layoutMinSize;
    }
    if (!requiredSize.isValid())
    {
        return;
    }

    const QSize baseRequiredSize = requiredSize;
    const QSize scaledRequiredSize = applySizeMultipliers(baseRequiredSize, widthMultiplier, heightMultiplier);

    const bool wasFixedSize = (root->maximumSize() == root->minimumSize());
    if (wasFixedSize)
    {
        root->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    }

    // Keep non-fixed windows shrinkable down to their natural minimum
    // (unscaled), while still opening them larger when multipliers apply.
    const QSize minSizeTarget = wasFixedSize ? root->minimumSize().expandedTo(scaledRequiredSize)
                                             : root->minimumSize().expandedTo(baseRequiredSize);
    const QSize minSize = sanitizeMinSize(minSizeTarget);
    root->setMinimumSize(minSize);

    const QSize targetSize = sanitizeMinSize(root->size().expandedTo(scaledRequiredSize));
    if (targetSize != root->size())
    {
        root->resize(targetSize);
    }

    // Re-lock the window to its expanded size when the original widget was
    // fixed-size but the layout does not use SetFixedSize (which would handle
    // it automatically).  This keeps sparse processes non-resizable after the
    // modern shell chrome has been accounted for.
    const bool hasExtraAxisScale = (widthMultiplier > PROCESS_WINDOW_SIZE_MULTIPLIER_DEFAULT) ||
                                   (heightMultiplier > PROCESS_WINDOW_SIZE_MULTIPLIER_DEFAULT);
    const bool isMarkedFixed = root->property("anFixedSizeWindow").toBool();
    const bool shouldRelockFixedSize =
        wasFixedSize && (hasExtraAxisScale || rootLayout->sizeConstraint() != QLayout::SetFixedSize || isMarkedFixed);
    if (shouldRelockFixedSize)
    {
        root->setFixedSize(root->size());
    }

    fitFreeformButtonWidths(root);
}

void ensureFixedWindowFitsContent(QWidget* root, const double widthMultiplier, const double heightMultiplier)
{
    if (!root)
    {
        return;
    }

    const bool wasFixedSize = (root->maximumSize() == root->minimumSize());
    const bool isMarkedFixed = root->property("anFixedSizeWindow").toBool();
    if (!wasFixedSize && !isMarkedFixed)
    {
        return;
    }

    QSize requiredSize;
    const QVariant baselineSizeValue = root->property(PROCESS_WINDOW_SIZE_BASELINE_PROPERTY);
    const QSize baselineSize = baselineSizeValue.toSize();
    const bool hasBaseline = baselineSizeValue.isValid() && baselineSize.isValid();
    const bool hasExtraAxisScale = (widthMultiplier > PROCESS_WINDOW_SIZE_MULTIPLIER_DEFAULT) ||
                                   (heightMultiplier > PROCESS_WINDOW_SIZE_MULTIPLIER_DEFAULT);
    const bool shouldReuseBaseline = hasBaseline && hasExtraAxisScale;
    if (shouldReuseBaseline)
    {
        requiredSize = baselineSize;
    }
    else
    {
        requiredSize = root->minimumSize();
        if (QLayout* rootLayout = root->layout())
        {
            const QSize layoutHint = rootLayout->sizeHint();
            if (layoutHint.isValid())
            {
                requiredSize = requiredSize.expandedTo(layoutHint);
            }
        }

        const QRect childrenBounds = root->childrenRect();
        if (childrenBounds.isValid() && !childrenBounds.isNull())
        {
            const QSize childrenSize = childrenBounds.size();
            requiredSize = requiredSize.expandedTo(childrenSize);
        }

        requiredSize = sanitizeMinSize(requiredSize);
        if (!requiredSize.isValid())
        {
            return;
        }

        if (hasExtraAxisScale)
        {
            root->setProperty(PROCESS_WINDOW_SIZE_BASELINE_PROPERTY, requiredSize);
        }
        else
        {
            root->setProperty(PROCESS_WINDOW_SIZE_BASELINE_PROPERTY, QVariant());
        }
    }

    requiredSize = applySizeMultipliers(requiredSize, widthMultiplier, heightMultiplier);

    root->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    root->setMinimumSize(requiredSize);

    if (root->size() != requiredSize)
    {
        root->resize(requiredSize);
    }

    root->setFixedSize(root->size());
}
} // namespace

TopLevelProcessBaseControl::TopLevelProcessBaseControl(const std::string& processName)
    : QWidget(an::ui::appContext()->getMainWindow(), Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                                                         Qt::WindowCloseButtonHint | Qt::WindowMinimizeButtonHint |
                                                         Qt::WindowMaximizeButtonHint),
      m_tl_process_name(processName)
{
    this->setAttribute(Qt::WA_DeleteOnClose);
    this->setProperty("anProcessShell", true);
    setWindowSizeFitMultipliers(PROCESS_WINDOW_SIZE_MULTIPLIER_DEFAULT, PROCESS_WINDOW_SIZE_MULTIPLIER_DEFAULT);
    an::ui::appContext()->getProcessRegistry()->registerProcessInstance(this);
    refreshTheme();
}

void TopLevelProcessBaseControl::setWindowSizeFitMultipliers(double widthMultiplier, double heightMultiplier)
{
    setProperty(PROCESS_WINDOW_SIZE_MULTIPLIER_X_PROPERTY, sanitizeWindowSizeMultiplier(widthMultiplier));
    setProperty(PROCESS_WINDOW_SIZE_MULTIPLIER_Y_PROPERTY, sanitizeWindowSizeMultiplier(heightMultiplier));
}

void TopLevelProcessBaseControl::setWindowSizeFitMultiplier(double multiplier)
{
    setWindowSizeFitMultipliers(multiplier, multiplier);
}

double TopLevelProcessBaseControl::windowSizeFitMultiplierX() const
{
    const QVariant multiplierValue = property(PROCESS_WINDOW_SIZE_MULTIPLIER_X_PROPERTY);
    if (!multiplierValue.isValid())
    {
        return PROCESS_WINDOW_SIZE_MULTIPLIER_DEFAULT;
    }

    bool conversionOk = false;
    const double multiplier = multiplierValue.toDouble(&conversionOk);
    if (!conversionOk)
    {
        return PROCESS_WINDOW_SIZE_MULTIPLIER_DEFAULT;
    }

    return sanitizeWindowSizeMultiplier(multiplier);
}

double TopLevelProcessBaseControl::windowSizeFitMultiplierY() const
{
    const QVariant multiplierValue = property(PROCESS_WINDOW_SIZE_MULTIPLIER_Y_PROPERTY);
    if (!multiplierValue.isValid())
    {
        return PROCESS_WINDOW_SIZE_MULTIPLIER_DEFAULT;
    }

    bool conversionOk = false;
    const double multiplier = multiplierValue.toDouble(&conversionOk);
    if (!conversionOk)
    {
        return PROCESS_WINDOW_SIZE_MULTIPLIER_DEFAULT;
    }

    return sanitizeWindowSizeMultiplier(multiplier);
}

void TopLevelProcessBaseControl::initializeTopLevelProcess()
{
    int _win_state;
    int _width;
    int _height;
    QString _json_settings;
    if (an::ui::appContext()->getProcessSettings()->select(QString::fromUtf8(m_tl_process_name), _win_state, _width,
                                                           _height, _json_settings))
    {
        if (shouldRememberWindowGeometry())
        {
            if (_width > 0 && _height > 0)
            {
                QSize targetSize(_width, _height);

                QScreen* screen = nullptr;
                if (QWindow* window = windowHandle())
                {
                    screen = window->screen();
                }

                if (!screen)
                {
                    screen = QGuiApplication::primaryScreen();
                }

                if (screen)
                {
                    const QSize maxSize = screen->availableGeometry().size();
                    targetSize.setWidth((std::min)(targetSize.width(), maxSize.width()));
                    targetSize.setHeight((std::min)(targetSize.height(), maxSize.height()));
                }

                resize(targetSize);
            }

            if (_win_state == Qt::WindowMaximized)
            {
                setWindowState(Qt::WindowMaximized);
            }
            else
            {
                setWindowState(Qt::WindowNoState);
            }
        }

        if (!_json_settings.isEmpty())
        {
            deserialize_json_settings(_json_settings.toStdString());
        }
    }
}

std::stop_token TopLevelProcessBaseControl::windowStopToken() const noexcept
{
    return m_WindowStop.get_token();
}

void TopLevelProcessBaseControl::stopWorkInFlight()
{
    m_WindowStop.request_stop();
    onStopWorkInFlight();
}

TopLevelProcessBaseControl::~TopLevelProcessBaseControl()
{
    // What the window still runs ends at its next stop point; nothing here waits for it.
    m_WindowStop.request_stop();
    // A window can be deleted after the application context it registered with has gone - a deferred deletion that
    // lands at shutdown - and then there is no registry left to leave.
    if (an::ui::IAppContext* context = an::ui::appContext())
    {
        if (an::ui::IProcessRegistry* registry = context->getProcessRegistry())
        {
            registry->unregisterProcessInstance(this);
        }
    }
}

void TopLevelProcessBaseControl::setFixedSizeWindow(const bool fixed)
{
    setProperty("anFixedSizeWindow", fixed);
    setWindowFlag(Qt::WindowMaximizeButtonHint, !fixed);
}

void TopLevelProcessBaseControl::setModernShellEnabled(const bool enabled)
{
    setProperty(PROCESS_MODERN_SHELL_ENABLED_PROPERTY, enabled);
    refreshTheme();
}

void TopLevelProcessBaseControl::initializeModernProcessShell()
{
    if (findShellCard(this))
    {
        return;
    }

    if (!an::ui::modernization::ModernizationFeatureFlags::uiTokensEnabled())
    {
        return;
    }

    if (!isModernShellEnabled(this))
    {
        return;
    }

    if (!isVisible())
    {
        return;
    }

    const QString processDescription = property(PROCESS_DESCRIPTION_PROPERTY).toString();
    QLayout* previousLayout = layout();
    const bool hasPreviousLayout = (previousLayout != nullptr);
    const auto topLevelChildren = findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
    // Capture the original minimum size before we add any shell chrome so the
    // content host can be given exactly the same minimum dimensions, ensuring
    // the root layout sizeHint reflects the full content area needed.
    const QSize originalMinimumSize = minimumSize();

    const auto& sp = an::ui::tokens().spacing;
    auto* processShellRootLayout = new QVBoxLayout();
    processShellRootLayout->setContentsMargins(sp.lg, sp.md, sp.lg, sp.md);
    processShellRootLayout->setSpacing(sp.sm);

    auto* processShellHeader = new QWidget(this);
    processShellHeader->setObjectName(QString::fromUtf8(PROCESS_HEADER_OBJECT_NAME));
    auto* headerLayout = new QVBoxLayout(processShellHeader);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(sp.xs);

    const auto& tok = an::ui::tokens();

    auto* processShellHeaderTitle = new QLabel(QString::fromUtf8(m_tl_process_name), processShellHeader);
    processShellHeaderTitle->setObjectName(QString::fromUtf8(PROCESS_HEADER_TITLE_OBJECT_NAME));
    processShellHeaderTitle->setFont(tok.typography.heading1);
    {
        QPalette pal = processShellHeaderTitle->palette();
        pal.setColor(QPalette::WindowText, tok.colors.textPrimary);
        processShellHeaderTitle->setPalette(pal);
    }
    headerLayout->addWidget(processShellHeaderTitle);

    auto* processShellHeaderDescription = new QLabel(processDescription, processShellHeader);
    processShellHeaderDescription->setObjectName(QString::fromUtf8(PROCESS_HEADER_DESCRIPTION_OBJECT_NAME));
    processShellHeaderDescription->setWordWrap(true);
    processShellHeaderDescription->setVisible(!processDescription.isEmpty());
    processShellHeaderDescription->setFont(tok.typography.body);
    {
        QPalette pal = processShellHeaderDescription->palette();
        pal.setColor(QPalette::WindowText, tok.colors.textSecondary);
        processShellHeaderDescription->setPalette(pal);
    }
    headerLayout->addWidget(processShellHeaderDescription);

    auto* processShellInfoBanner = new QWidget(this);
    processShellInfoBanner->setObjectName(QString::fromUtf8(PROCESS_BANNER_OBJECT_NAME));
    auto* bannerLayout = new QVBoxLayout(processShellInfoBanner);
    bannerLayout->setContentsMargins(sp.sm, sp.xs, sp.sm, sp.xs);
    bannerLayout->setSpacing(sp.xs);
    auto* processShellInfoBannerMessage = new QLabel(processShellInfoBanner);
    processShellInfoBannerMessage->setObjectName(QString::fromUtf8(PROCESS_BANNER_MESSAGE_OBJECT_NAME));
    processShellInfoBannerMessage->setWordWrap(true);
    processShellInfoBannerMessage->setFont(tok.typography.caption);
    bannerLayout->addWidget(processShellInfoBannerMessage);
    auto* processShellInfoBannerProgress = new QProgressBar(processShellInfoBanner);
    processShellInfoBannerProgress->setObjectName(QString::fromUtf8(PROCESS_BANNER_PROGRESS_OBJECT_NAME));
    processShellInfoBannerProgress->setRange(0, 1000);
    processShellInfoBannerProgress->setTextVisible(false);
    processShellInfoBannerProgress->setFixedHeight(sp.sm);
    processShellInfoBannerProgress->setAttribute(Qt::WA_TransparentForMouseEvents);
    processShellInfoBannerProgress->setVisible(false);
    bannerLayout->addWidget(processShellInfoBannerProgress);
    processShellInfoBanner->setVisible(false);

    auto* processShellCard = new QFrame(this);
    processShellCard->setObjectName(QString::fromUtf8(PROCESS_CARD_OBJECT_NAME));
    processShellCard->setFrameShape(QFrame::NoFrame);

    auto* cardContentLayout = new QVBoxLayout(processShellCard);
    if (hasPreviousLayout)
    {
        cardContentLayout->setContentsMargins(sp.sm, sp.sm, sp.sm, sp.sm);
    }
    else
    {
        // Geometry-based process UIs depend on the full client area.
        // Avoid shrinking them with card padding.
        cardContentLayout->setContentsMargins(0, 0, 0, 0);
    }
    cardContentLayout->setSpacing(sp.sm);

    QWidget* contentHost = new QWidget(processShellCard);
    contentHost->setObjectName(QString::fromUtf8(PROCESS_CONTENT_OBJECT_NAME));
    contentHost->setAttribute(Qt::WA_StyledBackground, false);
    cardContentLayout->addWidget(contentHost, 1);

    processShellRootLayout->addWidget(processShellHeader);
    processShellRootLayout->addWidget(processShellInfoBanner);
    processShellRootLayout->addWidget(processShellCard, 1);

    if (previousLayout)
    {
        // Preserve the original layout direction so that processes with a
        // QHBoxLayout (controls on the left, preview on the right) keep
        // their horizontal arrangement instead of being flattened into a
        // vertical stack.
        QBoxLayout* contentLayout = nullptr;
        if (qobject_cast<QHBoxLayout*>(previousLayout))
        {
            contentLayout = new QHBoxLayout(contentHost);
        }
        else
        {
            contentLayout = new QVBoxLayout(contentHost);
        }
        contentLayout->setContentsMargins(0, 0, 0, 0);
        contentLayout->setSpacing(previousLayout->spacing());

        processShellRootLayout->setSizeConstraint(previousLayout->sizeConstraint());

        moveLayoutItems(previousLayout, contentLayout);
        delete previousLayout;

        onContentLayoutReplaced(contentLayout);

        // Hoist any QDialogButtonBox out of the card so the action buttons
        // sit below the card chrome rather than inside it.
        //
        // Most horizontal-layout processes keep the action buttons in their
        // internal controls column.  Those that need the bar at window-bottom
        // can opt in by setting the "anForceBottomActionBar" property.
        const bool forceBottomActionBar = property("anForceBottomActionBar").toBool();
        if (!qobject_cast<QHBoxLayout*>(contentLayout) || forceBottomActionBar)
        {
            auto* actionBar = extractProcessActionBar(contentLayout);
            if (!actionBar)
            {
                actionBar = extractProcessActionBar(contentHost);
            }

            if (actionBar)
            {
                actionBar->setParent(this);
                processShellRootLayout->addWidget(actionBar);
            }
            else
            {
                auto* dialogBtnBox = extractDialogButtonBox(contentLayout);
                if (!dialogBtnBox)
                {
                    dialogBtnBox = extractDialogButtonBox(contentHost);
                }

                if (dialogBtnBox)
                {
                    dialogBtnBox->setParent(this);
                    processShellRootLayout->addWidget(dialogBtnBox);
                }
            }
        }
    }
    else
    {
        QVector<QWidget*> movableChildren;
        movableChildren.reserve(topLevelChildren.size());
        for (QWidget* child : topLevelChildren)
        {
            if (!child || child == processShellHeader || child == processShellInfoBanner || child == processShellCard)
            {
                continue;
            }
            movableChildren.push_back(child);
        }

        // A geometry-based form commonly has a single root widget (with its
        // own inner layout) placed by geometry. Promote that root widget into
        // a real layout so it stretches with the shell.
        if (movableChildren.size() == 1)
        {
            QWidget* formRootWidget = movableChildren.front();
            formRootWidget->setParent(contentHost);

            auto* contentLayout = new QVBoxLayout(contentHost);
            contentLayout->setContentsMargins(0, 0, 0, 0);
            contentLayout->setSpacing(0);
            contentLayout->addWidget(formRootWidget, 1);

            onContentLayoutReplaced(contentLayout);

            if (auto* dialogBtnBox = extractDialogButtonBox(contentLayout))
            {
                dialogBtnBox->setParent(this);
                processShellRootLayout->addWidget(dialogBtnBox);
            }

            if (originalMinimumSize.isValid())
            {
                contentHost->setMinimumSize(originalMinimumSize);
            }
        }
        else
        {
            QRect usedArea;
            for (QWidget* child : movableChildren)
            {
                const QRect originalGeometry = child->geometry();
                child->setParent(contentHost);
                child->setGeometry(originalGeometry);
                usedArea = usedArea.united(originalGeometry);
            }

            // Set a minimum size on the shell content host so the layout system
            // can compute the correct window height for geometry-based processes
            // whose children have no QLayout (and therefore no sizeHint).
            // Use the original widget minimum size (not usedArea) because the
            // process author already accounted for all required internal spacing.
            if (originalMinimumSize.isValid())
            {
                contentHost->setMinimumSize(originalMinimumSize);
            }
            else if (usedArea.isValid())
            {
                contentHost->setMinimumSize(usedArea.right() + 1, usedArea.bottom() + 1);
            }
        }
    }

    setLayout(processShellRootLayout);

    if (!hasPreviousLayout)
    {
        ensureModernShellSize(this, windowSizeFitMultiplierX(), windowSizeFitMultiplierY());

        // Apply a deferred pass after Qt finishes layout/polish to avoid clipping
        // for geometry-based forms with fixed-size UiBuilder windows.
        QTimer::singleShot(
            0, this, [this]() { ensureModernShellSize(this, windowSizeFitMultiplierX(), windowSizeFitMultiplierY()); });
    }
}

void TopLevelProcessBaseControl::refreshTheme()
{
    initializeModernProcessShell();

    applyProcessButtonRoles(this);
    applyProcessButtonIcons(this);
    applyFlatGroupBoxVariantStyles(this);

    // Re-apply typography tokens whenever the theme changes so the process shell
    // header and banner stay consistent across dark/light switches.
    const auto& tok = an::ui::tokens();

    if (auto* titleLabel = findChild<QLabel*>(QString::fromUtf8(PROCESS_HEADER_TITLE_OBJECT_NAME)))
    {
        titleLabel->setFont(tok.typography.heading1);
        QPalette pal = titleLabel->palette();
        pal.setColor(QPalette::WindowText, tok.colors.textPrimary);
        titleLabel->setPalette(pal);
    }

    if (auto* descLabel = findChild<QLabel*>(QString::fromUtf8(PROCESS_HEADER_DESCRIPTION_OBJECT_NAME)))
    {
        descLabel->setFont(tok.typography.body);
        QPalette pal = descLabel->palette();
        pal.setColor(QPalette::WindowText, tok.colors.textSecondary);
        descLabel->setPalette(pal);
    }

    if (auto* bannerLabel = findChild<QLabel*>(QString::fromUtf8(PROCESS_BANNER_MESSAGE_OBJECT_NAME)))
    {
        bannerLabel->setFont(tok.typography.caption);
    }

    if (auto* bannerProgress = findChild<QProgressBar*>(QString::fromUtf8(PROCESS_BANNER_PROGRESS_OBJECT_NAME)))
    {
        bannerProgress->setFixedHeight(tok.spacing.sm);
    }

    const auto childWidgets = findChildren<QWidget*>();
    for (auto* childWidget : childWidgets)
    {
        auto* viewControl = dynamic_cast<ImageViewControl*>(childWidget);
        if (viewControl)
        {
            viewControl->refreshTheme();
        }
    }

    // Force repaint to pick up new proxy style colors
    update();
}

void TopLevelProcessBaseControl::setProcessDescription(const QString& description)
{
    const QString trimmedDescription = description.trimmed();
    setProperty(PROCESS_DESCRIPTION_PROPERTY, trimmedDescription);

    if (QLabel* headerDescription = findHeaderDescriptionLabel(this))
    {
        headerDescription->setText(trimmedDescription);
        headerDescription->setVisible(!trimmedDescription.isEmpty());
    }
}

void TopLevelProcessBaseControl::showProcessMessage(const QString& title, const QString& message,
                                                    const ProcessMessageTone tone,
                                                    const std::optional<double> progressPercent)
{
    initializeModernProcessShell();

    QWidget* infoBanner = findInfoBanner(this);
    QLabel* infoBannerMessage = findInfoBannerMessageLabel(this);
    QProgressBar* infoBannerProgress = findInfoBannerProgressBar(this);
    if (!infoBanner || !infoBannerMessage)
    {
        return;
    }

    const QString trimmedTitle = title.trimmed();
    const QString trimmedMessage = message.trimmed();
    const QString mergedMessage =
        trimmedTitle.isEmpty()
            ? trimmedMessage
            : (trimmedMessage.isEmpty() ? trimmedTitle : QStringLiteral("%1: %2").arg(trimmedTitle, trimmedMessage));

    if (mergedMessage.isEmpty())
    {
        clearProcessMessage();
        return;
    }

    infoBanner->setProperty("anProcessMessageTone", tonePropertyValue(tone));
    if (auto* style = infoBanner->style())
    {
        style->unpolish(infoBanner);
        style->polish(infoBanner);
    }
    infoBannerMessage->setText(mergedMessage);
    if (infoBannerProgress)
    {
        if (progressPercent.has_value() && std::isfinite(*progressPercent))
        {
            const int progressValue = static_cast<int>(std::round((std::clamp)(*progressPercent, 0.0, 100.0) * 10.0));
            infoBannerProgress->setRange(0, 1000);
            infoBannerProgress->setValue(progressValue);
            infoBannerProgress->setVisible(true);
        }
        else
        {
            infoBannerProgress->setVisible(false);
            infoBannerProgress->reset();
        }
    }
    infoBanner->setVisible(true);
}

void TopLevelProcessBaseControl::clearProcessMessage()
{
    QWidget* infoBanner = findInfoBanner(this);
    QLabel* infoBannerMessage = findInfoBannerMessageLabel(this);
    if (!infoBanner || !infoBannerMessage)
    {
        return;
    }

    infoBannerMessage->clear();
    if (QProgressBar* infoBannerProgress = findInfoBannerProgressBar(this))
    {
        infoBannerProgress->setVisible(false);
        infoBannerProgress->reset();
    }
    infoBanner->setVisible(false);
}

QString TopLevelProcessBaseControl::processMessage() const
{
    const auto* infoBanner = findChild<QWidget*>(QString::fromUtf8(PROCESS_BANNER_OBJECT_NAME));
    const auto* infoBannerMessage = findChild<QLabel*>(QString::fromUtf8(PROCESS_BANNER_MESSAGE_OBJECT_NAME));
    if (infoBanner == nullptr || infoBannerMessage == nullptr || infoBanner->isHidden())
    {
        return {};
    }
    return infoBannerMessage->text();
}

void TopLevelProcessBaseControl::setHelpPath(const QString& helpPath)
{
    m_HelpPath = helpPath;
}

QString TopLevelProcessBaseControl::helpPath() const
{
    return m_HelpPath;
}

QString an::ui::helpPathFor(const QWidget* widget)
{
    for (; widget != nullptr; widget = widget->parentWidget())
    {
        // A process window declares no meta-object of its own, so it is known by its type.
        const auto* window = dynamic_cast<const TopLevelProcessBaseControl*>(widget);
        if (window != nullptr && !window->helpPath().isEmpty())
        {
            return window->helpPath();
        }
    }
    return {};
}

void TopLevelProcessBaseControl::keyPressEvent(QKeyEvent* evt)
{
    if (evt->key() == Qt::Key_Escape)
    {
        close();
        return;
    }
    QWidget::keyPressEvent(evt);
}

void TopLevelProcessBaseControl::closeEvent(QCloseEvent* evt)
{
    // Everything the window started is stopped and lands on a window that is gone; nothing is waited for.
    m_WindowStop.request_stop();
    const QString json_settings = QString::fromStdString(serialize_json_settings());

    if (shouldRememberWindowGeometry())
    {
        QSize normalSize = normalGeometry().size();
        an::ui::appContext()->getProcessSettings()->upsert(QString::fromUtf8(m_tl_process_name), this->windowState(),
                                                           normalSize.width(), normalSize.height(), json_settings);
    }
    else
    {
        an::ui::appContext()->getProcessSettings()->upsert(QString::fromUtf8(m_tl_process_name), Qt::WindowNoState, -1,
                                                           -1, json_settings);
    }
}

void TopLevelProcessBaseControl::showEvent(QShowEvent* evt)
{
    refreshTheme();
    QWidget::showEvent(evt);

    const double widthMultiplier = windowSizeFitMultiplierX();
    const double heightMultiplier = windowSizeFitMultiplierY();
    ensureFixedWindowFitsContent(this, widthMultiplier, heightMultiplier);
    ensureModernShellSize(this, widthMultiplier, heightMultiplier);

    const auto applyDeferredSizing = [this]() {
        const double deferredWidthMultiplier = windowSizeFitMultiplierX();
        const double deferredHeightMultiplier = windowSizeFitMultiplierY();
        ensureFixedWindowFitsContent(this, deferredWidthMultiplier, deferredHeightMultiplier);
        ensureModernShellSize(this, deferredWidthMultiplier, deferredHeightMultiplier);
    };

    // Some fixed-size layouts re-assert their own size during the first show
    // cycle. Apply immediate and slightly delayed deferred passes so explicit
    // multipliers survive late layout/polish updates.
    QTimer::singleShot(0, this, applyDeferredSizing);
    QTimer::singleShot(30, this, applyDeferredSizing);
}

void TopLevelProcessBaseControl::paintEvent(QPaintEvent* evt)
{
    // Fill with the palette window colour to prevent black (uninitialized)
    // buffer regions on Wayland during compositor-driven resize transitions.
    // WA_StyledBackground is NOT set so Qt does not also paint the background
    // (single paint path – no double-painting / tearing).  Using fillRect
    // instead of PE_Widget avoids overhead on every frame, which is critical
    // during rapid resize.
    QPainter painter(this);
    painter.fillRect(evt->rect(), palette().color(QPalette::Window));
}

void TopLevelProcessBaseControl::resizeEvent(QResizeEvent* evt)
{
    QWidget::resizeEvent(evt);
    // Ensure the full widget is repainted after every resize so the
    // backing-store covers newly-exposed regions on Wayland.
    update();
}

namespace an::ui
{
void destroyProcessWindowsAtExit(const std::vector<QPointer<TopLevelProcessBaseControl>>& windows)
{
    for (const QPointer<TopLevelProcessBaseControl>& window : windows)
    {
        if (window)
        {
            window->stopWorkInFlight();
        }
    }
    QThreadPool::globalInstance()->waitForDone();
    for (const QPointer<TopLevelProcessBaseControl>& window : windows)
    {
        delete window.data();
    }
    QThreadPool::globalInstance()->waitForDone();
}
} // namespace an::ui
