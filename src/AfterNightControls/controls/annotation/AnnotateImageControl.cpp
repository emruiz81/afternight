// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "controls/annotation/AnnotateImageControl.h"

#include "app/AppContextHelpers.h"
#include "app/FileDialogUtils.h"
#include "app/FilePolicies.h"
#include "components/AnCollapsibleSection.h"
#include "components/AnInfoBanner.h"
#include "components/AnSliderControl.h"
#include "controls/AutostretchApply.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewFiles.h"
#include "controls/ProgressDialogControl.h"
#include "controls/annotation/AnnotationLayout.h"
#include "controls/annotation/AnnotationPicture.h"
#include "controls/annotation/AnnotationRenderer.h"
#include "controls/annotation/AnnotationScene.h"
#include "controls/settings/SettingsKeys.h"
#include "session/IEngineSessionProvider.h"
#include "session/ImageDocument.h"
#include "theme/AnDesignTokens.h"
#include "widgets/ImageViewWidget.h"

#include <AfterNightPlatform/Logging.h>

#include <AfterNightEngine/IO/CodecFormat.h>

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QThreadPool>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <verdigris/wobjectimpl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <optional>
#include <stop_token>
#include <utility>
#include <vector>

W_OBJECT_IMPL(an::ui::AnnotateImageControl)

namespace an::ui
{
namespace
{

/// The preview's longest side it first asks the image presented at, before it knows its own size.
constexpr std::uint32_t kFirstPreviewEdge = 1600u;

/// The most the preview asks the image presented at, however far it is zoomed: the annotations stay sharp past it.
constexpr std::uint32_t kLargestPreviewEdge = 4096u;

/// How far the preview zooms in, as a multiple of the fitted picture.
constexpr double kLargestZoom = 16.0;

/// The output sizes the window offers, as long edges, below the image's own.
constexpr std::array<std::uint32_t, 7> kOutputEdges{4096u, 3840u, 2560u, 2048u, 1600u, 1280u, 1024u};

/// Runs work on a worker and its completion on the GUI thread; the completion is dropped when the application is gone.
template <typename WorkT, typename DoneT> void runThenLand(WorkT work, DoneT done)
{
    QThreadPool::globalInstance()->start([work = std::move(work), done = std::move(done)]() mutable {
        auto result = std::make_shared<decltype(work())>(work());
        QCoreApplication* application = QCoreApplication::instance();
        if (application == nullptr)
        {
            return;
        }
        QMetaObject::invokeMethod(
            application, [result, done]() mutable { done(std::move(*result)); }, Qt::QueuedConnection);
    });
}

[[nodiscard]] const an::consumer::EngineSession* currentSession()
{
    if (auto* provider = process_utils::currentEngineSessionProvider())
    {
        return &provider->engineSession();
    }
    return nullptr;
}

[[nodiscard]] QString translated(const char* text)
{
    return QCoreApplication::translate("an::ui::AnnotateImageControl", text);
}

/// A swatch of a colour, for a button that shows the colour it sets.
[[nodiscard]] QIcon swatchOf(const QColor& color, const QColor& rim)
{
    QPixmap pixmap(28, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(rim, 1.0));
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(0.5, 0.5, 27.0, 15.0), 3.0, 3.0);
    return QIcon(pixmap);
}

/// The size of a palette's swatch in the palettes' list.
constexpr QSize kPaletteSwatchSize(44, 16);

/// A palette's swatch: a stripe of each kind of object's colour, over its panel's colour.
[[nodiscard]] QIcon paletteSwatch(const AnnotationColors& colors)
{
    QPixmap pixmap(kPaletteSwatchSize);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF box(0.5, 0.5, kPaletteSwatchSize.width() - 1.0, kPaletteSwatchSize.height() - 1.0);
    QPainterPath rounded;
    rounded.addRoundedRect(box, 3.0, 3.0);
    painter.setClipPath(rounded);
    QColor panel = colors.panel;
    panel.setAlpha(255);
    painter.fillRect(box, panel);
    const std::array<QColor, 5> stripes{colors.galaxy, colors.nebula, colors.planetaryNebula, colors.openCluster,
                                        colors.globularCluster};
    const double width = (box.width() - 4.0) / static_cast<double>(stripes.size());
    for (std::size_t index = 0u; index < stripes.size(); ++index)
    {
        QColor stripe = stripes[index];
        stripe.setAlpha(255);
        painter.fillRect(QRectF(box.left() + 2.0 + width * static_cast<double>(index), box.top() + 3.0, width - 1.0,
                                box.height() - 6.0),
                         stripe);
    }
    painter.setClipping(false);
    painter.setPen(QPen(an::ui::tokens().colors.borderSubtle, 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(box, 3.0, 3.0);
    return QIcon(pixmap);
}

/// A palette's name, as the palettes' list offers it.
[[nodiscard]] QString paletteName(const AnnotationPalette palette)
{
    switch (palette)
    {
    case AnnotationPalette::Aurora:
        return translated("Aurora");
    case AnnotationPalette::Moonlight:
        return translated("Moonlight");
    case AnnotationPalette::Ember:
        return translated("Ember - for dark-adapted eyes");
    case AnnotationPalette::Ink:
        return translated("Ink - for bright images and print");
    case AnnotationPalette::Glacier:
        return translated("Glacier");
    case AnnotationPalette::Amethyst:
        return translated("Amethyst");
    case AnnotationPalette::Amber:
        return translated("Amber");
    case AnnotationPalette::Verdant:
        return translated("Verdant");
    case AnnotationPalette::Rose:
        return translated("Rose");
    case AnnotationPalette::Atlas:
        return translated("Atlas");
    case AnnotationPalette::Custom:
        break;
    }
    return translated("Custom");
}

/// What a change of the choices makes the window do again.
enum class Change : std::uint8_t
{
    /// Nothing but drawing the preview: a colour, an opacity, how marks are drawn.
    Drawing,

    /// The labels' places: their size, typeface, backing, the leader lines, the corners.
    Layout,

    /// What is shown: the scene and then its layout.
    Scene,

    /// The image under the annotations: its stretch, or whether it is drawn at all.
    Base,

    /// The picture's size alone.
    Output
};

/// The preview: the picture's base scaled into the widget and the annotations drawn over it at the same scale, zoomed
/// about the cursor by the wheel, moved by dragging and fitted again by a double click.
class AnnotationPreview final : public QWidget
{
public:
    explicit AnnotationPreview(QWidget* parent) : QWidget(parent)
    {
        setMinimumSize(480, 360);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMouseTracking(false);
        setFocusPolicy(Qt::WheelFocus);
    }

    /// Called with the long edge the image is now shown at, in device pixels, when it outgrows the base.
    std::function<void(std::uint32_t)> wantsResolution;

    void setPicture(const QSize imageSize, const bool transparent)
    {
        if (imageSize != m_imageSize)
        {
            m_zoom = 1.0;
            m_pan = QPointF();
        }
        m_imageSize = imageSize;
        m_transparent = transparent;
        update();
    }

    void setBase(std::shared_ptr<const AnnotationBase> base)
    {
        m_base = std::move(base);
        m_baseImage = m_base != nullptr ? m_base->image() : QImage();
        update();
    }

    void setDrawing(std::shared_ptr<const AnnotationScene> scene, std::shared_ptr<const AnnotationLayout> layout)
    {
        m_scene = std::move(scene);
        m_layout = std::move(layout);
        update();
    }

    void setStyle(const AnnotationStyle& style)
    {
        m_style = style;
        update();
    }

    void setBusy(const bool busy)
    {
        if (busy != m_busy)
        {
            m_busy = busy;
            update();
        }
    }

    void setMessage(const QString& message)
    {
        m_message = message;
        update();
    }

    /// The longest side the image is shown at now, in device pixels.
    [[nodiscard]] std::uint32_t shownEdge() const
    {
        const QRectF shown = pictureRect();
        return static_cast<std::uint32_t>(std::ceil((std::max)(shown.width(), shown.height()) * devicePixelRatioF()));
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const auto& tokenSet = an::ui::tokens();
        QPainter painter(this);
        painter.fillRect(rect(), tokenSet.colors.bgPrimary);
        if (m_imageSize.isEmpty())
        {
            painter.setPen(tokenSet.colors.textSecondary);
            painter.setFont(tokenSet.typography.body);
            painter.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, m_message);
            return;
        }
        const QRectF target = pictureRect();
        const double scale = target.width() / m_imageSize.width();
        painter.save();
        painter.setClipRect(target.intersected(QRectF(rect())));
        if (m_transparent)
        {
            // A checkerboard of the theme's two surfaces shows what is transparent.
            const double cell = 10.0;
            painter.fillRect(target, tokenSet.colors.bgSurface);
            const QRectF visible = target.intersected(QRectF(rect()));
            for (double y = std::floor((visible.top() - target.top()) / cell) * cell + target.top();
                 y < visible.bottom(); y += cell)
            {
                for (double x = std::floor((visible.left() - target.left()) / cell) * cell + target.left();
                     x < visible.right(); x += cell)
                {
                    const auto column = static_cast<long>(std::lround((x - target.left()) / cell));
                    const auto row = static_cast<long>(std::lround((y - target.top()) / cell));
                    if (((column + row) & 1L) != 0L)
                    {
                        painter.fillRect(QRectF(x, y, cell, cell), tokenSet.colors.bgCard);
                    }
                }
            }
        }
        else if (!m_baseImage.isNull() && m_base != nullptr)
        {
            painter.fillRect(target, Qt::black);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter.drawImage(
                QRectF(target.topLeft(), QSizeF(m_base->covers.width() * scale, m_base->covers.height() * scale)),
                m_baseImage);
        }
        else
        {
            painter.fillRect(target, Qt::black);
        }
        if (m_scene != nullptr && m_layout != nullptr)
        {
            painter.translate(target.topLeft());
            painter.scale(scale, scale);
            paintAnnotations(painter, *m_scene, *m_layout, m_style);
        }
        painter.restore();

        if (m_busy)
        {
            // A small pill in the corner while a newer drawing is on its way.
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setFont(tokenSet.typography.caption);
            const QString text = translated("Updating…");
            const QFontMetrics metrics(painter.font());
            const int padding = tokenSet.spacing.sm;
            const QRect pill(rect().right() - metrics.horizontalAdvance(text) - 3 * padding, rect().top() + padding,
                             metrics.horizontalAdvance(text) + 2 * padding, metrics.height() + padding);
            QColor fill = tokenSet.colors.bgSurface;
            fill.setAlpha(220);
            painter.setPen(Qt::NoPen);
            painter.setBrush(fill);
            painter.drawRoundedRect(pill, tokenSet.radius.md, tokenSet.radius.md);
            painter.setPen(tokenSet.colors.textSecondary);
            painter.drawText(pill, Qt::AlignCenter, text);
        }
    }

    void wheelEvent(QWheelEvent* event) override
    {
        if (m_imageSize.isEmpty())
        {
            return;
        }
        const double steps = event->angleDelta().y() / 120.0;
        const double zoom = std::clamp(m_zoom * std::pow(1.25, steps), 1.0, kLargestZoom);
        zoomAbout(event->position(), zoom);
        event->accept();
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && m_zoom > 1.0)
        {
            m_dragFrom = event->position();
            m_dragging = true;
            setCursor(Qt::ClosedHandCursor);
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (m_dragging)
        {
            m_pan += event->position() - m_dragFrom;
            m_dragFrom = event->position();
            clampPan();
            update();
        }
    }

    void mouseReleaseEvent(QMouseEvent* /*event*/) override
    {
        m_dragging = false;
        unsetCursor();
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (m_zoom > 1.0)
        {
            m_zoom = 1.0;
            m_pan = QPointF();
        }
        else
        {
            zoomAbout(event->position(), 4.0);
        }
        update();
        askResolution();
    }

    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        clampPan();
        askResolution();
    }

private:
    /// The picture fitted into the widget with a margin, before any zoom.
    [[nodiscard]] QRectF fittedRect() const
    {
        const double margin = an::ui::tokens().spacing.md;
        const QRectF area = QRectF(rect()).adjusted(margin, margin, -margin, -margin);
        if (m_imageSize.isEmpty() || area.isEmpty())
        {
            return area;
        }
        const double scale = (std::min)(area.width() / m_imageSize.width(), area.height() / m_imageSize.height());
        const QSizeF size(m_imageSize.width() * scale, m_imageSize.height() * scale);
        return QRectF(area.center() - QPointF(size.width() * 0.5, size.height() * 0.5), size);
    }

    /// Where the picture is drawn now, zoomed and moved.
    [[nodiscard]] QRectF pictureRect() const
    {
        const QRectF fitted = fittedRect();
        const QSizeF size = fitted.size() * m_zoom;
        return QRectF(fitted.center() + m_pan - QPointF(size.width() * 0.5, size.height() * 0.5), size);
    }

    void zoomAbout(const QPointF at, const double zoom)
    {
        const QRectF before = pictureRect();
        if (before.isEmpty())
        {
            return;
        }
        const QPointF fraction((at.x() - before.left()) / before.width(), (at.y() - before.top()) / before.height());
        m_zoom = zoom;
        const QRectF fitted = fittedRect();
        const QSizeF size = fitted.size() * m_zoom;
        const QPointF topLeft = at - QPointF(fraction.x() * size.width(), fraction.y() * size.height());
        m_pan = topLeft + QPointF(size.width() * 0.5, size.height() * 0.5) - fitted.center();
        clampPan();
        update();
        askResolution();
    }

    /// Keeps a zoomed picture covering the widget's middle, so it cannot be dragged away.
    void clampPan()
    {
        const QRectF fitted = fittedRect();
        const double reachX = (std::max)(0.0, (fitted.width() * m_zoom - fitted.width()) * 0.5);
        const double reachY = (std::max)(0.0, (fitted.height() * m_zoom - fitted.height()) * 0.5);
        m_pan.setX(std::clamp(m_pan.x(), -reachX, reachX));
        m_pan.setY(std::clamp(m_pan.y(), -reachY, reachY));
    }

    void askResolution()
    {
        if (wantsResolution && !m_imageSize.isEmpty())
        {
            wantsResolution(shownEdge());
        }
    }

    QSize m_imageSize;
    bool m_transparent = false;
    std::shared_ptr<const AnnotationBase> m_base;
    QImage m_baseImage;
    std::shared_ptr<const AnnotationScene> m_scene;
    std::shared_ptr<const AnnotationLayout> m_layout;
    AnnotationStyle m_style;
    QString m_message;
    bool m_busy = false;
    double m_zoom = 1.0;
    QPointF m_pan;
    QPointF m_dragFrom;
    bool m_dragging = false;
};

/// A colour the window lets a person set, with the member of the colours it is.
struct ColorRole
{
    const char* name;
    QColor AnnotationColors::* member;
};

} // namespace

struct AnnotateImageControl::Impl
{
    AnnotateImageControl* window = nullptr;
    ImageViewControl* view = nullptr;
    AnnotationSettings settings = defaultAnnotationSettings();
    BundledCatalogSource catalogSource;

    // What the window reads of the view's image, read again when it changes.
    std::optional<an::WcsSolution> solution;
    std::optional<an::CodecMetadata> frame;
    std::vector<an::CodecCard> cards;
    QSize imageSize;
    bool linear = false;
    QString imageName;

    // The answers the preview shows.
    std::shared_ptr<const AnnotationCatalogEntries> catalogs;
    std::shared_ptr<const AnnotationScene> scene;
    std::shared_ptr<const AnnotationLayout> layout;
    std::shared_ptr<const AnnotationBase> base;
    std::uint32_t baseEdge = 0u;
    AnnotationStretch baseStretch = AnnotationStretch::AsShown;
    std::optional<an::consumer::AssetRevision> baseRevision;
    std::uint32_t wantedEdge = kFirstPreviewEdge;

    // The requests on their way; each answer lands only while it is still the latest of its kind.
    std::uint64_t catalogRequest = 0u;
    std::uint64_t drawingRequest = 0u;
    std::uint64_t baseRequest = 0u;
    bool catalogBusy = false;
    bool drawingBusy = false;
    bool drawingAgain = false;
    bool drawingStale = false;
    bool baseBusy = false;
    bool pictureBusy = false;
    std::optional<std::pair<bool, QString>> pendingPicture;

    /// The palette the list shows: the one chosen, or the custom one once the person chose it or set a colour, even
    /// while their colours are still a palette's.
    AnnotationPalette paletteChoice = AnnotationPalette::Aurora;
    std::stop_source closing;
    QTimer* drawingTimer = nullptr;
    QTimer* baseTimer = nullptr;
    QTimer* saveTimer = nullptr;

    QString lastOutcome;
    std::size_t picturesMade = 0u;
    bool syncing = false;

    // The widgets.
    AnnotationPreview* preview = nullptr;
    QLabel* summary = nullptr;
    AnInfoBanner* banner = nullptr;
    QButtonGroup* detailGroup = nullptr;
    QLabel* detailNote = nullptr;
    QCheckBox* gridLayer = nullptr;
    QCheckBox* deepSkyLayer = nullptr;
    QCheckBox* starsLayer = nullptr;
    QCheckBox* compassLayer = nullptr;
    QCheckBox* scaleBarLayer = nullptr;
    QCheckBox* panelLayer = nullptr;
    QComboBox* labelText = nullptr;
    QComboBox* palette = nullptr;
    AnCollapsibleSection* colorsSection = nullptr;
    QFontComboBox* font = nullptr;
    QComboBox* fontWeight = nullptr;
    QCheckBox* italic = nullptr;
    QComboBox* labelColors = nullptr;
    QComboBox* backing = nullptr;
    AnSliderControl* textSize = nullptr;
    AnSliderControl* panelSize = nullptr;
    QComboBox* marker = nullptr;
    QComboBox* starMarker = nullptr;
    AnSliderControl* lineWidth = nullptr;
    AnSliderControl* lineOpacity = nullptr;
    QCheckBox* fill = nullptr;
    AnSliderControl* fillOpacity = nullptr;
    AnSliderControl* gridOpacity = nullptr;
    QCheckBox* lineHalo = nullptr;
    QCheckBox* leaders = nullptr;
    QCheckBox* unlabelled = nullptr;
    std::vector<std::pair<QToolButton*, QColor AnnotationColors::*>> colorButtons;
    QLineEdit* title = nullptr;
    QLineEdit* credit = nullptr;
    QCheckBox* factCoordinates = nullptr;
    QCheckBox* factField = nullptr;
    QCheckBox* factOptics = nullptr;
    QCheckBox* factCamera = nullptr;
    QCheckBox* factFilter = nullptr;
    QCheckBox* factDate = nullptr;
    QCheckBox* factIntegration = nullptr;
    QComboBox* panelCorner = nullptr;
    QComboBox* compassCorner = nullptr;
    QComboBox* scaleBarCorner = nullptr;
    QCheckBox* messier = nullptr;
    QCheckBox* caldwell = nullptr;
    QCheckBox* ngc = nullptr;
    QCheckBox* ic = nullptr;
    QCheckBox* namedOnly = nullptr;
    QCheckBox* galaxies = nullptr;
    QCheckBox* nebulae = nullptr;
    QCheckBox* planetaries = nullptr;
    QCheckBox* openClusters = nullptr;
    QCheckBox* globulars = nullptr;
    QCheckBox* others = nullptr;
    QCheckBox* anyMagnitude = nullptr;
    QDoubleSpinBox* magnitude = nullptr;
    QCheckBox* unmeasured = nullptr;
    QDoubleSpinBox* minimumSize = nullptr;
    QDoubleSpinBox* starMagnitude = nullptr;
    QComboBox* starNaming = nullptr;
    QCheckBox* markUnnamed = nullptr;
    QComboBox* gridDensity = nullptr;
    QCheckBox* gridLabels = nullptr;
    QComboBox* outputSize = nullptr;
    QComboBox* stretch = nullptr;
    QComboBox* background = nullptr;
    QPushButton* exportButton = nullptr;
    QPushButton* openButton = nullptr;

    [[nodiscard]] bool working() const
    {
        return catalogBusy || drawingBusy || baseBusy || pictureBusy;
    }

    void refreshBusy()
    {
        preview->setBusy(catalogBusy || drawingBusy || baseBusy);
        const bool ready = solution.has_value() && scene != nullptr && layout != nullptr && !pictureBusy;
        exportButton->setEnabled(ready);
        openButton->setEnabled(ready);
    }

    void showProblem(const QString& message)
    {
        banner->setMessage(message);
        banner->setTone(AnInfoBanner::Tone::Warning);
        banner->setVisible(true);
    }

    void showOutcome(const QString& message, const AnInfoBanner::Tone tone)
    {
        lastOutcome = message;
        banner->setMessage(message);
        banner->setTone(tone);
        banner->setVisible(true);
    }

    // ---------------------------------------------------------------------------------------------------------------
    // What the window reads of the view

    /// Reads the solution, the cards and the size of the view's image; answers whether the solution changed.
    bool readImage()
    {
        const std::optional<an::WcsSolution> before = solution;
        solution.reset();
        frame.reset();
        cards.clear();
        imageSize = QSize();
        linear = false;
        if (view == nullptr || !view->hasImage())
        {
            return before.has_value();
        }
        imageName = QString::fromStdString(view->getUniqueName());
        // A read that never waits: an image a writer holds reads as stating nothing until its next update.
        if (an::Result<an::consumer::AssetReadLease> lease = view->document().tryRead(); lease.hasValue())
        {
            lease->visit([this](const auto& typed) {
                const auto stated = typed.metadata().codec().cards();
                cards.assign(stated.begin(), stated.end());
                frame = typed.metadata().codec();
                linear = isLinearData(typed.metadata());
                if (const std::optional<an::ImageDescriptor> described = typed.descriptor(); described.has_value())
                {
                    imageSize = QSize(static_cast<int>(described->extent().width()),
                                      static_cast<int>(described->extent().height()));
                }
            });
        }
        solution = view->imageSolution();
        return !(before == solution);
    }

    /// Starts again from the view's image: the catalogues when the solution changed, then the scene and the preview.
    void refreshFromImage()
    {
        const bool solutionChanged = readImage();
        refillOutputSizes();
        preview->setPicture(imageSize, settings.output.background == AnnotationBackground::Transparent);
        if (!solution.has_value())
        {
            catalogs.reset();
            scene.reset();
            layout.reset();
            preview->setDrawing(nullptr, nullptr);
            preview->setMessage(translated("The image states no astrometric solution. Plate solve it to annotate it."));
            showProblem(translated("The image states no astrometric solution. Plate solve it first."));
            refreshBusy();
            return;
        }
        banner->setVisible(false);
        if (solutionChanged || catalogs == nullptr)
        {
            catalogs.reset();
            requestCatalogs();
        }
        else
        {
            requestDrawing();
        }
        requestBase(true);
        refreshBusy();
    }

    // ---------------------------------------------------------------------------------------------------------------
    // The requests

    void requestCatalogs()
    {
        const an::consumer::EngineSession* session = currentSession();
        if (session == nullptr || !solution.has_value())
        {
            return;
        }
        const std::uint64_t request = ++catalogRequest;
        catalogBusy = true;
        refreshBusy();
        QPointer<AnnotateImageControl> guard(window);
        runThenLand(
            [session, solution = *solution, source = catalogSource, stop = closing.get_token()]() {
                return viewWorkOutcome([&]() { return queryAnnotationCatalogs(*session, solution, source, stop); });
            },
            [guard, request](an::Result<AnnotationCatalogEntries> found) {
                if (!guard)
                {
                    return;
                }
                Impl& impl = *guard->m_impl;
                if (request != impl.catalogRequest)
                {
                    return;
                }
                impl.catalogBusy = false;
                if (!found.hasValue())
                {
                    LOG_WARNING("Annotate Image: the catalogues could not be read: {}", found.status().message());
                    impl.showProblem(translated("The catalogues could not be read: %1")
                                         .arg(QString::fromUtf8(found.status().message())));
                    impl.refreshBusy();
                    return;
                }
                impl.catalogs = std::make_shared<const AnnotationCatalogEntries>(std::move(*found));
                impl.requestDrawing();
            });
    }

    /// Builds the scene and its layout again on a worker; a change while one is on its way builds once more after it.
    void requestDrawing()
    {
        if (!solution.has_value() || catalogs == nullptr)
        {
            return;
        }
        if (drawingBusy)
        {
            drawingAgain = true;
            return;
        }
        drawingBusy = true;
        drawingAgain = false;
        drawingStale = false;
        const std::uint64_t request = ++drawingRequest;
        refreshBusy();
        QPointer<AnnotateImageControl> guard(window);
        runThenLand(
            [solution = *solution, catalogs = catalogs, cards = cards, content = settings.content,
             style = settings.style]() {
                using Drawn =
                    std::pair<std::shared_ptr<const AnnotationScene>, std::shared_ptr<const AnnotationLayout>>;
                // A drawing that cannot be had - memory, a font - lands as none, and the window says so.
                try
                {
                    auto built = std::make_shared<AnnotationScene>(
                        buildAnnotationScene(AnnotationSceneSources{.solution = &solution,
                                                                    .deepSky = catalogs->deepSky,
                                                                    .stars = catalogs->stars,
                                                                    .cards = cards},
                                             content));
                    auto laid = std::make_shared<AnnotationLayout>(layoutAnnotations(*built, content, style));
                    return Drawn(std::move(built), std::move(laid));
                }
                catch (const std::exception& error)
                {
                    LOG_ERROR("Annotate Image: the annotations could not be laid out: {}", error.what());
                    return Drawn();
                }
            },
            [guard, request](
                std::pair<std::shared_ptr<const AnnotationScene>, std::shared_ptr<const AnnotationLayout>> drawn) {
                if (!guard)
                {
                    return;
                }
                Impl& impl = *guard->m_impl;
                impl.drawingBusy = false;
                if (drawn.first == nullptr || drawn.second == nullptr)
                {
                    impl.pendingPicture.reset();
                    impl.showProblem(translated("The annotations could not be laid out."));
                    impl.refreshBusy();
                    return;
                }
                if (request == impl.drawingRequest && impl.solution.has_value() && !impl.drawingStale)
                {
                    impl.scene = std::move(drawn.first);
                    impl.layout = std::move(drawn.second);
                    impl.preview->setDrawing(impl.scene, impl.layout);
                    impl.preview->setStyle(impl.settings.style);
                    impl.refreshSummary();
                }
                if (impl.drawingAgain || impl.drawingStale)
                {
                    impl.requestDrawing();
                }
                else if (impl.pendingPicture.has_value())
                {
                    const auto [open, path] = *impl.pendingPicture;
                    impl.pendingPicture.reset();
                    impl.makePicture(open, path);
                }
                impl.refreshBusy();
            });
    }

    /// Presents the image for the preview at the size it is shown, or larger when it is zoomed.
    void requestBase(const bool force)
    {
        if (!solution.has_value() || imageSize.isEmpty() || view == nullptr)
        {
            return;
        }
        if (settings.output.background == AnnotationBackground::Transparent)
        {
            preview->setBase(nullptr);
            return;
        }
        // The image kept is shown again at once - after a transparent spell - until a newer one lands.
        if (base != nullptr)
        {
            preview->setBase(base);
        }
        const auto longest = static_cast<std::uint32_t>((std::max)(imageSize.width(), imageSize.height()));
        const std::uint32_t edge = (std::min)({wantedEdge, kLargestPreviewEdge, longest});
        const an::consumer::EngineSession* session = currentSession();
        std::shared_ptr<const an::consumer::ImageAsset> asset = view->document().asset();
        if (session == nullptr || asset == nullptr)
        {
            return;
        }
        // The image is leased before the presentation is admitted: an edit of the view holds its writer while it waits
        // for admission, and a presentation admitted first would wait for the edit that waits for it.
        an::Result<an::consumer::AssetReadLease> lease = asset->tryRead();
        if (!lease.hasValue())
        {
            baseTimer->start();
            return;
        }
        const an::consumer::AssetRevision revision = lease->revision();
        if (!force && base != nullptr && baseStretch == settings.output.stretch && edge <= baseEdge &&
            baseRevision == revision)
        {
            return;
        }
        std::optional<ViewPresentationSettings> shown;
        if (auto* widget = view->getViewWidget(); widget != nullptr)
        {
            shown = widget->framePresentationSettings();
        }
        const ViewPresentationSettings presentation =
            annotationPresentationSettings(shown, settings.output.stretch, linear);
        std::optional<an::DisplayPresentationAnalysis> analysis;
        if (auto* widget = view->getViewWidget(); widget != nullptr)
        {
            analysis = widget->keptAnalysisOf(lease->revision(), presentation);
        }
        auto held = std::make_shared<an::consumer::AssetReadLease>(std::move(*lease));
        const std::uint64_t request = ++baseRequest;
        const AnnotationStretch stretchAsked = settings.output.stretch;
        baseBusy = true;
        refreshBusy();
        QPointer<AnnotateImageControl> guard(window);
        runThenLand(
            [session, held, presentation, analysis, edge, imageSize = imageSize,
             stop = closing.get_token()]() -> an::Result<std::shared_ptr<const AnnotationBase>> {
                return viewWorkOutcome([&]() -> an::Result<std::shared_ptr<const AnnotationBase>> {
                    an::Result<an::consumer::OperationScope> scope = session->admit(an::consumer::OperationRequest{
                        .purpose = an::consumer::OperationPurpose::Preview, .stopToken = stop});
                    if (!scope.hasValue())
                    {
                        held->release();
                        return scope.status();
                    }
                    an::Result<AnnotationBase> presented =
                        presentAnnotationBase(*scope, *held, presentation, analysis.has_value() ? &*analysis : nullptr,
                                              &session->memory(), annotationPictureSize(imageSize, edge));
                    held->release();
                    scope->end();
                    if (!presented.hasValue())
                    {
                        return presented.status();
                    }
                    return std::shared_ptr<const AnnotationBase>(
                        std::make_shared<const AnnotationBase>(std::move(*presented)));
                });
            },
            [guard, request, edge, stretchAsked,
             revision](an::Result<std::shared_ptr<const AnnotationBase>> presented) {
                if (!guard)
                {
                    return;
                }
                Impl& impl = *guard->m_impl;
                if (request != impl.baseRequest)
                {
                    return;
                }
                impl.baseBusy = false;
                if (!presented.hasValue())
                {
                    if (presented.status().code() != an::ErrorCode::Cancelled)
                    {
                        LOG_WARNING("Annotate Image: the preview's image could not be presented: {}",
                                    presented.status().message());
                        impl.showProblem(translated("The image could not be shown: %1")
                                             .arg(QString::fromUtf8(presented.status().message())));
                    }
                    impl.refreshBusy();
                    return;
                }
                impl.base = std::move(*presented);
                impl.baseEdge = edge;
                impl.baseStretch = stretchAsked;
                impl.baseRevision = revision;
                if (impl.settings.output.background == AnnotationBackground::Image)
                {
                    impl.preview->setBase(impl.base);
                }
                impl.refreshBusy();
            });
    }

    /// Draws the picture the choices make and writes it to a file or opens it in a view of its own.
    void makePicture(const bool open, const QString& path)
    {
        if (!solution.has_value() || !frame.has_value() || view == nullptr)
        {
            showOutcome(translated("The image states no astrometric solution."), AnInfoBanner::Tone::Warning);
            return;
        }
        if (drawingBusy || drawingAgain || scene == nullptr || layout == nullptr)
        {
            // The picture is drawn from the choices as they are now, once their layout lands.
            pendingPicture = std::make_pair(open, path);
            requestDrawing();
            return;
        }
        const an::consumer::EngineSession* session = currentSession();
        std::shared_ptr<const an::consumer::ImageAsset> asset = view->document().asset();
        if (session == nullptr || asset == nullptr)
        {
            return;
        }
        const bool transparent = settings.output.background == AnnotationBackground::Transparent;
        const QSize pictureSize = annotationPictureSize(imageSize, settings.output.longEdge);

        // The output intent is decided here, from the export settings, before anything is drawn.
        std::optional<an::OutputIntent> intent;
        if (!open)
        {
            const an::CodecFormat container = an::codecFormatFromPath(QFile::encodeName(path).toStdString());
            if (container != an::CodecFormat::Png && container != an::CodecFormat::Jpeg &&
                container != an::CodecFormat::Tiff)
            {
                showOutcome(translated("Choose a PNG, JPEG or TIFF file."), AnInfoBanner::Tone::Warning);
                return;
            }
            if (transparent && container == an::CodecFormat::Jpeg)
            {
                showOutcome(translated("A JPEG file cannot keep transparency: choose a PNG or TIFF file."),
                            AnInfoBanner::Tone::Warning);
                return;
            }
            an::Result<an::consumer::SessionHold> hold = session->hold();
            if (!hold.hasValue())
            {
                showOutcome(QString::fromUtf8(hold.status().message()), AnInfoBanner::Tone::Error);
                return;
            }
            const std::optional<an::ImageDescriptor> described =
                annotatedPictureDescriptor(hold->engine().limits(), pictureSize, transparent);
            if (!described.has_value())
            {
                showOutcome(translated("The picture is larger than an image may be."), AnInfoBanner::Tone::Error);
                return;
            }
            an::Result<an::OutputIntent> built = an::Status::failure(an::ErrorCode::NotInitialized, "no settings");
            if (auto* context = an::ui::appContext();
                context != nullptr && context->getOutputIntentProvider() != nullptr)
            {
                built = context->getOutputIntentProvider()->buildOutputIntent(*described, container);
            }
            else
            {
                an::Result<std::shared_ptr<const an::CodecRegistry>> registry = createProductCodecRegistry();
                built = registry.hasValue() ? buildExportIntent(ExportSettings{}, **registry, *described, container)
                                            : an::Result<an::OutputIntent>(registry.status());
            }
            if (!built.hasValue())
            {
                showOutcome(translated("The picture cannot be written there: %1")
                                .arg(QString::fromUtf8(built.status().message())),
                            AnInfoBanner::Tone::Error);
                return;
            }
            intent = std::move(*built);
        }
        an::Result<an::consumer::AssetReadLease> lease = asset->tryRead();
        if (!lease.hasValue())
        {
            showOutcome(translated("The image is being changed; try again once it is shown."),
                        AnInfoBanner::Tone::Warning);
            return;
        }
        std::optional<ViewPresentationSettings> shown;
        if (auto* widget = view->getViewWidget(); widget != nullptr)
        {
            shown = widget->framePresentationSettings();
        }
        const ViewPresentationSettings presentation =
            annotationPresentationSettings(shown, settings.output.stretch, linear);
        std::optional<an::DisplayPresentationAnalysis> analysis;
        if (base != nullptr && baseStretch == settings.output.stretch && baseRevision == lease->revision())
        {
            // The preview's image was stretched through this revision's whole-image analysis: the picture takes the
            // same.
            analysis = base->analysis;
        }
        else if (auto* widget = view->getViewWidget(); widget != nullptr)
        {
            analysis = widget->keptAnalysisOf(lease->revision(), presentation);
        }
        auto held = std::make_shared<an::consumer::AssetReadLease>(std::move(*lease));

        auto* progress = new ProgressDialogControl(window, open ? translated("Annotate Image")
                                                                : translated("Export Annotated Image"));
        progress->setProgressBarRange(0, 0);
        progress->show();
        const QPointer<ProgressDialogControl> progressGuard(progress);
        pictureBusy = true;
        refreshBusy();

        QPointer<AnnotateImageControl> guard(window);
        using Made = std::optional<an::consumer::ImageAsset>;
        runThenLand(
            [session, held, presentation, analysis, pictureSize, transparent, open, path, intent, sceneHeld = scene,
             layoutHeld = layout, style = settings.style, frame = *frame, solution = *solution,
             stop = progress->stopToken()]() -> an::Result<Made> {
                return viewWorkOutcome([&]() -> an::Result<Made> {
                    std::optional<AnnotationBase> under;
                    if (!transparent)
                    {
                        an::Result<an::consumer::OperationScope> scope = session->admit(an::consumer::OperationRequest{
                            .purpose = an::consumer::OperationPurpose::Apply, .stopToken = stop});
                        if (!scope.hasValue())
                        {
                            held->release();
                            return scope.status();
                        }
                        an::Result<AnnotationBase> presented =
                            presentAnnotationBase(*scope, *held, presentation, analysis ? &*analysis : nullptr,
                                                  &session->memory(), pictureSize);
                        scope->end();
                        if (!presented.hasValue())
                        {
                            held->release();
                            return presented.status();
                        }
                        under = std::move(*presented);
                    }
                    held->release();
                    an::Result<AnnotatedPicture> drawn =
                        composeAnnotatedPicture(under.has_value() ? &*under : nullptr, pictureSize, *sceneHeld,
                                                *layoutHeld, style, &session->memory());
                    under.reset();
                    if (!drawn.hasValue())
                    {
                        return drawn.status();
                    }
                    an::Result<an::consumer::ImageAsset> made =
                        annotatedPictureAsset(*session, drawn->image, frame, solution, transparent, stop);
                    drawn->image = QImage();
                    if (!made.hasValue())
                    {
                        return made.status();
                    }
                    if (open)
                    {
                        return Made(std::move(*made));
                    }
                    const an::Status written = writeViewImage(*session, *made, path, *intent, stop);
                    if (written.failed())
                    {
                        return written;
                    }
                    return Made();
                });
            },
            [guard, progressGuard, open, path](an::Result<Made> made) {
                if (progressGuard)
                {
                    progressGuard->closeAsync();
                }
                if (!guard)
                {
                    return;
                }
                Impl& impl = *guard->m_impl;
                impl.pictureBusy = false;
                impl.refreshBusy();
                if (!made.hasValue())
                {
                    if (made.status().code() == an::ErrorCode::Cancelled)
                    {
                        impl.showOutcome(translated("Stopped."), AnInfoBanner::Tone::Info);
                        return;
                    }
                    LOG_ERROR("Annotate Image: the picture could not be made: {}", made.status().message());
                    impl.showOutcome(
                        translated("The picture could not be made: %1").arg(QString::fromUtf8(made.status().message())),
                        AnInfoBanner::Tone::Error);
                    return;
                }
                if (!open)
                {
                    ++impl.picturesMade;
                    LOG_INFO("Annotate Image: wrote '{}'", path.toStdString());
                    impl.showOutcome(translated("Saved %1").arg(QDir::toNativeSeparators(path)),
                                     AnInfoBanner::Tone::Success);
                    return;
                }
                impl.openPicture(std::move(**made));
            });
    }

    void openPicture(an::consumer::ImageAsset&& asset)
    {
        const QString name = (imageName.isEmpty() ? translated("Image") : imageName) + translated("_annotated");
        an::Result<ImageDocument> document =
            ImageDocument::create(std::move(asset), DocumentNames{.displayName = name});
        IViewFactory* factory = process_utils::currentViewFactory();
        ImageViewControl* opened = nullptr;
        if (document.hasValue() && factory != nullptr)
        {
            opened = factory->createNewImageViewControlFromImage(
                nullptr, std::move(*document), nullptr, "Annotate Image", ViewControlMode::standard, view, false, true);
        }
        if (opened == nullptr)
        {
            showOutcome(document.hasValue() ? translated("No view could be opened for the picture.")
                                            : translated("The picture could not be opened: %1")
                                                  .arg(QString::fromUtf8(document.status().message())),
                        AnInfoBanner::Tone::Error);
            return;
        }
        ++picturesMade;
        LOG_INFO("Annotate Image: opened the picture as '{}'", opened->getUniqueName());
        showOutcome(translated("Opened %1").arg(QString::fromStdString(opened->getUniqueName())),
                    AnInfoBanner::Tone::Success);
    }

    // ---------------------------------------------------------------------------------------------------------------
    // The choices

    /// Reacts to a change of the choices: keeps them, then draws, lays out or builds again as far as the change
    /// reaches.
    void changed(const Change change)
    {
        if (syncing)
        {
            return;
        }
        saveTimer->start();
        refreshDetail();
        refreshPalette();
        switch (change)
        {
        case Change::Drawing:
            preview->setStyle(settings.style);
            break;
        case Change::Layout:
        case Change::Scene:
            // A drawing on its way was made from the choices before this one; its answer is shown no more.
            if (drawingBusy)
            {
                drawingStale = true;
            }
            drawingTimer->start();
            break;
        case Change::Base:
            preview->setPicture(imageSize, settings.output.background == AnnotationBackground::Transparent);
            requestBase(false);
            break;
        case Change::Output:
            break;
        }
        refreshSummary();
    }

    void refreshDetail()
    {
        const AnnotationDetail detail = annotationDetailOf(settings.content);
        const bool wasSyncing = syncing;
        syncing = true;
        detailGroup->setExclusive(false);
        for (QAbstractButton* button : detailGroup->buttons())
        {
            const bool chosen = detailGroup->id(button) == static_cast<int>(detail);
            button->setChecked(chosen);
            // The chosen level reads as the primary button, which the theme paints apart from the others.
            const QVariant variant = chosen ? QVariant(QStringLiteral("primary")) : QVariant();
            if (button->property("anButtonVariant") != variant)
            {
                button->setProperty("anButtonVariant", variant);
                button->style()->unpolish(button);
                button->style()->polish(button);
                button->update();
            }
        }
        detailGroup->setExclusive(true);
        detailNote->setVisible(detail == AnnotationDetail::Custom);
        syncing = wasSyncing;
    }

    void refreshPalette()
    {
        const bool wasSyncing = syncing;
        syncing = true;
        palette->setCurrentIndex(palette->findData(static_cast<int>(paletteChoice)));
        const QColor rim = an::ui::tokens().colors.borderSubtle;
        for (const auto& [button, member] : colorButtons)
        {
            button->setIcon(swatchOf(settings.style.colors.*member, rim));
        }
        syncing = wasSyncing;
    }

    void refreshSummary()
    {
        QStringList parts;
        if (scene != nullptr && layout != nullptr)
        {
            // A star marked without a label by choice is not one whose label found no room.
            const auto labelled = static_cast<int>(std::count(layout->labelled.begin(), layout->labelled.end(), true));
            const auto markedOnly = static_cast<int>(std::count_if(
                scene->marks.begin(), scene->marks.end(), [](const AnnotationMark& mark) { return mark.markOnly; }));
            const auto named = static_cast<int>(scene->marks.size()) - markedOnly;
            parts << (named == 1 ? translated("1 object") : translated("%1 objects").arg(named));
            if (labelled < named)
            {
                parts << translated("%1 labelled").arg(labelled);
            }
            if (markedOnly > 0)
            {
                parts << (markedOnly == 1 ? translated("1 more star marked")
                                          : translated("%1 more stars marked").arg(markedOnly));
            }
            if (title->text().trimmed().isEmpty())
            {
                title->setPlaceholderText(scene->panel.title.isEmpty() ? translated("No title") : scene->panel.title);
            }
        }
        const QSize picture = annotationPictureSize(imageSize, settings.output.longEdge);
        if (!picture.isEmpty())
        {
            parts << translated("picture %1 × %2 px").arg(picture.width()).arg(picture.height());
        }
        summary->setText(parts.join(QStringLiteral("  ·  ")));
    }

    void refillOutputSizes()
    {
        const bool wasSyncing = syncing;
        syncing = true;
        outputSize->clear();
        outputSize->addItem(imageSize.isEmpty()
                                ? translated("Full size")
                                : translated("Full size — %1 × %2").arg(imageSize.width()).arg(imageSize.height()),
                            0u);
        // The usual long edges below the image's own, and the one chosen when it is another: a choice the list does
        // not hold is never dropped, and one at or past the image's own size is the image's size.
        const int longest = (std::max)(imageSize.width(), imageSize.height());
        std::vector<std::uint32_t> edges(kOutputEdges.begin(), kOutputEdges.end());
        const std::uint32_t chosen = settings.output.longEdge;
        if (chosen != 0u && std::find(edges.begin(), edges.end(), chosen) == edges.end())
        {
            edges.push_back(chosen);
            std::sort(edges.begin(), edges.end(), std::greater<>());
        }
        for (const std::uint32_t edge : edges)
        {
            if (static_cast<int>(edge) < longest)
            {
                const QSize size = annotationPictureSize(imageSize, edge);
                outputSize->addItem(translated("%1 px — %2 × %3").arg(edge).arg(size.width()).arg(size.height()), edge);
            }
        }
        const int index = outputSize->findData(chosen);
        outputSize->setCurrentIndex(index < 0 ? 0 : index);
        syncing = wasSyncing;
    }

    /// Shows the choices in the widgets without reacting to them.
    void showSettings()
    {
        paletteChoice = annotationPaletteOf(settings.style.colors);
        syncing = true;
        const AnnotationContent& content = settings.content;
        const AnnotationStyle& style = settings.style;
        gridLayer->setChecked(content.layers.grid);
        deepSkyLayer->setChecked(content.layers.deepSky);
        starsLayer->setChecked(content.layers.stars);
        compassLayer->setChecked(content.layers.compass);
        scaleBarLayer->setChecked(content.layers.scaleBar);
        panelLayer->setChecked(content.layers.infoPanel);
        labelText->setCurrentIndex(labelText->findData(static_cast<int>(content.labelText)));
        // An empty typeface is the system's sans-serif one, which the list shows as the application's own.
        font->setCurrentFont(style.fontFamily.trimmed().isEmpty() ? QFont() : QFont(style.fontFamily));
        fontWeight->setCurrentIndex(fontWeight->findData(static_cast<int>(style.fontWeight)));
        italic->setChecked(style.italicLabels);
        labelColors->setCurrentIndex(labelColors->findData(static_cast<int>(style.labelsInObjectColor)));
        backing->setCurrentIndex(backing->findData(static_cast<int>(style.labelBacking)));
        textSize->setValue(static_cast<int>(std::lround(style.textSize * 100.0)));
        panelSize->setValue(static_cast<int>(std::lround(style.panelSize * 100.0)));
        marker->setCurrentIndex(marker->findData(static_cast<int>(style.marker)));
        starMarker->setCurrentIndex(starMarker->findData(static_cast<int>(style.starMarker)));
        lineWidth->setValue(static_cast<int>(std::lround(style.lineWidth * 100.0)));
        lineOpacity->setValue(static_cast<int>(std::lround(style.lineOpacity * 100.0)));
        fill->setChecked(style.fillObjects);
        fill->setEnabled(style.marker == AnnotationMarker::Outline);
        fillOpacity->setValue(static_cast<int>(std::lround(style.fillOpacity * 100.0)));
        fillOpacity->setEnabled(style.fillObjects && style.marker == AnnotationMarker::Outline);
        gridOpacity->setValue(static_cast<int>(std::lround(style.gridOpacity * 100.0)));
        lineHalo->setChecked(style.lineHalo);
        leaders->setChecked(style.leaderLines);
        unlabelled->setChecked(style.unlabelledMarks);
        title->setText(content.title);
        credit->setText(content.credit);
        factCoordinates->setChecked(content.facts.coordinates);
        factField->setChecked(content.facts.field);
        factOptics->setChecked(content.facts.optics);
        factCamera->setChecked(content.facts.camera);
        factFilter->setChecked(content.facts.filter);
        factDate->setChecked(content.facts.date);
        factIntegration->setChecked(content.facts.integration);
        panelCorner->setCurrentIndex(panelCorner->findData(static_cast<int>(style.panelCorner)));
        compassCorner->setCurrentIndex(compassCorner->findData(static_cast<int>(style.compassCorner)));
        scaleBarCorner->setCurrentIndex(scaleBarCorner->findData(static_cast<int>(style.scaleBarCorner)));
        showSelection();
        gridDensity->setCurrentIndex(gridDensity->findData(static_cast<int>(content.gridDensity)));
        gridLabels->setChecked(content.gridLabels);
        stretch->setCurrentIndex(stretch->findData(static_cast<int>(settings.output.stretch)));
        background->setCurrentIndex(background->findData(static_cast<int>(settings.output.background)));
        syncing = false;
        refillOutputSizes();
        refreshDetail();
        refreshPalette();
    }

    /// Shows the catalogues' and stars' selection, which a level of detail changes, without reacting to it.
    void showSelection()
    {
        const bool wasSyncing = syncing;
        syncing = true;
        const AnnotationDeepSkySelection& deepSky = settings.content.deepSky;
        messier->setChecked(deepSky.messier);
        caldwell->setChecked(deepSky.caldwell);
        ngc->setChecked(deepSky.ngc);
        ic->setChecked(deepSky.ic);
        namedOnly->setChecked(deepSky.namedOnly);
        galaxies->setChecked(deepSky.galaxies);
        nebulae->setChecked(deepSky.nebulae);
        planetaries->setChecked(deepSky.planetaryNebulae);
        openClusters->setChecked(deepSky.openClusters);
        globulars->setChecked(deepSky.globularClusters);
        others->setChecked(deepSky.otherObjects);
        const bool any = deepSky.magnitudeLimit >= kAnnotationNoMagnitudeLimit;
        anyMagnitude->setChecked(any);
        magnitude->setEnabled(!any);
        magnitude->setValue(any ? 16.0 : deepSky.magnitudeLimit);
        unmeasured->setChecked(deepSky.unmeasured);
        minimumSize->setValue(deepSky.minimumSize * 100.0);
        starMagnitude->setValue(settings.content.stars.magnitudeLimit);
        starNaming->setCurrentIndex(starNaming->findData(static_cast<int>(settings.content.stars.naming)));
        markUnnamed->setChecked(settings.content.stars.markUnnamed);
        syncing = wasSyncing;
    }

    /// Sets one colour: the colours become the person's own, kept for the custom palette.
    void setColor(QColor AnnotationColors::* member, const QColor& color)
    {
        settings.style.colors.*member = color;
        settings.style.customColors = settings.style.colors;
        paletteChoice = AnnotationPalette::Custom;
        changed(Change::Drawing);
    }

    void save() const
    {
        if (auto* store = process_utils::currentGlobalSettings(); store != nullptr)
        {
            store->upsert(QString::fromLatin1(SettingsKeys::ANNOTATION_SETTINGS), storedAnnotationSettings(settings));
        }
    }

    // ---------------------------------------------------------------------------------------------------------------
    // The widgets

    QCheckBox* checkBox(const QString& text, QWidget* parent, const char* name, const std::function<void(bool)>& apply,
                        const Change change)
    {
        auto* box = new QCheckBox(text, parent);
        box->setObjectName(QString::fromLatin1(name));
        QObject::connect(box, &QCheckBox::toggled, window, [this, apply, change](const bool checked) {
            if (syncing)
            {
                return;
            }
            apply(checked);
            changed(change);
        });
        return box;
    }

    template <typename EnumT>
    QComboBox* comboBox(QWidget* parent, const char* name, const std::vector<std::pair<QString, EnumT>>& items,
                        const std::function<void(EnumT)>& apply, const Change change)
    {
        auto* combo = new QComboBox(parent);
        combo->setObjectName(QString::fromLatin1(name));
        for (const auto& [text, value] : items)
        {
            combo->addItem(text, static_cast<int>(value));
        }
        QObject::connect(combo, &QComboBox::currentIndexChanged, window, [this, combo, apply, change](const int) {
            if (syncing || combo->currentIndex() < 0)
            {
                return;
            }
            apply(static_cast<EnumT>(combo->currentData().toInt()));
            changed(change);
        });
        return combo;
    }

    AnSliderControl* slider(const QString& text, QWidget* parent, const char* name, const int minimum,
                            const int maximum, const std::function<void(int)>& apply, const Change change)
    {
        auto* control = new AnSliderControl(text, minimum, maximum, minimum, parent);
        control->setObjectName(QString::fromLatin1(name));
        control->spinBox()->setSuffix(QStringLiteral(" %"));
        QObject::connect(control->slider(), &QSlider::valueChanged, window, [this, apply, change](const int value) {
            if (syncing)
            {
                return;
            }
            apply(value);
            changed(change);
        });
        return control;
    }

    std::vector<std::pair<QString, AnnotationCorner>> cornerItems() const
    {
        return {{translated("Top left"), AnnotationCorner::TopLeft},
                {translated("Top right"), AnnotationCorner::TopRight},
                {translated("Bottom left"), AnnotationCorner::BottomLeft},
                {translated("Bottom right"), AnnotationCorner::BottomRight}};
    }

    QWidget* buildChoices(QWidget* parent)
    {
        const auto& tokenSet = an::ui::tokens();
        auto* column = new QWidget(parent);
        auto* layoutColumn = new QVBoxLayout(column);
        layoutColumn->setContentsMargins(0, 0, tokenSet.spacing.sm, 0);
        layoutColumn->setSpacing(tokenSet.spacing.sm);

        // What the picture names.
        auto* contentSection = new AnCollapsibleSection(translated("Annotations"), column);
        contentSection->setObjectName(QStringLiteral("annotateContentSection"));
        auto* contentLayout = new QVBoxLayout();
        contentLayout->setSpacing(tokenSet.spacing.sm);
        auto* detailLabel = new QLabel(translated("Level of detail"), contentSection->contentWidget());
        detailLabel->setProperty("anStatusTone", QStringLiteral("secondary"));
        contentLayout->addWidget(detailLabel);
        auto* detailRow = new QHBoxLayout();
        detailRow->setSpacing(tokenSet.spacing.xs);
        detailGroup = new QButtonGroup(window);
        const std::array<std::pair<QString, AnnotationDetail>, 4> details{
            {{translated("Essential"), AnnotationDetail::Essential},
             {translated("Standard"), AnnotationDetail::Standard},
             {translated("Detailed"), AnnotationDetail::Detailed},
             {translated("Complete"), AnnotationDetail::Complete}}};
        const std::array<QString, 4> detailTips{
            translated("The Messier and Caldwell objects and the named ones, and the brightest stars by name"),
            translated("Every catalogue to magnitude 12, objects large enough to show, and stars to magnitude 4.5 by "
                       "name and designation"),
            translated(
                "Every catalogue to magnitude 14 and smaller objects, and stars to magnitude 6, the unnamed ones "
                "marked"),
            translated("Every galaxy, nebula and cluster the catalogues hold in the field, and every bright star, by "
                       "catalogue number when it has no name")};
        for (std::size_t index = 0u; index < details.size(); ++index)
        {
            auto* button = new QPushButton(details[index].first, contentSection->contentWidget());
            button->setCheckable(true);
            button->setToolTip(detailTips[index]);
            button->setObjectName(QStringLiteral("annotateDetail%1").arg(details[index].first));
            detailGroup->addButton(button, static_cast<int>(details[index].second));
            detailRow->addWidget(button);
        }
        QObject::connect(detailGroup, &QButtonGroup::idClicked, window, [this](const int id) {
            if (syncing)
            {
                return;
            }
            applyAnnotationDetail(settings.content, static_cast<AnnotationDetail>(id));
            showSelection();
            changed(Change::Scene);
        });
        contentLayout->addLayout(detailRow);
        detailNote = new QLabel(translated("Custom: the catalogues and limits below are your own."),
                                contentSection->contentWidget());
        detailNote->setProperty("anStatusTone", QStringLiteral("secondary"));
        detailNote->setWordWrap(true);
        contentLayout->addWidget(detailNote);

        auto* layers = new QGridLayout();
        layers->setHorizontalSpacing(tokenSet.spacing.md);
        layers->setVerticalSpacing(tokenSet.spacing.xs);
        QWidget* content = contentSection->contentWidget();
        gridLayer = checkBox(
            translated("Coordinate grid"), content, "annotateGrid",
            [this](bool on) { settings.content.layers.grid = on; }, Change::Scene);
        deepSkyLayer = checkBox(
            translated("Deep-sky objects"), content, "annotateDeepSky",
            [this](bool on) { settings.content.layers.deepSky = on; }, Change::Scene);
        starsLayer = checkBox(
            translated("Stars"), content, "annotateStars", [this](bool on) { settings.content.layers.stars = on; },
            Change::Scene);
        compassLayer = checkBox(
            translated("Compass"), content, "annotateCompass",
            [this](bool on) { settings.content.layers.compass = on; }, Change::Layout);
        scaleBarLayer = checkBox(
            translated("Scale bar"), content, "annotateScaleBar",
            [this](bool on) { settings.content.layers.scaleBar = on; }, Change::Layout);
        panelLayer = checkBox(
            translated("Info panel"), content, "annotatePanel",
            [this](bool on) { settings.content.layers.infoPanel = on; }, Change::Scene);
        layers->addWidget(gridLayer, 0, 0);
        layers->addWidget(deepSkyLayer, 0, 1);
        layers->addWidget(starsLayer, 1, 0);
        layers->addWidget(compassLayer, 1, 1);
        layers->addWidget(scaleBarLayer, 2, 0);
        layers->addWidget(panelLayer, 2, 1);
        contentLayout->addLayout(layers);

        auto* labelsForm = new QFormLayout();
        labelText = comboBox<AnnotationLabelText>(
            content, "annotateLabelText",
            {{translated("Designation and name"), AnnotationLabelText::DesignationAndName},
             {translated("Designation"), AnnotationLabelText::Designation},
             {translated("Name"), AnnotationLabelText::Name}},
            [this](AnnotationLabelText value) { settings.content.labelText = value; }, Change::Layout);
        labelsForm->addRow(translated("Label text"), labelText);
        contentLayout->addLayout(labelsForm);
        contentSection->setContentLayout(contentLayout);
        contentSection->setExpanded(true);
        layoutColumn->addWidget(contentSection);

        // How it looks: the palette, then the text and the lines, each set apart from the other.
        auto* styleSection = new AnCollapsibleSection(translated("Style"), column);
        styleSection->setObjectName(QStringLiteral("annotateStyleSection"));
        QWidget* look = styleSection->contentWidget();
        auto* styleLayout = new QVBoxLayout();
        styleLayout->setSpacing(tokenSet.spacing.sm);
        auto* paletteForm = new QFormLayout();
        palette = new QComboBox(look);
        palette->setObjectName(QStringLiteral("annotatePalette"));
        palette->setIconSize(kPaletteSwatchSize);
        for (const AnnotationPalette value : annotationPalettes())
        {
            palette->addItem(paletteSwatch(annotationPaletteColors(value)), paletteName(value),
                             static_cast<int>(value));
        }
        palette->addItem(translated("Custom"), static_cast<int>(AnnotationPalette::Custom));
        QObject::connect(palette, &QComboBox::currentIndexChanged, window, [this](const int) {
            if (syncing)
            {
                return;
            }
            const auto chosen = static_cast<AnnotationPalette>(palette->currentData().toInt());
            if (chosen == AnnotationPalette::Custom)
            {
                // The person's own colours come back; the first time, they start from the palette shown, and the
                // colours open to be set.
                if (settings.style.customColors.has_value())
                {
                    settings.style.colors = *settings.style.customColors;
                }
                else
                {
                    settings.style.customColors = settings.style.colors;
                }
                colorsSection->setExpanded(true);
            }
            else
            {
                settings.style.colors = annotationPaletteColors(chosen);
            }
            paletteChoice = chosen;
            changed(Change::Drawing);
        });
        paletteForm->addRow(translated("Palette"), palette);
        styleLayout->addLayout(paletteForm);

        // The text: its typeface, its weight and slant, its colour, its backing and its sizes.
        auto* textSection = new AnCollapsibleSection(translated("Text"), look);
        textSection->setObjectName(QStringLiteral("annotateTextSection"));
        QWidget* text = textSection->contentWidget();
        auto* textLayout = new QVBoxLayout();
        textLayout->setSpacing(tokenSet.spacing.sm);
        auto* textForm = new QFormLayout();
        font = new QFontComboBox(text);
        font->setObjectName(QStringLiteral("annotateFont"));
        font->setFontFilters(QFontComboBox::ScalableFonts);
        font->setEditable(false);
        QObject::connect(font, &QFontComboBox::currentFontChanged, window, [this](const QFont& chosen) {
            if (syncing)
            {
                return;
            }
            settings.style.fontFamily = chosen.family();
            changed(Change::Layout);
        });
        textForm->addRow(translated("Font"), font);
        fontWeight = comboBox<AnnotationFontWeight>(
            text, "annotateFontWeight",
            {{translated("Light"), AnnotationFontWeight::Light},
             {translated("Regular"), AnnotationFontWeight::Regular},
             {translated("Medium"), AnnotationFontWeight::Medium},
             {translated("Bold"), AnnotationFontWeight::Bold}},
            [this](AnnotationFontWeight value) { settings.style.fontWeight = value; }, Change::Layout);
        italic = checkBox(
            translated("Italic labels"), text, "annotateItalic", [this](bool on) { settings.style.italicLabels = on; },
            Change::Layout);
        auto* weightRow = new QHBoxLayout();
        weightRow->setSpacing(tokenSet.spacing.sm);
        weightRow->addWidget(fontWeight, 1);
        weightRow->addWidget(italic);
        textForm->addRow(translated("Weight"), weightRow);
        labelColors = comboBox<bool>(
            text, "annotateLabelColors",
            {{translated("Each object's colour"), true}, {translated("The text colour"), false}},
            [this](bool value) { settings.style.labelsInObjectColor = value; }, Change::Drawing);
        textForm->addRow(translated("Colour"), labelColors);
        backing = comboBox<AnnotationLabelBacking>(
            text, "annotateBacking",
            {{translated("Halo"), AnnotationLabelBacking::Halo},
             {translated("Badge"), AnnotationLabelBacking::Badge},
             {translated("Plain"), AnnotationLabelBacking::Plain}},
            [this](AnnotationLabelBacking value) { settings.style.labelBacking = value; }, Change::Layout);
        textForm->addRow(translated("Backing"), backing);
        textLayout->addLayout(textForm);
        textSize = slider(
            translated("Label size"), text, "annotateTextSize", static_cast<int>(kAnnotationMinimumTextSize * 100.0),
            static_cast<int>(kAnnotationMaximumTextSize * 100.0),
            [this](int value) { settings.style.textSize = value / 100.0; }, Change::Layout);
        panelSize = slider(
            translated("Panel size"), text, "annotatePanelSize", static_cast<int>(kAnnotationMinimumPanelSize * 100.0),
            static_cast<int>(kAnnotationMaximumPanelSize * 100.0),
            [this](int value) { settings.style.panelSize = value / 100.0; }, Change::Layout);
        textLayout->addWidget(textSize);
        textLayout->addWidget(panelSize);
        textSection->setContentLayout(textLayout);
        textSection->setExpanded(true);
        styleLayout->addWidget(textSection);

        // The lines: how objects and stars are marked, the lines' width and opacity, the fill and the grid.
        auto* linesSection = new AnCollapsibleSection(translated("Lines"), look);
        linesSection->setObjectName(QStringLiteral("annotateLinesSection"));
        QWidget* lines = linesSection->contentWidget();
        auto* linesLayout = new QVBoxLayout();
        linesLayout->setSpacing(tokenSet.spacing.sm);
        auto* linesForm = new QFormLayout();
        marker = comboBox<AnnotationMarker>(
            lines, "annotateMarker",
            {{translated("Outlines"), AnnotationMarker::Outline}, {translated("Brackets"), AnnotationMarker::Brackets}},
            [this](AnnotationMarker value) {
                settings.style.marker = value;
                fill->setEnabled(value == AnnotationMarker::Outline);
                fillOpacity->setEnabled(value == AnnotationMarker::Outline && settings.style.fillObjects);
            },
            Change::Drawing);
        linesForm->addRow(translated("Objects"), marker);
        starMarker = comboBox<AnnotationStarMarker>(
            lines, "annotateStarMarker",
            {{translated("Rings"), AnnotationStarMarker::Ring},
             {translated("Reticles"), AnnotationStarMarker::Reticle}},
            [this](AnnotationStarMarker value) { settings.style.starMarker = value; }, Change::Layout);
        linesForm->addRow(translated("Stars"), starMarker);
        linesLayout->addLayout(linesForm);
        lineWidth = slider(
            translated("Line width"), lines, "annotateLineWidth", static_cast<int>(kAnnotationMinimumLineWidth * 100.0),
            static_cast<int>(kAnnotationMaximumLineWidth * 100.0),
            [this](int value) { settings.style.lineWidth = value / 100.0; }, Change::Layout);
        lineOpacity = slider(
            translated("Line opacity"), lines, "annotateLineOpacity",
            static_cast<int>(kAnnotationMinimumLineOpacity * 100.0), 100,
            [this](int value) { settings.style.lineOpacity = value / 100.0; }, Change::Drawing);
        linesLayout->addWidget(lineWidth);
        linesLayout->addWidget(lineOpacity);
        fill = checkBox(
            translated("Fill objects with their colour"), lines, "annotateFill",
            [this](bool on) {
                settings.style.fillObjects = on;
                fillOpacity->setEnabled(on && settings.style.marker == AnnotationMarker::Outline);
            },
            Change::Drawing);
        fill->setToolTip(translated("Tints the area of every outlined galaxy, nebula and cluster; a region that spans "
                                    "much of the picture is left unfilled"));
        fillOpacity = slider(
            translated("Fill opacity"), lines, "annotateFillOpacity", 0, 100,
            [this](int value) { settings.style.fillOpacity = value / 100.0; }, Change::Drawing);
        linesLayout->addWidget(fill);
        linesLayout->addWidget(fillOpacity);
        gridOpacity = slider(
            translated("Grid opacity"), lines, "annotateGridOpacity", 0, 100,
            [this](int value) { settings.style.gridOpacity = value / 100.0; }, Change::Drawing);
        linesLayout->addWidget(gridOpacity);
        lineHalo = checkBox(
            translated("Dark edge behind lines"), lines, "annotateLineHalo",
            [this](bool on) { settings.style.lineHalo = on; }, Change::Drawing);
        lineHalo->setToolTip(translated("A soft edge in the halo colour behind every line, so it reads over a bright "
                                        "part of the image"));
        leaders = checkBox(
            translated("Leader lines for crowded labels"), lines, "annotateLeaders",
            [this](bool on) { settings.style.leaderLines = on; }, Change::Layout);
        unlabelled = checkBox(
            translated("Mark objects whose labels do not fit"), lines, "annotateUnlabelled",
            [this](bool on) { settings.style.unlabelledMarks = on; }, Change::Drawing);
        linesLayout->addWidget(lineHalo);
        linesLayout->addWidget(leaders);
        linesLayout->addWidget(unlabelled);
        linesSection->setContentLayout(linesLayout);
        linesSection->setExpanded(true);
        styleLayout->addWidget(linesSection);

        colorsSection = new AnCollapsibleSection(translated("Colours"), look);
        colorsSection->setObjectName(QStringLiteral("annotateColorsSection"));
        auto* colorsGrid = new QGridLayout();
        colorsGrid->setHorizontalSpacing(tokenSet.spacing.sm);
        colorsGrid->setVerticalSpacing(tokenSet.spacing.xs);
        const std::array<ColorRole, 14> roles{{{"Grid", &AnnotationColors::grid},
                                               {"Galaxies", &AnnotationColors::galaxy},
                                               {"Nebulae", &AnnotationColors::nebula},
                                               {"Planetary nebulae", &AnnotationColors::planetaryNebula},
                                               {"Open clusters", &AnnotationColors::openCluster},
                                               {"Globular clusters", &AnnotationColors::globularCluster},
                                               {"Other objects", &AnnotationColors::otherObject},
                                               {"Stars", &AnnotationColors::star},
                                               {"Text", &AnnotationColors::text},
                                               {"Halo", &AnnotationColors::halo},
                                               {"Compass and scale", &AnnotationColors::decoration},
                                               {"Panel", &AnnotationColors::panel},
                                               {"Panel text", &AnnotationColors::panelText},
                                               {"Accent", &AnnotationColors::accent}}};
        for (std::size_t index = 0u; index < roles.size(); ++index)
        {
            const ColorRole role = roles[index];
            auto* button = new QToolButton(colorsSection->contentWidget());
            button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            button->setText(translated(role.name));
            button->setIconSize(QSize(28, 16));
            button->setAutoRaise(true);
            button->setObjectName(QStringLiteral("annotateColor%1").arg(index));
            QObject::connect(button, &QToolButton::clicked, window, [this, role]() {
                auto* dialog = new QColorDialog(settings.style.colors.*role.member, window);
                dialog->setAttribute(Qt::WA_DeleteOnClose);
                dialog->setOption(QColorDialog::ShowAlphaChannel, true);
                dialog->setWindowTitle(translated("%1 colour").arg(translated(role.name)));
                QObject::connect(dialog, &QColorDialog::colorSelected, window,
                                 [this, role](const QColor& color) { setColor(role.member, color); });
                dialog->open();
            });
            colorButtons.emplace_back(button, role.member);
            colorsGrid->addWidget(button, static_cast<int>(index / 2u), static_cast<int>(index % 2u));
        }
        colorsSection->setContentLayout(colorsGrid);
        colorsSection->setExpanded(false);
        styleLayout->addWidget(colorsSection);
        styleSection->setContentLayout(styleLayout);
        styleSection->setExpanded(true);
        layoutColumn->addWidget(styleSection);

        // The panel and the corners.
        auto* panelSection = new AnCollapsibleSection(translated("Info Panel and Placement"), column);
        panelSection->setObjectName(QStringLiteral("annotatePanelSection"));
        QWidget* panel = panelSection->contentWidget();
        auto* panelLayout = new QVBoxLayout();
        panelLayout->setSpacing(tokenSet.spacing.sm);
        auto* panelForm = new QFormLayout();
        title = new QLineEdit(panel);
        title->setObjectName(QStringLiteral("annotateTitle"));
        title->setClearButtonEnabled(true);
        QObject::connect(title, &QLineEdit::textEdited, window, [this](const QString& text) {
            settings.content.title = text;
            changed(Change::Scene);
        });
        credit = new QLineEdit(panel);
        credit->setObjectName(QStringLiteral("annotateCredit"));
        credit->setClearButtonEnabled(true);
        credit->setPlaceholderText(translated("Your name, your observatory"));
        QObject::connect(credit, &QLineEdit::textEdited, window, [this](const QString& text) {
            settings.content.credit = text;
            changed(Change::Scene);
        });
        panelForm->addRow(translated("Title"), title);
        panelForm->addRow(translated("Credit"), credit);
        panelLayout->addLayout(panelForm);
        auto* facts = new QGridLayout();
        facts->setHorizontalSpacing(tokenSet.spacing.md);
        facts->setVerticalSpacing(tokenSet.spacing.xs);
        factCoordinates = checkBox(
            translated("Center"), panel, "annotateFactCenter",
            [this](bool on) { settings.content.facts.coordinates = on; }, Change::Scene);
        factField = checkBox(
            translated("Field and scale"), panel, "annotateFactField",
            [this](bool on) { settings.content.facts.field = on; }, Change::Scene);
        factOptics = checkBox(
            translated("Optics"), panel, "annotateFactOptics", [this](bool on) { settings.content.facts.optics = on; },
            Change::Scene);
        factCamera = checkBox(
            translated("Camera"), panel, "annotateFactCamera", [this](bool on) { settings.content.facts.camera = on; },
            Change::Scene);
        factFilter = checkBox(
            translated("Filter"), panel, "annotateFactFilter", [this](bool on) { settings.content.facts.filter = on; },
            Change::Scene);
        factDate = checkBox(
            translated("Date"), panel, "annotateFactDate", [this](bool on) { settings.content.facts.date = on; },
            Change::Scene);
        factIntegration = checkBox(
            translated("Integration"), panel, "annotateFactIntegration",
            [this](bool on) { settings.content.facts.integration = on; }, Change::Scene);
        facts->addWidget(factCoordinates, 0, 0);
        facts->addWidget(factField, 0, 1);
        facts->addWidget(factOptics, 1, 0);
        facts->addWidget(factCamera, 1, 1);
        facts->addWidget(factFilter, 2, 0);
        facts->addWidget(factDate, 2, 1);
        facts->addWidget(factIntegration, 3, 0);
        panelLayout->addLayout(facts);
        auto* corners = new QFormLayout();
        panelCorner = comboBox<AnnotationCorner>(
            panel, "annotatePanelCorner", cornerItems(),
            [this](AnnotationCorner value) { settings.style.panelCorner = value; }, Change::Layout);
        compassCorner = comboBox<AnnotationCorner>(
            panel, "annotateCompassCorner", cornerItems(),
            [this](AnnotationCorner value) { settings.style.compassCorner = value; }, Change::Layout);
        scaleBarCorner = comboBox<AnnotationCorner>(
            panel, "annotateScaleBarCorner", cornerItems(),
            [this](AnnotationCorner value) { settings.style.scaleBarCorner = value; }, Change::Layout);
        corners->addRow(translated("Panel"), panelCorner);
        corners->addRow(translated("Compass"), compassCorner);
        corners->addRow(translated("Scale bar"), scaleBarCorner);
        panelLayout->addLayout(corners);
        panelSection->setContentLayout(panelLayout);
        panelSection->setExpanded(false);
        layoutColumn->addWidget(panelSection);

        // The catalogues and limits a level of detail sets.
        auto* limitsSection = new AnCollapsibleSection(translated("Catalogues and Limits"), column);
        limitsSection->setObjectName(QStringLiteral("annotateLimitsSection"));
        QWidget* limits = limitsSection->contentWidget();
        auto* limitsLayout = new QVBoxLayout();
        limitsLayout->setSpacing(tokenSet.spacing.sm);
        auto* catalogues = new QGridLayout();
        catalogues->setHorizontalSpacing(tokenSet.spacing.md);
        catalogues->setVerticalSpacing(tokenSet.spacing.xs);
        messier = checkBox(
            translated("Messier"), limits, "annotateMessier",
            [this](bool on) { settings.content.deepSky.messier = on; }, Change::Scene);
        caldwell = checkBox(
            translated("Caldwell"), limits, "annotateCaldwell",
            [this](bool on) { settings.content.deepSky.caldwell = on; }, Change::Scene);
        ngc = checkBox(
            translated("NGC"), limits, "annotateNgc", [this](bool on) { settings.content.deepSky.ngc = on; },
            Change::Scene);
        ic = checkBox(
            translated("IC"), limits, "annotateIc", [this](bool on) { settings.content.deepSky.ic = on; },
            Change::Scene);
        namedOnly = checkBox(
            translated("Only named NGC and IC objects"), limits, "annotateNamedOnly",
            [this](bool on) { settings.content.deepSky.namedOnly = on; }, Change::Scene);
        catalogues->addWidget(messier, 0, 0);
        catalogues->addWidget(caldwell, 0, 1);
        catalogues->addWidget(ngc, 1, 0);
        catalogues->addWidget(ic, 1, 1);
        catalogues->addWidget(namedOnly, 2, 0, 1, 2);
        limitsLayout->addLayout(catalogues);
        auto* kinds = new QGridLayout();
        kinds->setHorizontalSpacing(tokenSet.spacing.md);
        kinds->setVerticalSpacing(tokenSet.spacing.xs);
        galaxies = checkBox(
            translated("Galaxies"), limits, "annotateGalaxies",
            [this](bool on) { settings.content.deepSky.galaxies = on; }, Change::Scene);
        nebulae = checkBox(
            translated("Nebulae"), limits, "annotateNebulae",
            [this](bool on) { settings.content.deepSky.nebulae = on; }, Change::Scene);
        planetaries = checkBox(
            translated("Planetary nebulae"), limits, "annotatePlanetaries",
            [this](bool on) { settings.content.deepSky.planetaryNebulae = on; }, Change::Scene);
        openClusters = checkBox(
            translated("Open clusters"), limits, "annotateOpenClusters",
            [this](bool on) { settings.content.deepSky.openClusters = on; }, Change::Scene);
        globulars = checkBox(
            translated("Globular clusters"), limits, "annotateGlobulars",
            [this](bool on) { settings.content.deepSky.globularClusters = on; }, Change::Scene);
        others = checkBox(
            translated("Other entries"), limits, "annotateOthers",
            [this](bool on) { settings.content.deepSky.otherObjects = on; }, Change::Scene);
        kinds->addWidget(galaxies, 0, 0);
        kinds->addWidget(nebulae, 0, 1);
        kinds->addWidget(planetaries, 1, 0);
        kinds->addWidget(openClusters, 1, 1);
        kinds->addWidget(globulars, 2, 0);
        kinds->addWidget(others, 2, 1);
        limitsLayout->addLayout(kinds);
        auto* limitsForm = new QFormLayout();
        const QLocale dotLocale(QLocale::C);
        magnitude = new QDoubleSpinBox(limits);
        magnitude->setObjectName(QStringLiteral("annotateMagnitude"));
        magnitude->setLocale(dotLocale);
        magnitude->setRange(-2.0, 20.0);
        magnitude->setSingleStep(0.5);
        magnitude->setDecimals(1);
        QObject::connect(magnitude, &QDoubleSpinBox::valueChanged, window, [this](const double value) {
            if (syncing)
            {
                return;
            }
            settings.content.deepSky.magnitudeLimit = value;
            changed(Change::Scene);
        });
        anyMagnitude = checkBox(
            translated("Any"), limits, "annotateAnyMagnitude",
            [this](bool on) {
                settings.content.deepSky.magnitudeLimit = on ? kAnnotationNoMagnitudeLimit : magnitude->value();
                magnitude->setEnabled(!on);
            },
            Change::Scene);
        auto* magnitudeRow = new QHBoxLayout();
        magnitudeRow->addWidget(magnitude, 1);
        magnitudeRow->addWidget(anyMagnitude);
        limitsForm->addRow(translated("Faintest magnitude"), magnitudeRow);
        unmeasured = checkBox(
            translated("Objects of no measured magnitude"), limits, "annotateUnmeasured",
            [this](bool on) { settings.content.deepSky.unmeasured = on; }, Change::Scene);
        limitsForm->addRow(QString(), unmeasured);
        minimumSize = new QDoubleSpinBox(limits);
        minimumSize->setObjectName(QStringLiteral("annotateMinimumSize"));
        minimumSize->setLocale(dotLocale);
        minimumSize->setRange(0.0, 5.0);
        minimumSize->setSingleStep(0.05);
        minimumSize->setDecimals(2);
        minimumSize->setSuffix(QStringLiteral(" %"));
        minimumSize->setToolTip(
            translated("The smallest object marked, as a share of the image's size; Messier, Caldwell and "
                       "named objects are marked whatever their size"));
        QObject::connect(minimumSize, &QDoubleSpinBox::valueChanged, window, [this](const double value) {
            if (syncing)
            {
                return;
            }
            settings.content.deepSky.minimumSize = value / 100.0;
            changed(Change::Scene);
        });
        limitsForm->addRow(translated("Smallest object"), minimumSize);
        starMagnitude = new QDoubleSpinBox(limits);
        starMagnitude->setObjectName(QStringLiteral("annotateStarMagnitude"));
        starMagnitude->setLocale(dotLocale);
        starMagnitude->setRange(-2.0, kAnnotationFaintestStar);
        starMagnitude->setSingleStep(0.5);
        starMagnitude->setDecimals(1);
        QObject::connect(starMagnitude, &QDoubleSpinBox::valueChanged, window, [this](const double value) {
            if (syncing)
            {
                return;
            }
            settings.content.stars.magnitudeLimit = value;
            changed(Change::Scene);
        });
        limitsForm->addRow(translated("Faintest star"), starMagnitude);
        starNaming = comboBox<AnnotationStarNaming>(
            limits, "annotateStarNaming",
            {{translated("Proper names"), AnnotationStarNaming::ProperNames},
             {translated("Names and designations"), AnnotationStarNaming::Designations},
             {translated("Every star, by number when unnamed"), AnnotationStarNaming::CatalogueNumbers}},
            [this](AnnotationStarNaming value) { settings.content.stars.naming = value; }, Change::Scene);
        starNaming->setToolTip(translated("Designations are Bayer letters and Flamsteed numbers, such as α Ori and 61 "
                                          "Cyg; a star with neither is named by its catalogue number, such as HIP "
                                          "27989"));
        limitsForm->addRow(translated("Star labels"), starNaming);
        markUnnamed = checkBox(
            translated("Mark the stars left unnamed"), limits, "annotateMarkUnnamed",
            [this](bool on) { settings.content.stars.markUnnamed = on; }, Change::Scene);
        limitsForm->addRow(QString(), markUnnamed);
        gridDensity = comboBox<AnnotationGridDensity>(
            limits, "annotateGridDensity",
            {{translated("Coarse"), AnnotationGridDensity::Coarse},
             {translated("Normal"), AnnotationGridDensity::Normal},
             {translated("Fine"), AnnotationGridDensity::Fine}},
            [this](AnnotationGridDensity value) { settings.content.gridDensity = value; }, Change::Scene);
        limitsForm->addRow(translated("Grid spacing"), gridDensity);
        gridLabels = checkBox(
            translated("Label the grid at the edges"), limits, "annotateGridLabels",
            [this](bool on) { settings.content.gridLabels = on; }, Change::Layout);
        limitsForm->addRow(QString(), gridLabels);
        limitsLayout->addLayout(limitsForm);
        limitsSection->setContentLayout(limitsLayout);
        limitsSection->setExpanded(false);
        layoutColumn->addWidget(limitsSection);

        // The picture made.
        auto* outputSection = new AnCollapsibleSection(translated("Output"), column);
        outputSection->setObjectName(QStringLiteral("annotateOutputSection"));
        QWidget* output = outputSection->contentWidget();
        auto* outputForm = new QFormLayout();
        outputSize = new QComboBox(output);
        outputSize->setObjectName(QStringLiteral("annotateOutputSize"));
        QObject::connect(outputSize, &QComboBox::currentIndexChanged, window, [this](const int) {
            if (syncing || outputSize->currentIndex() < 0)
            {
                return;
            }
            settings.output.longEdge = outputSize->currentData().toUInt();
            changed(Change::Output);
        });
        outputForm->addRow(translated("Size"), outputSize);
        stretch = comboBox<AnnotationStretch>(
            output, "annotateStretch",
            {{translated("As the view shows it"), AnnotationStretch::AsShown},
             {translated("As stored, unstretched"), AnnotationStretch::AsStored}},
            [this](AnnotationStretch value) { settings.output.stretch = value; }, Change::Base);
        outputForm->addRow(translated("Image"), stretch);
        background = comboBox<AnnotationBackground>(
            output, "annotateBackground",
            {{translated("The image"), AnnotationBackground::Image},
             {translated("Transparent: annotations only"), AnnotationBackground::Transparent}},
            [this](AnnotationBackground value) { settings.output.background = value; }, Change::Base);
        outputForm->addRow(translated("Background"), background);
        outputSection->setContentLayout(outputForm);
        outputSection->setExpanded(true);
        layoutColumn->addWidget(outputSection);

        layoutColumn->addStretch(1);
        return column;
    }
};

AnnotateImageControl::AnnotateImageControl(ImageViewControl* viewControl, QWidget* parent)
    : QDialog(parent ? parent : viewControl), ImageViewControlListener(viewControl), m_impl(std::make_unique<Impl>())
{
    setAttribute(Qt::WA_DeleteOnClose);
    setModal(false);
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    Impl& impl = *m_impl;
    impl.window = this;
    impl.view = viewControl;
    if (auto* store = process_utils::currentGlobalSettings(); store != nullptr)
    {
        impl.settings =
            annotationSettingsFromStored(store->select(QString::fromLatin1(SettingsKeys::ANNOTATION_SETTINGS)));
    }

    const auto& tokenSet = an::ui::tokens();
    setWindowTitle(viewControl != nullptr
                       ? translated("Annotate Image <%1>").arg(QString::fromStdString(viewControl->getUniqueName()))
                       : translated("Annotate Image"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(tokenSet.spacing.lg, tokenSet.spacing.lg, tokenSet.spacing.lg, tokenSet.spacing.lg);
    root->setSpacing(tokenSet.spacing.md);

    auto* heading = new QLabel(translated("Annotate Image"), this);
    heading->setFont(tokenSet.typography.heading1);
    root->addWidget(heading);
    auto* description =
        new QLabel(translated("Name the objects, stars and coordinates of the solved image on a picture of it, "
                              "and export the picture or open it as an image."),
                   this);
    description->setWordWrap(true);
    description->setProperty("anStatusTone", QStringLiteral("secondary"));
    root->addWidget(description);

    impl.banner = new AnInfoBanner(QString(), AnInfoBanner::Tone::Info, this);
    impl.banner->setObjectName(QStringLiteral("annotateBanner"));
    impl.banner->setVisible(false);
    root->addWidget(impl.banner);

    auto* body = new QHBoxLayout();
    body->setSpacing(tokenSet.spacing.lg);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(impl.buildChoices(scroll));
    scroll->setMinimumWidth(360);
    scroll->setMaximumWidth(420);
    body->addWidget(scroll);

    auto* previewColumn = new QVBoxLayout();
    previewColumn->setSpacing(tokenSet.spacing.xs);
    impl.preview = new AnnotationPreview(this);
    impl.preview->setObjectName(QStringLiteral("annotatePreview"));
    impl.preview->setToolTip(translated("Scroll to zoom, drag to move, double-click to zoom in or fit"));
    impl.preview->setMessage(translated("Reading the image…"));
    previewColumn->addWidget(impl.preview, 1);
    impl.summary = new QLabel(this);
    impl.summary->setObjectName(QStringLiteral("annotateSummary"));
    impl.summary->setProperty("anStatusTone", QStringLiteral("secondary"));
    previewColumn->addWidget(impl.summary);
    body->addLayout(previewColumn, 1);
    root->addLayout(body, 1);

    auto* buttons = new QHBoxLayout();
    auto* reset = new QPushButton(translated("Reset"), this);
    reset->setObjectName(QStringLiteral("annotateReset"));
    reset->setProperty("anProcessButtonRole", QStringLiteral("secondary"));
    reset->setToolTip(translated("Go back to the default choices; the credit and your own colours are kept"));
    buttons->addWidget(reset);
    buttons->addStretch(1);
    impl.openButton = new QPushButton(translated("Open as Image"), this);
    impl.openButton->setObjectName(QStringLiteral("annotateOpen"));
    impl.openButton->setProperty("anProcessButtonRole", QStringLiteral("secondary"));
    impl.openButton->setToolTip(translated("Draw the picture and open it in a view of its own"));
    buttons->addWidget(impl.openButton);
    impl.exportButton = new QPushButton(translated("Export…"), this);
    impl.exportButton->setObjectName(QStringLiteral("annotateExport"));
    impl.exportButton->setProperty("anProcessButtonRole", QStringLiteral("primary"));
    impl.exportButton->setToolTip(translated("Draw the picture and write it to a PNG, JPEG or TIFF file"));
    impl.exportButton->setDefault(true);
    buttons->addWidget(impl.exportButton);
    auto* close = new QPushButton(translated("Close"), this);
    close->setObjectName(QStringLiteral("annotateClose"));
    close->setProperty("anProcessButtonRole", QStringLiteral("secondary"));
    buttons->addWidget(close);
    root->addLayout(buttons);

    impl.drawingTimer = new QTimer(this);
    impl.drawingTimer->setSingleShot(true);
    impl.drawingTimer->setInterval(60);
    connect(impl.drawingTimer, &QTimer::timeout, this, [this]() { m_impl->requestDrawing(); });
    impl.baseTimer = new QTimer(this);
    impl.baseTimer->setSingleShot(true);
    impl.baseTimer->setInterval(250);
    connect(impl.baseTimer, &QTimer::timeout, this, [this]() { m_impl->requestBase(false); });
    impl.saveTimer = new QTimer(this);
    impl.saveTimer->setSingleShot(true);
    impl.saveTimer->setInterval(500);
    connect(impl.saveTimer, &QTimer::timeout, this, [this]() { m_impl->save(); });

    impl.preview->wantsResolution = [this](const std::uint32_t edge) {
        Impl& state = *m_impl;
        if (edge > state.baseEdge + state.baseEdge / 4u || state.base == nullptr)
        {
            state.wantedEdge = (std::max)(edge, kFirstPreviewEdge);
            state.baseTimer->start();
        }
    };
    connect(reset, &QPushButton::clicked, this, [this]() {
        Impl& state = *m_impl;
        const QString keptCredit = state.settings.content.credit;
        const std::optional<AnnotationColors> keptColors = state.settings.style.customColors;
        state.settings = defaultAnnotationSettings();
        state.settings.content.credit = keptCredit;
        state.settings.style.customColors = keptColors;
        state.showSettings();
        state.changed(Change::Scene);
        state.changed(Change::Base);
    });
    connect(impl.exportButton, &QPushButton::clicked, this, [this]() {
        Impl& state = *m_impl;
        const bool transparent = state.settings.output.background == AnnotationBackground::Transparent;
        const QString filter =
            transparent ? translated("PNG image (*.png);;TIFF image (*.tif *.tiff)")
                        : translated("PNG image (*.png);;JPEG image (*.jpg *.jpeg);;TIFF image (*.tif *.tiff)");
        const QString name =
            (state.imageName.isEmpty() ? translated("annotated") : state.imageName + translated("_annotated")) +
            (transparent ? QStringLiteral(".png") : QStringLiteral(".jpg"));
        // The folder the last picture went to, remembered as the application remembers its other folders.
        auto* context = an::ui::appContext();
        an::ui::IPathMemoryService* memory = context != nullptr ? context->getPathMemoryService() : nullptr;
        const QString folder = memory != nullptr ? memory->selectPath(QStringLiteral("__ANNOTATE_IMAGE__")) : QString();
        const QString suggested = folder.isEmpty() ? name : QDir(folder).filePath(name);
        QString chosenFilter = transparent ? translated("PNG image (*.png)") : translated("JPEG image (*.jpg *.jpeg)");
        const QString path =
            an::ui::dialogs::getSaveFileName(process_utils::currentGlobalSettings(), this,
                                             translated("Export Annotated Image"), suggested, filter, &chosenFilter);
        if (!path.isEmpty())
        {
            if (memory != nullptr)
            {
                memory->upsertPath(QStringLiteral("__ANNOTATE_IMAGE__"), QFileInfo(path).absolutePath());
            }
            exportTo(path);
        }
    });
    connect(impl.openButton, &QPushButton::clicked, this, [this]() { openAsImage(); });
    connect(close, &QPushButton::clicked, this, &QDialog::close);

    impl.showSettings();
    impl.preview->setStyle(impl.settings.style);
    resize(1320, 860);
    setMinimumSize(980, 640);
    impl.refreshFromImage();
}

AnnotateImageControl::~AnnotateImageControl()
{
    m_impl->closing.request_stop();
    if (m_impl->saveTimer != nullptr && m_impl->saveTimer->isActive())
    {
        m_impl->save();
    }
}

void AnnotateImageControl::onImageLoaded(ImageViewControl* /*ctrl*/)
{
    m_impl->refreshFromImage();
}

void AnnotateImageControl::onImageUpdated(ImageViewControl* /*ctrl*/)
{
    m_impl->refreshFromImage();
}

void AnnotateImageControl::onImageRenamed(ImageViewControl* ctrl)
{
    if (ctrl != nullptr)
    {
        m_impl->imageName = QString::fromStdString(ctrl->getUniqueName());
        setWindowTitle(translated("Annotate Image <%1>").arg(m_impl->imageName));
    }
}

void AnnotateImageControl::onImageViewControlDestroyed(ImageViewControl* ctrl)
{
    ImageViewControlListener::onImageViewControlDestroyed(ctrl);
    m_impl->view = nullptr;
    m_impl->closing.request_stop();
    close();
}

AnnotationSettings AnnotateImageControl::settings() const
{
    return m_impl->settings;
}

void AnnotateImageControl::setSettings(const AnnotationSettings& settings)
{
    Impl& impl = *m_impl;
    impl.settings = settings;
    impl.showSettings();
    impl.preview->setStyle(settings.style);
    impl.changed(Change::Scene);
    impl.changed(Change::Base);
}

void AnnotateImageControl::setCatalogSource(BundledCatalogSource source)
{
    m_impl->catalogSource = std::move(source);
    m_impl->catalogs.reset();
    m_impl->requestCatalogs();
}

bool AnnotateImageControl::isWorking() const
{
    return m_impl->working() || m_impl->drawingTimer->isActive() || m_impl->pendingPicture.has_value();
}

std::shared_ptr<const AnnotationScene> AnnotateImageControl::scene() const
{
    return m_impl->scene;
}

std::shared_ptr<const AnnotationLayout> AnnotateImageControl::layout() const
{
    return m_impl->layout;
}

void AnnotateImageControl::exportTo(const QString& path)
{
    m_impl->makePicture(false, path);
}

void AnnotateImageControl::openAsImage()
{
    m_impl->makePicture(true, QString());
}

QString AnnotateImageControl::lastOutcome() const
{
    return m_impl->lastOutcome;
}

std::size_t AnnotateImageControl::picturesMade() const
{
    return m_impl->picturesMade;
}

void AnnotateImageControl::closeEvent(QCloseEvent* event)
{
    m_impl->closing.request_stop();
    m_impl->save();
    QDialog::closeEvent(event);
}

} // namespace an::ui
