// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Pixel Math as a mirror page, arranged its own way: in its column at 600 px of height, in both densities, with every
/// control in the column and reached by Tab and nothing clipped sideways where text is measured; its expression editor
/// growing with the body and keeping at least a third of its height; its function, operator and symbol tables in one
/// section of the column that collapses, serving the expression of whichever tab is shown; and an expression - one, or
/// one a channel - giving through the page the window's result; and a run, over an image or a sequence, stopped by the
/// page's Cancel with no progress window opened.

#include "Test_ProcessesPixelMath/PixelMathTestSupport.h"
#include "Test_ProcessesShared/PageLayoutCheck.h"

#include "components/AnCollapsibleSection.h"
#include "controls/ImageProcessControl.h"
#include "controls/PixelMathEditControl.h"
#include "processes/PixelMathProcess.h"
#include "widgets/TableWidget.h"

#include <QSet>
#include <QTabWidget>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>

namespace
{

namespace shared = an::tests::processes::shared;
using namespace an::tests::processes;
using namespace an::tests::processes::channels;
using namespace an::tests::processes::pixelmath;
using an::ui::processes::PixelMathExpressionTab;

/// The title of the section that holds the tables.
const QString kReferenceSection = QStringLiteral("Functions, operators and symbols");

/// The cases of Pixel Math's page.
class PixelMathPage : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_context = openProcessContext(4u);
        ASSERT_NE(m_context, nullptr);
        m_installed = std::make_unique<InstalledContext>(*m_context);
    }

    void TearDown() override
    {
        m_installed.reset();
        m_context.reset();
    }

    [[nodiscard]] ProcessTestContext& context()
    {
        return *m_context;
    }

    /// A view of the colour scene, owned by the case.
    [[nodiscard]] ViewHandle scene(const std::string& name)
    {
        return sceneView(m_context->engineSession(), "scene-rgb-float32", name);
    }

    /// Pixel Math launched over a view as a page of a centre of a height; null when it was not.
    [[nodiscard]] PixelMathProcess* openPage(ImageViewControl& target, PagePresenter& presenter)
    {
        activate(context(), target);
        context().presenter = &presenter;
        auto* const process = dynamic_cast<PixelMathProcess*>(shared::launchWindowOf(kDescriptorId, target));
        pumpQuietly();
        return process != nullptr && process->hasPageLayout() ? process : nullptr;
    }

private:
    std::unique_ptr<ProcessTestContext> m_context;
    std::unique_ptr<InstalledContext> m_installed;
};

/// The section of a page with a title; null for none.
[[nodiscard]] an::ui::AnCollapsibleSection* sectionTitled(const an::ui::ProcessPageLayout& page, const QString& title)
{
    for (an::ui::AnCollapsibleSection* section : page.sections())
    {
        if (section->title() == title)
        {
            return section;
        }
    }
    return nullptr;
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

TEST(PageLayout, PixelMathFitsItsColumn)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    ViewHandle target = sceneView(context->engineSession(), "scene-rgb-float32", "page target");
    ASSERT_NE(target, nullptr);
    activate(*context, *target);
    const bool measured =
        shared::expectPageFitsItsColumn(kDescriptorId, *target, *context, an::ui::ProcessSurface::MirrorPage);
    dropView(*context, target);
    if (!measured && !::testing::Test::HasFailure())
    {
        GTEST_SKIP() << "Tab reaches every control; whether anything is clipped is measured under the native platform, "
                        "where text is as wide as the interface draws it";
    }
}

TEST(PageLayout, PixelMathAppliesAsInAWindow)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    shared::MirrorApply how;
    how.configure = [](QWidget& window) {
        setExpression(window, PixelMathExpressionTab::RedOrAll, QStringLiteral("$T * 0.5"));
    };
    shared::expectMirrorPageAppliesAsTheWindow(
        kDescriptorId, *context,
        [&context]() { return sceneView(context->engineSession(), "scene-rgb-float32", "applied target"); }, how);
}

// The expression editor grows with the body and keeps at least a third of its height, whether the sections under it
// are open or closed, in a short window and in a tall one.
TEST_F(PixelMathPage, TheEditorKeepsAThirdOfTheBody)
{
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    ViewHandle target = scene("editor target");
    ASSERT_NE(target, nullptr);
    int shortEditor = 0;
    for (const int height : {600, 864})
    {
        SCOPED_TRACE(height);
        PagePresenter presenter(QSize(PagePresenter::kCentre.width(), height));
        PixelMathProcess* const process = openPage(*target, presenter);
        ASSERT_NE(process, nullptr);
        an::ui::ProcessPageLayout* const page = process->pageLayout();
        auto* const editor =
            dynamic_cast<PixelMathEditControl*>(process->findChild<QWidget*>(QStringLiteral("rgbkTextEdit")));
        ASSERT_NE(editor, nullptr);
        for (const bool open : {true, false})
        {
            SCOPED_TRACE(open ? "sections open" : "sections closed");
            for (an::ui::AnCollapsibleSection* section : page->sections())
            {
                setOpen(*section, open);
            }
            pumpQuietly();
            const int body = page->body()->viewport()->height();
            EXPECT_GE(editor->height() * 3, body)
                << "the editor is " << editor->height() << " px of a " << body << " px body";
        }
        if (height == 600)
        {
            shortEditor = editor->height();
        }
        else
        {
            EXPECT_GT(editor->height(), shortEditor) << "the editor grows with the body";
        }
        shared::closeOpenedProcessWindows();
        context().presenter = nullptr;
    }
    dropView(context(), target);
}

// The function, operator and symbol tables are one section of the column, each reached by Tab when it is open; they
// serve the expression of the tab shown, so the tabs keep no tables of their own.
TEST_F(PixelMathPage, EveryTableIsReached)
{
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    ViewHandle target = scene("tables target");
    ASSERT_NE(target, nullptr);
    {
        PagePresenter presenter(QSize(PagePresenter::kCentre.width(), 600));
        PixelMathProcess* const process = openPage(*target, presenter);
        ASSERT_NE(process, nullptr);
        an::ui::ProcessPageLayout* const page = process->pageLayout();
        an::ui::AnCollapsibleSection* const reference = sectionTitled(*page, kReferenceSection);
        ASSERT_NE(reference, nullptr) << "the tables are a section of their own";
        setOpen(*reference, true);

        QSet<const QWidget*> chain;
        for (QWidget* at = page->column()->nextInFocusChain(); at != nullptr && at != page->column();
             at = at->nextInFocusChain())
        {
            if ((at->focusPolicy() & Qt::TabFocus) != 0 && at->isEnabled() && at->isVisibleTo(process))
            {
                chain.insert(at);
            }
        }
        for (const char* name : {"functionsTableWidget1", "operatorsTableWidget1", "symbolsTableWidget1"})
        {
            SCOPED_TRACE(name);
            auto* const table = dynamic_cast<TableWidget*>(process->findChild<QWidget*>(QString::fromLatin1(name)));
            ASSERT_NE(table, nullptr);
            EXPECT_TRUE(reference->isAncestorOf(table)) << "in the section";
            EXPECT_TRUE(table->isVisibleTo(process));
            EXPECT_GT(table->rowCount(), 0);
            EXPECT_TRUE(chain.contains(table)) << "reached by Tab";
        }
        for (const char* name : {"functionsTableWidget2", "operatorsTableWidget3", "symbolsTableWidget4"})
        {
            auto* const table = dynamic_cast<TableWidget*>(process->findChild<QWidget*>(QString::fromLatin1(name)));
            ASSERT_NE(table, nullptr);
            EXPECT_FALSE(table->isVisibleTo(process)) << name << ": a tab keeps no tables of its own on a page";
        }

        // With an expression a channel, a function double-clicked in the section goes to the green tab shown.
        setChecked(*process, "useSingleRGBKExpression", false);
        auto* const tabs = process->findChild<QTabWidget*>(QStringLiteral("tabWidget"));
        ASSERT_NE(tabs, nullptr);
        tabs->setCurrentIndex(static_cast<int>(PixelMathExpressionTab::Green));
        auto* const functions =
            dynamic_cast<TableWidget*>(process->findChild<QWidget*>(QStringLiteral("functionsTableWidget1")));
        process->onRowDoubleClicked(functions, 0);
        auto* const green =
            dynamic_cast<PixelMathEditControl*>(process->findChild<QWidget*>(QStringLiteral("greenTextEdit")));
        ASSERT_NE(green, nullptr);
        EXPECT_TRUE(green->toPlainText().startsWith(functions->item(0, 0)->text()))
            << green->toPlainText().toStdString();

        setOpen(*reference, false);
        EXPECT_FALSE(reference->isExpanded()) << "the section closes";
        shared::closeOpenedProcessWindows();
        context().presenter = nullptr;
    }
    dropView(context(), target);
}

// A run over an image on the page shows in the page's row and opens no progress window; the page's Cancel stops it,
// and the image is left as it was.
TEST_F(PixelMathPage, AnImageRunStopsAtThePagesCancel)
{
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    ViewHandle target = scene("stopped target");
    ASSERT_NE(target, nullptr);
    context().addViewControl(target.get());
    shared::expectMirrorPageRunStopsAtItsCancel(kDescriptorId, context(), *target, [](QWidget& window) {
        setExpression(window, PixelMathExpressionTab::RedOrAll, QStringLiteral("$T * 0.5"));
    });
    dropView(context(), target);
}

// A run over a sequence on the page shows the run's two bars and opens no progress window; the page's Cancel stops it,
// and the view shows the sequence it was opened on.
TEST_F(PixelMathPage, ASequenceRunStopsAtThePagesCancel)
{
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path =
        writeSceneSequence(context().engineSession(), directory, {"scene-rgb-float32", "scene-rgb-float32-second"});
    ASSERT_FALSE(path.isEmpty());
    ImageViewControl* const target = openSequenceView(context(), path, an::SequenceFormat::ImageList, "page frames");
    ASSERT_NE(target, nullptr);
    ASSERT_TRUE(settleView(*target));
    shared::expectMirrorPageRunStopsAtItsCancel(kDescriptorId, context(), *target, [](QWidget& window) {
        setExpression(window, PixelMathExpressionTab::RedOrAll, QStringLiteral("$T * 0.5"));
    });
}

// An expression for each colour channel, written in the page's tabs, gives the window's result.
TEST_F(PixelMathPage, TheSameExpressionGivesTheWindowsResult)
{
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    shared::MirrorApply how;
    how.configure = [](QWidget& window) {
        setChecked(window, "useSingleRGBKExpression", false);
        setExpression(window, PixelMathExpressionTab::RedOrAll, QStringLiteral("$T * 0.5"));
        setExpression(window, PixelMathExpressionTab::Green, QStringLiteral("$T * $T"));
        setExpression(window, PixelMathExpressionTab::Blue, QStringLiteral("1 - $T"));
    };
    shared::expectMirrorPageAppliesAsTheWindow(
        kDescriptorId, context(), [this]() { return scene("expression target"); }, how);
}
