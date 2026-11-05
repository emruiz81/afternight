// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QPointF>
#include <QtWidgets/QMainWindow>

#include "controls/ImageViewControlListener.h"
#include "session/DerivedResultKey.h"
#include "widgets/ImageChannelViewMode.h"
#include "widgets/ImageViewWidgetDisplayModule.h"
#include "widgets/ViewPresentation.h"

#include <AfterNightEngine/Algorithms/Presentation/DisplayPresentation.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class ImageViewControl;
class QPainter;

namespace an::ui
{
class DisplayProxy;
class PresentedImage;
} // namespace an::ui

enum class ImageViewSelectionStyle : unsigned char
{
    CROP_STYLE = 0,
    AREA_SELECTION_STYLE,
    PREVIEW_STYLE
};

class AutostretchParams
{
public:
    double targetBackground = 0.25;
    double shadowsClipping = -2.8;
    bool linkedChannels = false;
    bool neutralizeBackground = false;
    bool colorCalibration = false;

    double saturation = 0.0;
    double saturationBackground = 1.0;

    int brightness = 0;
    int contrast = 0;

    bool operator==(const AutostretchParams&) const = default;
};

/// What a view's presentations came to since the widget was made: how many were asked for, painted, refused as stale,
/// failed and coalesced into one already on its way. Painted plus discarded plus failed plus coalesced is at most
/// requested; the difference is still on its way.
struct ImageViewPresentationTelemetry
{
    /// Presentations asked for.
    std::uint64_t requested = 0u;

    /// Presentations painted.
    std::uint64_t committed = 0u;

    /// Presentations that came back for a request no longer current - superseded, cancelled, its document closed or
    /// changed - and were discarded without a paint.
    std::uint64_t discarded = 0u;

    /// Presentations the engine refused.
    std::uint64_t failed = 0u;

    /// Requests that asked for the presentation already on its way, which paints for both.
    std::uint64_t coalesced = 0u;

    /// Mask layers painted.
    std::uint64_t masksCommitted = 0u;

    /// Mask layers discarded as stale.
    std::uint64_t masksDiscarded = 0u;

    /// Presentations the engine ran on a worker, whatever became of them.
    std::uint64_t presented = 0u;

    /// How long the engine took to present those, in all: the presentation itself, from its admission to its bytes.
    std::chrono::nanoseconds presentTime{0};

    /// The longest the engine took to present one.
    std::chrono::nanoseconds longestPresent{0};

    /// Pixmaps made from presented bytes on the GUI thread: the quick-zoom lens's, of the small region under it, and
    /// of the mask layer there. The paint draws the presented bytes themselves and makes none.
    std::uint64_t pixmapsMade = 0u;

    /// How long the GUI thread took to make those pixmaps, in all.
    std::chrono::nanoseconds pixmapTime{0};

    /// How long the GUI thread took to adopt the presentations it painted, in all: the bytes wrapped for the paint,
    /// and whatever else the new bytes asked of it.
    std::chrono::nanoseconds adoptTime{0};

    /// The longest the GUI thread took to adopt one presentation.
    std::chrono::nanoseconds longestAdopt{0};

    /// How many of the image's pixels, in each direction, one pixel of the presentation painted last stands for: one
    /// when every pixel was presented, the display proxy's factor when it was presented.
    std::uint32_t presentedFactor = 1u;

    /// Display proxies of the view's image or mask reduced on a worker for a presentation; one kept for a revision and
    /// factor is not reduced again.
    std::uint64_t proxiesReduced = 0u;

    /// How long those reductions took, in all.
    std::chrono::nanoseconds reduceTime{0};

    /// The longest one reduction took.
    std::chrono::nanoseconds longestReduce{0};

    /// Presentations of part of the image, or of its proxy, that measured the whole image to stretch it as the whole
    /// image is, because no analysis of the revision was kept or handed. The quick-zoom lens's presentation, made on
    /// the GUI thread, is counted here too should it ever measure.
    std::uint64_t wholeImageMeasured = 0u;

    /// Quick-zoom lenses presented from the image's own pixels under them, on the GUI thread, through the analysis the
    /// view painted with; every other lens magnifies what the view painted.
    std::uint64_t lensesOfTheImage = 0u;

    /// How long those measurements took, in all; part of `presentTime`.
    std::chrono::nanoseconds wholeImageMeasureTime{0};

    /// Presentations made elsewhere - in a real-time preview's frame - that the view painted as they came, presenting
    /// nothing itself (`adoptPresentation`). Not among `requested` or `committed`.
    std::uint64_t adopted = 0u;

    /// Presentations of a part of what the view's document shows becoming, made in a real-time preview's frame, that
    /// the view painted as a layer over its own (`adoptPresentationLayer`). Not among `requested`, `committed` or
    /// `adopted`.
    std::uint64_t layersAdopted = 0u;
};

/// The widget a view paints its image in: the presentation of the view's document, the overlay of its mask, the
/// display modules and the selection, at the view's zoom; and the quick-zoom lens over it.
///
/// What it paints is presented by the engine on a worker and published on the GUI thread only when the request it was
/// made for is still current, so a slow presentation of a superseded state never replaces a newer one; it never waits
/// on the GUI thread for one.
class AFTERNIGHTCONTROLS_API ImageViewWidget : public QWidget, public ImageViewControlListener
{
public:
    ImageViewWidget(ImageViewControl* control);

    void displayMainImage();

    /// Shows one channel or colour component of the image, or the chrominance view.
    void displayChannel(an::ui::ViewChannel channel);

    /// What is shown of the image, as asked.
    [[nodiscard]] an::ui::ViewChannel displayedChannel() const;

    void refreshDisplayImage();

    void fitInView();

    void zoomIn();

    void zoomOut();

    void zoom100();

    void scaleImageView(double factor, const bool absolute = false, bool forceUpdate = false);

    void setViewMode(ImageViewMode mode);

    double getCurrentZoom();

    double getCurrentScaleFactor();

    ImageViewMode getViewMode() const;

    ImageChannelViewMode getChannelViewMode() const;

    void onPreImageLoaded(ImageViewControl* ctrl) override;

    void onImageLoaded(ImageViewControl* ctrl) override;

    void onPreImageUpdated(ImageViewControl* ctrl) override;

    void onImageUpdated(ImageViewControl* ctrl) override;

    void onMaskUpdated(ImageViewControl* ctrl) override;

    void enableSelectionArea(bool enable);

    bool isSelectionAreaEnabled() const;

    void updateSelectionArea(const QRect& rect);

    void resetSelectionArea();

    QRect getCurrentImageArea();
    QRect getCurrentScaledImageArea();

    QRect getCurrentSelectionArea() const;
    QRect getCurrentScaledSelectionArea() const;

    bool changeSelectionAreaX(int xpos);
    bool changeSelectionAreaWidth(int width);
    bool changeSelectionAreaY(int ypos);
    bool changeSelectionAreaHeight(int height);

    bool moveSelectionArea(int xpos, int ypos);

    void setImageViewSelectionStyle(ImageViewSelectionStyle style);

    void setSelectionAreaText(const QString& text);

    QString getSelectionAreaText() const;

    void clearSelectionAreaText();

    ImageViewSelectionStyle getImageViewSelectionStyle() const;

    void enableRotationPreview(const bool enable);
    bool isRotationPreviewModeEnabled() const;
    void setRotationPreviewAngle(const double angle);
    void setRotationPreviewResizeArea(const bool enable);

    QSize getCurrentRotatedAreaSize();
    QSize getCurrentRotatedScaledAreaSize();

    void enableQuickZoom(const bool enable);
    bool isQuickZoomEnabled() const;

    void enableAlternateImageRendering(const bool enable);

    bool isAlternateImageRenderEnabled() const;

    using AlternateImageRenderingChangedCallback = std::function<void(bool enabled)>;
    void setAlternateImageRenderingChangedCallback(AlternateImageRenderingChangedCallback callback);

    void addDisplayModule(ImageViewWidgetDisplayModule* module);

    void removeDisplayModule(ImageViewWidgetDisplayModule* module);

    size_t getNumOfDisplayModules() const;

    bool isContextMenuSupported() const;

    /// Marks the view as one whose image changes many times a second, a real-time preview's or a playing sequence's.
    /// Every view presents at the size it paints whatever this says; it is kept for the view's callers.
    void enablePerformanceMode(bool enabled);

    bool isPerformanceModeEnabled() const;

    void enableAutostretchStatisticsCaching(bool enabled);

    bool isAutostretchStatisticsCachingEnabled() const;

    /// The presented bytes painted now, or null before the first presentation lands.
    [[nodiscard]] std::shared_ptr<const an::ui::PresentedImage> presentedImage() const;

    /// Asks for the document's own presentation when what is painted came from a real-time preview's frame - adopted
    /// as the presentation (`adoptPresentation`) or painted as a layer over it (`adoptPresentationLayer`) - and does
    /// nothing when the view paints its own presentation. The frame's bytes stay painted until the document's land.
    /// A preview that goes back to showing its target itself calls it, since its frames replaced nothing in the
    /// document that a request would otherwise notice.
    void presentDocumentAgain();

    /// The part of the image, in its own pixels, the presented bytes painted now cover: the whole image, or the region
    /// around what the view shows; empty before the first presentation lands.
    [[nodiscard]] QRect presentedRegion() const;

    /// The mask layer painted now, presented with the image over the same part of it at the same reduction, or null.
    [[nodiscard]] std::shared_ptr<const an::ui::PresentedImage> maskLayer() const;

    /// The balance and statistics the presentation painted now was made through - the view's own measurement or the
    /// analysis it was handed - or nothing before the first presentation lands.
    [[nodiscard]] std::optional<an::DisplayPresentationAnalysis> presentedAnalysis() const;

    /// The analysis the view keeps of a revision of an asset it showed whole, measured under a presentation's request:
    /// what `analyzeForView` measures of that revision under the same request, for a real-time preview that stretches
    /// its frames by the image its view showed to take over rather than measure again. Read on the GUI thread.
    ///
    /// @param revision the revision of the asset the analysis is of.
    /// @param settings what it was measured under: their request and their channel are read.
    /// @return the analysis, or nothing when the view keeps none of that revision under that request and channel.
    [[nodiscard]] std::optional<an::DisplayPresentationAnalysis>
    keptAnalysisOf(const an::consumer::AssetRevision& revision, const an::ui::ViewPresentationSettings& settings) const;

    /// The size of the image shown, in its own pixels.
    [[nodiscard]] QSize imageSize() const;

    void setQuickZoomDelayMs(const int ms);
    int getQuickZoomDelayMs() const;

    /// Overrides what the quick-zoom lens magnifies: these presented bytes instead of the view's own.
    void setQuickZoomImageOverride(std::shared_ptr<const an::ui::PresentedImage> image);
    void clearQuickZoomImageOverride();

    /// Cancel any active quick-zoom lens interaction (hides the lens widget).
    void cancelQuickZoom();

    /// Sets the stretch and adjustments the view shows, and asks for a new presentation. Never waits: `async` only
    /// says whether the request is made now or from the event loop.
    void setAutostretchSettings(const AutostretchParams& params, bool async = false);

    AutostretchParams getAutostretchSettings() const;

    /// The engine's presentation request the view presents through now: the stretch and adjustments it shows, decided
    /// from its mode, its settings and its output. Read on the GUI thread.
    [[nodiscard]] an::DisplayPresentationRequest presentationRequest() const;

    /// Asks for a new presentation of the view's image. On the GUI thread the request is issued at once; from any other
    /// thread it is queued to the GUI thread. The paint follows when the engine has presented, if the request is still
    /// current then.
    ///
    /// @param recomputeStatistics kept for its callers: a kept analysis is set aside exactly when what it was measured
    ///        from or under changes - another revision, channel, promotion or balance - which its key decides.
    /// @param refreshDisplay kept for its callers: every presentation refreshes the display.
    void imageUpdated(bool recomputeStatistics, bool refreshDisplay);

    /// `imageUpdated` from any thread, always through the event loop, never waited on.
    void imageUpdatedAsync(bool recomputeStatistics, bool refreshDisplay);

    /// The analysis the next presentation is made through instead of measuring, when it fits the presentation.
    ///
    /// @param analysis the analysis, or nothing to measure.
    /// @param autostretchReady whether it was taken after the colour balance the view asks for.
    void setPrecomputedAnalysis(std::optional<an::DisplayPresentationAnalysis> analysis, bool autostretchReady = false);

    /// What a presentation of the view's own document made now would be made with - the stretch, the channel, the
    /// reduction and the region - when the view would paint one made elsewhere: it shows its document, not the
    /// alternate image, with no mask layer and no rotation preview, and it is shown with an image size it knows.
    /// Read on the GUI thread, when a real-time preview issues a frame that presents its own result.
    ///
    /// @return the settings, or nothing when the view presents what it shows itself.
    [[nodiscard]] std::optional<an::ui::ViewPresentationSettings> framePresentationSettings() const;

    /// The part of the image the view shows now, in the image's own pixels: what its viewport lays over the image at
    /// the zoom, every pixel any part of which is visible. A real-time preview's frame over the part shown runs over
    /// it. Read on the GUI thread.
    ///
    /// @return the part, or nothing when the view is not shown, has no image size or no viewport.
    [[nodiscard]] std::optional<an::ui::ViewRegion> shownImageRegion() const;

    /// Paints a presentation a real-time preview's frame made of what the view's document holds now - or, for a frame
    /// that ran over a reduction of the target, of what it shows the document becoming - as the view's own would be
    /// painted, without presenting anything: the presentation is filed under the key a request of the same settings
    /// would make now, so such a request presents nothing again, and a presentation on its way for what this replaces
    /// is dropped when it lands. Called on the GUI thread.
    ///
    /// @param settings what the presentation was made with, as `framePresentationSettings` stated them for the frame.
    /// @param outcome the presentation.
    /// @param proxy the display proxy it was presented from, kept for the presentations of the same revision to come;
    ///        or null.
    /// @return whether the view paints it: false when the view would make another presentation now - other settings,
    ///         the alternate image or a mask layer shown, no open document - and then presents itself as it would.
    bool adoptPresentation(const an::ui::ViewPresentationSettings& settings,
                           const an::ui::ViewPresentationOutcome& outcome,
                           std::shared_ptr<const an::ui::DisplayProxy> proxy);

    /// Whether what the view paints now is its own presentation of what its document holds under these settings, with
    /// nothing on its way to replace it: then a real-time preview's frame over a part of the document may be shown as
    /// a layer over it (`adoptPresentationLayer`). Read on the GUI thread.
    ///
    /// @param settings what a frame would be issued under, as `framePresentationSettings` states them.
    /// @return whether the view paints the document's presentation under those settings now.
    [[nodiscard]] bool paintsDocumentAs(const an::ui::ViewPresentationSettings& settings) const;

    /// Whether what the view paints now is its own presentation of what its document holds under these settings - the
    /// stretch, the channel and the reduction - whatever region of the image it was made for, covering a part of the
    /// image, with nothing on its way to replace it: then a real-time preview's frame of that part may be shown as a
    /// layer over it (`adoptPresentationLayer`). Read on the GUI thread.
    ///
    /// @param settings what a frame would be issued under, as `framePresentationSettings` states them; their region is
    ///        not read.
    /// @param part the part of the image, in its own pixels, the frame shows.
    /// @return whether the view paints the document's presentation over the part under those settings now.
    [[nodiscard]] bool paintsDocumentOver(const an::ui::ViewPresentationSettings& settings, const QRect& part) const;

    /// Paints a presentation a real-time preview's frame made of a part of what the view's document shows becoming -
    /// the preview area's new patch - over what the view paints, as a layer of its own, until the view paints another
    /// presentation of its document; the document and what it paints of it are left as they are. Called on the GUI
    /// thread.
    ///
    /// @param settings what the frame was issued under, as `framePresentationSettings` stated them.
    /// @param outcome the part's presentation, its cover the part of the document it shows in the document's own
    ///        pixels.
    /// @return whether the view paints it: false when what it paints now is not its document's presentation under
    ///         those settings over the part, as `paintsDocumentOver` decides.
    bool adoptPresentationLayer(const an::ui::ViewPresentationSettings& settings,
                                const an::ui::ViewPresentationOutcome& outcome);

    /// The layer a real-time preview's frame of a part of the document is painted in now, or null.
    [[nodiscard]] std::shared_ptr<const an::ui::PresentedImage> presentationLayer() const;

    /// The part of the image, in its own pixels, the layer covers; empty while there is none.
    [[nodiscard]] QRect presentationLayerRegion() const;

    /// What the presentations came to so far.
    [[nodiscard]] ImageViewPresentationTelemetry presentationTelemetry() const;

    /// Whether no presentation or mask layer is on its way.
    [[nodiscard]] bool isPresentationIdle() const;

    ~ImageViewWidget();

private:
    void paintEvent(QPaintEvent* evt) override;
    void changeEvent(QEvent* ev) override;
    bool event(QEvent* ev) override;
    void leaveEvent(QEvent* ev) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

    /// Keeps an analysis for the key it was measured under, unless it measured nothing and one measured under the same
    /// key is kept already.
    void keepAnalysis(const an::ui::DerivedResultKey& key, const an::DisplayPresentationAnalysis& analysis);

    void mousePressEvent(QMouseEvent* ev) override;
    void mouseReleaseEvent(QMouseEvent* ev) override;
    void mouseMoveEvent(QMouseEvent* ev) override;

    void drawSelectionArea(QPainter& painter);

    void requestPresentation();
    void requestMaskOverlay();
    void clearPresentation();

    /// Lets a frame's layer of a part of the document go.
    void clearPresentationLayer();

    struct PresentationJob;
    struct PresentationResult;
    void commitPresentation(const PresentationJob& job, const PresentationResult& result);

    /// What a presentation made now would present: the proxy's factor and the region.
    [[nodiscard]] an::ui::ViewDisplayGeometry displayGeometry() const;

    /// Presents again when what the view shows has moved beyond what will be painted: the presentation on its way,
    /// which lands over what is presented, or what is presented when nothing is on its way.
    void onViewportMoved();
    void watchViewport();

    void updateImageScale();

    void checkSelectionAreaBoundaries();

    void updateQuickZoomWidget(const QPoint& mousePos, const bool useOriginalimg);

    void showImageLensWidget();

    void applyThemePalette();

    QSize getRenderImageSize() const;
    QPointF mapWidgetPointToImagePixel(const QPoint& pos) const;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
