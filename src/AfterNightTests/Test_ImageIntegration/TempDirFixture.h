// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A case that writes files owns a temporary directory for its lifetime, removed whether it passes or fails.

#include <gtest/gtest.h>

#include <QString>
#include <QTemporaryDir>

/// A case with a temporary directory of its own.
class TempDirFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_tempDir.isValid()) << "Failed to create temporary directory";
    }

    /// The directory's path.
    [[nodiscard]] QString tempPath() const
    {
        return m_tempDir.path();
    }

    QTemporaryDir m_tempDir;
};
