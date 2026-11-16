// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The two star profilers as mirror pages: each fits its column at 600 px of height in both densities, the row of
/// buttons under its table wrapped; and the stars are drawn and marked on the page's view of the target - the same
/// stars found, the same star selected by a click on it as in the window - while the target's own view shows nothing
/// of them.

#include "Test_ProcessesStarProfilers/StarProfilerTestSupport.h"
#include "Test_ProcessesShared/PageLayoutCheck.h"
#include "Test_ProcessesShared/SelectionDrag.h"

#include "controls/ProcessPageLayout.h"
#include "processes/StarProfilerProcessV1.h"
#include "processes/StarProfilerProcessV2.h"

#include <QPointer>

#include <gtest/gtest.h>

#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace
{

namespace shared = an::tests::processes::shared;
using namespace an::tests::processes;
using namespace an::tests::processes::stars;

constexpr const char* kFirstProfiler = "star_object.actionStar_Profiler_v1";
constexpr const char* kSecondProfiler = "star_object.actionStar_Profiler_v2";

/// The cases of the profilers' pages, each over a field of planted stars.
class StarProfilerPages : public ProfilerCase
{
protected:
    void SetUp() override
    {
        ProfilerCase::SetUp();
        m_field = makeStarField();
    }

public:
    [[nodiscard]] ViewHandle target(const std::string& name)
    {
        return fieldView<float>(session(), m_field, fieldMetadata(m_field), name);
    }

    [[nodiscard]] const StarField& field() const
    {
        return m_field;
    }

private:
    StarField m_field;
};

class PageLayout : public StarProfilerPages
{
protected:
    /// The page of a profiler held to its column; reported skipped where text is not measured as the interface
    /// measures it.
    void fitsItsColumn(const char* descriptorId)
    {
        const shared::ClosesOpenedWindows closesWindows;
        ViewHandle view = target("page target");
        ASSERT_NE(view, nullptr);
        context().addViewControl(view.get());
        context().setActiveImageViewControl(view.get());
        const bool measured =
            shared::expectPageFitsItsColumn(descriptorId, *view, context(), an::ui::ProcessSurface::MirrorPage);
        context().removeViewControl(view.get(), false);
        if (!measured && !::testing::Test::HasFailure())
        {
            GTEST_SKIP() << "Tab reaches every control; whether anything is clipped is measured under the native "
                            "platform, where text is as wide as the interface draws it";
        }
    }
};

class StarProfiler : public StarProfilerPages
{
};

/// What a profiler came to: the stars it found, and the row a click on a planted star selected.
struct Marked
{
    an::ui::StarList stars;
    int selectedRow = -1;
};

/// Clicks a point of the image on a view shown at 100 %, as a person clicks a star.
void clickImagePoint(ImageViewControl& view, const QPointF image)
{
    shared::showAtFullSize(view);
    QWidget* const viewport = view.getScrollArea()->viewport();
    const QPoint at = shared::viewportPointOf(
        view, QPoint(static_cast<int>(std::floor(image.x())), static_cast<int>(std::floor(image.y()))));
    shared::sendLeftMouse(*viewport, QEvent::MouseButtonPress, at);
    shared::sendLeftMouse(*viewport, QEvent::MouseButtonRelease, at);
    QCoreApplication::processEvents();
}

/// A profiler launched over a view of the field, in a window or as a page: its stars detected and a planted star
/// clicked on the view the person works on. On a page the same click on the target's own view selects nothing.
template <typename WindowT>
[[nodiscard]] std::optional<Marked> markIn(StarProfilerPages& test, ViewHandle view, const char* descriptorId,
                                           const PlantedStar& star, const bool asPage)
{
    const shared::ClosesOpenedWindows closesWindows;
    EXPECT_NE(view, nullptr);
    if (view == nullptr)
    {
        return std::nullopt;
    }
    test.context().addViewControl(view.get());
    test.context().setActiveImageViewControl(view.get());
    std::optional<Marked> marked;
    {
        PagePresenter presenter;
        presenter.adopting = asPage;
        test.context().presenter = &presenter;
        auto* const process = dynamic_cast<WindowT*>(shared::launchWindowOf(descriptorId, *view));
        EXPECT_NE(process, nullptr) << descriptorId;
        ImageViewControl* const worked = process != nullptr ? process->interactionView() : nullptr;
        EXPECT_NE(worked, nullptr) << "the profiler works on a view";
        if (process != nullptr && worked != nullptr)
        {
            const char* const where = asPage ? "on a page" : "in a window";
            EXPECT_EQ(process->hasPageLayout(), asPage) << where;
            if (asPage)
            {
                EXPECT_NE(worked, view.get()) << "the page's view is the one worked on";
                EXPECT_EQ(worked, process->pageLayout()->previewView());
            }
            else
            {
                EXPECT_EQ(worked, view.get());
            }
            EXPECT_TRUE(detectAndSettle(*process)) << where;
            Marked result;
            result.stars = heldStars(*process);
            TableWidget* const table = process->ui().tableWidget;
            table->clearSelection();
            table->setCurrentCell(-1, -1);
            clickImagePoint(*worked, QPointF(star.x, star.y));
            result.selectedRow = table->currentRow();
            if (asPage)
            {
                // The target's own view draws no star and hears no click of the profiler's.
                table->clearSelection();
                table->setCurrentCell(-1, -1);
                clickImagePoint(*view, QPointF(star.x, star.y));
                EXPECT_EQ(table->currentRow(), -1) << "a click on the target's own view selects nothing";
            }
            marked = std::move(result);
        }
        test.context().answerYes = true;
        shared::closeOpenedProcessWindows();
        test.context().presenter = nullptr;
    }
    test.context().removeViewControl(view.get(), false);
    return marked;
}

template <typename WindowT> void starsMarkedOnThePageGiveTheWindowsResult(StarProfilerPages& test, const char* id)
{
    SCOPED_TRACE(id);
    const shared::RegistrySnapshot snapshot;
    const PlantedStar& star = test.field().stars.front();
    const std::optional<Marked> window = markIn<WindowT>(test, test.target("window target"), id, star, false);
    const std::optional<Marked> page = markIn<WindowT>(test, test.target("page target"), id, star, true);
    ASSERT_TRUE(window.has_value() && page.has_value());
    ASSERT_FALSE(window->stars.empty());
    ASSERT_EQ(window->stars.size(), page->stars.size()) << "the page found the window's stars";
    for (std::size_t index = 0u; index < window->stars.size(); ++index)
    {
        EXPECT_DOUBLE_EQ(window->stars[index].positionX, page->stars[index].positionX);
        EXPECT_DOUBLE_EQ(window->stars[index].positionY, page->stars[index].positionY);
    }
    ASSERT_GE(window->selectedRow, 0) << "a click on the star selected its row in the window";
    EXPECT_EQ(page->selectedRow, window->selectedRow) << "the same click on the page's view selected the same row";
    const an::DetectedStar& selected = page->stars[static_cast<std::size_t>(page->selectedRow)];
    EXPECT_LT(std::hypot(selected.positionX - star.x, selected.positionY - star.y), 1.0) << "the row is the star's";
}

} // namespace

TEST_F(PageLayout, StarProfilerV1FitsItsColumn)
{
    fitsItsColumn(kFirstProfiler);
}

TEST_F(PageLayout, StarProfilerV2FitsItsColumn)
{
    fitsItsColumn(kSecondProfiler);
}

// A star clicked on the page's view selects the row the same click on the target's view selects in the window, over
// the same stars; on a page the target's own view shows nothing of them. Both profilers.
TEST_F(StarProfiler, StarsMarkedOnThePageGiveTheWindowsResult)
{
    starsMarkedOnThePageGiveTheWindowsResult<StarProfilerProcessV1>(*this, kFirstProfiler);
    starsMarkedOnThePageGiveTheWindowsResult<StarProfilerProcessV2>(*this, kSecondProfiler);
}
