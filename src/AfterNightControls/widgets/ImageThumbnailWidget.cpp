// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ImageThumbnailWidget.h"
#include "common.h"
#include "app/UiStyleTokens.h"
#include "theme/AnimationUtils.h"
#include "theme/AnDesignTokens.h"
#include "theme/PainterGuard.h"

#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QEasingCurve>
#include <QGraphicsOpacityEffect>
#include <QLocale>

#include "app/DesktopUtils.h"
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QPropertyAnimation>
#include <QStackedLayout>
#include <QUrl>

#include <algorithm>

namespace
{
class ThumbnailChipLabel final : public QLabel
{
public:
    explicit ThumbnailChipLabel(QWidget* parent = nullptr) : QLabel(parent)
    {
        setAlignment(Qt::AlignCenter);
        setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    }

    void setChipStyle(const QColor& backgroundColor, const QColor& textColor, const int cornerRadius,
                      const QMargins& padding)
    {
        m_BackgroundColor = backgroundColor;
        m_TextColor = textColor;
        m_CornerRadius = (std::max)(0, cornerRadius);
        m_Padding = padding;
        updateGeometry();
        update();
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        const QSize baseSize = QLabel::sizeHint();
        return QSize(baseSize.width() + m_Padding.left() + m_Padding.right(),
                     baseSize.height() + m_Padding.top() + m_Padding.bottom());
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF chipRect = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_BackgroundColor);
        painter.drawRoundedRect(chipRect, m_CornerRadius, m_CornerRadius);

        painter.setPen(m_TextColor);
        const QRect textRect = rect().marginsRemoved(m_Padding);
        const QString renderedText = fontMetrics().elidedText(text(), Qt::ElideRight, textRect.width());
        painter.drawText(textRect, alignment(), renderedText);
    }

private:
    QColor m_BackgroundColor = Qt::transparent;
    QColor m_TextColor = Qt::black;
    int m_CornerRadius = 0;
    QMargins m_Padding = QMargins(0, 0, 0, 0);
};

constexpr qreal CARD_BORDER_WIDTH = 1.0;
constexpr int CARD_SHADOW_BLUR = 3;
constexpr int CARD_SHADOW_OFFSET = 2;
constexpr int FADE_IN_DURATION_MS = 180;

QColor blendRgb(const QColor& base, const QColor& tint, const double ratio)
{
    const double t = std::clamp(ratio, 0.0, 1.0);
    const auto lerp = [t](const int a, const int b) { return static_cast<int>(a + (b - a) * t); };
    return QColor(lerp(base.red(), tint.red()), lerp(base.green(), tint.green()), lerp(base.blue(), tint.blue()),
                  base.alpha());
}

QColor applyCardSurfaceTint(const QColor& base, const QColor& tint, const bool darkMode)
{
    if (!tint.isValid())
    {
        return base;
    }
    QColor result = blendRgb(base, tint, darkMode ? 0.56 : 0.44);
    if (darkMode)
    {
        result = blendRgb(result, an::ui::tokens().colors.bgPrimary, 0.22);
    }
    return result;
}

QString normalizeLocalFilePath(const QString& inputPath)
{
    const QString trimmedPath = inputPath.trimmed();
    if (trimmedPath.isEmpty())
    {
        return trimmedPath;
    }

    auto cleanLocalPath = [](const QString& localPath) -> QString {
        return localPath.isEmpty() ? localPath : QDir::cleanPath(localPath);
    };

    const QUrl parsedUrl(trimmedPath);
    if (parsedUrl.isValid() && parsedUrl.isLocalFile())
    {
        return cleanLocalPath(parsedUrl.toLocalFile());
    }

    if (trimmedPath.startsWith("file://"))
    {
        const QUrl userInputUrl = QUrl::fromUserInput(trimmedPath);
        if (userInputUrl.isValid() && userInputUrl.isLocalFile())
        {
            return cleanLocalPath(userInputUrl.toLocalFile());
        }
    }

    return cleanLocalPath(trimmedPath);
}

void showInFileBrowser(QWidget* parent, const QString& inputPath)
{
    const QString normalizedPath = QDir::cleanPath(normalizeLocalFilePath(inputPath));
    const QFileInfo fileInfo(normalizedPath);
    if (!fileInfo.exists())
    {
        QMessageBox::warning(parent, QObject::tr("File Not Found"),
                             QObject::tr("The file does not exist on disk:\n%1").arg(normalizedPath));
        return;
    }

    an::ui::revealInFileManager(fileInfo.absoluteFilePath());
}

void repolishWidget(QWidget* widget)
{
    if (!widget)
    {
        return;
    }

    if (QStyle* currentStyle = widget->style())
    {
        currentStyle->unpolish(widget);
        currentStyle->polish(widget);
    }
}

} // namespace

ImageThumbnailWidget::ImageThumbnailWidget(ThumbnailPathResolver thumbnailPathResolver,
                                           OpenFileCallback openFileCallback, RemoveRecentCallback removeRecentCallback,
                                           bool darkMode, QWidget* parent, TogglePinnedCallback togglePinnedCallback,
                                           bool pinned)
    : QWidget(parent), m_ThumbnailPathResolver(std::move(thumbnailPathResolver)),
      m_OpenFileCallback(std::move(openFileCallback)), m_RemoveRecentCallback(std::move(removeRecentCallback)),
      m_TogglePinnedCallback(std::move(togglePinnedCallback)), m_IsPinned(pinned)
{
    this->setAttribute(Qt::WA_DeleteOnClose);
    setObjectName("dashboardThumbnailCard");
    setProperty("dashboardHoverActive", false);

    m_PreviewFrame = new QFrame(this);
    m_PreviewFrame->setObjectName("dashboardThumbnailPreview");
    m_PreviewFrame->setProperty("dashboardHoverActive", false);
    m_PreviewFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* previewLayout = new QStackedLayout(m_PreviewFrame);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->setStackingMode(QStackedLayout::StackAll);

    m_MainImage = new QLabel(m_PreviewFrame);
    m_MainImage->setObjectName("dashboardThumbnailImage");
    m_MainImage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_OverlayFrame = new QFrame(m_PreviewFrame);
    m_OverlayFrame->setObjectName("dashboardThumbnailOverlay");
    m_OverlayFrame->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* overlayLayout = new QVBoxLayout(m_OverlayFrame);
    overlayLayout->setContentsMargins(8, 8, 8, 8);
    overlayLayout->setSpacing(0);
    overlayLayout->addStretch();

    auto* chipsLayout = new QHBoxLayout();
    chipsLayout->setContentsMargins(0, 0, 0, 0);
    chipsLayout->setSpacing(6);

    m_DimensionsChipLabel = new ThumbnailChipLabel(m_OverlayFrame);
    m_DimensionsChipLabel->setObjectName("dashboardThumbnailChip");
    m_DimensionsChipLabel->setProperty("dashboardChipRole", "dimensions");
    chipsLayout->addWidget(m_DimensionsChipLabel);

    m_TypeChipLabel = new ThumbnailChipLabel(m_OverlayFrame);
    m_TypeChipLabel->setObjectName("dashboardThumbnailChip");
    m_TypeChipLabel->setProperty("dashboardChipRole", "type");
    chipsLayout->addWidget(m_TypeChipLabel);

    chipsLayout->addStretch();
    overlayLayout->addLayout(chipsLayout);

    previewLayout->addWidget(m_MainImage);
    previewLayout->addWidget(m_OverlayFrame);
    previewLayout->setCurrentWidget(m_OverlayFrame);

    m_NameLabel = new QLabel(this);
    m_NameLabel->setObjectName("dashboardThumbnailName");
    m_NameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_NameLabel->setTextFormat(Qt::PlainText);
    m_MetadataLabel = new QLabel(this);
    m_MetadataLabel->setObjectName("dashboardThumbnailMetadata");
    m_MetadataLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_MetadataLabel->setTextFormat(Qt::PlainText);

    // Use design-token fonts for consistent typography (F)
    const auto& tokenSet = an::ui::tokens();
    QFont nameFont = tokenSet.typography.body;
    nameFont.setWeight(QFont::DemiBold);
    m_NameLabel->setFont(nameFont);
    m_MetadataLabel->setFont(tokenSet.typography.caption);

    m_Layout = new QVBoxLayout(this);
    setLayout(m_Layout);
    m_Layout->addWidget(m_PreviewFrame);
    m_Layout->addWidget(m_NameLabel);
    m_Layout->addWidget(m_MetadataLabel);

    m_Layout->setStretch(0, 1);
    m_Layout->setStretch(1, 0);
    m_Layout->setStretch(2, 0);
    m_Layout->setSpacing(4);
    // Extra padding accounts for the painted drop shadow (A)
    m_Layout->setContentsMargins(8 + CARD_SHADOW_BLUR, 8 + CARD_SHADOW_BLUR, 8 + CARD_SHADOW_BLUR,
                                 8 + CARD_SHADOW_BLUR + CARD_SHADOW_OFFSET);

    // Fade-in animation for thumbnail load (D)
    m_OpacityEffect = new QGraphicsOpacityEffect(this);
    m_OpacityEffect->setOpacity(1.0);
    m_OpacityEffect->setEnabled(false);
    setGraphicsEffect(m_OpacityEffect);

    m_FadeInAnimation = new QPropertyAnimation(m_OpacityEffect, "opacity", this);
    m_FadeInAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_FadeInAnimation->setDuration(an::ui::AnimationUtils::duration(FADE_IN_DURATION_MS));

    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    m_DarkMode = darkMode;
    refreshColors(darkMode);
    setProperty("dashboardPinned", m_IsPinned);
}

bool ImageThumbnailWidget::loadImage(const QString& fileName)
{
    const QString rawFileName = fileName;
    const QString qFileName = normalizeLocalFilePath(rawFileName);

    if (!m_ThumbnailPathResolver)
    {
        return false;
    }

    auto thumnailFilePath = m_ThumbnailPathResolver(qFileName);
    QPixmap thumbnailPixmap(thumnailFilePath);
    if (thumbnailPixmap.isNull() && rawFileName != qFileName)
    {
        thumnailFilePath = m_ThumbnailPathResolver(rawFileName);
        thumbnailPixmap = QPixmap(thumnailFilePath);
    }

    if (thumbnailPixmap.isNull())
    {
        return false;
    }

    QSize displaySize;
    displaySize.setWidth(size().width() - 30);
    displaySize.setHeight(size().height() - 60);

    QPixmap scaledPixmap = thumbnailPixmap.scaled(displaySize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (scaledPixmap.isNull())
    {
        return false;
    }

    return applyLoadedThumbnail(qFileName, scaledPixmap, thumbnailPixmap.size());
}

bool ImageThumbnailWidget::loadImageFromData(const QString& fileName, const QPixmap& displayPixmap,
                                             const QSize& sourceSize)
{
    if (displayPixmap.isNull())
    {
        return false;
    }

    const QString normalizedFileName = normalizeLocalFilePath(fileName);
    const QSize effectiveSourceSize = sourceSize.isValid() ? sourceSize : displayPixmap.size();
    return applyLoadedThumbnail(normalizedFileName, displayPixmap, effectiveSourceSize);
}

bool ImageThumbnailWidget::applyLoadedThumbnail(const QString& fileName, const QPixmap& displayPixmap,
                                                const QSize& sourceSize)
{
    if (displayPixmap.isNull())
    {
        return false;
    }

    m_Pixmap = displayPixmap;
    m_OriginalFilePath = fileName;
    setProperty("recentFilePath", m_OriginalFilePath);
    setProperty("dashboardPinned", m_IsPinned);

    updateDisplayedPixmap();

    const QFileInfo fileInfo(fileName);
    const int sourceWidth = sourceSize.isValid() ? sourceSize.width() : m_Pixmap.width();
    const int sourceHeight = sourceSize.isValid() ? sourceSize.height() : m_Pixmap.height();
    QString suffix = fileInfo.suffix().toUpper();
    if (suffix.isEmpty())
    {
        suffix = "FILE";
    }
    else if (suffix == QStringLiteral("FIT"))
    {
        suffix = QStringLiteral("FITS");
    }

    m_NameLabel->setText(fileInfo.fileName());
    m_NameLabel->setToolTip(fileInfo.absoluteFilePath());
    // Elide long filenames with "..." for clean truncation (C)
    m_NameLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    const QFontMetrics nameMetrics(m_NameLabel->font());
    const int availableWidth =
        (std::max)(60, width() - m_Layout->contentsMargins().left() - m_Layout->contentsMargins().right());
    m_NameLabel->setText(nameMetrics.elidedText(fileInfo.fileName(), Qt::ElideMiddle, availableWidth));
    if (m_DimensionsChipLabel)
    {
        m_DimensionsChipLabel->setText(QString("%1x%2").arg(sourceWidth).arg(sourceHeight));
    }
    if (m_TypeChipLabel)
    {
        m_TypeChipLabel->setText(suffix);
    }

    // Default metadata: containing folder name.  Callers may override this
    // with richer astro metadata via setMetadataText() after loading.
    const QString folderName = fileInfo.absoluteDir().dirName();
    m_MetadataLabel->setText(folderName.isEmpty() ? QDir::toNativeSeparators(fileInfo.absolutePath()) : folderName);
    m_MetadataLabel->setToolTip(fileInfo.absoluteFilePath());

    // Build rich tooltip for the entire thumbnail card
    const QString nativePath = QDir::toNativeSeparators(fileInfo.absoluteFilePath());
    const QString fileSizeStr = fileInfo.exists() ? QLocale().formattedDataSize(fileInfo.size()) : tr("N/A");
    const QString modifiedStr =
        fileInfo.exists() ? QLocale().toString(fileInfo.lastModified(), QLocale::ShortFormat) : tr("N/A");

    const QString tooltipHtml =
        QStringLiteral("<table style='white-space:nowrap; max-width:420px;'>"
                       "<tr><td colspan='2' style='padding-bottom:4px; white-space:normal; "
                       "word-wrap:break-word;'><b>%1</b></td></tr>"
                       "<tr><td style='color:gray;padding-right:8px;'>Path:</td>"
                       "<td style='white-space:normal; word-wrap:break-word;'>%2</td></tr>"
                       "<tr><td style='color:gray;padding-right:8px;'>Type:</td><td>%3</td></tr>"
                       "<tr><td style='color:gray;padding-right:8px;'>Dimensions:</td><td>%4 \u00d7 %5 px</td></tr>"
                       "<tr><td style='color:gray;padding-right:8px;'>File size:</td><td>%6</td></tr>"
                       "<tr><td style='color:gray;padding-right:8px;'>Modified:</td><td>%7</td></tr>"
                       "</table>")
            .arg(fileInfo.fileName().toHtmlEscaped(), nativePath.toHtmlEscaped(), suffix, QString::number(sourceWidth),
                 QString::number(sourceHeight), fileSizeStr.toHtmlEscaped(), modifiedStr.toHtmlEscaped());
    setToolTip(tooltipHtml);

    // Fade-in animation when a thumbnail is first loaded (D)
    if (m_OpacityEffect && m_FadeInAnimation && an::ui::AnimationUtils::animationsEnabled())
    {
        m_OpacityEffect->setEnabled(true);
        m_OpacityEffect->setOpacity(0.0);
        m_FadeInAnimation->stop();
        m_FadeInAnimation->setDuration(an::ui::AnimationUtils::duration(FADE_IN_DURATION_MS));
        m_FadeInAnimation->setStartValue(0.0);
        m_FadeInAnimation->setEndValue(1.0);
        connect(
            m_FadeInAnimation, &QPropertyAnimation::finished, this,
            [this]() {
                if (m_OpacityEffect)
                {
                    m_OpacityEffect->setOpacity(1.0);
                    m_OpacityEffect->setEnabled(false);
                }
            },
            Qt::SingleShotConnection);
        m_FadeInAnimation->start();
    }

    return true;
}

void ImageThumbnailWidget::updateDisplayedPixmap()
{
    if (!m_MainImage)
    {
        return;
    }

    if (m_Pixmap.isNull())
    {
        m_MainImage->clear();
        return;
    }

    QSize targetSize = m_MainImage->size();
    if (m_PreviewFrame)
    {
        targetSize = m_PreviewFrame->size();
    }

    targetSize.setWidth((std::max)(24, targetSize.width() - 2));
    targetSize.setHeight((std::max)(24, targetSize.height() - 2));

    const QPixmap fittedPixmap = m_Pixmap.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_MainImage->setPixmap(fittedPixmap);
    m_MainImage->setScaledContents(false);
    m_MainImage->setAlignment(Qt::AlignCenter);
}

void ImageThumbnailWidget::setMetadataText(const QString& text)
{
    if (m_MetadataLabel && !text.isEmpty())
    {
        m_MetadataLabel->setText(text);
    }
}

void ImageThumbnailWidget::setColors(const QColor& background, const QColor& backgroundHighlight)
{
    m_BackgroundColor = background;
    m_BackgroundHighlightColor = backgroundHighlight;

    // Derive border colors from background colors with increased opacity and slight lightening.
    m_BorderColor = QColor((std::min)(background.red() + 30, 255), (std::min)(background.green() + 30, 255),
                           (std::min)(background.blue() + 40, 255), (std::min)(background.alpha() + 20, 255));
    m_BorderHighlightColor =
        QColor((std::min)(backgroundHighlight.red() + 40, 255), (std::min)(backgroundHighlight.green() + 40, 255),
               (std::min)(backgroundHighlight.blue() + 50, 255), (std::min)(backgroundHighlight.alpha() + 10, 255));

    refreshBackground();
}

void ImageThumbnailWidget::togglePinned()
{
    if (!m_TogglePinnedCallback)
    {
        return;
    }
    const bool newPinnedState = !m_IsPinned;
    setPinned(newPinnedState);
    m_TogglePinnedCallback(normalizeLocalFilePath(m_OriginalFilePath), newPinnedState);
}

void ImageThumbnailWidget::setPinned(bool pinned)
{
    if (m_IsPinned == pinned)
    {
        return;
    }

    m_IsPinned = pinned;
    setProperty("dashboardPinned", m_IsPinned);
    repolishWidget(this);
    update();
}

bool ImageThumbnailWidget::isPinned() const
{
    return m_IsPinned;
}

void ImageThumbnailWidget::refreshColors(bool darkMode)
{
    m_DarkMode = darkMode;

    const auto themeVariant = darkMode ? an::ui::styling::ThemeVariant::Dark : an::ui::styling::ThemeVariant::Light;
    const auto semantic = [themeVariant](const an::ui::styling::SemanticColorRole role) {
        return an::ui::styling::semanticColor(themeVariant, role);
    };

    const auto& colors = darkMode ? an::ui::resolvedDarkTokens().colors : an::ui::resolvedLightTokens().colors;
    m_BackgroundColor = applyCardSurfaceTint(colors.bgCardPrimary, colors.cardSurfaceTint, darkMode);
    m_BackgroundHighlightColor = applyCardSurfaceTint(colors.bgHover, colors.cardSurfaceTint, darkMode);
    m_BorderColor = colors.borderSubtle;
    m_BorderHighlightColor = colors.borderPrimary;
    m_ShadowColor = colors.shadowOverlay;
    m_ShadowColor.setAlpha(darkMode ? 40 : 18);

    // Token-driven fonts (F)
    const auto& tokenSet = an::ui::tokens();
    if (m_NameLabel)
    {
        QFont nameFont = tokenSet.typography.heading3;
        m_NameLabel->setFont(nameFont);
        QPalette pal = m_NameLabel->palette();
        pal.setColor(QPalette::WindowText, semantic(an::ui::styling::SemanticColorRole::TextPrimary));
        m_NameLabel->setPalette(pal);
    }

    if (m_MetadataLabel)
    {
        m_MetadataLabel->setFont(tokenSet.typography.caption);
        QPalette pal = m_MetadataLabel->palette();
        pal.setColor(QPalette::WindowText, semantic(an::ui::styling::SemanticColorRole::TextSecondary));
        m_MetadataLabel->setPalette(pal);
    }

    // Pill-shaped chip badges with rounded corners (B)
    const QColor chipBackground = [darkMode, &semantic]() {
        QColor color = semantic(an::ui::styling::SemanticColorRole::SurfaceWindow);
        color.setAlpha(darkMode ? 170 : 190);
        return color;
    }();
    const QColor chipTextColor = semantic(an::ui::styling::SemanticColorRole::TextSecondary);
    const int chipRadius = tokenSet.radius.sm;
    auto styleChip = [chipBackground, chipTextColor, chipRadius, &tokenSet](QLabel* chip) {
        if (!chip)
        {
            return;
        }

        chip->setFont(tokenSet.typography.caption);
        if (auto* chipLabel = dynamic_cast<ThumbnailChipLabel*>(chip))
        {
            chipLabel->setChipStyle(chipBackground, chipTextColor, chipRadius, QMargins(6, 2, 6, 2));
        }
    };
    styleChip(m_DimensionsChipLabel);
    styleChip(m_TypeChipLabel);

    refreshBackground();
}

ImageThumbnailWidget::~ImageThumbnailWidget() {}

void ImageThumbnailWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QRectF bounds = QRectF(rect());

    // Paint drop shadow for elevation, matching AnCardWidget (A)
    const auto& t = an::ui::tokens();
    const qreal cardRadius = t.radius.lg;

    QColor fallbackShadow = t.colors.shadowOverlay;
    fallbackShadow.setAlpha(m_DarkMode ? 40 : 18);
    const QColor shadowBase = m_ShadowColor.isValid() ? m_ShadowColor : fallbackShadow;
    QRectF shadowBounds =
        bounds.adjusted(CARD_SHADOW_BLUR, CARD_SHADOW_BLUR + CARD_SHADOW_OFFSET, -CARD_SHADOW_BLUR, -CARD_SHADOW_BLUR);
    painter.setPen(Qt::NoPen);
    QColor sc = shadowBase;
    for (int i = 0; i < CARD_SHADOW_BLUR; ++i)
    {
        sc.setAlphaF(shadowBase.alphaF() * (1.0 - static_cast<qreal>(i) / CARD_SHADOW_BLUR));
        painter.setBrush(sc);
        painter.drawRoundedRect(shadowBounds.adjusted(-i, -i, i, i), cardRadius + i, cardRadius + i);
    }

    // Inset bounds past the shadow area for the actual card
    bounds.adjust(CARD_SHADOW_BLUR, CARD_SHADOW_BLUR, -CARD_SHADOW_BLUR, -CARD_SHADOW_BLUR - CARD_SHADOW_OFFSET);
    bounds = bounds.adjusted(0.5, 0.5, -0.5, -0.5);

    const QColor bg = m_IsActive ? m_BackgroundHighlightColor : m_BackgroundColor;
    QColor border = m_IsActive ? m_BorderHighlightColor : m_BorderColor;
    qreal borderWidth = CARD_BORDER_WIDTH;

    if (m_IsPinned)
    {
        border = an::ui::tokens().colors.accentPinned;
        borderWidth = 2.0;
    }

    QPainterPath path;
    path.addRoundedRect(bounds, cardRadius, cardRadius);

    painter.setPen(Qt::NoPen);
    painter.setBrush(bg);
    painter.drawPath(path);

    painter.setPen(QPen(border, borderWidth));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);

    if (m_IsPinned)
    {
        const qreal padding = 8.0;
        const qreal circleRadius = 11.0;
        const QPointF center(bounds.right() - padding - circleRadius, bounds.top() + padding + circleRadius);

        painter.setPen(Qt::NoPen);
        painter.setBrush(an::ui::tokens().colors.accentPinned);
        painter.drawEllipse(center, circleRadius, circleRadius);

        an::ui::PainterGuard painterGuard(&painter);
        painter.translate(center);

        // Modern thumbtack: rounded head + tapered shaft
        QPainterPath pinPath;
        pinPath.addRoundedRect(QRectF(-4.0, -5.0, 8.0, 2.8), 1.4, 1.4);
        pinPath.moveTo(-2.0, -2.2);
        pinPath.lineTo(2.0, -2.2);
        pinPath.lineTo(2.0, 0.2);
        pinPath.lineTo(0.7, 0.2);
        pinPath.lineTo(0.0, 5.0);
        pinPath.lineTo(-0.7, 0.2);
        pinPath.lineTo(-2.0, 0.2);
        pinPath.closeSubpath();

        painter.setPen(Qt::NoPen);
        painter.setBrush(an::ui::tokens().colors.textOnAccent);
        painter.drawPath(pinPath);
    }
}

void ImageThumbnailWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_OpenFileCallback)
    {
        m_OpenFileCallback(m_OriginalFilePath);
    }

    QWidget::mousePressEvent(event);
}

void ImageThumbnailWidget::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this);
    QAction* openAction = menu.addAction("Open");
    QAction* togglePinAction = menu.addAction(m_IsPinned ? "Unpin from top" : "Pin to top");
    QAction* openFolderAction = menu.addAction("Show in File Browser");
    QAction* copyPathAction = menu.addAction("Copy path");
    QAction* removeFromRecentAction = menu.addAction("Remove from recent files");

    QAction* chosenAction = menu.exec(event->globalPos());
    const QString filePath = normalizeLocalFilePath(m_OriginalFilePath);
    QFileInfo fileInfo(filePath);

    if (chosenAction == openAction && m_OpenFileCallback)
    {
        m_OpenFileCallback(filePath);
    }
    else if (chosenAction == togglePinAction)
    {
        togglePinned();
    }
    else if (chosenAction == openFolderAction)
    {
        showInFileBrowser(this, filePath);
    }
    else if (chosenAction == copyPathAction)
    {
        if (QClipboard* clipboard = QGuiApplication::clipboard())
        {
            clipboard->setText(filePath);
        }
    }
    else if (chosenAction == removeFromRecentAction && m_RemoveRecentCallback)
    {
        m_RemoveRecentCallback(filePath);
    }
}

void ImageThumbnailWidget::enterEvent(QEnterEvent* event)
{
    Q_UNUSED(event);
    m_IsActive = true;
    refreshBackground();
}

void ImageThumbnailWidget::leaveEvent(QEvent* event)
{
    Q_UNUSED(event);
    m_IsActive = false;
    refreshBackground();
}

void ImageThumbnailWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_OverlayFrame)
    {
        m_OverlayFrame->raise();
    }
    updateDisplayedPixmap();

    // Re-elide the filename for the new width (C)
    if (m_NameLabel && !m_OriginalFilePath.isEmpty())
    {
        const QFileInfo fileInfo(m_OriginalFilePath);
        const QFontMetrics nameMetrics(m_NameLabel->font());
        const int availableWidth =
            (std::max)(60, width() - m_Layout->contentsMargins().left() - m_Layout->contentsMargins().right());
        m_NameLabel->setText(nameMetrics.elidedText(fileInfo.fileName(), Qt::ElideMiddle, availableWidth));
    }
}

void ImageThumbnailWidget::refreshBackground()
{
    setProperty("dashboardHoverActive", m_IsActive);
    repolishWidget(this);
    if (m_PreviewFrame)
    {
        m_PreviewFrame->setProperty("dashboardHoverActive", m_IsActive);
        repolishWidget(m_PreviewFrame);
    }
    update();
}
