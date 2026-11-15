// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The view's chrome: the toolbar row a view builds by default, held to the record beside this suite, entry for entry,
/// in each of the view's modes; a view that builds no row and offers the same actions to a toolbar outside it; the
/// notice a run lays over a view; the processing lock told on the GUI thread; the steps of a view's history as a strip
/// reads them; and the time a pan takes under a floating toolbar.

#include "Test_ControlsViewer/ViewerTestContext.h"

#include "components/AnFloatingToolbar.h"
#include "controls/ViewProcessingNotice.h"
#include "fixtures/BaselineRecord.h"
#include "fixtures/ObservatoryLook.h"
#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"

#include <QAbstractButton>
#include <QAction>
#include <QComboBox>
#include <QElapsedTimer>
#include <QIcon>
#include <QLayout>
#include <QMenu>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{

using namespace an::tests::viewer;
using an::tests::fixtures::baselineEntry;
using an::tests::fixtures::BaselineRecord;
using an::ui::ViewChromeActions;
using an::ui::ViewChromeMode;

constexpr const char* kViewToolbarRecordPath = "src/AfterNightTests/Test_ImageViewControl/data/ViewToolbar.txt";

[[nodiscard]] QStringList viewToolbarPreamble()
{
    return {
        QStringLiteral(
            "The toolbar row of AfterNight 1.1's image view, in each mode a view is made in, item for item:"),
        QStringLiteral("  row | where the row is: the canvas layout's menu bar, or its first item"),
        QStringLiteral("  action | text | tool tip | checkable or plain | shown or hidden"),
        QStringLiteral("  widget | class | accessible name, else text, else object name | shown or hidden"),
        QStringLiteral("  separator | shown or hidden"),
        QStringLiteral(
            "An item is read as a view with no image has it, before the toolbar's overflow pass first runs."),
        QString(),
        QStringLiteral(
            "ViewToolbar.EmbeddedContentsMatchTheRecord in Test_ImageViewControl compares the view with this"),
        QStringLiteral("record. An entry is edited by the change that means to move it - a renamed action, a new"),
        QStringLiteral("button; the record is not written again whole. A field is `-` when empty; `\\t` is a tab"),
        QStringLiteral("and `\\|` a bar inside a text."),
    };
}

/// The product's own icons by name, as the application's provider hands them out: a toolbar button with an icon shows
/// no text, so a row is as wide as the application's.
class ResourceIcons final : public an::ui::IIconProvider
{
public:
    [[nodiscard]] QIcon getIcon(const QString& name) override
    {
        return QIcon(QStringLiteral(":/AfterNight/icons/SVG/%1.svg").arg(name));
    }

    [[nodiscard]] QPixmap loadSvgFileAsPixmap(const QString&, QSize size, QColor) override
    {
        return QPixmap(size);
    }

    [[nodiscard]] QColor getColorNormal() const override
    {
        return QColor(Qt::white);
    }

    [[nodiscard]] QColor getColorDisabled() const override
    {
        return QColor(Qt::gray);
    }
};

/// A viewer context whose view manager docks its views, as the application's does - a standard view then offers to be
/// maximized - and whose icons are the product's.
class DockingViewerContext final : public ViewerTestContext
{
public:
    using ViewerTestContext::ViewerTestContext;

    [[nodiscard]] bool supportsDockWidgets() const override
    {
        return true;
    }

    [[nodiscard]] an::ui::IIconProviderPtr getIconProvider() override
    {
        return m_icons;
    }

private:
    std::shared_ptr<ResourceIcons> m_icons = std::make_shared<ResourceIcons>();
};

/// The name a toolbar's widget is read by.
[[nodiscard]] QString widgetName(const QWidget* widget)
{
    if (widget == nullptr)
    {
        return {};
    }
    if (!widget->accessibleName().isEmpty())
    {
        return widget->accessibleName();
    }
    if (const auto* button = qobject_cast<const QAbstractButton*>(widget);
        button != nullptr && !button->text().isEmpty())
    {
        return button->text();
    }
    return widget->objectName();
}

[[nodiscard]] QToolBar* toolbarOf(const ImageViewControl& view)
{
    return view.findChild<QToolBar*>(QStringLiteral("anImageViewToolbar"));
}

/// The toolbar row a view built, as the record writes it: where the row is, then every item in order.
[[nodiscard]] QStringList toolbarEntries(const ImageViewControl& view)
{
    const QToolBar* toolbar = toolbarOf(view);
    if (toolbar == nullptr)
    {
        return {QStringLiteral("the view built no toolbar")};
    }
    QStringList entries;
    const auto* layout = qobject_cast<const QVBoxLayout*>(view.layout());
    QString row = QStringLiteral("elsewhere");
    if (layout != nullptr && layout->menuBar() == toolbar)
    {
        row = QStringLiteral("the menu bar");
    }
    else if (layout != nullptr && layout->indexOf(const_cast<QToolBar*>(toolbar)) == 0)
    {
        row = QStringLiteral("the first item");
    }
    entries.append(baselineEntry({QStringLiteral("row"), row}));
    for (QAction* action : toolbar->actions())
    {
        const QString shown = action->isVisible() ? QStringLiteral("shown") : QStringLiteral("hidden");
        if (action->isSeparator())
        {
            entries.append(baselineEntry({QStringLiteral("separator"), shown}));
        }
        else if (auto* widgetAction = qobject_cast<QWidgetAction*>(action); widgetAction != nullptr)
        {
            const QWidget* widget = widgetAction->defaultWidget();
            entries.append(
                baselineEntry({QStringLiteral("widget"),
                               widget != nullptr ? QString::fromLatin1(widget->metaObject()->className()) : QString(),
                               widgetName(widget), shown}));
        }
        else
        {
            entries.append(
                baselineEntry({QStringLiteral("action"), action->text(), action->toolTip(),
                               action->isCheckable() ? QStringLiteral("checkable") : QStringLiteral("plain"), shown}));
        }
    }
    return entries;
}

/// Every action member of a view's chrome, by its member's name.
[[nodiscard]] std::vector<std::pair<std::string, QAction*>> actionMembers(const ViewChromeActions& actions)
{
    return {{"fit", actions.fit},
            {"zoomIn", actions.zoomIn},
            {"zoomOut", actions.zoomOut},
            {"zoom100", actions.zoom100},
            {"maximize", actions.maximize},
            {"autostretchSettings", actions.autostretchSettings},
            {"undo", actions.undo},
            {"redo", actions.redo},
            {"history", actions.history},
            {"statistics", actions.statistics},
            {"histogram", actions.histogram},
            {"exif", actions.exif},
            {"fitsHeader", actions.fitsHeader},
            {"summary", actions.summary},
            {"showAstrometry", actions.showAstrometry},
            {"astrometrySettings", actions.astrometrySettings},
            {"astrometryInfo", actions.astrometryInfo},
            {"annotate", actions.annotate},
            {"plateSolve", actions.plateSolve},
            {"switchImage", actions.switchImage},
            {"takeSnapshot", actions.takeSnapshot},
            {"statisticsOverlay", actions.statisticsOverlay}};
}

/// Every widget member of a view's chrome, by its member's name.
[[nodiscard]] std::vector<std::pair<std::string, QWidget*>> widgetMembers(const ViewChromeActions& actions)
{
    return {{"channel", actions.channel}, {"displayMode", actions.displayMode}, {"previewArea", actions.previewArea}};
}

/// The names of the members that are not null.
[[nodiscard]] std::vector<std::string> presentMembers(const ViewChromeActions& actions)
{
    std::vector<std::string> present;
    for (const auto& [name, action] : actionMembers(actions))
    {
        if (action != nullptr)
        {
            present.push_back(name);
        }
    }
    for (const auto& [name, widget] : widgetMembers(actions))
    {
        if (widget != nullptr)
        {
            present.push_back(name);
        }
    }
    if (actions.astrometryMenu != nullptr)
    {
        present.push_back("astrometryMenu");
    }
    std::sort(present.begin(), present.end());
    return present;
}

/// Counts what the view tells its listeners of its chrome and its lock, and on which thread.
class ChromeListener final : public ImageViewControlListener
{
public:
    explicit ChromeListener(ImageViewControl* view) : ImageViewControlListener(view) {}

    void onChromeActionsChanged(ImageViewControl*) override
    {
        ++chromeChanges;
    }

    void onProcessingLockChanged(ImageViewControl*, const bool locked) override
    {
        lockStates.push_back(locked);
        toldOnGuiThread.push_back(QThread::currentThread() == QCoreApplication::instance()->thread());
    }

    int chromeChanges = 0;
    std::vector<bool> lockStates;
    std::vector<bool> toldOnGuiThread;
};

class ChromeCase : public ::testing::Test
{
protected:
    void SetUp() override
    {
        std::optional<TestSessionProvider> provider = openFileProvider(2u);
        ASSERT_TRUE(provider.has_value());
        m_context = std::make_unique<DockingViewerContext>(std::move(provider->owned()));
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

    /// A view made in a mode with its toolbar where `chrome` says, told before it is initialized.
    [[nodiscard]] static ViewHandle viewIn(const ViewControlMode mode, const ViewChromeMode chrome)
    {
        ViewHandle view(new ImageViewControl());
        view->setChromeMode(chrome);
        view->initialize("chrome", mode);
        view->resize(640, 480);
        return view;
    }

    /// A view made as `viewIn` makes it, shown off the screen at a width its whole row fits: a row too narrow for its
    /// actions moves some into its overflow by hiding them, and a hidden action is a disabled one.
    [[nodiscard]] static ViewHandle shownViewIn(const ViewControlMode mode, const ViewChromeMode chrome)
    {
        ViewHandle view = viewIn(mode, chrome);
        view->setAttribute(Qt::WA_DontShowOnScreen, true);
        view->resize(1600, 900);
        view->show();
        QCoreApplication::processEvents();
        return view;
    }

    /// A grey image of a side, a gradient.
    [[nodiscard]] std::optional<an::consumer::ImageAsset> gradient(const std::uint32_t width,
                                                                   const std::uint32_t height) const
    {
        return makeAsset<float>(session(), width, height, Channels::Grey,
                                [width](std::uint32_t x, std::uint32_t y, std::uint32_t) {
                                    return 0.05f + 0.5f * static_cast<float>((x + y) % width) / width;
                                });
    }

    /// Shows an image in a view as a read shows it - its actions then offered as the image allows - its history begun
    /// as the application begins it, and waits for the view.
    void show(ImageViewControl& view, an::consumer::ImageAsset&& image) const
    {
        an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(std::move(image));
        ASSERT_TRUE(document.hasValue()) << document.status().message();
        view.setDocument(std::move(*document));
        view.notifyImageLoaded();
        if (view.getViewControlMode() == ViewControlMode::standard)
        {
            view.initializeNewImageHistory("Image read from a file");
        }
        ASSERT_TRUE(settle(view));
    }

    /// Edits a view's image into another image of the same shape, recorded in its history as `name`.
    void edit(ImageViewControl& view, const std::string& name, const float value) const
    {
        std::optional<an::consumer::ImageAsset> next =
            makeAsset<float>(session(), 32u, 32u, Channels::Grey,
                             [value](std::uint32_t, std::uint32_t, std::uint32_t) { return value; });
        ASSERT_TRUE(next.has_value());
        auto held = std::make_shared<an::consumer::ImageAsset>(std::move(*next));
        std::optional<bool> shown;
        view.editImage(
            name, "Edited by the case",
            [held](const an::consumer::EngineSession&, const an::consumer::AssetWriteLease&, std::stop_token) {
                return an::Result<an::consumer::ImageAsset>(std::move(*held));
            },
            [&shown](const bool done) { shown = done; });
        ASSERT_TRUE(settle(view));
        ASSERT_TRUE(shown.has_value() && *shown) << "the edit " << name << " was not shown";
    }

    std::unique_ptr<DockingViewerContext> m_context;
    std::optional<InstalledContext> m_installed;
};

class ViewToolbar : public ChromeCase
{
};

class ViewChrome : public ChromeCase
{
};

class ProcessingNotice : public ChromeCase
{
};

class ProcessingLock : public ChromeCase
{
};

class ViewHistorySteps : public ChromeCase
{
};

class Timing : public ChromeCase
{
};

[[nodiscard]] an::ui::ViewProcessingNotice* noticeOf(const ImageViewControl& view)
{
    return view.findChild<an::ui::ViewProcessingNotice*>();
}

} // namespace

// ---- The row a view builds by default.

// The row a view builds by default is the one the record holds, in every mode a view is made in: a standard view, a
// real-time preview before and after it offers its autostretch controls, and a static preview. A button renamed,
// reworded, moved, added or removed fails here until the record's entry says so.
TEST_F(ViewToolbar, EmbeddedContentsMatchTheRecord)
{
    BaselineRecord record(QString::fromLatin1(kViewToolbarRecordPath), viewToolbarPreamble());

    {
        const ViewHandle view = makeView("standard", ViewControlMode::standard);
        EXPECT_TRUE(record.matches(QStringLiteral("standard"), toolbarEntries(*view)));
    }
    {
        const ViewHandle view = makeView("realtime", ViewControlMode::rt_preview);
        EXPECT_TRUE(record.matches(QStringLiteral("rt_preview"), toolbarEntries(*view)));
        view->enableRTPreviewAutostretchControls(true);
        EXPECT_TRUE(record.matches(QStringLiteral("rt_preview with its autostretch controls"), toolbarEntries(*view)));
    }
    {
        const ViewHandle view = makeView("static", ViewControlMode::st_preview);
        EXPECT_TRUE(record.matches(QStringLiteral("st_preview"), toolbarEntries(*view)));
    }
}

// ---- A view whose toolbar is outside it.

// A view told its toolbar is outside it builds no row: nothing is laid out above its canvas, which is the layout's
// first item, and the toolbar that keeps its actions is never shown. Told so after it was made, a view takes its row
// away, and gives it back, item for item, when told its toolbar is its own again.
TEST_F(ViewChrome, ExternalModeBuildsNoToolbarRow)
{
    for (const ViewControlMode mode :
         {ViewControlMode::standard, ViewControlMode::rt_preview, ViewControlMode::st_preview})
    {
        SCOPED_TRACE(static_cast<int>(mode));
        const ViewHandle view = viewIn(mode, ViewChromeMode::External);
        EXPECT_EQ(view->chromeMode(), ViewChromeMode::External);
        const auto* layout = qobject_cast<QVBoxLayout*>(view->layout());
        ASSERT_NE(layout, nullptr);
        EXPECT_EQ(layout->menuBar(), nullptr) << "a view whose toolbar is outside it has no row above its canvas";
        const QToolBar* toolbar = toolbarOf(*view);
        ASSERT_NE(toolbar, nullptr) << "the toolbar keeps the actions";
        EXPECT_EQ(layout->indexOf(const_cast<QToolBar*>(toolbar)), -1);
        EXPECT_EQ(layout->indexOf(view->getScrollArea()), 0) << "the canvas is the view's first row";

        view->setAttribute(Qt::WA_DontShowOnScreen, true);
        view->show();
        QCoreApplication::processEvents();
        EXPECT_FALSE(toolbar->isVisible());
        EXPECT_EQ(view->getScrollArea()->geometry().top(), 0) << "the canvas starts at the view's top";
        view->hide();
    }

    const ViewHandle view = makeView("switched", ViewControlMode::standard);
    const QStringList embedded = toolbarEntries(*view);
    view->setChromeMode(ViewChromeMode::External);
    const auto* layout = qobject_cast<QVBoxLayout*>(view->layout());
    EXPECT_EQ(layout->menuBar(), nullptr);
    view->setChromeMode(ViewChromeMode::Embedded);
    EXPECT_EQ(layout->menuBar(), toolbarOf(*view));
    EXPECT_EQ(toolbarEntries(*view), embedded) << "the row comes back as it was";
}

// What a toolbar shows of a view is the objects its own row holds, and the same objects whichever chrome mode the view
// is in: a part outside the view shows the view's own actions, so their states, shortcuts and tool tips are the view's.
TEST_F(ViewChrome, ActionsAreTheSameObjectsInBothModes)
{
    for (const ViewControlMode mode :
         {ViewControlMode::standard, ViewControlMode::rt_preview, ViewControlMode::st_preview})
    {
        SCOPED_TRACE(static_cast<int>(mode));
        const ViewHandle view = makeView("same", mode);
        if (mode == ViewControlMode::rt_preview)
        {
            view->enableRTPreviewAutostretchControls(true);
        }
        const ViewChromeActions embedded = view->chromeActions();
        const QToolBar* toolbar = toolbarOf(*view);
        ASSERT_NE(toolbar, nullptr);
        const QList<QAction*> held = toolbar->actions();
        for (const auto& [name, action] : actionMembers(embedded))
        {
            if (action != nullptr)
            {
                EXPECT_TRUE(held.contains(action)) << name << " is no action of the view's row";
            }
        }
        for (const auto& [name, widget] : widgetMembers(embedded))
        {
            if (widget == nullptr)
            {
                continue;
            }
            const bool inRow = std::any_of(held.begin(), held.end(), [widget](QAction* action) {
                const auto* widgetAction = qobject_cast<QWidgetAction*>(action);
                return widgetAction != nullptr && widgetAction->defaultWidget() == widget;
            });
            EXPECT_TRUE(inRow) << name << " is no choice of the view's row";
        }
        EXPECT_FALSE(presentMembers(embedded).empty());

        view->setChromeMode(ViewChromeMode::External);
        EXPECT_EQ(view->chromeActions(), embedded) << "the same objects outside the view";
        view->setChromeMode(ViewChromeMode::Embedded);
        EXPECT_EQ(view->chromeActions(), embedded);
    }
}

// A member is null where the view's mode has none, and a choice that is a widget while the view does not offer it -
// told to its listeners when that changes.
TEST_F(ViewChrome, NullMembersPerMode)
{
    {
        const ViewHandle view = makeView("standard", ViewControlMode::standard);
        EXPECT_EQ(presentMembers(view->chromeActions()), (std::vector<std::string>{"annotate",
                                                                                   "astrometryInfo",
                                                                                   "astrometryMenu",
                                                                                   "astrometrySettings",
                                                                                   "autostretchSettings",
                                                                                   "channel",
                                                                                   "displayMode",
                                                                                   "exif",
                                                                                   "fit",
                                                                                   "fitsHeader",
                                                                                   "histogram",
                                                                                   "history",
                                                                                   "maximize",
                                                                                   "plateSolve",
                                                                                   "redo",
                                                                                   "showAstrometry",
                                                                                   "statistics",
                                                                                   "summary",
                                                                                   "undo",
                                                                                   "zoom100",
                                                                                   "zoomIn",
                                                                                   "zoomOut"}));
    }
    {
        // A view the manager does not dock cannot be maximized.
        std::unique_ptr<ViewerTestContext> undocked = openViewerContext();
        ASSERT_NE(undocked, nullptr);
        InstalledContext installed(*undocked);
        const ViewHandle view = makeView("undocked", ViewControlMode::standard);
        EXPECT_EQ(view->chromeActions().maximize, nullptr);
    }
    {
        const ViewHandle view = makeView("realtime", ViewControlMode::rt_preview);
        ChromeListener listener(view.get());
        EXPECT_EQ(presentMembers(view->chromeActions()),
                  (std::vector<std::string>{"channel", "fit", "previewArea", "switchImage", "takeSnapshot", "zoom100",
                                            "zoomIn", "zoomOut"}));
        view->enableRTPreviewAutostretchControls(true);
        EXPECT_EQ(presentMembers(view->chromeActions()),
                  (std::vector<std::string>{"autostretchSettings", "channel", "displayMode", "fit", "previewArea",
                                            "switchImage", "takeSnapshot", "zoom100", "zoomIn", "zoomOut"}));
        EXPECT_EQ(listener.chromeChanges, 1);
        view->enableRTPreviewAutostretchControls(false);
        EXPECT_EQ(view->chromeActions().displayMode, nullptr) << "withdrawn, the display mode is not offered";
        EXPECT_NE(view->chromeActions().autostretchSettings, nullptr) << "an action withdrawn is hidden, not null";
        view->setRTPreviewAreaActionEnabled(false);
        EXPECT_EQ(view->chromeActions().previewArea, nullptr);
        EXPECT_EQ(listener.chromeChanges, 3);
        view->unsubscribeListener(&listener);
    }
    {
        const ViewHandle view = makeView("static", ViewControlMode::st_preview);
        ChromeListener listener(view.get());
        EXPECT_EQ(presentMembers(view->chromeActions()),
                  (std::vector<std::string>{"autostretchSettings", "channel", "displayMode", "fit", "statisticsOverlay",
                                            "zoom100", "zoomIn", "zoomOut"}));
        view->setPresentationChoicesOffered(false);
        EXPECT_EQ(view->chromeActions().channel, nullptr);
        EXPECT_EQ(view->chromeActions().displayMode, nullptr);
        EXPECT_EQ(listener.chromeChanges, 1);
        view->setPresentationChoicesOffered(true);
        EXPECT_NE(view->chromeActions().channel, nullptr);
        EXPECT_EQ(listener.chromeChanges, 2);
        view->unsubscribeListener(&listener);
    }
}

// The view's own row moves what it has no room for into its overflow by hiding the actions themselves. A view whose
// toolbar is outside it hides none - every part showing an action follows the action's visibility - and gives back
// those its row had hidden.
TEST_F(ViewChrome, ExternalModeHidesNoActionForWantOfRoom)
{
    const ViewHandle view = makeView("narrow", ViewControlMode::standard);
    view->setMinimumSize(0, 0);
    view->resize(220, 300);
    view->setAttribute(Qt::WA_DontShowOnScreen, true);
    view->show();
    const auto hidden = [&view]() {
        int count = 0;
        for (const auto& [name, action] : actionMembers(view->chromeActions()))
        {
            if (action != nullptr && action->property("anOverflowHidden").toBool())
            {
                ++count;
            }
        }
        return count;
    };
    ASSERT_TRUE(pumpUntil([&hidden]() { return hidden() > 0; })) << "a narrow row moves actions into its overflow";
    const QAction* zoom100 = view->chromeActions().zoom100;
    ASSERT_NE(zoom100, nullptr);

    view->setChromeMode(ViewChromeMode::External);
    EXPECT_EQ(hidden(), 0);
    EXPECT_TRUE(view->chromeActions().fit->isVisible());
    EXPECT_TRUE(view->chromeActions().statistics->isVisible());
    view->resize(200, 300);
    pumpQuietly();
    EXPECT_EQ(hidden(), 0) << "outside the view, nothing is hidden for want of the row's room";
    view->hide();
}

// ---- The notice of a run.

// A notice covers the canvas with the light scrim, under which the image is still seen, and lays the pill at its
// centre.
TEST_F(ProcessingNotice, LaysAScrimOverTheCanvas)
{
    an::ui::testing::ObservatoryStyleScope look(an::ui::testing::observatoryLooks()[0]);
    const ViewHandle view = makeView("covered", ViewControlMode::standard);
    std::optional<an::consumer::ImageAsset> image = gradient(32u, 32u);
    ASSERT_TRUE(image.has_value());
    show(*view, std::move(*image));
    EXPECT_FALSE(view->hasProcessingNotice());
    EXPECT_EQ(noticeOf(*view), nullptr) << "a view no run writes builds no notice";

    view->setAttribute(Qt::WA_DontShowOnScreen, true);
    view->show();
    view->setProcessingNotice({.text = QStringLiteral("Non Local Bayes is applying")});
    EXPECT_TRUE(view->hasProcessingNotice());
    an::ui::ViewProcessingNotice* notice = noticeOf(*view);
    ASSERT_NE(notice, nullptr);
    EXPECT_TRUE(notice->isVisible());
    EXPECT_EQ(notice->geometry(), view->getScrollArea()->rect()) << "the notice covers the canvas";
    view->resize(700, 520);
    EXPECT_EQ(notice->geometry(), view->getScrollArea()->rect()) << "and follows its size";

    const QColor under(40, 90, 160);
    const an::ui::testing::RenderedPicture picture = an::ui::testing::RenderedPicture::ofWidget(*notice, 1.0, under);
    const an::ui::DesignTokens& t = an::ui::tokens();
    EXPECT_TRUE(an::ui::testing::sameColour(picture.at(2, 2), an::ui::compositeOver(t.colors.scrimSoft, under)))
        << an::ui::testing::colourName(picture.at(2, 2));
    const QRect pill = notice->pillRect();
    EXPECT_EQ(pill.height(), an::ui::parts::kNoticePillHeight);
    EXPECT_LE(std::abs(pill.center().x() - notice->rect().center().x()), 1) << "the pill is at the canvas's centre";
    EXPECT_LE(std::abs(pill.center().y() - notice->rect().center().y()), 1);
    const QColor inPill = picture.at(pill.left() + an::ui::parts::kNoticePillPaddingStart / 2.0, pill.top() + 3.0);
    EXPECT_TRUE(an::ui::testing::sameColour(
        inPill, an::ui::compositeOver(t.colors.scrim, an::ui::compositeOver(t.colors.scrimSoft, under))))
        << "the pill is the scrim over the light scrim: " << an::ui::testing::colourName(inPill);
    view->hide();
}

// Under a notice every action and choice of the toolbar is disabled, in either chrome mode: the image is the run's.
TEST_F(ProcessingNotice, DisablesTheToolbarsActionsAndChoices)
{
    for (const ViewChromeMode chrome : {ViewChromeMode::Embedded, ViewChromeMode::External})
    {
        SCOPED_TRACE(static_cast<int>(chrome));
        const ViewHandle view = shownViewIn(ViewControlMode::standard, chrome);
        std::optional<an::consumer::ImageAsset> image = gradient(32u, 32u);
        ASSERT_TRUE(image.has_value());
        show(*view, std::move(*image));
        ASSERT_TRUE(view->chromeActions().statistics->isEnabled());
        ASSERT_TRUE(view->chromeActions().displayMode->isEnabled());

        view->setProcessingNotice({.text = QStringLiteral("Curves Transformation is applying")});
        for (const auto& [name, action] : actionMembers(view->chromeActions()))
        {
            if (action != nullptr)
            {
                EXPECT_FALSE(action->isEnabled()) << name << " is enabled under the notice";
            }
        }
        EXPECT_FALSE(view->chromeActions().channel->isEnabled());
        EXPECT_FALSE(view->chromeActions().displayMode->isEnabled());
        int triggered = 0;
        QObject::connect(view->chromeActions().zoomIn, &QAction::triggered, view.get(),
                         [&triggered]() { ++triggered; });
        view->chromeActions().zoomIn->trigger();
        EXPECT_EQ(triggered, 0) << "a disabled action does nothing";
    }
}

// The pill says what runs and, when the run can tell, how far it has come: the share in the monospace and a bar.
TEST_F(ProcessingNotice, ShowsWhatRunsAndHowFar)
{
    const ViewHandle view = makeView("told", ViewControlMode::standard);
    view->setProcessingNotice({.text = QStringLiteral("Non Local Bayes is applying"), .progress = 0.42});
    an::ui::ViewProcessingNotice* notice = noticeOf(*view);
    ASSERT_NE(notice, nullptr);
    EXPECT_EQ(notice->notice().text, QStringLiteral("Non Local Bayes is applying"));
    EXPECT_EQ(notice->progressText(), QStringLiteral("42 %"));
    EXPECT_EQ(notice->accessibleName(), QStringLiteral("Non Local Bayes is applying"));
    const int withProgress = notice->pillSize().width();

    view->setProcessingNotice({.text = QStringLiteral("Non Local Bayes is applying")});
    EXPECT_TRUE(notice->progressText().isEmpty()) << "a run that cannot tell shows no share";
    EXPECT_EQ(notice->pillSize().width(),
              withProgress - (2 * an::ui::parts::kNoticePillGap + an::ui::parts::kNoticeBarWidth +
                              an::ui::parts::textWidth(an::ui::parts::sizedFont(an::ui::tokens().typography.monospace,
                                                                                an::ui::tokens().metrics.fontCaption),
                                                       QStringLiteral("42 %"))))
        << "nor its bar";

    view->setProcessingNotice({.text = QStringLiteral("Done soon"), .progress = 1.7});
    EXPECT_EQ(notice->progressText(), QStringLiteral("100 %")) << "a share is at most whole";
}

// The pill's button calls back what the notice offers, and a notice that offers nothing has no button.
TEST_F(ProcessingNotice, ShowCallsBack)
{
    const ViewHandle view = makeView("offered", ViewControlMode::standard);
    int shown = 0;
    view->setProcessingNotice({.text = QStringLiteral("Curves Transformation is applying"),
                               .actionLabel = QStringLiteral("Show"),
                               .action = [&shown]() { ++shown; }});
    an::ui::ViewProcessingNotice* notice = noticeOf(*view);
    ASSERT_NE(notice, nullptr);
    QAbstractButton* button = notice->actionButton();
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(button->text(), QStringLiteral("Show"));
    EXPECT_EQ(button->accessibleName(), QStringLiteral("Show"));
    button->click();
    EXPECT_EQ(shown, 1);
    EXPECT_GT(button->geometry().left(), notice->pillRect().center().x()) << "the button closes the pill";

    view->setProcessingNotice({.text = QStringLiteral("Curves Transformation is applying")});
    EXPECT_EQ(notice->actionButton(), nullptr);
    view->setProcessingNotice({.text = QStringLiteral("No action"), .actionLabel = QStringLiteral("Show")});
    EXPECT_EQ(notice->actionButton(), nullptr) << "a label with nothing to do is no button";
}

// Taken away, a notice leaves every action and choice as the view would have it had there been none - an action the
// view enabled meanwhile enabled, one it disabled meanwhile disabled - and the canvas uncovered.
TEST_F(ProcessingNotice, ClearingRestores)
{
    const ViewHandle view = shownViewIn(ViewControlMode::standard, ViewChromeMode::Embedded);
    const ViewChromeActions& actions = view->chromeActions();
    ASSERT_FALSE(actions.statistics->isEnabled()) << "a view with no image offers no statistics";
    const bool zoomInBefore = actions.zoomIn->isEnabled();

    view->setProcessingNotice({.text = QStringLiteral("Star Removal is applying")});
    // The run's image lands under the notice: the view offers its statistics, stops offering its astrometry info, and
    // offers no channel of a grey image.
    std::optional<an::consumer::ImageAsset> image = gradient(32u, 32u);
    ASSERT_TRUE(image.has_value());
    show(*view, std::move(*image));
    actions.astrometryInfo->setEnabled(false);
    EXPECT_FALSE(actions.statistics->isEnabled()) << "still under the notice";
    EXPECT_FALSE(actions.displayMode->isEnabled()) << "still under the notice";

    view->clearProcessingNotice();
    EXPECT_FALSE(view->hasProcessingNotice());
    for (const auto& [name, action] : actionMembers(actions))
    {
        if (action != nullptr)
        {
            ASSERT_FALSE(action->property("anOverflowHidden").toBool()) << name << " went into the row's overflow";
        }
    }
    EXPECT_TRUE(actions.statistics->isEnabled()) << "enabled meanwhile, enabled now";
    EXPECT_FALSE(actions.astrometryInfo->isEnabled()) << "disabled meanwhile, disabled now";
    EXPECT_EQ(actions.zoomIn->isEnabled(), zoomInBefore);
    EXPECT_FALSE(actions.channel->isEnabled()) << "a grey image's channel stays as the view has it";
    EXPECT_TRUE(actions.displayMode->isEnabled());
    EXPECT_EQ(actions.zoomIn->actionGroup(), nullptr) << "nothing of the notice is left on the actions";
    an::ui::ViewProcessingNotice* notice = noticeOf(*view);
    ASSERT_NE(notice, nullptr);
    EXPECT_FALSE(notice->isVisibleTo(view.get()));

    view->clearProcessingNotice();
    EXPECT_TRUE(actions.statistics->isEnabled()) << "clearing twice changes nothing";
}

// ---- The processing lock.

// The lock is taken on whichever thread starts a run, and the view's listeners are told on the GUI thread, of the
// lock's state then; a lock taken and let go before the GUI thread could tell is not told.
TEST_F(ProcessingLock, NotifiesOnTheGuiThread)
{
    const ViewHandle view = makeView("locked", ViewControlMode::standard);
    ChromeListener listener(view.get());

    std::atomic<bool> taken = false;
    std::thread worker([&view, &taken]() { taken = view->tryRequestLockForProcessing(); });
    worker.join();
    ASSERT_TRUE(taken.load());
    EXPECT_TRUE(listener.lockStates.empty()) << "a worker's lock is not told on the worker";
    ASSERT_TRUE(pumpUntil([&listener]() { return !listener.lockStates.empty(); }));
    EXPECT_EQ(listener.lockStates, std::vector<bool>{true});
    EXPECT_EQ(listener.toldOnGuiThread, std::vector<bool>{true});

    view->releaseLockForProcessing();
    EXPECT_EQ(listener.lockStates, (std::vector<bool>{true, false})) << "the GUI thread's release is told at once";
    ASSERT_TRUE(view->tryRequestLockForProcessing());
    EXPECT_FALSE(view->tryRequestLockForProcessing()) << "a lock held is not taken twice, nor told twice";
    EXPECT_EQ(listener.lockStates, (std::vector<bool>{true, false, true}));
    view->releaseLockForProcessing();

    std::thread brief([&view]() {
        if (view->tryRequestLockForProcessing())
        {
            view->releaseLockForProcessing();
        }
    });
    brief.join();
    pumpQuietly();
    EXPECT_EQ(listener.lockStates, (std::vector<bool>{true, false, true, false}))
        << "taken and let go before the GUI thread could tell: nothing to tell";
    EXPECT_TRUE(std::all_of(listener.toldOnGuiThread.begin(), listener.toldOnGuiThread.end(),
                            [](const bool gui) { return gui; }));
    view->unsubscribeListener(&listener);
}

// ---- The steps of a view's history.

// The steps are what the history window lists - the state the view was created with, then each edit - with the one the
// view shows, and each change of them is told to the steps' listeners until a listener is removed.
TEST_F(ViewHistorySteps, NameTheHistoryAndTheStepShown)
{
    const ViewHandle view = makeView("history", ViewControlMode::standard);
    an::ui::IViewHistory* history = view->viewHistory();
    ASSERT_NE(history, nullptr);
    int told = 0;
    const std::uint64_t token = history->addStepsListener([&told]() { ++told; });

    std::optional<an::consumer::ImageAsset> image = gradient(32u, 32u);
    ASSERT_TRUE(image.has_value());
    show(*view, std::move(*image));
    edit(*view, "Curves Transformation", 0.2f);
    edit(*view, "Histogram Transformation", 0.3f);
    const an::ui::ViewHistorySteps steps = history->steps();
    ASSERT_EQ(steps.steps.size(), 3u);
    EXPECT_EQ(steps.steps[0].name, "Initial state");
    EXPECT_EQ(steps.steps[0].description, "Image read from a file");
    EXPECT_EQ(steps.steps[1].name, "Curves Transformation");
    EXPECT_EQ(steps.steps[1].description, "Edited by the case");
    EXPECT_EQ(steps.steps[2].name, "Histogram Transformation");
    EXPECT_FALSE(steps.steps[2].inherited);
    EXPECT_EQ(steps.current, 2u);
    EXPECT_GE(told, 3) << "the first step and each edit are told";

    const int before = told;
    ASSERT_TRUE(history->undo());
    ASSERT_TRUE(pumpUntil([history]() { return history->steps().current == 1u; }));
    EXPECT_GT(told, before);

    history->removeStepsListener(token);
    const int removed = told;
    ASSERT_TRUE(history->redo());
    ASSERT_TRUE(pumpUntil([history]() { return history->steps().current == 2u; }));
    EXPECT_EQ(told, removed) << "a listener removed is told no more";
    history->removeStepsListener(token);
}

// A step is jumped to as the history window's Jump To Step does: read on a worker and shown when it lands. The step
// shown, one past the end and an inherited one are refused.
TEST_F(ViewHistorySteps, JumpsToAStepAsTheWindowDoes)
{
    const ViewHandle view = makeView("jumped", ViewControlMode::standard);
    std::optional<an::consumer::ImageAsset> image = gradient(32u, 32u);
    ASSERT_TRUE(image.has_value());
    show(*view, std::move(*image));
    edit(*view, "First", 0.2f);
    edit(*view, "Second", 0.3f);
    an::ui::IViewHistory* history = view->viewHistory();
    ASSERT_NE(history, nullptr);

    EXPECT_FALSE(history->jumpToStep(2u)) << "the step shown";
    EXPECT_FALSE(history->jumpToStep(3u)) << "no such step";
    ASSERT_TRUE(history->jumpToStep(0u));
    ASSERT_TRUE(pumpUntil([history]() { return history->steps().current == 0u; }));
    ASSERT_TRUE(settle(*view));
    EXPECT_EQ(sampleOf<float>(*view->document().asset(), 0u, 0u), std::optional<float>(0.05f));

    const ViewHandle derived = makeView("derived", ViewControlMode::standard);
    std::optional<an::consumer::ImageAsset> other = gradient(32u, 32u);
    ASSERT_TRUE(other.has_value());
    show(*derived, std::move(*other));
    an::ui::IViewHistory* inheriting = derived->viewHistory();
    ASSERT_NE(inheriting, nullptr);
    int told = 0;
    const std::uint64_t token = inheriting->addStepsListener([&told]() { ++told; });
    inheriting->inheritLineage(*history);
    EXPECT_EQ(told, 1) << "an inherited lineage is a change of the steps";
    const an::ui::ViewHistorySteps steps = inheriting->steps();
    ASSERT_EQ(steps.steps.size(), 4u) << "the three steps inherited, then the derived view's own";
    EXPECT_TRUE(steps.steps[0].inherited);
    EXPECT_EQ(steps.steps[2].name, "Second");
    EXPECT_TRUE(steps.steps[2].inherited);
    EXPECT_FALSE(steps.steps[3].inherited);
    EXPECT_EQ(steps.current, 3u);
    EXPECT_FALSE(inheriting->jumpToStep(1u)) << "an inherited step is shown, never restored";
    inheriting->removeStepsListener(token);
}

// ---- The time a pan takes under the floating toolbar.

// The GUI thread's time in a frame of a pan - the view's offset moved, then the window's paint flushed - of a view
// 3840 x 2160 showing a 6000 x 4000 image at 100 %, with the floating toolbar over the bottom of the view and without
// it, the two in alternation, the median of nine rounds of twelve frames each. Measured, never asserted, and only when
// AFTERNIGHT_TEST_TIMINGS=1 asks for it; the budget is a frame under the toolbar within one frame of a 60 Hz display,
// 16.7 ms. The suites' platform paints every frame whole; the native platform (`QT_QPA_PLATFORM=windows`, the window
// kept off the screen) moves what was painted where nothing lies over the view, and is the one to read.
TEST_F(Timing, PanUnderTheFloatingToolbar)
{
    if (qEnvironmentVariable("AFTERNIGHT_TEST_TIMINGS") != QStringLiteral("1"))
    {
        GTEST_SKIP() << "a time is measured only when AFTERNIGHT_TEST_TIMINGS=1 asks for it";
    }
    an::ui::testing::ObservatoryStyleScope look(an::ui::testing::observatoryLooks()[0]);
    QWidget host;
    host.setAttribute(Qt::WA_DontShowOnScreen, true);
    auto* layout = new QVBoxLayout(&host);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* view = new ImageViewControl();
    view->setChromeMode(ViewChromeMode::External);
    view->initialize("panned", ViewControlMode::standard);
    layout->addWidget(view);
    host.resize(3840, 2160);
    host.show();
    std::optional<an::consumer::ImageAsset> image = gradient(6000u, 4000u);
    ASSERT_TRUE(image.has_value());
    show(*view, std::move(*image));
    view->zoom100();
    ASSERT_TRUE(settle(*view));

    an::ui::AnFloatingToolbar toolbar(&host);
    toolbar.setActions(view->chromeActions());
    toolbar.setZoom(view->getCurrentZoom());
    toolbar.placeOver(host.rect());
    toolbar.show();
    QCoreApplication::processEvents();

    const auto median = [](std::vector<double> values) {
        std::sort(values.begin(), values.end());
        return values[values.size() / 2u];
    };
    const QPoint start = view->getViewportOffset();
    const auto round = [&](const bool withToolbar) {
        toolbar.setVisible(withToolbar);
        QCoreApplication::processEvents();
        view->setViewportOffset(start);
        EXPECT_TRUE(settle(*view));
        std::vector<double> frames;
        for (int frame = 1; frame <= 12; ++frame)
        {
            QElapsedTimer timer;
            timer.start();
            view->setViewportOffset(start + QPoint(frame * 24, frame * 16));
            // The frame: what the move posted - the update of what it exposed - and the window's paint.
            QCoreApplication::sendPostedEvents();
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            frames.push_back(static_cast<double>(timer.nsecsElapsed()) / 1.0e6);
            EXPECT_TRUE(settle(*view));
        }
        return median(frames);
    };
    std::vector<double> under;
    std::vector<double> bare;
    for (int index = 0; index < 9; ++index)
    {
        under.push_back(round(true));
        bare.push_back(round(false));
    }
    const double withToolbar = median(under);
    const double without = median(bare);
    std::printf("\nPan of a 3840 x 2160 view at 100 %% over a 6000 x 4000 image, median of 9 rounds of 12 frames: "
                "%.3f ms a frame under the floating toolbar (budget 16.7 ms), %.3f ms without it; %.3f ms more\n",
                withToolbar, without, withToolbar - without);
    ::testing::Test::RecordProperty("underTheToolbarMs", std::to_string(withToolbar));
    ::testing::Test::RecordProperty("withoutTheToolbarMs", std::to_string(without));
    host.hide();
}
