// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The statistics window over a view: the view's image measured on a worker and the table filled on the GUI thread only
/// when the measurement lands, measured again when the clipped samples are included or left out, when Refresh is
/// pressed and when the view shows another image, a scale that changes only the numbers shown, and only the latest
/// measurement shown, not held to the threads kept for previews. A window closed while its measurement waits is
/// `Test_ControlsSeams`', whose quarantine needs the window compiled into it.

#include "Test_ControlsStatistics/StatisticsTestSupport.h"
#include "Test_ControlsViewer/EditHoldingTheWriter.h"

#include "controls/ImageStatisticsControl.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <QCheckBox>
#include <QComboBox>
#include <QPointer>
#include <QPushButton>
#include <QTableWidget>
#include <QThreadPool>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace
{

using an::tests::statistics::Channels;
using an::tests::statistics::InstalledContext;
using an::tests::statistics::makeDocument;
using an::tests::statistics::makeView;
using an::tests::statistics::openStatisticsContext;
using an::tests::statistics::pumpQuietly;
using an::tests::statistics::pumpUntil;
using an::tests::statistics::settle;
using an::tests::statistics::StatisticsTestContext;
using an::tests::statistics::ViewHandle;

/// A sixteen-bit grey image: the first column at zero, the last at full scale, the rest at a thousand times the column.
[[nodiscard]] std::uint16_t columnCode(const std::uint32_t x, std::uint32_t, std::uint32_t)
{
    return static_cast<std::uint16_t>(x == 0u ? 0u : (x == 9u ? 65535u : 1000u * x));
}

class StatisticsWindowTest : public ::testing::Test
{
protected:
    void open(const std::uint32_t width)
    {
        m_context = openStatisticsContext(width);
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

    [[nodiscard]] ViewHandle columnView()
    {
        ViewHandle view = makeView("statistics");
        std::optional<an::ui::ImageDocument> document =
            makeDocument<std::uint16_t>(session(), 10u, 4u, Channels::Grey, columnCode);
        EXPECT_TRUE(document.has_value());
        if (document.has_value())
        {
            view->setDocument(std::move(*document));
        }
        return view;
    }

    std::unique_ptr<StatisticsTestContext> m_context;
    std::optional<InstalledContext> m_installed;
};

[[nodiscard]] QString cell(const QTableWidget& table, const int row, const int column)
{
    const QTableWidgetItem* item = table.item(row, column);
    return item != nullptr ? item->text() : QString();
}

} // namespace

// The table is empty until the measurement lands, and then holds the figures of the samples inside the range: the
// window opens with the clipped samples left out.
TEST_F(StatisticsWindowTest, TheTableIsFilledWhenTheMeasurementLands)
{
    open(2u);
    ViewHandle view = columnView();
    auto* window = new ImageStatisticsControl(view.get());
    auto* table = window->findChild<QTableWidget*>(QStringLiteral("tableWidget"));
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(window->windowTitle(), QStringLiteral("Statistics <statistics>"));
    EXPECT_EQ(cell(*table, 0, 0), QString()) << "the table was filled before the GUI thread took the measurement";

    ASSERT_TRUE(pumpUntil([window]() { return window->isIdle(); }));
    ASSERT_EQ(window->reportsShown(), 1u);
    ASSERT_TRUE(window->report().has_value());
    EXPECT_EQ(table->columnCount(), 1);
    EXPECT_EQ(table->horizontalHeaderItem(0)->text(), QStringLiteral("Gray"));
    EXPECT_EQ(table->rowCount(), 13);
    // Eight columns of four rows lie inside the range: 32 of 40 pixels.
    EXPECT_EQ(cell(*table, 0, 0), QStringLiteral("32"));
    EXPECT_EQ(cell(*table, 1, 0), QStringLiteral("80.0000000"));
    EXPECT_EQ(cell(*table, 2, 0), QString::number(4500.0 / 65535.0, 'f', 7));
    EXPECT_EQ(cell(*table, 5, 0), QString::number(1000.0 / 65535.0, 'f', 7));
    ASSERT_TRUE(settle(*view));
}

// Including the clipped samples measures again; so do Refresh and another image in the view.
TEST_F(StatisticsWindowTest, IncludingTheClippedSamplesRefreshingOrAnotherImageMeasuresAgain)
{
    open(2u);
    ViewHandle view = columnView();
    auto* window = new ImageStatisticsControl(view.get());
    auto* table = window->findChild<QTableWidget*>(QStringLiteral("tableWidget"));
    ASSERT_TRUE(pumpUntil([window]() { return window->isIdle(); }));

    auto* unclipped = window->findChild<QCheckBox*>(QStringLiteral("unclipped"));
    ASSERT_NE(unclipped, nullptr);
    unclipped->setChecked(true);
    ASSERT_TRUE(pumpUntil([window]() { return window->isIdle() && window->reportsShown() == 2u; }));
    EXPECT_EQ(cell(*table, 0, 0), QStringLiteral("40"));
    EXPECT_EQ(cell(*table, 6, 0), QString::number(1.0, 'f', 7));

    auto* refresh = window->findChild<QPushButton*>(QStringLiteral("refreshButton"));
    ASSERT_NE(refresh, nullptr);
    refresh->click();
    ASSERT_TRUE(pumpUntil([window]() { return window->isIdle() && window->reportsShown() == 3u; }));

    std::optional<an::ui::ImageDocument> colour =
        makeDocument<float>(session(), 4u, 4u, Channels::Rgb,
                            [](std::uint32_t, std::uint32_t, std::uint32_t c) { return 0.1f * (c + 1u); });
    ASSERT_TRUE(colour.has_value());
    view->setDocument(std::move(*colour));
    ASSERT_TRUE(pumpUntil([window]() { return window->isIdle() && window->reportsShown() == 4u; }));
    EXPECT_EQ(table->columnCount(), 3);
    EXPECT_EQ(table->horizontalHeaderItem(2)->text(), QStringLiteral("Blue"));
    EXPECT_EQ(cell(*table, 2, 2), QString::number(0.3f, 'f', 7));
    ASSERT_TRUE(settle(*view));
}

// A scale changes the numbers shown and measures nothing: a location by the scale's full scale, a variance by its
// square.
TEST_F(StatisticsWindowTest, AScaleChangesOnlyTheNumbersShown)
{
    open(2u);
    ViewHandle view = columnView();
    auto* window = new ImageStatisticsControl(view.get());
    auto* table = window->findChild<QTableWidget*>(QStringLiteral("tableWidget"));
    ASSERT_TRUE(pumpUntil([window]() { return window->isIdle(); }));
    const std::uint64_t shown = window->reportsShown();
    const double variance = window->report()->channels.front().variance;

    auto* scale = window->findChild<QComboBox*>(QStringLiteral("comboBox"));
    ASSERT_NE(scale, nullptr);
    scale->setCurrentText(QStringLiteral("16-bit (0-65K)"));
    pumpQuietly();
    EXPECT_EQ(window->reportsShown(), shown);
    EXPECT_EQ(cell(*table, 2, 0), QString::number(4500.0, 'f', 3));
    EXPECT_EQ(cell(*table, 10, 0), QString::number(variance * 65535.0 * 65535.0, 'f', 3));
    ASSERT_TRUE(settle(*view));
}

// A measurement asked for while another waits stops it, and only the latest is shown: with the session's only slot
// held, including and then leaving out the clipped samples again shows one table, of the samples inside the range.
TEST_F(StatisticsWindowTest, OnlyTheLatestMeasurementIsShown)
{
    open(1u);
    ViewHandle view = columnView();
    ASSERT_TRUE(settle(*view));
    auto* window = new ImageStatisticsControl(view.get());
    ASSERT_TRUE(pumpUntil([window]() { return window->isIdle(); }));
    const std::uint64_t shown = window->reportsShown();
    auto* unclipped = window->findChild<QCheckBox*>(QStringLiteral("unclipped"));
    ASSERT_NE(unclipped, nullptr);
    {
        std::optional<an::consumer::OperationScope> gate = an::tests::viewer::admitted(session());
        ASSERT_TRUE(gate.has_value());
        unclipped->setChecked(true);
        unclipped->setChecked(false);
        pumpQuietly();
        EXPECT_EQ(window->reportsShown(), shown) << "a measurement was shown while the session's slot was held";
    }
    ASSERT_TRUE(pumpUntil([window]() { return window->isIdle(); }));
    EXPECT_EQ(window->reportsShown(), shown + 1u);
    ASSERT_TRUE(window->report().has_value());
    EXPECT_EQ(window->report()->channels.front().count, 32u);
    ASSERT_TRUE(settle(*view));
}

// A measurement that finished before a newer one was asked for, and lands after it, is not shown: with the session's
// only slot held for the newer one, the older lands alone and the table waits for the newer.
TEST_F(StatisticsWindowTest, AMeasurementThatLandsAfterANewerRequestIsNotShown)
{
    open(1u);
    ViewHandle view = columnView();
    ASSERT_TRUE(settle(*view));
    auto* window = new ImageStatisticsControl(view.get());
    ASSERT_TRUE(pumpUntil([window]() { return window->isIdle(); }));
    const std::uint64_t shown = window->reportsShown();
    auto* unclipped = window->findChild<QCheckBox*>(QStringLiteral("unclipped"));
    ASSERT_NE(unclipped, nullptr);

    unclipped->setChecked(true);
    QThreadPool::globalInstance()->waitForDone();
    {
        std::optional<an::consumer::OperationScope> gate = an::tests::viewer::admitted(session());
        ASSERT_TRUE(gate.has_value());
        unclipped->setChecked(false);
        pumpQuietly();
        EXPECT_EQ(window->reportsShown(), shown) << "the measurement of every sample was shown after it was superseded";
    }
    ASSERT_TRUE(pumpUntil([window]() { return window->isIdle(); }));
    EXPECT_EQ(window->reportsShown(), shown + 1u);
    ASSERT_TRUE(window->report().has_value());
    EXPECT_EQ(window->report()->channels.front().count, 32u);
    ASSERT_TRUE(settle(*view));
}

// The window measures because someone asked it to, once, so it is not held to the threads a session keeps for the
// previews that follow a dialog's parameters: with every one of them taken, the measurement still lands.
TEST_F(StatisticsWindowTest, AMeasurementIsNotQueuedBehindThePreviews)
{
    open(8u);
    ViewHandle view = columnView();
    ASSERT_TRUE(settle(*view));
    {
        an::Result<an::consumer::OperationScope> previews = session().admit(
            an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Preview,
                                           .maxConcurrency = an::consumer::previewReservationWidth(8u)});
        ASSERT_TRUE(previews.hasValue()) << previews.status().message();
        ASSERT_EQ(session().admission().previewThreads, an::consumer::previewReservationWidth(8u));

        auto* window = new ImageStatisticsControl(view.get());
        EXPECT_TRUE(pumpUntil([window]() { return window->isIdle() && window->reportsShown() == 1u; }))
            << "the measurement waited for a preview thread";
        ASSERT_TRUE(window->report().has_value());
        EXPECT_EQ(window->report()->channels.front().count, 32u);
    }
    ASSERT_TRUE(settle(*view));
}

// A measurement takes its read lease before it asks to be admitted. An edit of the view holds the image's writer and
// then asks to be admitted, so at a width of one a measurement admitted first would hold the only thread while it
// waited for the writer, and the edit would wait for that thread: neither would end.
TEST_F(StatisticsWindowTest, AMeasurementWaitingForAnEditsWriterHoldsNoThreadTheEditNeeds)
{
    open(1u);
    ViewHandle view = columnView();
    ASSERT_TRUE(settle(*view));
    auto* window = new ImageStatisticsControl(view.get());
    ASSERT_TRUE(pumpUntil([window]() { return window->isIdle(); }));
    const std::uint64_t shown = window->reportsShown();
    auto* refresh = window->findChild<QPushButton*>(QStringLiteral("refreshButton"));
    ASSERT_NE(refresh, nullptr);

    an::tests::viewer::EditHoldingTheWriter edit(*view);
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.holdsTheWriter(); }));
    const std::uint64_t admittedBefore = session().admission().admitted;
    refresh->click();
    EXPECT_FALSE(an::tests::viewer::admittedBeyond(session(), admittedBefore))
        << "the measurement was admitted while it could not read the image";
    edit.askToBeAdmitted();
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.finished(); }));
    EXPECT_TRUE(edit.wasAdmitted())
        << "the edit waited for the thread the measurement held while the measurement waited for the edit's writer";

    ASSERT_TRUE(pumpUntil([window, shown]() { return window->isIdle() && window->reportsShown() == shown + 1u; }));
    ASSERT_TRUE(window->report().has_value());
    EXPECT_EQ(window->report()->channels.front().count, 32u);
    ASSERT_TRUE(settle(*view));
}
