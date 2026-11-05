// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// How individual process windows present themselves once the registry opens them on a view: the PixelMath symbol
/// editors in the monospace face and the window fitted a quarter wider, the generalized hyperbolic transformation's
/// symmetry-point warning in the heading face, the image list analyzer's optimization status in the body face, the
/// star mask window tall enough for its detection settings, the total variation denoise keeping its isotropic choice
/// for the Bregman method only, the NBRGB combination stretching its preview through the framework, and the profiled
/// star mask window holding the size of its content.

#include "Test_ProcessFramework/ProcessFrameworkSuiteSupport.h"

#include "controls/ImageProcessControl.h"
#include "controls/ImageRTPreviewControl.h"
#include "controls/ProfiledStarMaskCreatorControl.h"
#include "controls/StarsFilterControlHandler.h"
#include "processes/NBRGBCombinationProcess.h"
#include "theme/AnDesignTokens.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QFont>
#include <QLabel>
#include <QPlainTextEdit>
#include <QString>
#include <QWidget>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{

using an::tests::processes::Channels;
using an::tests::processes::makeDocument;
using an::tests::processes::makeTargetView;
using an::tests::processes::openProcessContext;
using an::tests::processes::ProcessTestContext;
using an::tests::processes::ViewHandle;
using an::tests::processes::framework::ScopedAppContext;
using an::tests::processes::shared::ClosesOpenedWindows;
using an::tests::processes::shared::openedProcessWindows;
using an::tests::processes::shared::registered;
using Mode = an::ui::process_registry::ProcessLaunchMode;

/// A context of the case's own, installed for it, with a colour Float32 view of sixteen by twelve made its active view;
/// every process window the case opens closes before the context goes.
class OpenedOnAView
{
public:
    OpenedOnAView() : m_context(openProcessContext())
    {
        if (m_context == nullptr)
        {
            return;
        }
        m_installed.emplace(*m_context);
        m_closes.emplace();
        std::optional<an::ui::ImageDocument> document = makeDocument<float>(
            m_context->engineSession(), 16u, 12u, Channels::Rgb,
            [](std::uint32_t x, std::uint32_t y, std::uint32_t c) { return static_cast<float>(x + y + c) / 64.0f; });
        if (!document.has_value())
        {
            return;
        }
        m_view = makeTargetView("target", std::move(*document));
        m_context->addViewControl(m_view.get());
        m_context->setActiveImageViewControl(m_view.get());
    }

    ~OpenedOnAView()
    {
        m_closes.reset();
        if (m_context != nullptr && m_view != nullptr)
        {
            m_context->setActiveImageViewControl(nullptr);
            m_context->removeViewControl(m_view.get(), false);
        }
    }

    OpenedOnAView(const OpenedOnAView&) = delete;
    OpenedOnAView& operator=(const OpenedOnAView&) = delete;

    /// Whether the context and the view were made.
    [[nodiscard]] bool ready() const
    {
        return m_context != nullptr && m_view != nullptr;
    }

    /// Launches a registered process as the application does for its launch mode and answers the one window of a class
    /// it opened; null when it opened none or more than one.
    [[nodiscard]] QWidget* open(const std::string& id, const std::string& className)
    {
        const an::ui::process_registry::ProcessDescriptor* descriptor = registered(id);
        EXPECT_NE(descriptor, nullptr) << id;
        if (descriptor == nullptr)
        {
            return nullptr;
        }
        if (descriptor->launch_mode == Mode::TopLevel)
        {
            descriptor->launch_top_level();
        }
        else
        {
            descriptor->launch_single_image(m_view.get());
        }
        const std::vector<an::tests::processes::shared::OpenedProcessWindow> opened = openedProcessWindows(className);
        EXPECT_EQ(opened.size(), 1u) << id;
        return opened.size() == 1u ? opened.front().window : nullptr;
    }

    [[nodiscard]] ProcessTestContext& context()
    {
        return *m_context;
    }

    [[nodiscard]] ImageViewControl* view()
    {
        return m_view.get();
    }

private:
    std::unique_ptr<ProcessTestContext> m_context;
    std::optional<ScopedAppContext> m_installed;
    std::optional<ClosesOpenedWindows> m_closes;
    ViewHandle m_view;
};

/// A widget of a window by its object name; fails the case when there is none.
template <typename WidgetT> [[nodiscard]] WidgetT* widgetNamed(const QWidget& window, const char* name)
{
    auto* widget = window.findChild<WidgetT*>(QString::fromLatin1(name));
    EXPECT_NE(widget, nullptr) << name;
    return widget;
}

/// Reaches the framework's own enabling of a window's controls - what it does as a run starts and lands.
struct ProcessControlsAccess : ImageProcessControl
{
    static void enable(ImageProcessControl& window, bool enabled)
    {
        (window.*(&ProcessControlsAccess::enableProcessControls))(enabled);
    }
};

/// Reaches whether a preview window stretches its preview through the framework's execution autostretch.
struct PreviewStretchAccess : ImageRTPreviewControl
{
    [[nodiscard]] static bool stretchesThroughTheFramework(const ImageRTPreviewControl& window)
    {
        return (window.*(&PreviewStretchAccess::isExecutionAutostretchSupported))();
    }
};

/// A process holding stars for the star windows: no stars, and the view they would be read from.
class StandInStarHandler final : public QWidget, public StarsFilterControlHandler
{
public:
    explicit StandInStarHandler(ImageViewControl* view) : m_view(view) {}

    ImageViewControl* getViewControl() override
    {
        return m_view;
    }

private:
    ImageViewControl* m_view;
};

} // namespace

// The PixelMath window writes its local and global symbols in the monospace face and asks the process shell for a
// quarter more width than its content needs, leaving the height as it is.
TEST(ProcessWindowPresentationTest, PixelMathWritesSymbolsInTheMonospaceFaceInAWiderWindow)
{
    OpenedOnAView opened;
    ASSERT_TRUE(opened.ready());
    QWidget* window = opened.open("transforms.actionPixel_Math_2", "PixelMathProcess");
    ASSERT_NE(window, nullptr);

    const QFont& monospace = an::ui::tokens().typography.monospace;
    for (const char* name : {"localSymbolsTextEdit", "globalSymbolsTextEdit"})
    {
        auto* editor = widgetNamed<QPlainTextEdit>(*window, name);
        ASSERT_NE(editor, nullptr);
        EXPECT_EQ(editor->font().family(), monospace.family()) << name;
        EXPECT_EQ(editor->font().pointSize(), monospace.pointSize()) << name;
    }
    EXPECT_DOUBLE_EQ(window->property("anProcessWindowSizeMultiplierX").toDouble(), 1.25);
    EXPECT_DOUBLE_EQ(window->property("anProcessWindowSizeMultiplierY").toDouble(), 1.0);
}

// The generalized hyperbolic transformation's symmetry-point warning is set in the heading face, bold, and centred.
TEST(ProcessWindowPresentationTest, TheSymmetryPointWarningIsSetInTheHeadingFace)
{
    OpenedOnAView opened;
    ASSERT_TRUE(opened.ready());
    QWidget* window = opened.open("transforms.actionGeneralized_Hyperbolic_Transformation", "GHTransformationProcess");
    ASSERT_NE(window, nullptr);

    auto* warning = widgetNamed<QLabel>(*window, "spText");
    ASSERT_NE(warning, nullptr);
    const QFont& heading = an::ui::tokens().typography.heading3;
    EXPECT_EQ(warning->font().family(), heading.family());
    EXPECT_EQ(warning->font().pointSize(), heading.pointSize());
    EXPECT_EQ(warning->font().weight(), QFont::Bold);
    EXPECT_TRUE(warning->alignment().testFlag(Qt::AlignHCenter));
}

// The image list analyzer states its optimization status in the body face at a heavier weight - with no images, that
// none is needed.
TEST(ProcessWindowPresentationTest, TheAnalyzersOptimizationStatusIsSetInTheBodyFace)
{
    OpenedOnAView opened;
    ASSERT_TRUE(opened.ready());
    QWidget* window = opened.open("multi_image_stacking.actionImage_List_Analyzer", "ImageListAnalyzerProcess");
    ASSERT_NE(window, nullptr);

    auto* status = widgetNamed<QLabel>(*window, "showInfoLabel");
    ASSERT_NE(status, nullptr);
    EXPECT_EQ(status->text(), QStringLiteral("No Optimization Needed"));
    const QFont& body = an::ui::tokens().typography.body;
    EXPECT_EQ(status->font().pointSize(), body.pointSize());
    EXPECT_EQ(status->font().weight(), QFont::DemiBold);
}

// The star mask window is never shorter than its detection settings need, whichever star profile it fits.
TEST(ProcessWindowPresentationTest, TheStarMaskWindowKeepsRoomForItsDetectionSettings)
{
    OpenedOnAView opened;
    ASSERT_TRUE(opened.ready());
    QWidget* window = opened.open("star_object.actionStar_Mask", "StarMaskProcess");
    ASSERT_NE(window, nullptr);

    EXPECT_GE(window->minimumHeight(), 860);
    EXPECT_GE(window->height(), window->minimumHeight());
    auto* profile = widgetNamed<QComboBox>(*window, "profileTypeSelector");
    ASSERT_NE(profile, nullptr);
    ASSERT_GT(profile->count(), 1);
    for (int index = profile->count() - 1; index >= 0; --index)
    {
        profile->setCurrentIndex(index);
        EXPECT_GE(window->minimumHeight(), 860) << profile->itemText(index).toStdString();
        EXPECT_GE(window->height(), window->minimumHeight()) << profile->itemText(index).toStdString();
    }
}

// The isotropic choice belongs to the Bregman method: with Chambolle chosen it stays disabled when the window's
// controls come back after a run, and with Bregman chosen it comes back with them.
TEST(ProcessWindowPresentationTest, TotalVariationKeepsTheIsotropicChoiceForBregmanOnly)
{
    OpenedOnAView opened;
    ASSERT_TRUE(opened.ready());
    auto* window = dynamic_cast<ImageProcessControl*>(
        opened.open("denoising.actionTotal_Variation_Denoise_Filter", "TotalVariationDenoiseProcess"));
    ASSERT_NE(window, nullptr);
    auto* method = widgetNamed<QComboBox>(*window, "algorithmSelector");
    auto* isotropic = widgetNamed<QCheckBox>(*window, "isotropic");
    ASSERT_NE(method, nullptr);
    ASSERT_NE(isotropic, nullptr);

    method->setCurrentText(QStringLiteral("Chambolle"));
    ASSERT_EQ(method->currentText(), QStringLiteral("Chambolle"));
    EXPECT_FALSE(isotropic->isEnabled());
    ProcessControlsAccess::enable(*window, false);
    EXPECT_FALSE(method->isEnabled());
    ProcessControlsAccess::enable(*window, true);
    EXPECT_TRUE(method->isEnabled());
    EXPECT_FALSE(isotropic->isEnabled()) << "Chambolle has no isotropic choice";

    method->setCurrentText(QStringLiteral("Bregman"));
    ASSERT_EQ(method->currentText(), QStringLiteral("Bregman"));
    ProcessControlsAccess::enable(*window, false);
    EXPECT_FALSE(isotropic->isEnabled());
    ProcessControlsAccess::enable(*window, true);
    EXPECT_TRUE(isotropic->isEnabled());
}

// The NBRGB combination stretches its preview through the framework's execution autostretch, as every preview window
// that offers one does, and offers no stretch controls of its own beside it.
TEST(ProcessWindowPresentationTest, TheNbrgbPreviewIsStretchedThroughTheFramework)
{
    OpenedOnAView opened;
    ASSERT_TRUE(opened.ready());
    auto* window =
        dynamic_cast<NBRGBCombinationProcess*>(opened.open("color.actionNBRGB_Combination", "NBRGBCombinationProcess"));
    ASSERT_NE(window, nullptr);

    EXPECT_TRUE(PreviewStretchAccess::stretchesThroughTheFramework(*window));
    for (const QAbstractButton* button : window->findChildren<QAbstractButton*>())
    {
        EXPECT_FALSE(button->text().contains(QStringLiteral("AutoSTF")) ||
                     button->text().contains(QStringLiteral("Linked STF")))
            << button->objectName().toStdString();
    }
}

// The profiled star mask window holds the size of its content: it can be neither shrunk nor grown, so no control
// overlaps another.
TEST(ProcessWindowPresentationTest, TheProfiledStarMaskWindowHoldsTheSizeOfItsContent)
{
    OpenedOnAView opened;
    ASSERT_TRUE(opened.ready());
    StandInStarHandler handler(opened.view());
    auto window = std::make_unique<ProfiledStarMaskCreatorControl>(&handler);
    window->setAttribute(Qt::WA_DeleteOnClose, false);

    EXPECT_EQ(window->minimumSize(), window->maximumSize());
    EXPECT_EQ(window->size(), window->minimumSize());
    EXPECT_TRUE(window->minimumSize().isValid());
    EXPECT_GT(window->minimumWidth(), 0);
    window->resize(window->width() + 200, window->height() + 200);
    EXPECT_EQ(window->size(), window->minimumSize());
}
