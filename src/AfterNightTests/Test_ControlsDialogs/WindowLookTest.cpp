// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The windows this suite owns that stay windows, under the Observatory profile: the FITS header, Exif, summary and
/// astrometry windows of a view, the About and the System Information dialogs. Each has a heading, a footer whose
/// rightmost button is the primary one, text that meets the contrast rule, nothing clipped as it opens or at its least
/// size, and opens within its screen's available area - in both densities. And a message box, which has no heading of
/// its own: it lies on a dialog's surface, its text meets the rule and the button that answers yes is its rightmost.

#include "Test_ControlsDialogs/DialogsTestSupport.h"

#include "app/MessageBoxes.h"
#include "components/AnWindowHeading.h"
#include "controls/AboutDialog.h"
#include "controls/ImageFITSHeaderViewerControl.h"
#include "controls/ImageViewCompanions.h"
#include "controls/SystemInformationDialog.h"
#include "fixtures/WindowLook.h"

#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <vector>

namespace
{

using an::tests::dialogs::card;
using an::tests::dialogs::DialogsTestContext;
using an::tests::dialogs::InstalledContext;
using an::tests::dialogs::makeCardDocument;
using an::tests::dialogs::makeView;
using an::tests::look::expectModalWindowLook;
using an::tests::look::expectWindowLook;
using an::tests::look::WindowLookMeasured;
using an::ui::ViewTool;

/// The dialogs' context and a view of an image that states FITS cards, Exif fields and a solution, installed for a
/// case.
class WindowLook : public ::testing::Test
{
protected:
    void SetUp() override
    {
        context = an::tests::dialogs::openDialogsContext();
        ASSERT_NE(context, nullptr);
        installed = std::make_unique<InstalledContext>(*context);
        view = makeView("NGC7000_Ha");
        std::optional<an::ui::ImageDocument> document =
            makeCardDocument(context->engineSession(),
                             {card(an::CodecNamespace::Fits, "OBJECT", "'NGC 7000'", "the target"),
                              card(an::CodecNamespace::Fits, "EXPTIME", "300.0", "seconds"),
                              card(an::CodecNamespace::Fits, "FILTER", "'Ha'", "the filter in the wheel"),
                              card(an::CodecNamespace::Tiff, "Exif.Image.Model", "ZWO ASI2600MM Pro", "Ascii"),
                              card(an::CodecNamespace::Tiff, "Exif.Photo.ExposureTime", "300/1", "Rational")},
                             {.displayName = "NGC7000_Ha"});
        ASSERT_TRUE(document.has_value());
        view->setDocument(std::move(*document));
        ASSERT_TRUE(an::tests::dialogs::settle(*view));
    }

    void TearDown() override
    {
        an::tests::viewer::pumpQuietly();
        view.reset();
        installed.reset();
        context.reset();
    }

    /// A tool window of the view, made and shown as the view's own command makes it.
    [[nodiscard]] an::tests::look::MakeWindow tool(const ViewTool which)
    {
        return [this, which]() -> QWidget* { return an::ui::openViewTool(which, *view); };
    }

    std::unique_ptr<DialogsTestContext> context;
    std::unique_ptr<InstalledContext> installed;
    an::tests::dialogs::ViewHandle view;
};

/// What a window's heading says over its title.
[[nodiscard]] QString eyebrowOf(const QWidget& window)
{
    const auto* const heading = window.findChild<an::ui::AnWindowHeading*>(QStringLiteral("anWindowHeading"));
    return heading != nullptr ? heading->eyebrow() : QString();
}

} // namespace

TEST_F(WindowLook, FitsHeader)
{
    const WindowLookMeasured measured = expectWindowLook([this]() -> QWidget* {
        auto* const window = new ImageFITSHeaderViewerControl(view.get());
        EXPECT_EQ(eyebrowOf(*window), QStringLiteral("Information · NGC7000_Ha"));
        return window;
    });
    AN_WINDOW_LOOK_REPORT(measured);
}

TEST_F(WindowLook, Exif)
{
    const WindowLookMeasured measured = expectWindowLook(tool(ViewTool::Exif));
    AN_WINDOW_LOOK_REPORT(measured);
}

TEST_F(WindowLook, Summary)
{
    const WindowLookMeasured measured = expectWindowLook(tool(ViewTool::Summary));
    AN_WINDOW_LOOK_REPORT(measured);
}

TEST_F(WindowLook, AstrometryInfo)
{
    const WindowLookMeasured measured = expectWindowLook([this]() -> QWidget* {
        QWidget* const window = an::ui::openViewTool(ViewTool::AstrometryInfo, *view);
        if (window != nullptr)
        {
            EXPECT_EQ(eyebrowOf(*window), QStringLiteral("Astrometry · NGC7000_Ha"));
        }
        return window;
    });
    AN_WINDOW_LOOK_REPORT(measured);
}

TEST_F(WindowLook, About)
{
    const WindowLookMeasured measured = expectWindowLook([]() -> QWidget* { return new AboutDialog(); });
    AN_WINDOW_LOOK_REPORT(measured);
}

// Under the Observatory profile the sections go by their names alone - a picture in a title is drawn in its own
// colours, which no theme and no Night reaches - and the window's heading says what it is.
TEST_F(WindowLook, SystemInformation)
{
    const WindowLookMeasured measured = expectWindowLook([]() -> QWidget* {
        auto* const dialog = new SystemInformationDialog();
        for (const QGroupBox* section : dialog->findChildren<QGroupBox*>())
        {
            for (const QChar letter : section->title())
            {
                EXPECT_LT(letter.unicode(), 0x2000u)
                    << "a section's title holds a picture: " << section->title().toStdString();
            }
        }
        return dialog;
    });
    AN_WINDOW_LOOK_REPORT(measured);
}

// A message box: on the surface a dialog lies on, its text meeting the contrast rule, and of its buttons the one that
// answers yes at the right, the primary one - in both densities.
TEST_F(WindowLook, AMessageBox)
{
    const auto& looks = an::ui::testing::observatoryLooks();
    for (const std::size_t first : {std::size_t{0}, std::size_t{3}})
    {
        SCOPED_TRACE(looks[first].name);
        an::ui::testing::ObservatoryStyleScope scope(looks[first]);
        bool seen = false;
        QTimer poll;
        poll.setInterval(10);
        QObject::connect(&poll, &QTimer::timeout, &poll, [&]() {
            auto* const box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (box == nullptr || !box->isVisible())
            {
                return;
            }
            poll.stop();
            seen = true;
            an::tests::look::settleWindow();
            EXPECT_EQ(box->palette().color(QPalette::Window), an::ui::tokens().layers.surface1);
            for (const std::size_t theme : {first, first + 1u, first + 2u})
            {
                scope.setLook(looks[theme]);
                an::tests::look::settleWindow(10);
                const QStringList contrast = an::tests::look::contrastProblems(*box);
                EXPECT_TRUE(contrast.isEmpty())
                    << looks[theme].name << ": " << contrast.join(QStringLiteral("\n  ")).toStdString();
            }
            const QStringList footer = an::tests::look::footerProblems(*box);
            EXPECT_TRUE(footer.isEmpty()) << footer.join(QStringLiteral("\n  ")).toStdString();
            const QList<const QPushButton*> row = an::tests::look::footerButtons(*box);
            ASSERT_EQ(row.size(), 2);
            EXPECT_EQ(row.back(), box->button(QMessageBox::Yes));
            if (an::tests::look::screenHolds(*box))
            {
                EXPECT_EQ(an::tests::look::standsOutOfItsScreen(*box), QString());
            }
            box->reject();
        });
        poll.start();
        (void)an::ui::showModernMessageBox(nullptr, QMessageBox::Question, QStringLiteral("Close Image"),
                                           QStringLiteral("The image has changes that were not saved. Close it?"),
                                           QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        poll.stop();
        EXPECT_TRUE(seen) << "no message box was shown";
    }
}
