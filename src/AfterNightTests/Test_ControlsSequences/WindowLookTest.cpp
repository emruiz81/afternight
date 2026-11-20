// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The New Sequence and the Export Sequence windows under the Observatory profile: each has a heading, a footer whose
/// rightmost button is the primary one - Apply after Close in New Sequence, which applies and stays open - text that
/// meets the contrast rule, nothing clipped as it opens or at its least size, and opens within its screen's available
/// area, in both densities.

#include "Test_ControlsSequences/SequencesTestSupport.h"

#include "controls/AutostretchApply.h"
#include "controls/ExportSequenceControl.h"
#include "controls/NewSequenceControl.h"
#include "controls/SequenceContainers.h"
#include "fixtures/WindowLook.h"

#include <QDialogButtonBox>
#include <QPushButton>
#include <QThreadPool>

#include <gtest/gtest.h>

#include <memory>
#include <optional>

namespace
{

using namespace an::tests::sequences;
using an::tests::look::expectWindowLook;
using an::tests::look::WindowLookMeasured;

/// The suite's context, installed for a case.
class WindowLook : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_context = openSequencesContext();
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
    }

    void TearDown() override
    {
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::processEvents();
        m_installed.reset();
        m_context.reset();
    }

    std::unique_ptr<SequencesTestContext> m_context;
    std::optional<InstalledContext> m_installed;
};

} // namespace

TEST_F(WindowLook, NewSequence)
{
    const auto letGoOnceIdle = [](QWidget* window) {
        if (auto* const sequence = dynamic_cast<NewSequenceControl*>(window))
        {
            (void)pumpUntil([sequence]() { return sequence->isIdle(); });
        }
        an::tests::look::letGo(window);
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::processEvents();
    };
    const WindowLookMeasured measured = expectWindowLook(
        []() -> QWidget* {
            auto* const window = new NewSequenceControl();
            // A size remembered from the case before would stand for the size the window is built at.
            return window;
        },
        letGoOnceIdle);
    AN_WINDOW_LOOK_REPORT(measured);
}

// Apply is the window's primary button and nothing else of its box accepts: it stands at the right, after Close.
TEST_F(WindowLook, NewSequenceAppliesAtTheRight)
{
    const an::ui::testing::ObservatoryStyleScope scope(an::ui::testing::observatoryLooks()[0]);
    auto* const window = new NewSequenceControl();
    an::tests::look::showOffTheScreen(*window);
    const auto* const box = window->findChild<QDialogButtonBox*>(QStringLiteral("buttonBox"));
    ASSERT_NE(box, nullptr);
    const QPushButton* const apply = box->button(QDialogButtonBox::Apply);
    const QPushButton* const close = box->button(QDialogButtonBox::Close);
    ASSERT_TRUE(apply != nullptr && close != nullptr);
    EXPECT_LT(an::tests::look::placeIn(*window, close).left(), an::tests::look::placeIn(*window, apply).left());
    (void)pumpUntil([window]() { return window->isIdle(); });
    an::tests::look::letGo(window);
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::processEvents();
}

TEST_F(WindowLook, ExportSequence)
{
    an::ui::DocumentSequence sequence;
    const FixtureSequence fixture{.frames = 2u};
    sequence.sourcePath = m_context->pathOf(QStringLiteral("capture.ser"));
    sequence.descriptor.format = an::SequenceFormat::Ser;
    sequence.descriptor.frameCount = fixture.frames;
    ASSERT_TRUE(writeFixture(m_context->engineSession(), sequence.sourcePath, an::SequenceFormat::Ser, fixture));

    const WindowLookMeasured measured = expectWindowLook([&sequence]() -> QWidget* {
        return new ExportSequenceControl(nullptr, sequence, 8u, 6u, false, AutostretchParams{});
    });
    AN_WINDOW_LOOK_REPORT(measured);
}
