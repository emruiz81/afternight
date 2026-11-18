// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What the cases of Image Integration as a page share: a main window that holds shortcuts of its own, as a shell's
/// catalogue does, with the presenter that takes a process into its centre as a page; and the fixture that makes the
/// process as its launch makes it, in an application context of its own, over the session database of the suite's
/// storage, its session emptied before and after.

#include "Test_ImageIntegration/ui/ProcessWindowContext.h"

#include "app/ProcessRegistry.h"
#include "controls/PageShortcutScope.h"
#include "controls/ProcessLaunchScope.h"
#include "controls/ProcessPresentation.h"
#include "processes/ImageIntegration/ImageIntegrationProcess.h"
#include "processes/ImageIntegration/model/FrameCollection.h"
#include "processes/ImageIntegration/model/FrameMetadata.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/persistence/IntegrationDatabase.h"
#include "processes/ImageIntegration/ui_models/IntegrationJobsModel.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QKeySequence>
#include <QMainWindow>
#include <QPointer>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <functional>
#include <map>

namespace an::tests::integration
{

/// The descriptor Image Integration is launched as.
inline constexpr char kImageIntegration[] = "multi_image_stacking.actionImage_Integration";

/// The session a window opens on.
inline const QString kPageSession = QStringLiteral("last_session");

inline void pump()
{
    for (int round = 0; round < 3; ++round)
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
}

/// Pumps the events until a condition holds, or the bound passes; whether it held.
[[nodiscard]] inline bool pumpUntil(const std::function<bool()>& holds, const int bound = 10000)
{
    QElapsedTimer waited;
    waited.start();
    while (!holds())
    {
        if (waited.elapsed() > bound)
        {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return true;
}

/// An entry's text as its menu draws it: without the mnemonic's mark, and without the keys a page writes after a tab.
[[nodiscard]] inline QString entryText(const QAction& action)
{
    QString text = action.text();
    const qsizetype tab = text.indexOf(QLatin1Char('\t'));
    if (tab >= 0)
    {
        text.truncate(tab);
    }
    return text.remove(QLatin1Char('&'));
}

[[nodiscard]] inline QString portable(const QKeySequence& keys)
{
    return keys.toString(QKeySequence::PortableText);
}

/// A main window that holds a shortcut for every sequence it is given, as a shell's catalogue does, with a button
/// outside its centre; and the presenter that takes a process into its centre as a page.
class CatalogueWindow final : public an::ui::IProcessPresenter
{
public:
    CatalogueWindow()
    {
        auto* central = new QWidget(&m_main);
        auto* layout = new QVBoxLayout(central);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        outside = new QPushButton(QStringLiteral("outside the page"), central);
        layout->addWidget(outside);
        centre = new QWidget(central);
        auto* centreLayout = new QVBoxLayout(centre);
        centreLayout->setContentsMargins(0, 0, 0, 0);
        centreLayout->setSpacing(0);
        layout->addWidget(centre, 1);
        m_main.setCentralWidget(central);
        m_main.resize(1536, 864);
    }

    /// Holds a shortcut of the window's own for a sequence, once.
    void hold(const QKeySequence& keys)
    {
        const QString name = portable(keys);
        if (ran.contains(name))
        {
            return;
        }
        ran[name] = 0;
        auto* action = new QAction(name, &m_main);
        action->setShortcut(keys);
        m_main.addAction(action);
        QObject::connect(action, &QAction::triggered, &m_main, [this, name]() { ++ran[name]; });
    }

    void show()
    {
        m_main.show();
        m_main.activateWindow();
        (void)QTest::qWaitForWindowActive(&m_main);
        pump();
    }

    [[nodiscard]] bool isActive() const
    {
        return m_main.isActiveWindow();
    }

    bool adopt(TopLevelProcessBaseControl& process) override
    {
        return adopting && process.attachToPage(*centre);
    }
    void runStateChanged(TopLevelProcessBaseControl&, const an::ui::ProcessRunState&) override {}
    void targetChanged(TopLevelProcessBaseControl&, ImageViewControl*) override {}

    QWidget* centre = nullptr;
    QPushButton* outside = nullptr;
    bool adopting = true;

    /// How often the window's own shortcut of each sequence ran, by the sequence's portable text.
    std::map<QString, int> ran;

private:
    QMainWindow m_main;
};

/// Presses a key sequence with the focus on a widget.
inline void press(QWidget& focus, const QKeySequence& keys)
{
    focus.setFocus(Qt::OtherFocusReason);
    pump();
    ASSERT_EQ(QApplication::focusWidget(), &focus) << focus.objectName().toStdString() << " did not take the focus";
    const QKeyCombination combination = keys[0];
    QTest::keyClick(&focus, combination.key(), combination.keyboardModifiers());
    pump();
}

/// A light frame as a session holds it: its file need not be there.
[[nodiscard]] inline FrameInfo lightFrame(const QString& folder, const int index)
{
    const QString file = QDir(folder).filePath(QStringLiteral("light_%1.fit").arg(index));
    return FrameInfo(FrameId(file), file,
                     FrameMetadata(FrameType::Light, QSize(64, 48), 120.0, -10.0, 800, 1, QStringLiteral("L"),
                                   QStringLiteral("M 42"), QDateTime::currentDateTimeUtc(), QString()));
}

/// The cases of Image Integration as a page: each in an application context of its own, over the session database of
/// the suite's storage, its session emptied before and after.
class IntegrationPageCase : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_scope.context().valid());
        forgetTheSession();
    }

    void TearDown() override
    {
        delete m_process.data();
        m_scope.context().presenter = nullptr;
        pump();
        forgetTheSession();
    }

    static void forgetTheSession()
    {
        IntegrationDatabase database(ImageIntegration::applicationSessionDatabasePath());
        if (database.initialize())
        {
            (void)database.deleteSession(kPageSession);
        }
    }

    /// Makes the process as its launch makes it - its descriptor stating a full page - without showing it.
    ImageIntegrationProcess& make()
    {
        an::ui::process_registry::ProcessDescriptor descriptor;
        descriptor.id = kImageIntegration;
        descriptor.surface = an::ui::ProcessSurface::FullPage;
        const an::ui::ProcessLaunchScope launch(descriptor);
        m_process = new ImageIntegrationProcess(nullptr);
        return *m_process;
    }

    /// Makes the process and shows it in the window's centre, as a page.
    ImageIntegrationProcess& openAsPage()
    {
        m_scope.context().presenter = &m_window;
        ImageIntegrationProcess& process = make();
        m_window.show();
        process.show();
        pump();
        return process;
    }

    /// Makes the process and shows it as the window it is where no presenter takes it.
    ImageIntegrationProcess& openAsWindow()
    {
        m_scope.context().presenter = nullptr;
        ImageIntegrationProcess& process = make();
        process.show();
        pump();
        return process;
    }

    /// An action of the process by the text of its entry; null for none.
    [[nodiscard]] QAction* action(const QString& text) const
    {
        for (QAction* candidate : m_process->findChildren<QAction*>())
        {
            if (entryText(*candidate) == text)
            {
                return candidate;
            }
        }
        return nullptr;
    }

    /// The page's shortcuts.
    [[nodiscard]] an::ui::PageShortcutScope* scope() const
    {
        for (QObject* child : m_process->children())
        {
            if (auto* found = dynamic_cast<an::ui::PageShortcutScope*>(child))
            {
                return found;
            }
        }
        return nullptr;
    }

    /// Whether a dialog or a message box stands open.
    [[nodiscard]] static bool aDialogIsOpen()
    {
        for (const QWidget* widget : QApplication::topLevelWidgets())
        {
            if (widget->isVisible() && qobject_cast<const QDialog*>(widget) != nullptr)
            {
                return true;
            }
        }
        return false;
    }

    ScopedProcessWindowContext m_scope;
    CatalogueWindow m_window;
    QPointer<ImageIntegrationProcess> m_process;
};

} // namespace an::tests::integration
