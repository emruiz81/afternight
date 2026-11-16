// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A process with no preview shown as a mirror page, on a test process of the framework: what its window laid out is
/// the page's body and a view of its target is the canvas - the view the person works on, `interactionView`; that view
/// shows the target's own image, shared, follows each edit of it and a change of target, never writes it, and lets it
/// go when the task closes; a process whose descriptor states no mirror page stays the window it was.

#include "Test_ControlsProcessFramework/ProcessFrameworkTestSupport.h"

#include "app/ProcessRegistry.h"
#include "components/AnCollapsibleSection.h"
#include "controls/ImageProcessControl.h"
#include "controls/ProcessLaunchScope.h"
#include "controls/ProcessPageLayout.h"
#include "controls/ViewChrome.h"
#include "controls/ViewControlMode.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{

using namespace an::tests::processes;
using an::ui::ProcessPageLayout;

/// A process of no preview, built as the processes of that kind are: a form naming its target, two groups and its
/// buttons in its window's own layout. It keeps every change of the view it is told to work on.
class MirrorTestProcess final : public ImageProcessControl
{
public:
    explicit MirrorTestProcess(ImageViewControl* target) : ImageProcessControl(target, "Test Mirror")
    {
        auto* root = new QVBoxLayout(this);
        auto* form = new QFormLayout();
        targetSelector = new QComboBox(this);
        targetSelector->setObjectName(QStringLiteral("targetImageSelector"));
        form->addRow(new QLabel(QStringLiteral("Target image"), this), targetSelector);
        root->addLayout(form);
        for (int group = 1; group <= 2; ++group)
        {
            auto* box = new QGroupBox(QStringLiteral("Group %1").arg(group), this);
            box->setObjectName(QStringLiteral("group%1").arg(group));
            auto* inside = new QVBoxLayout(box);
            inside->addWidget(new QSpinBox(box));
            root->addWidget(box);
        }
        buttons = new QDialogButtonBox(QDialogButtonBox::Close | QDialogButtonBox::Reset, this);
        root->addWidget(buttons);
        QObject::connect(buttons->button(QDialogButtonBox::Close), &QAbstractButton::clicked, this,
                         [this]() { close(); });
        m_SupportTargetViewControlSelection = true;
        initializeTargetViewControlComboBox(targetSelector);
        setAttribute(Qt::WA_DeleteOnClose, false);
        resize(400, 300);
    }

    /// Every change of the view the process works on it was told of: the view before, the view now.
    std::vector<std::pair<ImageViewControl*, ImageViewControl*>> interactionChanges;

    QComboBox* targetSelector = nullptr;
    QDialogButtonBox* buttons = nullptr;

protected:
    void interactionViewChanged(ImageViewControl* previous, ImageViewControl* now) override
    {
        interactionChanges.emplace_back(previous, now);
    }
};

/// A mirror test process, deleted with the case.
struct MirrorDeleter
{
    void operator()(MirrorTestProcess* process) const
    {
        delete process;
        QCoreApplication::processEvents();
    }
};
using MirrorHandle = std::unique_ptr<MirrorTestProcess, MirrorDeleter>;

/// A context with a page presenter lent, installed for the case; the presenter goes after what the case opens.
struct MirrorContext
{
    MirrorContext() : context(openProcessContext())
    {
        if (context != nullptr)
        {
            context->presenter = &presenter;
            installed = std::make_unique<InstalledContext>(*context);
        }
    }
    ~MirrorContext()
    {
        installed.reset();
        if (context != nullptr)
        {
            context->presenter = nullptr;
        }
    }

    PagePresenter presenter;
    std::unique_ptr<ProcessTestContext> context;
    std::unique_ptr<InstalledContext> installed;
};

/// A grey 64 by 48 target whose samples are `value`, registered with the context as a view of the application is.
[[nodiscard]] ViewHandle registeredTarget(ProcessTestContext& context, float value, const std::string& name)
{
    std::optional<an::ui::ImageDocument> document =
        greyDocument(context.engineSession(), 64u, 48u, [value](std::uint32_t, std::uint32_t) { return value; });
    EXPECT_TRUE(document.has_value());
    if (!document.has_value())
    {
        return ViewHandle();
    }
    ViewHandle view = makeTargetView(name, std::move(*document));
    context.addViewControl(view.get());
    context.setActiveImageViewControl(view.get());
    return view;
}

/// The mirror test process over a target, made inside a launch whose descriptor states a mirror page - or a window -
/// and shown, which takes it into a page when the presenter adopts it.
[[nodiscard]] MirrorHandle showMirror(ImageViewControl& target,
                                      const an::ui::ProcessSurface surface = an::ui::ProcessSurface::MirrorPage)
{
    an::ui::process_registry::ProcessDescriptor descriptor;
    descriptor.id = "test.actionTest_Mirror";
    descriptor.surface = surface;
    MirrorHandle process;
    {
        const an::ui::ProcessLaunchScope launched(descriptor);
        process.reset(new MirrorTestProcess(&target));
    }
    process->show();
    pumpQuietly();
    return process;
}

/// A grey asset of the target's size whose samples are `value`.
[[nodiscard]] std::optional<an::consumer::ImageAsset> greyAsset(const an::consumer::EngineSession& session,
                                                                const float value)
{
    return makeAsset<float>(session, 64u, 48u, Channels::Grey,
                            [value](std::uint32_t, std::uint32_t, std::uint32_t) { return value; });
}

} // namespace

// The window's controls are the page's body - its groups as sections, its target at the body's head, its buttons the
// footer's - and the canvas is a view of the target in a static preview's mode, its toolbar outside it: the view the
// person works on, which the process is told of once. A mirror page has no toggles row.
TEST(MirrorPage, TheWindowsControlsAreTheBodyAndTheCanvasIsAViewOfTheTarget)
{
    MirrorContext mirrored;
    ASSERT_NE(mirrored.context, nullptr);
    ViewHandle target = registeredTarget(*mirrored.context, 0.25f, "mirror target");
    ASSERT_NE(target, nullptr);
    MirrorHandle process = showMirror(*target);
    ProcessPageLayout* const page = process->pageLayout();
    ASSERT_NE(page, nullptr) << "a process whose descriptor states a mirror page is arranged as one";
    EXPECT_TRUE(process->isPresentedAsPage());

    ImageViewControl* const mirror = page->previewView();
    ASSERT_NE(mirror, nullptr) << "the canvas shows a view";
    EXPECT_NE(mirror, target.get());
    EXPECT_EQ(process->interactionView(), mirror) << "the person works on the page's view";
    EXPECT_TRUE(page->canvas()->isAncestorOf(mirror));
    EXPECT_EQ(mirror->getViewControlMode(), ViewControlMode::st_preview);
    EXPECT_EQ(mirror->chromeMode(), an::ui::ViewChromeMode::External);
    ASSERT_EQ(process->interactionChanges.size(), 1u);
    EXPECT_EQ(process->interactionChanges.front(), std::make_pair(target.get(), mirror));

    EXPECT_FALSE(page->toggles()->isVisibleTo(process.get())) << "no preview, no toggles";
    EXPECT_TRUE(page->bodyContent()->isAncestorOf(process->targetSelector));
    ASSERT_EQ(page->sections().size(), 2u);
    EXPECT_EQ(page->sections()[0]->title(), QStringLiteral("Group 1"));
    EXPECT_EQ(page->sections()[1]->title(), QStringLiteral("Group 2"));
    EXPECT_LT(process->targetSelector->mapTo(page->bodyContent(), QPoint(0, 0)).y(),
              page->sections()[0]->mapTo(page->bodyContent(), QPoint(0, 0)).y())
        << "the target leads the body";
    EXPECT_NE(page->footerButton(QDialogButtonBox::Close), nullptr);
    EXPECT_NE(page->footerButton(QDialogButtonBox::Reset), nullptr);
    EXPECT_FALSE(process->buttons->isVisibleTo(process.get())) << "the window's buttons are the footer's";
    EXPECT_EQ(page->parts().buttonBox, process->buttons);
    EXPECT_EQ(page->parts().targetSelector, process->targetSelector);
    EXPECT_EQ(page->parts().previewLayout, nullptr);
}

// The page's view shows the target's own image, shared - no copy of it - and follows each edit of the target: it shows
// the image the target holds after it.
TEST(MirrorPage, ThePagesViewShowsTheTargetAndFollowsAnEditOfIt)
{
    MirrorContext mirrored;
    ASSERT_NE(mirrored.context, nullptr);
    ViewHandle target = registeredTarget(*mirrored.context, 0.25f, "mirror followed");
    ASSERT_NE(target, nullptr);
    MirrorHandle process = showMirror(*target);
    ImageViewControl* const mirror = process->interactionView();
    ASSERT_NE(mirror, nullptr);
    ASSERT_TRUE(settleView(*mirror));
    ASSERT_TRUE(mirror->hasImage());
    EXPECT_EQ(mirror->document().asset(), target->document().asset()) << "the target's image, shared";
    EXPECT_TRUE(mirror->document().isShared());
    EXPECT_FLOAT_EQ(static_cast<float>(documentSample(mirror->document(), 3u, 2u)), 0.25f);

    std::optional<an::consumer::ImageAsset> edited = greyAsset(mirrored.context->engineSession(), 0.75f);
    ASSERT_TRUE(edited.has_value());
    target->replaceImage(std::move(*edited));
    ASSERT_TRUE(settleView(*target));
    ASSERT_TRUE(settleView(*mirror));
    EXPECT_EQ(mirror->document().asset(), target->document().asset()) << "the page shows the target after its edit";
    EXPECT_FLOAT_EQ(static_cast<float>(documentSample(mirror->document(), 3u, 2u)), 0.75f);
}

// The page's view never writes the target: its document refuses every writer, and the target's image and revision are
// what they were while the page shows it.
TEST(MirrorPage, ThePagesViewNeverWrites)
{
    MirrorContext mirrored;
    ASSERT_NE(mirrored.context, nullptr);
    ViewHandle target = registeredTarget(*mirrored.context, 0.25f, "mirror read only");
    ASSERT_NE(target, nullptr);
    ASSERT_TRUE(settleView(*target));
    const an::ui::DocumentRevision before = target->document().revision();
    const bool pendingBefore = target->hasPendingChanges();
    MirrorHandle process = showMirror(*target);
    ImageViewControl* const mirror = process->interactionView();
    ASSERT_NE(mirror, nullptr);
    ASSERT_TRUE(settleView(*mirror));

    an::Result<an::ui::ImageDocumentWriter> writer = mirror->document().tryWrite();
    EXPECT_FALSE(writer.hasValue()) << "the page's view hands out no writer";
    mirror->fitToWindow();
    mirror->setViewMode(ImageViewMode::AUTOSTRETCH_MODE);
    ASSERT_TRUE(settleView(*mirror));
    EXPECT_EQ(target->document().revision(), before) << "nothing the page's view does writes the target";
    EXPECT_EQ(target->hasPendingChanges(), pendingBefore);
    EXPECT_FLOAT_EQ(static_cast<float>(documentSample(target->document(), 3u, 2u)), 0.25f);
}

// A process pointed at another image shows that image on its page, which stays the view the person works on.
TEST(MirrorPage, AnotherTargetIsShownOnThePage)
{
    MirrorContext mirrored;
    ASSERT_NE(mirrored.context, nullptr);
    ViewHandle first = registeredTarget(*mirrored.context, 0.25f, "mirror first");
    ViewHandle second = registeredTarget(*mirrored.context, 0.5f, "mirror second");
    ASSERT_TRUE(first != nullptr && second != nullptr);
    MirrorHandle process = showMirror(*first);
    ImageViewControl* const mirror = process->interactionView();
    ASSERT_NE(mirror, nullptr);

    process->changeTargetViewControl(second.get());
    ASSERT_TRUE(settleView(*mirror));
    EXPECT_EQ(process->interactionView(), mirror);
    EXPECT_EQ(mirror->document().asset(), second->document().asset());
    EXPECT_FLOAT_EQ(static_cast<float>(documentSample(mirror->document(), 3u, 2u)), 0.5f);
}

// Closing the task lets the target's image go: the page's view holds none of it, and the image is held as before the
// task opened.
TEST(MirrorPage, ClosingTheTaskReleasesIt)
{
    MirrorContext mirrored;
    ASSERT_NE(mirrored.context, nullptr);
    ViewHandle target = registeredTarget(*mirrored.context, 0.25f, "mirror released");
    ASSERT_NE(target, nullptr);
    ASSERT_TRUE(settleView(*target));
    const long holdersBefore = target->document().asset().use_count();
    MirrorHandle process = showMirror(*target);
    ImageViewControl* const mirror = process->interactionView();
    ASSERT_NE(mirror, nullptr);
    ASSERT_TRUE(settleView(*mirror));
    ASSERT_GT(target->document().asset().use_count(), holdersBefore) << "the page's view holds the image";

    ASSERT_TRUE(process->close());
    pumpQuietly();
    ASSERT_TRUE(settleView(*target));
    EXPECT_FALSE(mirror->hasImage()) << "a closed task's view shows nothing";
    EXPECT_EQ(target->document().asset().use_count(), holdersBefore) << "the image is let go";
}

// A process whose descriptor states no mirror page is no page: it stays a window, and the person works on its target.
TEST(MirrorPage, AProcessOfAnotherSurfaceStaysAWindow)
{
    MirrorContext mirrored;
    ASSERT_NE(mirrored.context, nullptr);
    ViewHandle target = registeredTarget(*mirrored.context, 0.25f, "mirror window");
    ASSERT_NE(target, nullptr);
    MirrorHandle process = showMirror(*target, an::ui::ProcessSurface::Window);
    EXPECT_EQ(process->pageLayout(), nullptr);
    EXPECT_TRUE(process->isWindow());
    EXPECT_EQ(process->interactionView(), target.get());
    EXPECT_TRUE(process->interactionChanges.empty());
}
