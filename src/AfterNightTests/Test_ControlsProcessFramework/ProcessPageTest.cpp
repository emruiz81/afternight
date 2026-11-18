// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A process shown as a page, on the framework's test process: the column's parts in their order beside the canvas,
/// the body that scrolls under a footer that does not, the column's widths, the controls' groups as sections that a
/// short body closes by their use, Target image and Channel at the body's head; page mode - no window geometry, the
/// JSON settings still kept, a page and a window switched with nothing of the process touched, a run's row, its Cancel,
/// Esc and Ctrl+Enter; and a page's own shortcuts, which win inside it over a main window's.

#include "Test_ControlsProcessFramework/ProcessFrameworkTestSupport.h"

#include "app/ProcessRegistry.h"
#include "components/AnCollapsibleSection.h"
#include "components/AnInfoBanner.h"
#include "controls/PageShortcutScope.h"
#include "controls/ProcessLaunchScope.h"
#include "controls/ProcessPageLayout.h"
#include "controls/ViewChrome.h"
#include "widgets/ImageViewWidget.h"
#include "theme/AnimationUtils.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSplitter>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <tuple>

namespace
{

using namespace an::tests::processes;
using an::ui::ProcessColumnWidth;
using an::ui::ProcessPageLayout;
using Phase = an::ui::ProcessRunState::Phase;

/// A grey 64 by 48 Float32 target whose samples are `value`.
[[nodiscard]] ViewHandle flatTarget(const an::consumer::EngineSession& session, float value,
                                    const std::string& name = "page target")
{
    std::optional<an::ui::ImageDocument> document =
        greyDocument(session, 64u, 48u, [value](std::uint32_t, std::uint32_t) { return value; });
    EXPECT_TRUE(document.has_value());
    return makeTargetView(name, std::move(*document));
}

/// A context with a page presenter lent, installed for the case. The presenter is declared before anything a case
/// opens, so the processes go before the hosts that hold them.
struct PagedContext
{
    explicit PagedContext(QSize centre = PagePresenter::kCentre) : presenter(centre), context(openProcessContext())
    {
        if (context != nullptr)
        {
            context->presenter = &presenter;
            installed = std::make_unique<InstalledContext>(*context);
        }
    }
    ~PagedContext()
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

/// A descriptor whose launch states a column width, for a process made inside its scope.
[[nodiscard]] an::ui::process_registry::ProcessDescriptor describedAs(const ProcessColumnWidth width)
{
    an::ui::process_registry::ProcessDescriptor descriptor;
    descriptor.id = "test.actionTest_Scale";
    descriptor.surface = an::ui::ProcessSurface::PreviewPage;
    descriptor.columnWidth = width;
    return descriptor;
}

/// Opens the test process over a target and shows it, which takes it into a page when the presenter adopts.
[[nodiscard]] ProcessHandle showProcess(ImageViewControl& target, const TestProcessOptions& options)
{
    ProcessHandle process = openProcess(target, options);
    process->show();
    pumpQuietly();
    return process;
}

/// The index of a widget among the items of a layout; -1 when it is none of them.
[[nodiscard]] int itemIndex(const QLayout& layout, const QWidget* widget)
{
    for (int index = 0; index < layout.count(); ++index)
    {
        if (layout.itemAt(index)->widget() == widget)
        {
            return index;
        }
    }
    return -1;
}

/// A widget's top, in the coordinates of another it lies in.
[[nodiscard]] int topIn(const QWidget& widget, const QWidget& ancestor)
{
    return widget.mapTo(&ancestor, QPoint(0, 0)).y();
}

/// The sections' open states, in order.
[[nodiscard]] std::vector<bool> openStates(const ProcessPageLayout& page)
{
    std::vector<bool> states;
    for (const an::ui::AnCollapsibleSection* section : page.sections())
    {
        states.push_back(section->isExpanded());
    }
    return states;
}

/// How many sections lie under a widget; a section declares no meta-object of its own, so it is known by its type.
[[nodiscard]] int sectionsUnder(const QWidget& widget)
{
    int count = 0;
    for (const QWidget* child : widget.findChildren<QWidget*>())
    {
        count += dynamic_cast<const an::ui::AnCollapsibleSection*>(child) != nullptr ? 1 : 0;
    }
    return count;
}

/// Opens or closes a section as its header's click does.
void setOpen(an::ui::AnCollapsibleSection& section, const bool open)
{
    if (section.isExpanded() != open)
    {
        section.toggleButton()->click();
    }
    pumpQuietly();
}

} // namespace

// The page is the canvas beside the column; the column holds, in order, the readout slot, the header, the toggles,
// the banner, the body and the footer; the preview lies in the canvas, the preview checkbox in the toggles.
TEST(ProcessPageLayout, TheColumnHoldsItsPartsInTheOrderOfThePage)
{
    PagedContext paged;
    ASSERT_NE(paged.context, nullptr);
    ViewHandle target = flatTarget(paged.context->engineSession(), 0.25f);
    ProcessHandle process = showProcess(*target, TestProcessOptions{.preview = true});
    ProcessPageLayout* page = process->pageLayout();
    ASSERT_NE(page, nullptr) << "the presenter took the process into a page";
    EXPECT_TRUE(process->isPresentedAsPage());
    EXPECT_FALSE(process->isWindow());
    EXPECT_EQ(paged.presenter.adoptions, 1);
    ASSERT_TRUE(settleProcess(*process));

    QLayout* const column = page->column()->layout();
    ASSERT_NE(column, nullptr);
    const std::vector<int> order{itemIndex(*column, page->readoutSlot()), itemIndex(*column, page->header()),
                                 itemIndex(*column, page->toggles()),     itemIndex(*column, page->banner()),
                                 itemIndex(*column, page->body()),        itemIndex(*column, page->footer())};
    for (const int index : order)
    {
        EXPECT_GE(index, 0) << "every part is the column's";
    }
    EXPECT_TRUE(std::is_sorted(order.begin(), order.end()));
    EXPECT_TRUE(page->page()->isAncestorOf(page->canvas()));
    EXPECT_LT(page->canvas()->mapTo(page->page(), QPoint(0, 0)).x(),
              page->column()->mapTo(page->page(), QPoint(0, 0)).x())
        << "the canvas is left of the column";
    ASSERT_NE(process->previewView(), nullptr);
    EXPECT_TRUE(page->canvas()->isAncestorOf(process->previewView())) << "the preview is the canvas";
    EXPECT_TRUE(page->toggles()->isAncestorOf(process->previewCheckbox));
    EXPECT_FALSE(page->banner()->isVisible()) << "no message, no banner";
    EXPECT_EQ(page->header()->findChild<QLabel*>(QStringLiteral("pageTitle"))->text(), QStringLiteral("Test Scale"));
}

// The footer stays under the body and outside it, and shows the box's buttons - Reset, Close, Apply, in that order -
// each pressing the box's own.
TEST(ProcessPageLayout, TheFooterIsOutsideTheScrollArea)
{
    PagedContext paged;
    ASSERT_NE(paged.context, nullptr);
    ViewHandle target = flatTarget(paged.context->engineSession(), 0.25f);
    ProcessHandle process = showProcess(*target, TestProcessOptions{});
    ProcessPageLayout* page = process->pageLayout();
    ASSERT_NE(page, nullptr);

    EXPECT_FALSE(page->body()->isAncestorOf(page->footer()));
    EXPECT_GE(topIn(*page->footer(), *page->column()), topIn(*page->body(), *page->column()) + page->body()->height());
    QAbstractButton* reset = page->footerButton(QDialogButtonBox::Reset);
    QAbstractButton* close = page->footerButton(QDialogButtonBox::Close);
    QAbstractButton* apply = page->footerButton(QDialogButtonBox::Apply);
    ASSERT_TRUE(reset != nullptr && close != nullptr && apply != nullptr);
    EXPECT_LT(reset->x(), close->x());
    EXPECT_LT(close->x(), apply->x());
    EXPECT_FALSE(process->buttons->isVisibleTo(process.get())) << "the box is kept, out of sight";

    // The footer shows the box's state, and presses its buttons.
    EXPECT_FALSE(apply->isEnabled()) << "the framework's Apply starts disabled";
    process->buttons->button(QDialogButtonBox::Apply)->setEnabled(true);
    pumpQuietly();
    EXPECT_TRUE(apply->isEnabled());
    apply->click();
    ASSERT_TRUE(settleProcess(*process));
    EXPECT_EQ(process->runsOf(an::ui::ProcessRunKind::Apply), 1);
    close->click();
    pumpQuietly();
    EXPECT_TRUE(process->isHidden()) << "Close closed the page";
}

// A short page scrolls its body up and down, never sideways, and keeps its footer at its foot.
TEST(ProcessPageLayout, TheBodyScrollsWhenThePageIsShort)
{
    PagedContext paged(QSize(1492, 600));
    ASSERT_NE(paged.context, nullptr);
    ViewHandle target = flatTarget(paged.context->engineSession(), 0.25f);
    ProcessHandle process = showProcess(*target, TestProcessOptions{.groups = 1, .groupHeight = 900});
    ProcessPageLayout* page = process->pageLayout();
    ASSERT_NE(page, nullptr);
    pumpQuietly();

    EXPECT_TRUE(page->body()->verticalScrollBar()->isVisible());
    EXPECT_EQ(page->body()->horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
    EXPECT_LE(page->bodyContent()->width(), page->body()->viewport()->width()) << "nothing scrolls sideways";
    QWidget* const host = paged.presenter.hostOf(*process);
    ASSERT_NE(host, nullptr);
    EXPECT_EQ(page->footer()->mapTo(host, QPoint(0, page->footer()->height())).y(), host->height())
        << "the footer is at the page's foot";
}

// The controls' side margins give way to what the controls need, whenever they need it: a control that grows wider
// after the page's first fit - a style, a font or a text that settles later - takes the room the column lacks from the
// margins, though the body has not changed and has no scroll bar to say so, so nothing of the controls lies past the
// column.
TEST(ProcessPageLayout, TheControlsMarginsGiveWayWhenTheControlsGrowAfterTheFirstFit)
{
    PagedContext paged(QSize(1492, 1000));
    ASSERT_NE(paged.context, nullptr);
    ViewHandle target = flatTarget(paged.context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{.groups = 1});
    auto* const group = process->findChild<QGroupBox*>(QStringLiteral("group1"));
    ASSERT_NE(group, nullptr);
    group->parentWidget()->layout()->setContentsMargins(12, 12, 12, 12);
    process->show();
    pumpQuietly();
    ProcessPageLayout* page = process->pageLayout();
    ASSERT_NE(page, nullptr);
    ASSERT_FALSE(page->body()->verticalScrollBar()->isVisible()) << "a tall page: the body needs no scroll bar";
    const int column = page->body()->viewport()->width();
    ASSERT_EQ(column, 360);

    // The group grows to 12 px short of the column, which the margins' 24 px do not leave.
    group->setMinimumWidth(column - 12);
    group->updateGeometry();
    pumpQuietly();
    pumpQuietly();
    EXPECT_LE(page->bodyContent()->minimumSizeHint().width(), page->body()->viewport()->width())
        << "the controls are wider than the column";
    EXPECT_EQ(page->body()->viewport()->width(), column) << "the body did not change";
    const QPoint at = group->mapTo(page->bodyContent(), QPoint(0, 0));
    EXPECT_GE(at.x(), 0);
    EXPECT_LE(at.x() + group->width(), page->body()->viewport()->width()) << "the group lies within the column";
    process.reset();
}

// The column is 360 px wide, 520 px for a wide process; 320 and 440 px in a window under 1200 px wide.
TEST(ProcessPageLayout, TheColumnsWidths)
{
    for (const auto& [width, wanted, narrow] :
         {std::tuple{ProcessColumnWidth::Standard, 360, 320}, std::tuple{ProcessColumnWidth::Wide, 520, 440}})
    {
        PagedContext paged;
        ASSERT_NE(paged.context, nullptr);
        ViewHandle target = flatTarget(paged.context->engineSession(), 0.25f);
        const an::ui::process_registry::ProcessDescriptor descriptor = describedAs(width);
        ProcessHandle process;
        {
            const an::ui::ProcessLaunchScope launched(descriptor);
            process = openProcess(*target, TestProcessOptions{});
        }
        EXPECT_EQ(process->processColumnWidth(), width);
        process->show();
        pumpQuietly();
        ProcessPageLayout* page = process->pageLayout();
        ASSERT_NE(page, nullptr);
        EXPECT_EQ(page->column()->width(), wanted);
        EXPECT_EQ(an::ui::pageColumnWidth(width, 1536), wanted);

        QWidget* const host = paged.presenter.hostOf(*process);
        ASSERT_NE(host, nullptr);
        host->resize(1100, 700);
        pumpQuietly();
        EXPECT_EQ(page->column()->width(), narrow) << "a window under 1200 px narrows the column";
        EXPECT_EQ(an::ui::pageColumnWidth(width, 1199), narrow);
        process.reset();
    }
}

// The controls' top-level group boxes are sections, titled as the groups and in their order, each holding its group,
// which open and close by their header.
TEST(ProcessPageLayout, TheGroupsAreSectionsInTheirOrder)
{
    PagedContext paged;
    ASSERT_NE(paged.context, nullptr);
    ViewHandle target = flatTarget(paged.context->engineSession(), 0.25f);
    ProcessHandle process = showProcess(*target, TestProcessOptions{.groups = 3, .groupHeight = 60});
    ProcessPageLayout* page = process->pageLayout();
    ASSERT_NE(page, nullptr);

    const std::vector<an::ui::AnCollapsibleSection*>& sections = page->sections();
    ASSERT_EQ(sections.size(), 3u);
    int previous = -1;
    for (int index = 0; index < 3; ++index)
    {
        an::ui::AnCollapsibleSection* section = sections[static_cast<std::size_t>(index)];
        EXPECT_EQ(section->title(), QStringLiteral("Group %1").arg(index + 1));
        auto* group = process->findChild<QGroupBox*>(QStringLiteral("group%1").arg(index + 1));
        ASSERT_NE(group, nullptr);
        EXPECT_TRUE(section->isAncestorOf(group));
        EXPECT_TRUE(page->bodyContent()->isAncestorOf(section));
        const int top = topIn(*section, *page->bodyContent());
        EXPECT_GT(top, previous) << "in the groups' order";
        previous = top;
        EXPECT_TRUE(section->isExpanded()) << "a section starts open";
    }
    setOpen(*sections[1], false);
    EXPECT_FALSE(sections[1]->isExpanded());
    setOpen(*sections[1], true);
    EXPECT_TRUE(sections[1]->isExpanded());
}

// Controls of a single group are left as they are: a lone section would only add a header.
TEST(ProcessPageLayout, ASingleGroupIsNoSection)
{
    PagedContext paged;
    ASSERT_NE(paged.context, nullptr);
    ViewHandle target = flatTarget(paged.context->engineSession(), 0.25f);
    ProcessHandle process = showProcess(*target, TestProcessOptions{.groups = 1});
    ASSERT_NE(process->pageLayout(), nullptr);
    EXPECT_TRUE(process->pageLayout()->sections().empty());
    EXPECT_TRUE(sectionsUnder(*process) == 0);
}

// Which sections are open is kept with the process's settings, and the next page of the process opens them so.
TEST(ProcessPageLayout, TheSectionsAreKeptWithTheProcessSettings)
{
    PagedContext paged;
    ASSERT_NE(paged.context, nullptr);
    ViewHandle target = flatTarget(paged.context->engineSession(), 0.25f);
    {
        ProcessHandle process = showProcess(*target, TestProcessOptions{.groups = 3, .groupHeight = 60});
        ASSERT_NE(process->pageLayout(), nullptr);
        setOpen(*process->pageLayout()->sections()[1], false);
        process->close();
        pumpQuietly();
    }
    {
        ProcessHandle again = showProcess(*target, TestProcessOptions{.groups = 3, .groupHeight = 60});
        ASSERT_NE(again->pageLayout(), nullptr);
        EXPECT_EQ(openStates(*again->pageLayout()), (std::vector<bool>{true, false, true}));
    }
}

// Opening a section that would push the body past the column's height closes the section used longest ago: opened,
// or worked in, longest ago.
TEST(ProcessPageLayout, OpeningASectionClosesTheOneUsedLongestAgo)
{
    // The sections open and close at once, so the heights read are the ones their states ask.
    const bool animated = an::ui::AnimationUtils::animationsEnabled();
    an::ui::AnimationUtils::setAnimationsEnabled(false);
    const std::shared_ptr<void> restore(nullptr,
                                        [animated](void*) { an::ui::AnimationUtils::setAnimationsEnabled(animated); });
    PagedContext paged(QSize(1492, 700));
    ASSERT_NE(paged.context, nullptr);
    ViewHandle target = flatTarget(paged.context->engineSession(), 0.25f);
    ProcessHandle process = showProcess(*target, TestProcessOptions{.groups = 3, .groupHeight = 40});
    ProcessPageLayout* page = process->pageLayout();
    ASSERT_NE(page, nullptr);
    const std::vector<an::ui::AnCollapsibleSection*>& sections = page->sections();
    ASSERT_EQ(sections.size(), 3u);
    for (an::ui::AnCollapsibleSection* section : sections)
    {
        setOpen(*section, false);
    }

    // Groups of a height that lets two open sections fit the body and not three.
    const int viewport = page->body()->viewport()->height();
    const int closed = page->bodyContent()->sizeHint().height();
    const int header = sections[0]->sizeHint().height();
    const int group = (viewport - closed) * 2 / 5 - header;
    ASSERT_GT(group, 20) << "the body is " << viewport << " px high";
    for (int index = 1; index <= 3; ++index)
    {
        process->findChild<QGroupBox*>(QStringLiteral("group%1").arg(index))->setFixedHeight(group);
    }
    pumpQuietly();

    setOpen(*sections[0], true);
    setOpen(*sections[1], true);
    EXPECT_EQ(openStates(*page), (std::vector<bool>{true, true, false})) << "two fit";
    setOpen(*sections[2], true);
    EXPECT_EQ(openStates(*page), (std::vector<bool>{false, true, true})) << "the first was used longest ago";

    // Working in the second makes the third the one used longest ago.
    QWidget* const host = paged.presenter.hostOf(*process);
    ASSERT_NE(host, nullptr);
    host->activateWindow();
    ASSERT_TRUE(QTest::qWaitForWindowActive(host));
    process->findChild<QSpinBox*>(QStringLiteral("groupValue2"))->setFocus(Qt::OtherFocusReason);
    pumpQuietly();
    setOpen(*sections[0], true);
    EXPECT_EQ(openStates(*page), (std::vector<bool>{true, true, false}));
}

// Target image and Channel lead the body, side by side, their labels with them.
TEST(ProcessPageLayout, TargetImageAndChannelLeadTheBody)
{
    PagedContext paged;
    ASSERT_NE(paged.context, nullptr);
    ViewHandle target = flatTarget(paged.context->engineSession(), 0.25f);
    ProcessHandle process = showProcess(
        *target, TestProcessOptions{.perChannel = true, .groups = 2, .groupHeight = 60, .targetSelector = true});
    ProcessPageLayout* page = process->pageLayout();
    ASSERT_NE(page, nullptr);
    QWidget* const body = page->bodyContent();
    ASSERT_TRUE(body->isAncestorOf(process->targetSelector) && body->isAncestorOf(process->channelSelector));
    ASSERT_TRUE(body->isAncestorOf(process->targetLabel)) << "the label goes with its field";
    EXPECT_EQ(topIn(*process->targetSelector, *body), topIn(*process->channelSelector, *body)) << "side by side";
    EXPECT_LT(process->targetSelector->mapTo(body, QPoint(0, 0)).x(),
              process->channelSelector->mapTo(body, QPoint(0, 0)).x());
    EXPECT_LT(topIn(*process->targetLabel, *body), topIn(*process->targetSelector, *body));
    for (const an::ui::AnCollapsibleSection* section : page->sections())
    {
        EXPECT_GT(topIn(*section, *body), topIn(*process->targetSelector, *body)) << "the selectors come first";
    }
}

// Target image leads the body whatever layout of the controls holds it: from a row of a grid inside a group - as a
// builder that groups the images a process reads lays them out - it goes with its label, and another image's chooser
// stays in the group with its own.
TEST(ProcessPageLayout, ATargetInAGridLeadsTheBodyWithItsLabel)
{
    PagedContext paged;
    ASSERT_NE(paged.context, nullptr);
    QWidget process;
    auto* const mainLayout = new QHBoxLayout(&process);
    auto* const splitter = new QSplitter(Qt::Horizontal);
    splitter->setObjectName(QStringLiteral("mainSplitter"));
    auto* const controls = new QWidget(splitter);
    auto* const controlsLayout = new QVBoxLayout(controls);
    auto* const previewLayout = new QVBoxLayout(new QWidget(splitter));
    mainLayout->addWidget(splitter);
    auto* const images = new QGroupBox(QStringLiteral("Views"), controls);
    auto* const grid = new QGridLayout(images);
    auto* const targetLabel = new QLabel(QStringLiteral("Target image"), images);
    auto* const target = new QComboBox(images);
    auto* const otherLabel = new QLabel(QStringLiteral("Starless image"), images);
    auto* const other = new QComboBox(images);
    grid->addWidget(targetLabel, 0, 0);
    grid->addWidget(target, 0, 1, 1, 2);
    grid->addWidget(otherLabel, 1, 0);
    grid->addWidget(other, 1, 1, 1, 2);
    controlsLayout->addWidget(images);
    auto* const buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close, controls);
    controlsLayout->addWidget(buttons);

    const an::ui::ProcessStandardParts parts{
        .mainLayout = mainLayout, .previewLayout = previewLayout, .buttonBox = buttons, .targetSelector = target};
    ProcessPageLayout page(process, parts, ProcessPageLayout::Description{.title = QStringLiteral("Grid")});
    process.resize(PagePresenter::kCentre);
    process.show();
    pumpQuietly();
    QWidget* const body = page.bodyContent();
    EXPECT_FALSE(images->isAncestorOf(target)) << "the target's chooser left the group";
    EXPECT_FALSE(images->isAncestorOf(targetLabel)) << "its label went with it";
    EXPECT_TRUE(body->isAncestorOf(target) && body->isAncestorOf(targetLabel));
    EXPECT_EQ(targetLabel->parentWidget(), target->parentWidget());
    EXPECT_LT(topIn(*targetLabel, *body), topIn(*target, *body)) << "the label over its field";
    EXPECT_LT(topIn(*target, *body), topIn(*images, *body)) << "at the body's head";
    EXPECT_TRUE(images->isAncestorOf(other) && images->isAncestorOf(otherLabel)) << "the other chooser stays";
    EXPECT_GE(grid->indexOf(otherLabel), 0);
    EXPECT_GE(grid->indexOf(other), 0);
}

// The controls' groups are sections wherever the controls lay them out - inside a plain container of the controls too,
// as a builder that gives its groups a widget of their own lays them out - each in its group's place and order.
TEST(ProcessPageLayout, GroupsInAContainerOfTheControlsAreSections)
{
    PagedContext paged;
    ASSERT_NE(paged.context, nullptr);
    QWidget process;
    auto* const mainLayout = new QHBoxLayout(&process);
    auto* const splitter = new QSplitter(Qt::Horizontal);
    splitter->setObjectName(QStringLiteral("mainSplitter"));
    auto* const controls = new QWidget(splitter);
    auto* const controlsLayout = new QVBoxLayout(controls);
    auto* const previewLayout = new QVBoxLayout(new QWidget(splitter));
    mainLayout->addWidget(splitter);
    auto* const container = new QWidget(controls);
    auto* const containerLayout = new QVBoxLayout(container);
    std::vector<QGroupBox*> groups;
    for (int index = 1; index <= 2; ++index)
    {
        auto* const group = new QGroupBox(QStringLiteral("Group %1").arg(index), container);
        auto* const inside = new QVBoxLayout(group);
        inside->addWidget(new QComboBox(group));
        containerLayout->addWidget(group);
        groups.push_back(group);
    }
    controlsLayout->addWidget(container);
    auto* const buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close, controls);
    controlsLayout->addWidget(buttons);

    const an::ui::ProcessStandardParts parts{
        .mainLayout = mainLayout, .previewLayout = previewLayout, .buttonBox = buttons};
    ProcessPageLayout page(process, parts, ProcessPageLayout::Description{.title = QStringLiteral("Container")});
    process.resize(PagePresenter::kCentre);
    process.show();
    pumpQuietly();
    const std::vector<an::ui::AnCollapsibleSection*>& sections = page.sections();
    ASSERT_EQ(sections.size(), 2u);
    for (std::size_t index = 0; index < 2u; ++index)
    {
        EXPECT_EQ(sections[index]->title(), QStringLiteral("Group %1").arg(index + 1));
        EXPECT_TRUE(sections[index]->isAncestorOf(groups[index]));
        EXPECT_TRUE(container->isAncestorOf(sections[index])) << "in its group's place";
    }
    EXPECT_LT(topIn(*sections[0], *page.bodyContent()), topIn(*sections[1], *page.bodyContent()));
}

// In a shell with no readout to give, the readout slot is empty.
TEST(ProcessPageLayout, TheReadoutSlotIsEmptyWithoutAShell)
{
    PagedContext paged;
    ASSERT_NE(paged.context, nullptr);
    ViewHandle target = flatTarget(paged.context->engineSession(), 0.25f);
    ProcessHandle process = showProcess(*target, TestProcessOptions{});
    ASSERT_NE(process->pageLayout(), nullptr);
    QWidget* const slot = process->pageLayout()->readoutSlot();
    EXPECT_TRUE(slot->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly).isEmpty());
    EXPECT_EQ(slot->height(), 0);
}

// A process no presenter takes is the window it always was: no page, its shell's header and card.
TEST(ProcessPageLayout, AWindowIsUntouched)
{
    PagedContext paged;
    ASSERT_NE(paged.context, nullptr);
    paged.presenter.adopting = false;
    ViewHandle target = flatTarget(paged.context->engineSession(), 0.25f);
    ProcessHandle process = showProcess(*target, TestProcessOptions{.preview = true, .groups = 3});
    ASSERT_TRUE(settleProcess(*process));
    EXPECT_EQ(paged.presenter.adoptions, 1) << "asked once";
    EXPECT_EQ(process->pageLayout(), nullptr);
    EXPECT_FALSE(process->isPresentedAsPage());
    EXPECT_TRUE(process->isWindow());
    EXPECT_NE(process->findChild<QWidget*>(QStringLiteral("anProcessShellCard")), nullptr);
    EXPECT_TRUE(sectionsUnder(*process) == 0);
    ASSERT_NE(process->previewView(), nullptr);
    EXPECT_TRUE(process->findChild<QSplitter*>(QStringLiteral("mainSplitter"))->isAncestorOf(process->previewView()));

    process->hide();
    process->show();
    pumpQuietly();
    EXPECT_EQ(paged.presenter.adoptions, 1) << "not asked again";
}

// A window that is a row of its controls' layout and its preview's, with no splitter, is arranged as one with a
// splitter is.
TEST(ProcessPageLayout, ARowIsArrangedAsASplitterIs)
{
    PagedContext paged;
    ASSERT_NE(paged.context, nullptr);
    ViewHandle target = flatTarget(paged.context->engineSession(), 0.25f);
    ProcessHandle process = showProcess(*target, TestProcessOptions{.preview = true, .row = true});
    ProcessPageLayout* page = process->pageLayout();
    ASSERT_NE(page, nullptr);
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_NE(process->previewView(), nullptr);
    EXPECT_TRUE(page->canvas()->isAncestorOf(process->previewView()));
    EXPECT_TRUE(page->bodyContent()->isAncestorOf(process->channelSelector));
    EXPECT_TRUE(page->toggles()->isAncestorOf(process->previewCheckbox));

    // The preview off and on again lands in the canvas again.
    process->previewCheckbox->click();
    ASSERT_TRUE(settleProcess(*process));
    EXPECT_EQ(process->previewView(), nullptr);
    process->previewCheckbox->click();
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_NE(process->previewView(), nullptr);
    EXPECT_TRUE(page->canvas()->isAncestorOf(process->previewView()));
}

// The toggles row acts on the preview: Preview area draws the area as the view's own checkbox does, and Compare shows
// the original and back, naming what it shows.
TEST(ProcessPageLayout, TheTogglesActOnThePreview)
{
    PagedContext paged;
    ASSERT_NE(paged.context, nullptr);
    ViewHandle target = flatTarget(paged.context->engineSession(), 0.25f);
    ProcessHandle process = showProcess(*target, TestProcessOptions{.preview = true, .previewArea = true});
    ProcessPageLayout* page = process->pageLayout();
    ASSERT_NE(page, nullptr);
    ASSERT_TRUE(settleProcess(*process));
    ImageViewControl* const preview = process->previewView();
    ASSERT_NE(preview, nullptr);
    EXPECT_EQ(preview->chromeMode(), an::ui::ViewChromeMode::External) << "the view's toolbar is outside it";
    QAbstractButton* const area = page->previewAreaToggle();
    QAbstractButton* const compare = page->compareToggle();
    ASSERT_TRUE(area != nullptr && compare != nullptr);
    EXPECT_FALSE(area->isHidden());
    EXPECT_FALSE(compare->isHidden());

    // The test process opens with its area drawn.
    EXPECT_TRUE(area->isChecked());
    EXPECT_FALSE(process->previewArea().isEmpty());
    area->click();
    ASSERT_TRUE(settleProcess(*process));
    EXPECT_TRUE(process->previewArea().isEmpty()) << "the area is put away";
    EXPECT_FALSE(preview->getRTPreviewAreaCheckBox()->isChecked());
    preview->getRTPreviewAreaCheckBox()->click();
    ASSERT_TRUE(settleProcess(*process));
    EXPECT_FALSE(process->previewArea().isEmpty());
    EXPECT_TRUE(area->isChecked()) << "the toggle follows the view's checkbox";

    EXPECT_EQ(compare->text(), QStringLiteral("Processed"));
    compare->click();
    pumpQuietly();
    EXPECT_TRUE(preview->getViewWidget()->isAlternateImageRenderEnabled());
    EXPECT_EQ(compare->text(), QStringLiteral("Original"));
    compare->click();
    pumpQuietly();
    EXPECT_FALSE(preview->getViewWidget()->isAlternateImageRenderEnabled());
    EXPECT_EQ(compare->text(), QStringLiteral("Processed"));

    // The preview off: the canvas is empty and the toggles that act on the view are hidden.
    process->previewCheckbox->click();
    ASSERT_TRUE(settleProcess(*process));
    EXPECT_EQ(process->previewView(), nullptr);
    EXPECT_TRUE(area->isHidden());
    EXPECT_TRUE(compare->isHidden());
}

// A page restores no window geometry and stores none; its JSON settings are restored and stored as a window's are.
TEST(PageMode, NoGeometryIsRestoredOrStoredAndTheJsonSettingsStillAre)
{
    PagedContext paged;
    ASSERT_NE(paged.context, nullptr);
    paged.context->processSettings.insert(
        QStringLiteral("Test Scale"),
        ProcessTestContext::StoredProcessSettings{Qt::WindowMaximized, 900, 700, QStringLiteral("{\"factor\":5}")});
    ViewHandle target = flatTarget(paged.context->engineSession(), 0.25f);
    ProcessHandle process = showProcess(*target, TestProcessOptions{.restoresSettings = true});
    ASSERT_NE(process->pageLayout(), nullptr);
    EXPECT_EQ(process->restoredJson, "{\"factor\":5}");
    EXPECT_FALSE(process->isMaximized());
    QWidget* const host = paged.presenter.hostOf(*process);
    ASSERT_NE(host, nullptr);
    EXPECT_EQ(process->size(), host->size()) << "the page is the host's size, not the stored window's";

    process->close();
    pumpQuietly();
    const ProcessTestContext::StoredProcessSettings stored =
        paged.context->processSettings.value(QStringLiteral("Test Scale"));
    EXPECT_EQ(stored.state, Qt::WindowMaximized);
    EXPECT_EQ(stored.width, 900);
    EXPECT_EQ(stored.height, 700);
    EXPECT_EQ(stored.json, QStringLiteral("{\"factor\":2}")) << "the JSON settings are stored";
}

// A page made a window and a page again keeps its parameters, its preview, a run on its way and the lock on its
// target.
TEST(PageMode, SwitchingBetweenPageAndWindowKeepsTheProcessAsItWas)
{
    PagedContext paged;
    ASSERT_NE(paged.context, nullptr);
    ViewHandle target = flatTarget(paged.context->engineSession(), 0.25f);
    ProcessHandle process = showProcess(*target, TestProcessOptions{.preview = true});
    ASSERT_NE(process->pageLayout(), nullptr);
    ASSERT_TRUE(settleProcess(*process));
    process->setParams(TestParams{.factor = 3.0});
    ASSERT_TRUE(settleProcess(*process));
    ImageViewControl* const preview = process->previewView();
    ASSERT_NE(preview, nullptr);
    QWidget* const host = paged.presenter.hostOf(*process);
    ASSERT_NE(host, nullptr);

    process->gate.close();
    process->applyProcess();
    ASSERT_TRUE(pumpUntil([&process]() { return process->gate.arrived() >= 1; }));
    ASSERT_TRUE(target->isLockedForProcessing());

    process->detachToWindow();
    pumpQuietly();
    EXPECT_TRUE(process->isWindow());
    EXPECT_FALSE(process->isPresentedAsPage());
    EXPECT_TRUE(process->hasPageLayout()) << "the page goes with it";
    EXPECT_EQ(process->previewView(), preview);
    EXPECT_TRUE(process->runState().running());
    EXPECT_TRUE(target->isLockedForProcessing());

    ASSERT_TRUE(process->attachToPage(*host));
    pumpQuietly();
    EXPECT_FALSE(process->isWindow());
    EXPECT_TRUE(process->isPresentedAsPage());
    EXPECT_EQ(process->previewView(), preview);
    EXPECT_TRUE(process->runState().running());
    EXPECT_TRUE(target->isLockedForProcessing());

    process->gate.open();
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_TRUE(settleView(*target));
    EXPECT_EQ(process->runState().phase, Phase::Succeeded);
    EXPECT_FALSE(target->isLockedForProcessing());
    EXPECT_DOUBLE_EQ(process->runs().back().params.factor, 3.0) << "the parameters were kept";
}

// During a run the run's row shows in the banner's place, the toggles, Reset, Close and the controls are disabled and
// Cancel takes Apply's place; Cancel stops the run and leaves the image as it was.
TEST(PageMode, ARunShowsItsRowAndCancelStopsIt)
{
    PagedContext paged;
    ASSERT_NE(paged.context, nullptr);
    ViewHandle target = flatTarget(paged.context->engineSession(), 0.25f);
    ProcessHandle process = showProcess(*target, TestProcessOptions{.groups = 2, .groupHeight = 60});
    ProcessPageLayout* page = process->pageLayout();
    ASSERT_NE(page, nullptr);
    process->buttons->button(QDialogButtonBox::Apply)->setEnabled(true);
    process->setParams(TestParams{.body = TestBody::WaitForStop});
    ASSERT_TRUE(settleProcess(*process));
    EXPECT_FALSE(page->runBanner()->isVisible());
    EXPECT_FALSE(page->cancelButton()->isVisible());

    process->applyProcess();
    ASSERT_TRUE(pumpUntil([&process]() { return process->runsOf(an::ui::ProcessRunKind::Apply) == 1; }));
    pumpQuietly();
    EXPECT_TRUE(page->runBanner()->isVisible());
    EXPECT_TRUE(page->cancelButton()->isVisible());
    EXPECT_TRUE(page->cancelButton()->isEnabled());
    EXPECT_FALSE(page->footerButton(QDialogButtonBox::Apply)->isVisible());
    EXPECT_FALSE(page->footerButton(QDialogButtonBox::Reset)->isEnabled());
    EXPECT_FALSE(page->footerButton(QDialogButtonBox::Close)->isEnabled());
    EXPECT_FALSE(page->toggles()->isEnabled());
    EXPECT_FALSE(page->bodyContent()->isEnabled());

    page->cancelButton()->click();
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_TRUE(settleView(*target));
    EXPECT_EQ(process->runState().phase, Phase::Cancelled);
    EXPECT_DOUBLE_EQ(documentSample(target->document(), 3u, 3u), 0.25) << "the image keeps its state";
    EXPECT_FALSE(page->runBanner()->isVisible());
    EXPECT_FALSE(page->cancelButton()->isVisible());
    EXPECT_TRUE(page->footerButton(QDialogButtonBox::Apply)->isVisible());
    EXPECT_TRUE(page->footerButton(QDialogButtonBox::Close)->isEnabled());
    EXPECT_TRUE(page->bodyContent()->isEnabled());
    EXPECT_FALSE(target->isLockedForProcessing());
}

// A run's row shows one bar for a run of one step, and two over a sequence - Overall, how far the frames are, and
// Processing, how far the frame on its way is - which follow what the run tells; it is taller then, and none shows
// outside a run.
TEST(PageMode, ARunOverASequenceShowsTwoBars)
{
    PagedContext paged;
    ASSERT_NE(paged.context, nullptr);
    ViewHandle target = flatTarget(paged.context->engineSession(), 0.25f);
    ProcessHandle process = showProcess(*target, TestProcessOptions{});
    ProcessPageLayout* page = process->pageLayout();
    ASSERT_NE(page, nullptr);
    EXPECT_EQ(page->runBars(), 0);

    process->setRunState(an::ui::runningState({}, 0.4));
    pumpQuietly();
    EXPECT_EQ(page->runBars(), 1);
    const int oneBar = page->runBanner()->sizeHint().height();

    process->setRunState(an::ui::sequenceRunningState());
    pumpQuietly();
    EXPECT_EQ(page->runBars(), 2);
    EXPECT_GT(page->runBanner()->sizeHint().height(), oneBar) << "two bars take more room";
    process->reportRunProgress(1.0 / 3.0);
    process->reportFrameProgress(0.5);
    EXPECT_NEAR(*process->runState().progress, 1.0 / 3.0, 1e-9);
    ASSERT_TRUE(process->runState().frameProgress.has_value());
    EXPECT_DOUBLE_EQ(*process->runState().frameProgress, 0.5);
    process->reportRunProgress(2.0 / 3.0);
    EXPECT_DOUBLE_EQ(*process->runState().frameProgress, 0.5) << "the overall share keeps the frame's";

    // A frame's progress means nothing outside a run over a sequence.
    process->setRunState(an::ui::runningState());
    process->reportFrameProgress(0.5);
    EXPECT_FALSE(process->runState().frameProgress.has_value());
    process->setRunState(an::ui::ProcessRunState{.phase = Phase::Cancelled});
    pumpQuietly();
    EXPECT_EQ(page->runBars(), 0);
}

// A run that fails says why in the banner, in the error tone, and opens no dialog.
TEST(PageMode, AFailureIsSaidInTheBanner)
{
    PagedContext paged;
    ASSERT_NE(paged.context, nullptr);
    ViewHandle target = flatTarget(paged.context->engineSession(), 0.25f);
    ProcessHandle process = showProcess(*target, TestProcessOptions{});
    ASSERT_NE(process->pageLayout(), nullptr);
    process->setParams(TestParams{.body = TestBody::Fail});
    process->applyProcess();
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_TRUE(settleView(*target));
    EXPECT_EQ(process->runState().phase, Phase::Failed);
    EXPECT_EQ(paged.context->errorCount(), 0) << "no dialog";
    an::ui::AnInfoBanner* banner = process->pageLayout()->banner();
    EXPECT_TRUE(banner->isVisible());
    EXPECT_EQ(banner->tone(), an::ui::AnInfoBanner::Tone::Error);
    EXPECT_TRUE(banner->message().contains(QStringLiteral("failed on purpose")));
}

// Esc closes an idle page and does nothing during a run; Ctrl+Enter applies, from either Enter key.
TEST(PageMode, EscAndCtrlEnter)
{
    PagedContext paged;
    ASSERT_NE(paged.context, nullptr);
    ViewHandle target = flatTarget(paged.context->engineSession(), 0.25f);
    ProcessHandle process = showProcess(*target, TestProcessOptions{.perChannel = true});
    ASSERT_NE(process->pageLayout(), nullptr);
    process->buttons->button(QDialogButtonBox::Apply)->setEnabled(true);
    QWidget* const host = paged.presenter.hostOf(*process);
    ASSERT_NE(host, nullptr);
    host->activateWindow();
    ASSERT_TRUE(QTest::qWaitForWindowActive(host));
    process->channelSelector->setFocus(Qt::OtherFocusReason);
    pumpQuietly();

    QTest::keyClick(process->channelSelector, Qt::Key_Return, Qt::ControlModifier);
    ASSERT_TRUE(settleProcess(*process));
    EXPECT_EQ(process->runsOf(an::ui::ProcessRunKind::Apply), 1) << "Ctrl+Return applies";
    QTest::keyClick(process->channelSelector, Qt::Key_Enter, Qt::ControlModifier | Qt::KeypadModifier);
    ASSERT_TRUE(settleProcess(*process));
    EXPECT_EQ(process->runsOf(an::ui::ProcessRunKind::Apply), 2) << "Ctrl+Enter on the keypad applies";

    process->setParams(TestParams{.body = TestBody::WaitForStop});
    process->applyProcess();
    ASSERT_TRUE(pumpUntil([&process]() { return process->runsOf(an::ui::ProcessRunKind::Apply) == 3; }));
    QTest::keyClick(process->channelSelector, Qt::Key_Escape);
    pumpQuietly();
    EXPECT_FALSE(process->isHidden()) << "Esc does nothing during a run";
    EXPECT_TRUE(process->cancelRun());
    ASSERT_TRUE(settleProcess(*process));

    QTest::keyClick(process->channelSelector, Qt::Key_Escape);
    pumpQuietly();
    EXPECT_TRUE(process->isHidden()) << "Esc closes an idle page";
}

namespace
{

/// A main window that holds the shortcuts a page's process also defines, and a field outside the page; the process is
/// taken into its centre.
class ShortcutWindow
{
public:
    ShortcutWindow()
    {
        open = new QAction(QStringLiteral("Open"), &m_main);
        open->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_O));
        QObject::connect(open, &QAction::triggered, &m_main, [this]() { ++windowOpen; });
        m_main.addAction(open);
        auto* toolbar = m_main.addToolBar(QStringLiteral("outside"));
        outside = new QLineEdit(toolbar);
        toolbar->addWidget(outside);
        centre = new QWidget(&m_main);
        auto* layout = new QVBoxLayout(centre);
        layout->setContentsMargins(0, 0, 0, 0);
        m_main.setCentralWidget(centre);
        m_main.resize(1536, 864);
    }

    void show()
    {
        m_main.show();
        m_main.activateWindow();
        (void)QTest::qWaitForWindowActive(&m_main);
        pumpQuietly();
    }

    QMainWindow m_main;
    QAction* open = nullptr;
    QLineEdit* outside = nullptr;
    QWidget* centre = nullptr;
    int windowOpen = 0;
};

/// A presenter that takes a process into a shortcut window's centre.
class CentrePresenter final : public an::ui::IProcessPresenter
{
public:
    explicit CentrePresenter(QWidget& centre) : m_centre(centre) {}
    bool adopt(TopLevelProcessBaseControl& process) override
    {
        return process.attachToPage(m_centre);
    }
    void runStateChanged(TopLevelProcessBaseControl&, const an::ui::ProcessRunState&) override {}
    void targetChanged(TopLevelProcessBaseControl&, ImageViewControl*) override {}

private:
    QWidget& m_centre;
};

/// Counts the messages Qt logs of an ambiguous shortcut while it lives.
class AmbiguityCounter
{
public:
    AmbiguityCounter()
    {
        s_counted = 0;
        m_previous = qInstallMessageHandler(&AmbiguityCounter::handle);
    }
    ~AmbiguityCounter()
    {
        qInstallMessageHandler(m_previous);
    }
    [[nodiscard]] static int count()
    {
        return s_counted;
    }

private:
    static void handle(QtMsgType, const QMessageLogContext&, const QString& message)
    {
        if (message.contains(QStringLiteral("Ambiguous shortcut"), Qt::CaseInsensitive))
        {
            ++s_counted;
        }
    }
    static inline int s_counted = 0;
    QtMessageHandler m_previous = nullptr;
};

/// The test process in a shortcut window's centre with an action of its own on Ctrl+O, a list that removes its item on
/// Delete while it has the focus, and a field; the case's counts of each.
struct ShortcutCase
{
    explicit ShortcutCase(ProcessTestContext& context, ImageViewControl& target) : presenter(*window.centre)
    {
        context.presenter = &presenter;
        process = openProcess(target, TestProcessOptions{});
        addFiles = new QAction(QStringLiteral("Add files"), process.get());
        addFiles->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_O));
        QObject::connect(addFiles, &QAction::triggered, process.get(), [this]() { ++pageOpen; });
        process->addAction(addFiles);
        list = new QListWidget(process->controlsPanel);
        list->addItem(QStringLiteral("frame"));
        process->controlsLayout->addWidget(list);
        auto* remove =
            new QShortcut(QKeySequence(Qt::Key_Delete), list, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
        QObject::connect(remove, &QShortcut::activated, list, [this]() { ++removed; });
        field = new QLineEdit(process->controlsPanel);
        process->controlsLayout->addWidget(field);
        window.show();
        process->show();
        pumpQuietly();
    }
    ~ShortcutCase()
    {
        process.reset();
    }

    /// Presses a key with the focus on a widget.
    void press(QWidget& focus, const Qt::Key key, const Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        focus.setFocus(Qt::OtherFocusReason);
        pumpQuietly();
        EXPECT_EQ(QApplication::focusWidget(), &focus);
        QTest::keyClick(&focus, key, modifiers);
        pumpQuietly();
    }

    ShortcutWindow window;
    CentrePresenter presenter;
    ProcessHandle process;
    QAction* addFiles = nullptr;
    QListWidget* list = nullptr;
    QLineEdit* field = nullptr;
    int pageOpen = 0;
    int removed = 0;
};

} // namespace

// A sequence the page and its window both hold runs the page's with the focus inside the page and the window's with
// the focus outside it, and Qt never finds the two ambiguous.
TEST(PageShortcuts, ThePagesShortcutWinsInsideItAndTheWindowsOutside)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ShortcutCase shortcuts(*context, *target);
    ASSERT_TRUE(shortcuts.process->isPresentedAsPage());
    ASSERT_TRUE(shortcuts.window.m_main.isActiveWindow()) << "a shortcut is looked for in the active window";
    const AmbiguityCounter ambiguity;

    shortcuts.press(*shortcuts.process->channelSelector, Qt::Key_O, Qt::ControlModifier);
    EXPECT_EQ(shortcuts.pageOpen, 1);
    EXPECT_EQ(shortcuts.window.windowOpen, 0);
    shortcuts.press(*shortcuts.window.outside, Qt::Key_O, Qt::ControlModifier);
    EXPECT_EQ(shortcuts.pageOpen, 1);
    EXPECT_EQ(shortcuts.window.windowOpen, 1);
    EXPECT_EQ(AmbiguityCounter::count(), 0);
    EXPECT_TRUE(shortcuts.addFiles->shortcut().isEmpty()) << "the action carries no live shortcut in a page";
    context->presenter = nullptr;
}

// A shortcut made for a widget and its children runs only with the focus in it; a field keeps the keys it edits with.
TEST(PageShortcuts, AWidgetsShortcutRunsInItAndAFieldKeepsItsKeys)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ShortcutCase shortcuts(*context, *target);
    ASSERT_TRUE(shortcuts.process->isPresentedAsPage());

    shortcuts.press(*shortcuts.list, Qt::Key_Delete);
    EXPECT_EQ(shortcuts.removed, 1) << "with the focus in the list";
    shortcuts.press(*shortcuts.process->channelSelector, Qt::Key_Delete);
    EXPECT_EQ(shortcuts.removed, 1) << "not with the focus elsewhere in the page";

    shortcuts.field->setText(QStringLiteral("ab"));
    shortcuts.field->setFocus(Qt::OtherFocusReason);
    shortcuts.field->home(false);
    QTest::keyClick(shortcuts.field, Qt::Key_Delete);
    pumpQuietly();
    EXPECT_EQ(shortcuts.field->text(), QStringLiteral("b")) << "Delete deleted a character";
    EXPECT_EQ(shortcuts.removed, 1);
    context->presenter = nullptr;
}

// In a window of its own a process's shortcuts are live objects again, and in a page again they are the page's.
TEST(PageShortcuts, InAWindowTheShortcutsAreLiveAgain)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ShortcutCase shortcuts(*context, *target);
    ASSERT_TRUE(shortcuts.process->isPresentedAsPage());
    EXPECT_TRUE(shortcuts.addFiles->shortcut().isEmpty());

    shortcuts.process->detachToWindow();
    pumpQuietly();
    EXPECT_EQ(shortcuts.addFiles->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_O));
    QShortcut* remove = shortcuts.list->findChild<QShortcut*>();
    ASSERT_NE(remove, nullptr);
    EXPECT_TRUE(remove->isEnabled());

    ASSERT_TRUE(shortcuts.process->attachToPage(*shortcuts.window.centre));
    pumpQuietly();
    EXPECT_TRUE(shortcuts.addFiles->shortcut().isEmpty());
    EXPECT_FALSE(remove->isEnabled());
    context->presenter = nullptr;
}

// An action of a menu still says its keys in its menu's entry while the page holds them - a menu draws what follows a
// tab in an entry's text as its keys - and is itself again in a window; an action of no menu has no entry to say them
// in, and keeps its text.
TEST(PageShortcuts, AMenuEntryStillSaysItsKeys)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ShortcutCase shortcuts(*context, *target);
    ASSERT_TRUE(shortcuts.process->isPresentedAsPage());
    shortcuts.process->detachToWindow();
    pumpQuietly();
    auto* const menu = new QMenu(QStringLiteral("Frames"), shortcuts.process.get());
    QAction* const goTo = menu->addAction(QStringLiteral("Go to Group..."));
    const QKeySequence keys(Qt::CTRL | Qt::Key_G);
    goTo->setShortcut(keys);
    int went = 0;
    QObject::connect(goTo, &QAction::triggered, shortcuts.process.get(), [&went]() { ++went; });

    ASSERT_TRUE(shortcuts.process->attachToPage(*shortcuts.window.centre));
    pumpQuietly();
    EXPECT_TRUE(goTo->shortcut().isEmpty()) << "the action carries no live shortcut in a page";
    EXPECT_EQ(goTo->text(), QStringLiteral("Go to Group...\t") + keys.toString(QKeySequence::NativeText));
    EXPECT_EQ(shortcuts.addFiles->text(), QStringLiteral("Add files")) << "an action of no menu keeps its text";
    // The key as it reaches a widget of the page: its override, which the page answers by running the action.
    QKeyEvent pressed(QEvent::ShortcutOverride, Qt::Key_G, Qt::ControlModifier);
    QCoreApplication::sendEvent(shortcuts.process->channelSelector, &pressed);
    EXPECT_TRUE(pressed.isAccepted());
    EXPECT_EQ(went, 1) << "and its keys still run it inside the page";

    shortcuts.process->detachToWindow();
    pumpQuietly();
    EXPECT_EQ(goTo->text(), QStringLiteral("Go to Group..."));
    EXPECT_EQ(goTo->shortcut(), keys);
    context->presenter = nullptr;
}

// A dialog the page opened is a window of its own, where no key reaches the page: its actions and its shortcuts keep
// their keys while the page holds its own.
TEST(PageShortcuts, ADialogOfThePageKeepsItsKeys)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ShortcutCase shortcuts(*context, *target);
    ASSERT_TRUE(shortcuts.process->isPresentedAsPage());
    shortcuts.process->detachToWindow();
    pumpQuietly();
    auto* const dialog = new QDialog(shortcuts.process.get());
    auto* const next = new QAction(QStringLiteral("Next frame"), dialog);
    next->setShortcut(QKeySequence(Qt::Key_Right));
    dialog->addAction(next);
    auto* const blink = new QShortcut(QKeySequence(Qt::Key_Space), dialog);

    ASSERT_TRUE(shortcuts.process->attachToPage(*shortcuts.window.centre));
    pumpQuietly();
    EXPECT_TRUE(shortcuts.addFiles->shortcut().isEmpty()) << "the page holds its own";
    EXPECT_EQ(next->shortcut(), QKeySequence(Qt::Key_Right)) << "a dialog's action keeps its keys";
    EXPECT_TRUE(blink->isEnabled()) << "and a dialog's shortcut stays live";
    context->presenter = nullptr;
}
