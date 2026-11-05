// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The static preview an image list shows its selected file in, made through the library as a list makes it: a read
/// on a worker that asks no question, a mosaic shown as recorded or demosaiced by the fast method for its shape, a
/// failure told to the view's listeners, the Calibrated toggle - shown and enabled independently, unchecked when its
/// masters go, calibrating the light by every master and setting its configuration states, placed before the display
/// mode on the toolbar and offered in the overflow menu when the toolbar has no room for it - the empty preview's
/// toolbar and placeholder, the statistics card measured from the image shown, kept with a preview read before and
/// moved off the autostretch banner, the display mode combo rebuilt silently and following the mode the image is shown
/// in, and the caches the calibration keeps until they are cleared.

#include "Test_ControlsViewer/ViewerTestContext.h"

#include "controls/CalibrationPreviewConfig.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Demosaic/Demosaic.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/Provenance.h>

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QList>
#include <QMenu>
#include <QSignalSpy>
#include <QStringList>
#include <QTemporaryDir>
#include <QToolBar>
#include <QToolButton>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{

using namespace an::tests::viewer;

/// The view's toolbar.
[[nodiscard]] QToolBar* toolbarOf(const ImageViewControl& view)
{
    return view.findChild<QToolBar*>(QStringLiteral("anImageViewToolbar"));
}

/// The static preview's Calibrated toggle.
[[nodiscard]] QCheckBox* calibratedToggleOf(const ImageViewControl& view)
{
    return view.findChild<QCheckBox*>(QStringLiteral("chkCalibrationPreview"));
}

/// The toolbar combo a person knows by its name: "Display Mode" or "View Channel".
[[nodiscard]] QComboBox* comboNamed(const ImageViewControl& view, const QString& name)
{
    for (QComboBox* combo : view.findChildren<QComboBox*>())
    {
        if (combo->accessibleName() == name)
        {
            return combo;
        }
    }
    return nullptr;
}

/// The toolbar entry that holds a widget, or null.
[[nodiscard]] QAction* entryHolding(const QToolBar& toolbar, const QWidget* widget)
{
    if (widget == nullptr)
    {
        return nullptr;
    }
    for (QAction* entry : toolbar.actions())
    {
        if (toolbar.widgetForAction(entry) == widget)
        {
            return entry;
        }
    }
    return nullptr;
}

/// The toolbar entry named `text`, or null.
[[nodiscard]] QAction* entryNamed(const QToolBar& toolbar, const QString& text)
{
    for (QAction* entry : toolbar.actions())
    {
        if (entry->text() == text)
        {
            return entry;
        }
    }
    return nullptr;
}

/// The entry named `text` of the menu the toolbar's overflow button opens, or null when the menu offers none.
[[nodiscard]] QAction* overflowEntry(const QToolBar& toolbar, const QString& text)
{
    const auto* button = toolbar.findChild<QToolButton*>(QStringLiteral("imageViewToolbarOverflowButton"));
    if (button == nullptr || button->menu() == nullptr)
    {
        return nullptr;
    }
    for (QAction* entry : button->menu()->actions())
    {
        if (!entry->isSeparator() && entry->text() == text)
        {
            return entry;
        }
    }
    return nullptr;
}

/// The card a static preview shows its statistics in over its image.
[[nodiscard]] QWidget* statisticsCardOf(const ImageViewControl& view)
{
    return view.findChild<QWidget*>(QStringLiteral("staticPreviewStatsOverlay"));
}

/// What the statistics card says - its rows, which it also gives as its accessible description - or nothing while it is
/// hidden.
[[nodiscard]] QString cardText(const ImageViewControl& view)
{
    QWidget* card = statisticsCardOf(view);
    return card != nullptr && !card->isHidden() ? card->accessibleDescription() : QString();
}

/// What a static preview shows in place of its image area while it has no image: the one widget the view holds besides
/// its toolbar and its image area; null when there is not exactly one.
[[nodiscard]] QWidget* placeholderOf(const ImageViewControl& view)
{
    QWidget* placeholder = nullptr;
    int found = 0;
    for (QWidget* child : view.findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
    {
        if (qobject_cast<QToolBar*>(child) == nullptr && child != view.getScrollArea())
        {
            placeholder = child;
            ++found;
        }
    }
    return found == 1 ? placeholder : nullptr;
}

/// The autostretch banner over the preview's image area, or null.
[[nodiscard]] ViewControlTextOverlay* bannerOf(const ImageViewControl& view)
{
    QWidget* viewport = view.getScrollArea() != nullptr ? view.getScrollArea()->viewport() : nullptr;
    if (viewport == nullptr)
    {
        return nullptr;
    }
    for (QWidget* child : viewport->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
    {
        if (auto* banner = dynamic_cast<ViewControlTextOverlay*>(child))
        {
            return banner;
        }
    }
    return nullptr;
}

/// Whether the context was asked any question.
[[nodiscard]] bool askedAnything(const ViewerTestContext& context)
{
    for (const QString& message : context.messages)
    {
        if (message.startsWith(QStringLiteral("question:")))
        {
            return true;
        }
    }
    return false;
}

/// The detail of the last entry of an asset's lineage.
[[nodiscard]] std::string lastLineageDetail(const an::consumer::ImageAsset& asset)
{
    an::Result<an::consumer::AssetReadLease> lease = asset.read();
    if (!lease.hasValue())
    {
        return {};
    }
    std::string detail;
    lease->visit([&detail](const auto& typed) {
        const auto entries = typed.metadata().provenance().entries();
        if (!entries.empty())
        {
            detail = entries.back().detail();
        }
    });
    return detail;
}

/// The Float32 sample a view shows at a position, or -1 when it shows none.
[[nodiscard]] float shownSample(const ImageViewControl& view, std::uint32_t x, std::uint32_t y)
{
    const std::shared_ptr<const an::consumer::ImageAsset> asset = view.document().asset();
    return view.hasImage() && asset ? sampleOf<float>(*asset, x, y).value_or(-1.0f) : -1.0f;
}

/// Keeps every read failure a view tells its listeners of.
class ReadFailures final : public ImageViewControlListener
{
public:
    explicit ReadFailures(ImageViewControl* view) : ImageViewControlListener(view) {}

    void onImageLoadFailed(ImageViewControl* view, const QString& message) override
    {
        views.push_back(view);
        messages.push_back(message);
    }

    /// The views that failed, in order.
    std::vector<ImageViewControl*> views;

    /// What each failure said, in order.
    QStringList messages;
};

class StaticPreviewBehavior : public ::testing::Test
{
protected:
    /// Opens a context of a width and installs it for the case.
    void open(std::uint32_t width = 2u)
    {
        m_context = openViewerContext(width);
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
    }

    void TearDown() override
    {
        m_installed.reset();
        m_context.reset();
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_context->engineSession();
    }

    /// Writes a grey Float32 FITS file whose every sample is `value`, and returns its path.
    [[nodiscard]] QString writeFlat(const QString& name, float value, std::uint32_t size = 8u)
    {
        const QString path = m_directory.filePath(name);
        std::optional<an::consumer::ImageAsset> asset =
            makeAsset<float>(session(), size, size, Channels::Grey,
                             [value](std::uint32_t, std::uint32_t, std::uint32_t) { return value; });
        EXPECT_TRUE(asset.has_value());
        if (asset.has_value())
        {
            writeFitsFile(session(), *asset, path);
        }
        return path;
    }

    /// Writes a grey Float32 FITS file of `size` pixels a side whose sample at (x, y) is `offset + step (x + size y)`,
    /// and returns its path.
    [[nodiscard]] QString writeGradient(const QString& name, float offset, float step, std::uint32_t size = 16u)
    {
        const QString path = m_directory.filePath(name);
        std::optional<an::consumer::ImageAsset> asset =
            makeAsset<float>(session(), size, size, Channels::Grey,
                             [offset, step, size](std::uint32_t x, std::uint32_t y, std::uint32_t) {
                                 return offset + step * static_cast<float>(x + size * y);
                             });
        EXPECT_TRUE(asset.has_value());
        if (asset.has_value())
        {
            writeFitsFile(session(), *asset, path);
        }
        return path;
    }

    /// Writes a sixteen-bit RGGB mosaic whose red, green and blue sites hold three different values, and returns its
    /// path.
    [[nodiscard]] QString writeMosaic(const QString& name)
    {
        const QString path = m_directory.filePath(name);
        const std::optional<an::CfaDescriptor> rggb = an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u);
        EXPECT_TRUE(rggb.has_value());
        if (!rggb.has_value())
        {
            return path;
        }
        std::optional<an::consumer::ImageAsset> mosaic = makeAsset<std::uint16_t>(
            session(), 8u, 8u, Channels::Grey,
            [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
                const bool redRow = y % 2u == 0u;
                const bool redColumn = x % 2u == 0u;
                if (redRow && redColumn)
                {
                    return std::uint16_t{40000u};
                }
                return !redRow && !redColumn ? std::uint16_t{10000u} : std::uint16_t{20000u};
            },
            an::NumericDomain::NormalizedLinear, *rggb);
        EXPECT_TRUE(mosaic.has_value());
        if (mosaic.has_value())
        {
            writeFitsFile(session(), *mosaic, path);
        }
        return path;
    }

    /// A configuration whose masters are available: the bias, dark and flat named, no cosmetic correction.
    [[nodiscard]] static an::ui::CalibrationPreviewConfig
    availableMasters(const QString& bias, const QString& dark = QString(), const QString& flat = QString())
    {
        an::ui::CalibrationPreviewConfig config;
        config.available = true;
        config.biasPath = bias;
        config.darkPath = dark;
        config.flatPath = flat;
        config.settings.cosmeticCorrection = false;
        return config;
    }

    std::unique_ptr<ViewerTestContext> m_context;
    std::optional<InstalledContext> m_installed;
    QTemporaryDir m_directory;
};

// Whenever the Calibrated toggle is shown it is enabled exactly when its masters are available: showing it enables
// nothing, and hiding and showing it again keeps what the masters decided. The toolbar hands the toggle the state of
// the action that holds it, which Qt enables again whenever it is shown unless the masters' state was given to the
// action.
TEST_F(StaticPreviewBehavior, TheCalibratedToggleIsShownAndEnabledIndependently)
{
    open();
    ViewHandle view = makeView("preview", ViewControlMode::st_preview);
    QToolBar* toolbar = toolbarOf(*view);
    QCheckBox* calibrated = calibratedToggleOf(*view);
    ASSERT_NE(toolbar, nullptr);
    ASSERT_NE(calibrated, nullptr);
    QAction* entry = entryHolding(*toolbar, calibrated);
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(calibrated->isHidden());
    EXPECT_FALSE(entry->isVisible());
    EXPECT_FALSE(calibrated->isEnabled());

    view->setCalibrationPreviewVisible(true);
    EXPECT_FALSE(calibrated->isHidden());
    EXPECT_TRUE(entry->isVisible()) << "the toolbar shows the entry that holds the toggle";
    EXPECT_FALSE(calibrated->isEnabled()) << "a toggle shown with no masters is not enabled by being shown";

    view->setCalibrationPreviewConfig(availableMasters(m_directory.filePath(QStringLiteral("bias.fits"))));
    EXPECT_TRUE(calibrated->isEnabled()) << "masters that are available enable it";

    view->setCalibrationPreviewVisible(false);
    EXPECT_TRUE(calibrated->isHidden());
    EXPECT_FALSE(entry->isVisible());
    view->setCalibrationPreviewVisible(true);
    EXPECT_FALSE(calibrated->isHidden());
    EXPECT_TRUE(calibrated->isEnabled()) << "hiding and showing the toggle keeps it enabled";

    // Masters that stop being available while the toggle is hidden leave it disabled when it is shown again.
    view->setCalibrationPreviewVisible(false);
    an::ui::CalibrationPreviewConfig unavailable = availableMasters(m_directory.filePath(QStringLiteral("bias.fits")));
    unavailable.available = false;
    view->setCalibrationPreviewConfig(unavailable);
    view->setCalibrationPreviewVisible(true);
    EXPECT_FALSE(calibrated->isHidden());
    EXPECT_FALSE(calibrated->isEnabled()) << "showing the toggle enables nothing the masters disabled";
}

// Masters that stop being available uncheck the toggle, and the preview is read again as the raw light.
TEST_F(StaticPreviewBehavior, MastersThatBecomeUnavailableUncheckTheToggleAndShowTheRawLight)
{
    open();
    const QString light = writeFlat(QStringLiteral("light.fits"), 0.5f);
    const QString bias = writeFlat(QStringLiteral("bias.fits"), 0.125f);
    ViewHandle view = makeView("preview", ViewControlMode::st_preview);
    an::ui::CalibrationPreviewConfig config = availableMasters(bias);
    view->setCalibrationPreviewConfig(config);
    view->setCalibrationPreviewVisible(true);
    QCheckBox* calibrated = calibratedToggleOf(*view);
    ASSERT_NE(calibrated, nullptr);
    calibrated->setChecked(true);
    view->loadImageFromFile(light.toStdString());
    ASSERT_TRUE(settle(*view));
    EXPECT_NEAR(shownSample(*view, 3u, 3u), 0.375f, 1e-6f) << "the preview shows the light less its bias";

    config.available = false;
    config.tooltip = QStringLiteral("the masters are being rebuilt");
    view->setCalibrationPreviewConfig(config);
    EXPECT_FALSE(calibrated->isEnabled());
    EXPECT_FALSE(calibrated->isChecked()) << "a toggle that cannot be used is not left checked";
    EXPECT_FALSE(view->isCalibrationPreviewEnabled());
    ASSERT_TRUE(settle(*view));
    EXPECT_FLOAT_EQ(shownSample(*view, 3u, 3u), 0.5f) << "the preview is read again, uncalibrated";
}

// A preview reads a mosaic on a worker and shows it as recorded, without the question a standard view asks.
TEST_F(StaticPreviewBehavior, APreviewOfAMosaicIsReadOnAWorkerAndShownAsRecordedWithoutAQuestion)
{
    open();
    const QString mosaic = writeMosaic(QStringLiteral("mosaic.fits"));
    m_context->answerYes = true;
    ViewHandle view = makeView("preview", ViewControlMode::st_preview);
    ASSERT_FALSE(view->isAutoDebayerCfaImagePreviewsEnabled());

    view->loadImageFromFile(mosaic.toStdString());
    EXPECT_FALSE(view->hasImage()) << "the read lands later, from a worker";
    EXPECT_EQ(view->workTelemetry().readsRequested, 1u);
    ASSERT_TRUE(settle(*view));

    ASSERT_TRUE(view->hasImage());
    ASSERT_TRUE(view->imageDescriptor().has_value());
    EXPECT_TRUE(view->imageDescriptor()->isMosaiced()) << "a preview that does not show colour shows the mosaic";
    EXPECT_FALSE(askedAnything(*m_context)) << m_context->messages.join(QStringLiteral(", ")).toStdString();
    EXPECT_EQ(view->workTelemetry().readsCommitted, 1u);
}

// A preview that shows colour demosaics a mosaic by the engine's `Fast` method as it reads it, asking nothing.
TEST_F(StaticPreviewBehavior, APreviewThatShowsColourDemosaicsAMosaicByTheFastMethodWithoutAsking)
{
    open();
    const QString mosaic = writeMosaic(QStringLiteral("mosaic.fits"));
    ViewHandle view = makeView("preview", ViewControlMode::st_preview);
    view->setAutoDebayerCfaImagePreviews(true);
    EXPECT_TRUE(view->isAutoDebayerCfaImagePreviewsEnabled());

    view->loadImageFromFile(mosaic.toStdString());
    ASSERT_TRUE(settle(*view));

    ASSERT_TRUE(view->imageDescriptor().has_value());
    EXPECT_FALSE(view->imageDescriptor()->isMosaiced());
    EXPECT_EQ(view->imageDescriptor()->extent().channelCount(), 3u);
    const std::string detail = lastLineageDetail(*view->document().asset());
    EXPECT_NE(detail.find(an::demosaicMethodName(an::DemosaicMethod::Fast)), std::string::npos) << detail;
    EXPECT_FALSE(askedAnything(*m_context)) << m_context->messages.join(QStringLiteral(", ")).toStdString();
}

// A preview that shows colour asked the engine for the Bayer fast method over an X-Trans mosaic, which the engine
// refuses, so an image list of X-Trans files previewed nothing: it demosaics one by the tabulated fast estimate, asking
// nothing.
TEST_F(StaticPreviewBehavior, APreviewThatShowsColourDemosaicsAnXTransMosaicByTheTabulatedFastMethod)
{
    open();
    const QString mosaic = m_directory.filePath(QStringLiteral("xtrans.tif"));
    ASSERT_TRUE(writeXTransTiff(session(), mosaic));
    ViewHandle view = makeView("preview", ViewControlMode::st_preview);
    view->setAutoDebayerCfaImagePreviews(true);

    view->loadImageFromFile(mosaic.toStdString());
    ASSERT_TRUE(settle(*view));

    ASSERT_TRUE(view->hasImage());
    ASSERT_TRUE(view->imageDescriptor().has_value());
    EXPECT_FALSE(view->imageDescriptor()->isMosaiced());
    EXPECT_EQ(view->imageDescriptor()->extent().channelCount(), 3u);
    const std::string detail = lastLineageDetail(*view->document().asset());
    EXPECT_NE(detail.find(an::demosaicMethodName(an::DemosaicMethod::XTransFast)), std::string::npos) << detail;
    EXPECT_FALSE(askedAnything(*m_context)) << m_context->messages.join(QStringLiteral(", ")).toStdString();
}

// The statistics card of a preview that shows colour measures the colour image it shows, not the mosaic it read.
TEST_F(StaticPreviewBehavior, TheStatisticsCardMeasuresTheImageThePreviewShows)
{
    open();
    const QString mosaic = writeMosaic(QStringLiteral("mosaic.fits"));
    ViewHandle colour = makeView("colour", ViewControlMode::st_preview);
    colour->setAutoDebayerCfaImagePreviews(true);
    colour->loadImageFromFile(mosaic.toStdString());
    ASSERT_TRUE(settle(*colour));
    ASSERT_TRUE(colour->imageDescriptor().has_value());
    ASSERT_EQ(colour->imageDescriptor()->extent().channelCount(), 3u);

    // The colour image the preview shows, read by another preview from a file of its own.
    const QString shown = m_directory.filePath(QStringLiteral("shown.fits"));
    writeFitsFile(session(), *colour->document().asset(), shown);
    ViewHandle direct = makeView("direct", ViewControlMode::st_preview);
    direct->loadImageFromFile(shown.toStdString());
    ViewHandle recorded = makeView("recorded", ViewControlMode::st_preview);
    recorded->loadImageFromFile(mosaic.toStdString());
    ASSERT_TRUE(settle(*direct));
    ASSERT_TRUE(settle(*recorded));

    const QString colourCard = cardText(*colour);
    const QString directCard = cardText(*direct);
    const QString recordedCard = cardText(*recorded);
    ASSERT_FALSE(colourCard.isEmpty()) << "the card shows once the statistics are measured";
    ASSERT_FALSE(directCard.isEmpty());
    ASSERT_FALSE(recordedCard.isEmpty());
    EXPECT_EQ(colourCard, directCard) << "the card measures the colour image the preview shows";
    EXPECT_NE(colourCard, recordedCard) << "and not the mosaic it was read from";
}

// A preview read that fails tells the view's listeners, and the preview shows nothing.
TEST_F(StaticPreviewBehavior, APreviewReadThatFailsTellsTheViewsListeners)
{
    open();
    ViewHandle view = makeView("preview", ViewControlMode::st_preview);
    ReadFailures failures(view.get());
    const QString missing = m_directory.filePath(QStringLiteral("absent.fits"));

    view->loadImageFromFile(missing.toStdString());
    ASSERT_TRUE(settle(*view));

    ASSERT_EQ(failures.messages.size(), 1);
    EXPECT_EQ(failures.views.front(), view.get());
    EXPECT_TRUE(failures.messages.front().contains(missing)) << failures.messages.front().toStdString();
    EXPECT_FALSE(view->hasImage());
    EXPECT_EQ(view->workTelemetry().readsFailed, 1u);
}

// A preview is calibrated by every master and setting its configuration states; unchecked, it shows the raw light.
TEST_F(StaticPreviewBehavior, APreviewIsCalibratedByEveryMasterAndSettingItsConfigurationStates)
{
    open();
    const QString light = writeFlat(QStringLiteral("light.fits"), 0.6f);
    const QString bias = writeFlat(QStringLiteral("bias.fits"), 0.05f);
    const QString dark = writeFlat(QStringLiteral("dark.fits"), 0.25f);
    const QString flat = writeFlat(QStringLiteral("flat.fits"), 0.5f);
    ViewHandle view = makeView("preview", ViewControlMode::st_preview);
    an::ui::CalibrationPreviewConfig config = availableMasters(bias, dark, flat);
    config.subtractBiasFromDark = true;
    config.settings.darkMultiplicationFactor = 0.5;
    // The pedestal is stated in sixteen-bit codes: this one is a tenth of full scale.
    config.settings.pedestal = 6553.5;
    config.settings.flatAutoLevel = false;
    view->setCalibrationPreviewConfig(config);
    view->setCalibrationPreviewVisible(true);
    QCheckBox* calibrated = calibratedToggleOf(*view);
    ASSERT_NE(calibrated, nullptr);
    calibrated->setChecked(true);
    ASSERT_TRUE(view->isCalibrationPreviewEnabled());

    view->loadImageFromFile(light.toStdString());
    ASSERT_TRUE(settle(*view));
    // (light + pedestal - bias - scale (dark - bias)) / flat
    const double expected = (0.6 + 0.1 - 0.05 - 0.5 * (0.25 - 0.05)) / 0.5;
    EXPECT_NEAR(shownSample(*view, 4u, 4u), expected, 1e-5);

    calibrated->setChecked(false);
    ASSERT_TRUE(settle(*view));
    EXPECT_NEAR(shownSample(*view, 4u, 4u), 0.6, 1e-6) << "unchecked, the preview is read again as the raw light";
}

// An empty preview keeps its toolbar and shows a placeholder in place of its image area, cleared or never shown.
TEST_F(StaticPreviewBehavior, AnEmptyPreviewKeepsItsToolbarAndShowsItsPlaceholder)
{
    open();
    const QString light = writeFlat(QStringLiteral("light.fits"), 0.5f);
    ViewHandle view = makeView("preview", ViewControlMode::st_preview);
    QToolBar* toolbar = toolbarOf(*view);
    QWidget* imageArea = view->getScrollArea();
    QWidget* placeholder = placeholderOf(*view);
    ASSERT_NE(toolbar, nullptr);
    ASSERT_NE(imageArea, nullptr);
    ASSERT_NE(placeholder, nullptr);
    EXPECT_FALSE(toolbar->isHidden()) << "a preview with no image keeps its toolbar";
    EXPECT_TRUE(imageArea->isHidden());
    EXPECT_FALSE(placeholder->isHidden());

    view->loadImageFromFile(light.toStdString());
    ASSERT_TRUE(settle(*view));
    EXPECT_FALSE(toolbar->isHidden());
    EXPECT_FALSE(imageArea->isHidden());
    EXPECT_TRUE(placeholder->isHidden());

    view->clear();
    EXPECT_FALSE(view->hasImage());
    EXPECT_FALSE(toolbar->isHidden()) << "clearing the preview keeps its toolbar";
    EXPECT_TRUE(imageArea->isHidden());
    EXPECT_FALSE(placeholder->isHidden());
    ASSERT_TRUE(settle(*view));
}

// A Calibrated toggle the toolbar has no room for is offered, checkable, in the toolbar's overflow menu.
TEST_F(StaticPreviewBehavior, ACalibratedToggleTheToolbarHasNoRoomForIsOfferedInItsOverflowMenu)
{
    open();
    ViewHandle view = makeView("preview", ViewControlMode::st_preview);
    QToolBar* toolbar = toolbarOf(*view);
    QCheckBox* calibrated = calibratedToggleOf(*view);
    ASSERT_NE(toolbar, nullptr);
    ASSERT_NE(calibrated, nullptr);
    auto* overflowing = dynamic_cast<CustomToolBar*>(toolbar);
    ASSERT_NE(overflowing, nullptr);
    QAction* entry = entryHolding(*toolbar, calibrated);
    ASSERT_NE(entry, nullptr);

    // A toolbar with room for nothing: every entry it shows goes to its overflow menu.
    toolbar->resize(1, toolbar->height());
    view->setCalibrationPreviewVisible(true);
    overflowing->refreshOverflow();
    ASSERT_TRUE(pumpUntil([toolbar]() { return overflowEntry(*toolbar, QStringLiteral("Calibrated")) != nullptr; }))
        << "the toggle the toolbar has no room for is offered in its overflow menu";
    EXPECT_FALSE(entry->isVisible()) << "and not on the toolbar itself";
    QAction* offered = overflowEntry(*toolbar, QStringLiteral("Calibrated"));
    ASSERT_NE(offered, nullptr);
    EXPECT_TRUE(offered->isCheckable());
    EXPECT_FALSE(offered->isEnabled()) << "with no masters it cannot be checked there either";

    view->setCalibrationPreviewConfig(availableMasters(m_directory.filePath(QStringLiteral("bias.fits"))));
    ASSERT_TRUE(pumpUntil([toolbar]() {
        const QAction* item = overflowEntry(*toolbar, QStringLiteral("Calibrated"));
        return item != nullptr && item->isEnabled();
    })) << "the entry follows the toggle once its masters are available";
    offered = overflowEntry(*toolbar, QStringLiteral("Calibrated"));
    ASSERT_NE(offered, nullptr);
    EXPECT_FALSE(offered->isChecked());
    offered->trigger();
    EXPECT_TRUE(calibrated->isChecked()) << "checking the overflow entry checks the toggle";
    EXPECT_TRUE(view->isCalibrationPreviewEnabled());
}

// The Calibrated toggle comes before the display mode and its settings, so the overflow takes them first.
TEST_F(StaticPreviewBehavior, TheCalibratedToggleComesBeforeTheDisplayModeAndItsSettings)
{
    open();
    ViewHandle view = makeView("preview", ViewControlMode::st_preview);
    QToolBar* toolbar = toolbarOf(*view);
    ASSERT_NE(toolbar, nullptr);
    QAction* calibrated = entryHolding(*toolbar, calibratedToggleOf(*view));
    QAction* displayMode = entryHolding(*toolbar, comboNamed(*view, QStringLiteral("Display Mode")));
    QAction* settings = entryNamed(*toolbar, QStringLiteral("Autostretch Settings"));
    ASSERT_NE(calibrated, nullptr);
    ASSERT_NE(displayMode, nullptr);
    ASSERT_NE(settings, nullptr);

    const QList<QAction*> entries = toolbar->actions();
    EXPECT_LT(entries.indexOf(calibrated), entries.indexOf(displayMode));
    EXPECT_LT(entries.indexOf(calibrated), entries.indexOf(settings));
}

// A preview shown again shows the statistics kept with it, while a measurement would still wait for the session.
TEST_F(StaticPreviewBehavior, APreviewShownAgainShowsTheStatisticsKeptWithIt)
{
    open(1u);
    const QString first = writeGradient(QStringLiteral("first.fits"), 0.1f, 0.001f);
    const QString second = writeGradient(QStringLiteral("second.fits"), 0.4f, 0.002f);
    ViewHandle view = makeView("preview", ViewControlMode::st_preview);
    QWidget* card = statisticsCardOf(*view);
    ASSERT_NE(card, nullptr);
    view->loadImageFromFile(first.toStdString());
    ASSERT_TRUE(settle(*view));
    const QString firstCard = cardText(*view);
    ASSERT_FALSE(firstCard.isEmpty()) << "the card shows once the statistics are measured";
    view->loadImageFromFile(second.toStdString());
    ASSERT_TRUE(settle(*view));
    const QString secondCard = cardText(*view);
    ASSERT_FALSE(secondCard.isEmpty());
    ASSERT_NE(firstCard, secondCard) << "the two images' statistics differ";

    {
        std::optional<an::consumer::OperationScope> gate = admitted(session());
        ASSERT_TRUE(gate.has_value());
        view->loadImageFromFile(first.toStdString());
        ASSERT_TRUE(pumpUntil([&view, &first, card]() {
            return view->document().names().sourcePath == first && !card->isHidden();
        })) << "the card of a preview shown again shows before anything could be measured";
        EXPECT_EQ(cardText(*view), firstCard) << "with the statistics the preview was first shown with";
    }
    ASSERT_TRUE(settle(*view));
    EXPECT_EQ(cardText(*view), firstCard);
}

// The statistics card moves below the autostretch banner where the banner would cover it, and back when it goes.
TEST_F(StaticPreviewBehavior, TheStatisticsCardMovesBelowTheAutostretchBanner)
{
    open();
    const QString light = writeGradient(QStringLiteral("light.fits"), 0.1f, 0.0001f, 64u);
    ViewHandle view = makeView("preview", ViewControlMode::st_preview);
    // A preview as narrow as one is laid out, so the banner and the card both reach the middle of its image area.
    view->resize(view->minimumWidth(), 400);
    view->show();
    view->loadImageFromFile(light.toStdString());
    ASSERT_TRUE(settle(*view));
    QWidget* card = statisticsCardOf(*view);
    ViewControlTextOverlay* banner = bannerOf(*view);
    ASSERT_NE(card, nullptr);
    ASSERT_NE(banner, nullptr);
    ASSERT_TRUE(pumpUntil([card]() { return card->isVisible(); })) << "the card shows once the statistics are measured";

    view->setViewMode(ImageViewMode::LINEAR_MODE);
    EXPECT_FALSE(banner->isVisible()) << "a linear preview shows no autostretch banner";
    const QRect unobstructed = card->geometry();

    view->setViewMode(ImageViewMode::AUTOSTRETCH_MODE);
    ASSERT_TRUE(banner->isVisible());
    const QRect bannerArea = banner->geometry();
    ASSERT_TRUE(unobstructed.intersects(bannerArea)) << "in a preview this narrow the banner shows where the card sits";
    EXPECT_FALSE(card->geometry().intersects(bannerArea)) << "the card does not cover the banner";
    EXPECT_GT(card->geometry().top(), bannerArea.bottom()) << "it moves below it";
    EXPECT_EQ(card->geometry().left(), unobstructed.left()) << "on the side it sat";

    view->setViewMode(ImageViewMode::LINEAR_MODE);
    EXPECT_TRUE(card->geometry() == unobstructed) << "with the banner gone, the card goes back to the top";
    ASSERT_TRUE(settle(*view));
}

// Showing a read image rebuilds the display combos silently, and choosing a mode presents again, reading nothing.
TEST_F(StaticPreviewBehavior, ShowingAReadImageRebuildsTheDisplayCombosWithoutReadingAgain)
{
    open();
    const QString light = writeGradient(QStringLiteral("light.fits"), 0.1f, 0.001f);
    ViewHandle view = makeView("preview", ViewControlMode::st_preview);
    QComboBox* mode = comboNamed(*view, QStringLiteral("Display Mode"));
    QComboBox* channel = comboNamed(*view, QStringLiteral("View Channel"));
    ASSERT_NE(mode, nullptr);
    ASSERT_NE(channel, nullptr);
    QSignalSpy modeChanges(mode, &QComboBox::currentTextChanged);
    QSignalSpy channelChanges(channel, &QComboBox::currentTextChanged);
    ASSERT_TRUE(modeChanges.isValid());
    ASSERT_TRUE(channelChanges.isValid());

    view->loadImageFromFile(light.toStdString());
    ASSERT_TRUE(settle(*view));
    EXPECT_GT(mode->count(), 0) << "the combo holds the modes the image can be shown in";
    EXPECT_GT(channel->count(), 0);
    EXPECT_EQ(modeChanges.count(), 0) << "rebuilding the combo for the image is not a choice of mode";
    EXPECT_EQ(channelChanges.count(), 0) << "nor of channel";
    EXPECT_EQ(view->workTelemetry().readsRequested, 1u) << "the rebuild read nothing again";

    const bool linear = mode->currentText() == QStringLiteral("Linear");
    const std::uint64_t presented = view->getViewWidget()->presentationTelemetry().requested;
    mode->setCurrentText(linear ? QStringLiteral("Autostretch") : QStringLiteral("Linear"));
    ASSERT_TRUE(settle(*view));
    EXPECT_EQ(view->getViewWidget()->getViewMode(),
              linear ? ImageViewMode::AUTOSTRETCH_MODE : ImageViewMode::LINEAR_MODE);
    EXPECT_GT(view->getViewWidget()->presentationTelemetry().requested, presented) << "the image is presented again";
    EXPECT_EQ(view->workTelemetry().readsRequested, 1u) << "a mode picked presents the image the preview holds";
}

// The display mode combo rebuilt for a read image shows the mode the image is shown in, even one set without it.
TEST_F(StaticPreviewBehavior, TheDisplayModeComboShowsTheModeTheImageIsShownIn)
{
    open();
    const QString first = writeFlat(QStringLiteral("first.fits"), 0.25f);
    const QString second = writeFlat(QStringLiteral("second.fits"), 0.75f);
    ViewHandle view = makeView("preview", ViewControlMode::st_preview);
    QComboBox* mode = comboNamed(*view, QStringLiteral("Display Mode"));
    ASSERT_NE(mode, nullptr);
    view->loadImageFromFile(first.toStdString());
    ASSERT_TRUE(settle(*view));

    // The image is shown in the other mode without the combo hearing of it.
    const ImageViewMode other = view->getViewWidget()->getViewMode() == ImageViewMode::LINEAR_MODE
                                    ? ImageViewMode::AUTOSTRETCH_MODE
                                    : ImageViewMode::LINEAR_MODE;
    const QString otherText =
        other == ImageViewMode::LINEAR_MODE ? QStringLiteral("Linear") : QStringLiteral("Autostretch");
    view->getViewWidget()->setViewMode(other);
    ASSERT_NE(mode->currentText(), otherText);

    view->loadImageFromFile(second.toStdString());
    ASSERT_TRUE(settle(*view));
    EXPECT_EQ(mode->currentText(), otherText) << "the rebuilt combo shows the mode the image is shown in";
    EXPECT_EQ(view->getViewWidget()->getViewMode(), other) << "and the rebuild leaves that mode as it is";
}

// Autostretch can be chosen before the first image arrives, and the image lands stretched.
TEST_F(StaticPreviewBehavior, AutostretchCanBeChosenBeforeTheFirstImageArrives)
{
    open();
    const QString light = writeGradient(QStringLiteral("light.fits"), 0.1f, 0.001f);
    ViewHandle view = makeView("preview", ViewControlMode::st_preview);
    QComboBox* mode = comboNamed(*view, QStringLiteral("Display Mode"));
    ASSERT_NE(mode, nullptr);
    ASSERT_FALSE(view->hasImage());

    view->setViewMode(ImageViewMode::AUTOSTRETCH_MODE);
    EXPECT_EQ(mode->currentText(), QStringLiteral("Autostretch"));
    EXPECT_NE(mode->findText(QStringLiteral("Linear")), -1) << "linear can be chosen back";
    EXPECT_EQ(view->getViewWidget()->getViewMode(), ImageViewMode::AUTOSTRETCH_MODE);

    view->loadImageFromFile(light.toStdString());
    ASSERT_TRUE(settle(*view));
    EXPECT_EQ(view->getViewWidget()->getViewMode(), ImageViewMode::AUTOSTRETCH_MODE) << "the image lands stretched";
    EXPECT_EQ(mode->currentText(), QStringLiteral("Autostretch"));
}

// A preview and its masters are kept until the calibration caches are cleared, then read again from the files.
TEST_F(StaticPreviewBehavior, ClearingTheCalibrationCachesReadsThePreviewAndItsMastersAgain)
{
    open();
    const QString light = writeFlat(QStringLiteral("light.fits"), 0.5f);
    const QString other = writeFlat(QStringLiteral("other.fits"), 0.6f);
    const QString bias = writeFlat(QStringLiteral("bias.fits"), 0.125f);
    ViewHandle view = makeView("preview", ViewControlMode::st_preview);
    view->setCalibrationPreviewConfig(availableMasters(bias));
    view->setCalibrationPreviewVisible(true);
    QCheckBox* calibrated = calibratedToggleOf(*view);
    ASSERT_NE(calibrated, nullptr);
    calibrated->setChecked(true);
    const auto shownFor = [&view](const QString& path) {
        view->loadImageFromFile(path.toStdString());
        EXPECT_TRUE(settle(*view));
        return shownSample(*view, 2u, 2u);
    };
    EXPECT_NEAR(shownFor(light), 0.375f, 1e-6f);

    ASSERT_TRUE(QFile::remove(bias));
    (void)writeFlat(QStringLiteral("bias.fits"), 0.25f);
    EXPECT_NEAR(shownFor(light), 0.375f, 1e-6f) << "the preview read before is shown as it was read";
    EXPECT_NEAR(shownFor(other), 0.475f, 1e-6f) << "a preview read now is calibrated by the master kept";

    view->clearCalibrationCaches();
    EXPECT_NEAR(shownFor(light), 0.25f, 1e-6f) << "with the caches cleared, the preview is read through the new master";
    EXPECT_NEAR(shownFor(other), 0.35f, 1e-6f);
}

} // namespace
