// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <optional>

#include <QByteArray>
#include <QtGlobal>

#include "app/ModernizationFeatureFlags.h"

namespace
{

class ScopedEnvVar
{
public:
    ScopedEnvVar(const char* name, std::optional<QByteArray> value) : m_name(name ? name : "")
    {
        if (m_name.isEmpty())
        {
            return;
        }

        m_had_original = qEnvironmentVariableIsSet(m_name.constData());
        if (m_had_original)
        {
            m_original = qgetenv(m_name.constData());
        }

        if (value.has_value())
        {
            qputenv(m_name.constData(), *value);
        }
        else
        {
            qunsetenv(m_name.constData());
        }
    }

    ~ScopedEnvVar()
    {
        if (m_name.isEmpty())
        {
            return;
        }

        if (m_had_original)
        {
            qputenv(m_name.constData(), m_original);
        }
        else
        {
            qunsetenv(m_name.constData());
        }
    }

private:
    QByteArray m_name;
    QByteArray m_original;
    bool m_had_original = false;
};

} // namespace

TEST(ThemeSystemFeatureFlags, UiTokensEnabled_UsesDefaultWhenEnvUnset)
{
    ScopedEnvVar unset("AN_UI_TOKENS_ENABLED", std::nullopt);

    EXPECT_EQ(an::ui::modernization::ModernizationFeatureFlags::uiTokensEnabled(),
              an::ui::modernization::ModernizationFeatureFlags::kUiTokensEnabledDefault);
}

TEST(ThemeSystemFeatureFlags, UiTokensEnabled_ParsesDisabledEnvValue)
{
    ScopedEnvVar disabled("AN_UI_TOKENS_ENABLED", QByteArrayLiteral("0"));

    EXPECT_FALSE(an::ui::modernization::ModernizationFeatureFlags::uiTokensEnabled());
}

TEST(ThemeSystemFeatureFlags, UiTokensEnabled_ParsesEnabledEnvValue)
{
    ScopedEnvVar enabled("AN_UI_TOKENS_ENABLED", QByteArrayLiteral("1"));

    EXPECT_TRUE(an::ui::modernization::ModernizationFeatureFlags::uiTokensEnabled());
}
