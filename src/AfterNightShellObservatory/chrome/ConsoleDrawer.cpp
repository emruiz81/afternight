// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "chrome/ConsoleDrawer.h"

#include "app/SessionConsoleBridge.h"
#include "components/AnTag.h"
#include "controls/LogViewerControl.h"
#include "controls/settings/SettingsDefaults.h"
#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"

#include <kddockwidgets/Config.h>
#include <kddockwidgets/DockWidget.h>
#include <kddockwidgets/core/DockWidget.h>
#include <kddockwidgets/core/Group.h>
#include <kddockwidgets/core/Stack.h>

#include <QAction>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSizePolicy>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include <AfterNightPlatform/Logging.h>

#include <utility>

namespace an::ui::observatory
{

namespace
{

/// Whether a line of the session is a warning or worse.
[[nodiscard]] bool isWarning(const int level)
{
    return level >= static_cast<int>(spdlog::level::warn) && level < static_cast<int>(spdlog::level::off);
}

/// The console's text, whose scroll position a move keeps.
[[nodiscard]] QScrollBar* scrollOf(const LogViewerControl* console)
{
    const auto* const text = console != nullptr ? console->findChild<QPlainTextEdit*>() : nullptr;
    return text != nullptr ? text->verticalScrollBar() : nullptr;
}

} // namespace

/// The band along the drawer's top edge: a press and a drag up or down resize the drawer, the height chosen told when
/// the button is released.
class ConsoleDrawer::Grip final : public QWidget
{
public:
    explicit Grip(ConsoleDrawer& drawer) : QWidget(&drawer), m_drawer(drawer)
    {
        setObjectName(QStringLiteral("observatoryConsoleDrawerGrip"));
        setFixedHeight(an::ui::parts::kConsoleDrawerGrip);
        setCursor(Qt::SizeVerCursor);
        setToolTip(tr("Drag to resize the Session Console"));
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton)
        {
            QWidget::mousePressEvent(event);
            return;
        }
        m_dragging = true;
        m_pressY = event->globalPosition().toPoint().y();
        m_pressHeight = m_drawer.height();
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!m_dragging || !event->buttons().testFlag(Qt::LeftButton))
        {
            QWidget::mouseMoveEvent(event);
            return;
        }
        // The edge follows the pointer: up grows the drawer.
        m_drawer.resizeWhileDragged(m_pressHeight - (event->globalPosition().toPoint().y() - m_pressY));
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (!m_dragging || event->button() != Qt::LeftButton)
        {
            QWidget::mouseReleaseEvent(event);
            return;
        }
        m_dragging = false;
        m_drawer.resizeWhileDragged(m_pressHeight - (event->globalPosition().toPoint().y() - m_pressY));
        m_drawer.tellChanged();
        event->accept();
    }

private:
    ConsoleDrawer& m_drawer;
    bool m_dragging = false;
    int m_pressY = 0;
    int m_pressHeight = 0;
};

ConsoleDrawer::ConsoleDrawer(QWidget& window, Hooks hooks, QWidget* parent)
    : QWidget(parent), m_window(window), m_hooks(std::move(hooks)),
      m_height(SettingsDefaults::INTERFACE_CONSOLE_DRAWER_HEIGHT)
{
    setObjectName(QStringLiteral("observatoryConsoleDrawer"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);
    m_grip = new Grip(*this);
    m_layout->addWidget(m_grip);
    hide();

    // The session's lines are counted from the start, the console made or not.
    if (an::ui::SessionConsoleBridge* const bridge = an::ui::SessionConsoleBridge::instance())
    {
        for (const an::ui::SessionConsoleEntry& entry : bridge->entriesSnapshot())
        {
            if (isWarning(static_cast<int>(entry.level)))
            {
                ++m_warnings;
            }
        }
        QObject::connect(bridge, &an::ui::SessionConsoleBridge::sessionEntryAppended, this,
                         [this](const QString& /*text*/, const int level) { entryAppended(level); });
        QObject::connect(bridge, &an::ui::SessionConsoleBridge::sessionHistoryCleared, this,
                         [this]() { historyCleared(); });
    }
}

ConsoleDrawer::~ConsoleDrawer()
{
    // Nothing is told while the drawer goes: the window it would tell may already be gone.
    m_hooks = {};
    if (m_dock != nullptr)
    {
        QObject::disconnect(m_dock, nullptr, this, nullptr);
        // The console comes back to the drawer and goes with it; the dock widget goes with its window.
        if (m_console != nullptr && m_dock->widget() == m_console)
        {
            m_dock->setWidget(nullptr);
            m_console->setParent(this);
        }
        delete m_dock;
        m_dock = nullptr;
    }
}

void ConsoleDrawer::setOpen(const bool open)
{
    if (open == m_open)
    {
        return;
    }
    m_open = open;
    if (open)
    {
        makeConsole();
        setUnseen(0);
    }
    placeConsole();
    tellChanged();
}

bool ConsoleDrawer::isOpen() const noexcept
{
    return m_open;
}

void ConsoleDrawer::setFloating(const bool floating)
{
    if (floating == m_floating)
    {
        return;
    }
    m_floating = floating;
    updateHeader();
    placeConsole();
    tellChanged();
}

bool ConsoleDrawer::isFloating() const noexcept
{
    return m_floating;
}

void ConsoleDrawer::setDrawerHeight(const int height)
{
    m_height = std::clamp(height, minimumDrawerHeight(), maximumDrawerHeight());
    applyHeight();
    tellChanged();
}

int ConsoleDrawer::drawerHeight() const noexcept
{
    return m_height;
}

int ConsoleDrawer::minimumDrawerHeight() const noexcept
{
    return an::ui::parts::kConsoleDrawerMinimumHeight;
}

int ConsoleDrawer::maximumDrawerHeight() const
{
    return (std::max)(minimumDrawerHeight(), m_window.height() * an::ui::parts::kConsoleDrawerMaximumPercent / 100);
}

void ConsoleDrawer::windowResized()
{
    applyHeight();
}

int ConsoleDrawer::warnings() const noexcept
{
    return m_warnings;
}

int ConsoleDrawer::unseenWarnings() const noexcept
{
    return m_unseen;
}

LogViewerControl* ConsoleDrawer::console() const noexcept
{
    return m_console.data();
}

QToolBar* ConsoleDrawer::header() const noexcept
{
    return m_console != nullptr ? m_console->toolBar() : nullptr;
}

QAction* ConsoleDrawer::floatAction() const noexcept
{
    return m_float;
}

QAction* ConsoleDrawer::closeAction() const noexcept
{
    return m_close;
}

an::ui::AnTag* ConsoleDrawer::warningTag() const noexcept
{
    return m_tag;
}

QWidget* ConsoleDrawer::grip() const noexcept
{
    return m_grip;
}

KDDockWidgets::QtWidgets::DockWidget* ConsoleDrawer::floatingDock() const noexcept
{
    return m_dock;
}

void ConsoleDrawer::refreshIcons()
{
    if (!m_hooks.icon)
    {
        return;
    }
    if (m_float != nullptr)
    {
        m_float->setIcon(m_hooks.icon(m_floating ? QStringLiteral("window") : QStringLiteral("open_in_new")));
    }
    if (m_close != nullptr)
    {
        m_close->setIcon(m_hooks.icon(QStringLiteral("close")));
    }
}

void ConsoleDrawer::paintEvent(QPaintEvent* /*event*/)
{
    const an::ui::DesignTokens& t = an::ui::tokens();
    QPainter painter(this);
    painter.fillRect(rect(), t.layers.surface1);
    // The hairline along the top edge, which the person drags.
    painter.fillRect(QRect(0, 0, width(), 1), t.colors.borderSubtle);
}

void ConsoleDrawer::makeConsole()
{
    if (m_console != nullptr)
    {
        return;
    }
    // The console as AfterNight 1.1 makes it, its toolbar the header, the drawer's parts set about the console's.
    m_console = new LogViewerControl(this);
    m_console->setObjectName(QStringLiteral("observatorySessionConsole"));
    m_console->initialize();
    m_layout->addWidget(m_console, 1);
    QToolBar* const toolbar = m_console->toolBar();
    if (toolbar == nullptr)
    {
        return;
    }
    const auto own = [](QAction* action) {
        action->setProperty(kOwnPartProperty, true);
        return action;
    };
    m_title = new QLabel(tr("Session Console"), toolbar);
    m_title->setObjectName(QStringLiteral("consoleDrawerTitle"));
    m_title->setContentsMargins(an::ui::parts::kTopBarGap, 0, an::ui::parts::kTopBarGap, 0);
    QAction* const first = toolbar->actions().value(0, nullptr);
    own(toolbar->insertWidget(first, m_title));

    QAction* const search = m_console->searchAction();
    auto* const stretch = new QWidget(toolbar);
    stretch->setObjectName(QStringLiteral("consoleDrawerStretch"));
    stretch->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    own(toolbar->insertWidget(search, stretch));
    m_tag = new an::ui::AnTag(QString(), an::ui::AnTag::Tone::Warning, toolbar);
    m_tag->setObjectName(QStringLiteral("consoleDrawerWarnings"));
    m_tag->setToolTip(tr("Warnings and errors in this session"));
    m_tagAction = own(toolbar->insertWidget(search, m_tag));

    m_float = own(toolbar->addAction(tr("Float")));
    m_float->setObjectName(QStringLiteral("consoleDrawerFloat"));
    QObject::connect(m_float, &QAction::triggered, this, [this]() { setFloating(!m_floating); });
    m_close = own(toolbar->addAction(tr("Close")));
    m_close->setObjectName(QStringLiteral("consoleDrawerClose"));
    m_close->setToolTip(tr("Close the Session Console"));
    QObject::connect(m_close, &QAction::triggered, this, [this]() { setOpen(false); });
    updateHeader();
    refreshIcons();
}

void ConsoleDrawer::makeDock()
{
    if (m_dock != nullptr)
    {
        return;
    }
    m_dock = new KDDockWidgets::QtWidgets::DockWidget(QStringLiteral("an.console.window"),
                                                      KDDockWidgets::DockWidgetOption_None,
                                                      KDDockWidgets::LayoutSaverOption::Skip);
    m_dock->setObjectName(QStringLiteral("observatoryConsoleWindow"));
    m_dock->setTitle(tr("Session Console"));
    m_dock->setAffinities({QString::fromLatin1(kAffinity)});
    // A window with one title bar and no tab strip: the system's title bar where the library uses one.
    KDDockWidgets::FloatingWindowFlags flags = KDDockWidgets::FloatingWindowFlag::AlwaysTitleBarWhenFloating;
    if (KDDockWidgets::Config::self().flags().testFlag(KDDockWidgets::Config::Flag_NativeTitleBar))
    {
        flags |= KDDockWidgets::FloatingWindowFlag::NativeTitleBar;
    }
    m_dock->dockWidget()->setFloatingWindowFlags(flags);
    // First over the window's lower half, as wide as two thirds of it.
    const QSize size(m_window.width() * 2 / 3, (std::max)(m_height * 2, minimumDrawerHeight() * 3));
    const QPoint centre = m_window.mapToGlobal(QPoint(m_window.width() / 2, m_window.height() * 2 / 3));
    m_dock->dockWidget()->setFloatingGeometry(QRect(centre - QPoint(size.width() / 2, size.height() / 2), size));
    // Its window closed by the person, the console is closed; it floats still.
    QObject::connect(m_dock, &KDDockWidgets::QtWidgets::DockWidget::isOpenChanged, this, [this](const bool open) {
        if (m_placing || open || !m_open || !m_floating)
        {
            return;
        }
        m_open = false;
        tellChanged();
    });
}

void ConsoleDrawer::placeConsole()
{
    QScrollBar* const scroll = scrollOf(m_console);
    const int position = scroll != nullptr ? scroll->value() : 0;
    const bool keepPosition = scroll != nullptr && position < scroll->maximum();
    m_placing = true;
    const bool inWindow = m_open && m_floating;
    const bool inDrawer = m_open && !m_floating;
    if (inWindow && m_console != nullptr)
    {
        makeDock();
        if (m_dock->widget() != m_console)
        {
            m_layout->removeWidget(m_console);
            m_dock->setWidget(m_console);
        }
        m_console->show();
        if (!m_dock->isOpen())
        {
            m_dock->open();
        }
        // A strip with one tab hides: the window's title is the console's.
        if (KDDockWidgets::Core::Group* const group = m_dock->group())
        {
            group->stack()->setTabBarAutoHide(true);
        }
        m_dock->raise();
    }
    else
    {
        if (m_dock != nullptr && m_dock->isOpen())
        {
            m_dock->forceClose();
        }
        if (m_console != nullptr && m_dock != nullptr && m_dock->widget() == m_console)
        {
            m_dock->setWidget(nullptr);
            m_console->setParent(this);
            m_layout->addWidget(m_console, 1);
            m_console->show();
        }
    }
    setVisible(inDrawer);
    applyHeight();
    m_placing = false;
    // The scroll position is restored once the console is laid out where it went.
    if (keepPosition && m_console != nullptr)
    {
        QTimer::singleShot(0, this, [this, position]() {
            if (QScrollBar* const moved = scrollOf(m_console))
            {
                moved->setValue(position);
            }
        });
    }
}

void ConsoleDrawer::applyHeight()
{
    setFixedHeight(std::clamp(m_height, minimumDrawerHeight(), maximumDrawerHeight()));
}

void ConsoleDrawer::resizeWhileDragged(const int height)
{
    m_height = std::clamp(height, minimumDrawerHeight(), maximumDrawerHeight());
    applyHeight();
}

void ConsoleDrawer::entryAppended(const int level)
{
    if (!isWarning(level))
    {
        return;
    }
    ++m_warnings;
    updateHeader();
    if (!m_open)
    {
        setUnseen(m_unseen + 1);
    }
}

void ConsoleDrawer::historyCleared()
{
    m_warnings = 0;
    updateHeader();
    setUnseen(0);
}

void ConsoleDrawer::setUnseen(const int unseen)
{
    if (unseen == m_unseen)
    {
        return;
    }
    m_unseen = unseen;
    if (m_hooks.unseenChanged)
    {
        m_hooks.unseenChanged(m_unseen);
    }
}

void ConsoleDrawer::updateHeader()
{
    if (m_tag != nullptr && m_tagAction != nullptr)
    {
        m_tag->setText(QString::number(m_warnings));
        m_tagAction->setVisible(m_warnings > 0);
    }
    if (m_float != nullptr)
    {
        m_float->setText(m_floating ? tr("Attach") : tr("Float"));
        m_float->setToolTip(m_floating ? tr("Attach the Session Console to the window")
                                       : tr("Float the Session Console in a window of its own"));
        refreshIcons();
    }
}

void ConsoleDrawer::tellChanged()
{
    if (m_hooks.changed)
    {
        m_hooks.changed();
    }
}

} // namespace an::ui::observatory
