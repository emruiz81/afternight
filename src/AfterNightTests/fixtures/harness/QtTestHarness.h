// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

// The Qt test harness: `TestHarness`'s storage root, temporary directory, logger and crash handler, Qt's own settings
// store under that root, then a headless QApplication with the test message filter and the Fusion style.
//
// The application is created last and destroyed first. Qt resolves and caches the temporary directory the first
// time anything asks, so the redirection has to be in place before the application exists; and the application may
// still hold files under the storage root while it runs, so it is gone before the root is removed. It names no
// engine: a suite composes it with the fixture adapter it needs.

#include "../HeadlessQtFixture.h"
#include "TestHarness.h"

#include <QApplication>
#include <QSettings>
#include <QString>

#include <memory>
#include <utility>

namespace an::tests::harness
{

/// A Qt suite's process services. Construct it first in `main`, before `InitGoogleTest`, and return `finish`.
class QtTestHarness
{
public:
    QtTestHarness(int& argc, char** argv, SuiteIdentity identity) : m_harness(std::move(identity))
    {
        if (!m_harness.valid())
        {
            return;
        }
        // Qt's own widget file dialog keeps its history in the native settings store, which is a file under the user's
        // configuration directory where it is not the registry; a case that opens one writes it under the root instead.
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope,
                           QString::fromStdWString(m_harness.storage().root().wstring()));
        an::tests::fixtures::prepareHeadlessQtEnvironment(argc, argv);
        m_application = std::make_unique<QApplication>(argc, argv);
        an::tests::fixtures::installQtTestMessageFilters();
        an::tests::fixtures::applyFusionStyle(*m_application);
    }

    ~QtTestHarness()
    {
        (void)finish(0);
    }

    QtTestHarness(const QtTestHarness&) = delete;
    QtTestHarness& operator=(const QtTestHarness&) = delete;

    /// Whether the storage root exists and the application was created.
    [[nodiscard]] bool valid() const noexcept
    {
        return m_harness.valid() && m_application != nullptr;
    }

    [[nodiscard]] const TestHarness& harness() const noexcept
    {
        return m_harness;
    }

    [[nodiscard]] const TestStorage& storage() const noexcept
    {
        return m_harness.storage();
    }

    /// Destroys the application, then does what `TestHarness::finish` does.
    [[nodiscard]] int finish(int result) noexcept
    {
        m_application.reset();
        return m_harness.finish(result);
    }

private:
    TestHarness m_harness;
    std::unique_ptr<QApplication> m_application;
};

} // namespace an::tests::harness
