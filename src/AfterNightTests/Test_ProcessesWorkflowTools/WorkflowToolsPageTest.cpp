// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The four workflow tools as pages: each states a full page and is taken into one whole - what its window laid out,
/// in order, in the page's area, with no column and no menus of its own - which scrolls only where the centre is
/// smaller than the least the tool takes; and the page's primary action is the tool's own run button. Each case
/// records the least size its tool takes (`leastWidth`, `leastHeight`), which is the interface's when the suite is
/// started under the native platform, where text is as wide as the interface draws it.

#include "Test_ProcessesWorkflowTools/WorkflowToolsTestSupport.h"

#include "processes/ImageCalibrationProcess.h"
#include "processes/ImageListAnalyzerProcess.h"
#include "processes/ImageRegistrationProcess.h"
#include "processes/ImageStackingProcess.h"

#include <QAbstractButton>
#include <QLayout>
#include <QMenuBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>

#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <string>

namespace an::tests::processes::workflow
{
namespace
{

/// Whether a scroll bar can move: the content is larger than the area along it.
[[nodiscard]] bool scrolls(const QScrollBar& bar)
{
    return bar.maximum() > bar.minimum();
}

/// The least size a page's contents take, as the page's area reads it: their least width, and at that width the
/// height they ask for where it follows the width - text that wraps - when that is more than their least height.
[[nodiscard]] QSize leastSizeOf(const QWidget& contents)
{
    QSize least = contents.minimumSizeHint();
    if (contents.layout() != nullptr && contents.layout()->hasHeightForWidth())
    {
        least.setHeight((std::max)(least.height(), contents.heightForWidth(least.width())));
    }
    return least;
}

/// The cases of the tools' pages.
class PageLayout : public WorkflowCase
{
protected:
    /// Holds a tool to its page.
    ///
    /// @param descriptorId the tool's id in the registry.
    /// @param runButtonOf the tool's own run button.
    template <typename ToolT>
    void fillsItsPage(const std::string& descriptorId, const std::function<QAbstractButton*(ToolT&)>& runButtonOf)
    {
        SCOPED_TRACE(descriptorId);
        const an::ui::process_registry::ProcessDescriptor* const descriptor = shared::registered(descriptorId);
        ASSERT_NE(descriptor, nullptr);
        EXPECT_EQ(descriptor->surface, an::ui::ProcessSurface::FullPage) << "the tool states a full page";

        PagedTool<ToolT> paged(context(), descriptorId);
        ASSERT_NE(paged.tool, nullptr) << "the tool's action opened it";
        ToolT& tool = *paged.tool;
        EXPECT_EQ(tool.processSurface(), an::ui::ProcessSurface::FullPage);
        ASSERT_TRUE(tool.isPresentedAsPage()) << "the presenter took the tool into a page";
        an::ui::ProcessPageLayout* const page = tool.pageLayout();
        ASSERT_NE(page, nullptr);
        QScrollArea* const area = page->fullArea();
        ASSERT_NE(area, nullptr) << "the tool is its page whole";
        EXPECT_EQ(page->column(), nullptr);
        EXPECT_EQ(page->canvas(), nullptr);
        EXPECT_TRUE(page->menus().isEmpty()) << "a tool has no menus of its own, so its header shows no menu button";
        EXPECT_TRUE(tool.findChildren<QMenuBar*>().isEmpty());
        QWidget* const host = paged.host();
        ASSERT_NE(host, nullptr);
        EXPECT_EQ(page->page()->size(), host->size()) << "the page fills the centre";

        // What the window laid out is in the area: every button a person can press but the page's own Cancel.
        int inTheArea = 0;
        for (QAbstractButton* button : tool.findChildren<QAbstractButton*>())
        {
            if (button == page->cancelButton() || !button->isVisibleTo(&tool))
            {
                continue;
            }
            EXPECT_TRUE(area->isAncestorOf(button))
                << "'" << button->text().toStdString() << "' (" << button->objectName().toStdString()
                << ") lies outside the page's area";
            ++inTheArea;
        }
        EXPECT_GT(inTheArea, 0);

        // The page's primary action is the tool's run button: nothing while the button is not offered, a press of it
        // otherwise.
        QAbstractButton* const run = runButtonOf(tool);
        ASSERT_NE(run, nullptr);
        EXPECT_TRUE(area->isAncestorOf(run));
        int pressed = 0;
        QObject::connect(run, &QAbstractButton::released, &tool, [&pressed]() { ++pressed; });
        const bool offered = run->isEnabled();
        run->setEnabled(false);
        EXPECT_FALSE(tool.triggerPrimaryAction()) << "a run button that is not offered is not pressed";
        EXPECT_EQ(pressed, 0);
        run->setEnabled(offered);

        // The page scrolls only where the centre is smaller than the least the tool takes: a centre as large holds
        // the tool whole, and a smaller one scrolls to the rest, the tool keeping its least size.
        const QSize least = leastSizeOf(*area->widget());
        ASSERT_TRUE(least.isValid());
        ::testing::Test::RecordProperty("leastWidth", least.width());
        ::testing::Test::RecordProperty("leastHeight", least.height());
        host->resize(least);
        QCoreApplication::processEvents();
        EXPECT_FALSE(scrolls(*area->horizontalScrollBar()))
            << "a centre of the tool's least size, " << least.width() << " x " << least.height() << ", holds it whole";
        EXPECT_FALSE(scrolls(*area->verticalScrollBar()))
            << "a centre of the tool's least size, " << least.width() << " x " << least.height() << ", holds it whole";
        host->resize(least.width() / 2, least.height() / 2);
        QCoreApplication::processEvents();
        EXPECT_TRUE(scrolls(*area->horizontalScrollBar())) << "a smaller centre scrolls sideways";
        EXPECT_TRUE(scrolls(*area->verticalScrollBar())) << "and up and down";
        EXPECT_GE(area->widget()->width(), least.width()) << "the tool keeps its least size";
    }
};

} // namespace

TEST_F(PageLayout, ImageCalibrationFillsItsPage)
{
    fillsItsPage<ImageCalibrationProcess>(
        kImageCalibrationTool, [](ImageCalibrationProcess& tool) { return tool.ui().calibrateImagesButton; });
}

TEST_F(PageLayout, ImageRegistrationFillsItsPage)
{
    fillsItsPage<ImageRegistrationProcess>(kImageRegistrationTool,
                                           [](ImageRegistrationProcess& tool) { return tool.ui().registerButton; });
}

TEST_F(PageLayout, ImageStackingFillsItsPage)
{
    fillsItsPage<ImageStackingProcess>(kImageStackingTool,
                                       [](ImageStackingProcess& tool) { return tool.ui().stackImagesButton; });
}

TEST_F(PageLayout, ImageListAnalyzerFillsItsPage)
{
    fillsItsPage<ImageListAnalyzerProcess>(kImageListAnalyzerTool,
                                           [](ImageListAnalyzerProcess& tool) { return tool.ui().optimizeButton; });
}

} // namespace an::tests::processes::workflow
