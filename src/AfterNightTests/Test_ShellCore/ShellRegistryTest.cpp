// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The registry of shells the application can make: what it takes, what it refuses and what it answers.

#include "ShellCoreTestSupport.h"

#include "core/IShell.h"
#include "core/ShellRegistry.h"

#include <gtest/gtest.h>

#include <QString>
#include <QStringList>

#include <type_traits>
#include <utility>

namespace
{

using an::tests::shellcore::descriptorOf;
using an::tests::shellcore::registryOf;
using an::ui::shell::IShell;
using an::ui::shell::IStatusDisplay;
using an::ui::shell::IViewHost;
using an::ui::shell::ShellDescriptor;
using an::ui::shell::ShellRegistry;

// A shell is reached only through its contract, and destroyed through it.
static_assert(std::is_abstract_v<IShell> && std::has_virtual_destructor_v<IShell>);
static_assert(std::is_abstract_v<IViewHost> && std::has_virtual_destructor_v<IViewHost>);
static_assert(std::is_abstract_v<IStatusDisplay> && std::has_virtual_destructor_v<IStatusDisplay>);

TEST(ShellRegistry, FindsARegisteredShellByItsId)
{
    ShellRegistry registry;
    ASSERT_TRUE(registry.add(descriptorOf(QStringLiteral("classic"), QStringLiteral("Classic"))));
    ASSERT_TRUE(registry.add(descriptorOf(QStringLiteral("observatory"), QStringLiteral("Observatory"))));

    const ShellDescriptor* found = registry.find(QStringLiteral("observatory"));
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->id, QStringLiteral("observatory"));
    EXPECT_EQ(found->displayName, QStringLiteral("Observatory"));
    EXPECT_TRUE(static_cast<bool>(found->create));
}

TEST(ShellRegistry, FindsNothingUnderAnIdNoShellIsRegisteredUnder)
{
    const ShellRegistry registry = registryOf({"classic"});

    EXPECT_EQ(registry.find(QStringLiteral("observatory")), nullptr);
    EXPECT_EQ(registry.find(QString()), nullptr);
    // An id is matched as it is written: the registry holds lowercase ids only.
    EXPECT_EQ(registry.find(QStringLiteral("Classic")), nullptr);
}

TEST(ShellRegistry, ListsTheShellsInTheOrderTheyWereRegistered)
{
    const ShellRegistry registry = registryOf({"observatory", "classic", "nebula"});

    EXPECT_EQ(registry.ids(),
              (QStringList{QStringLiteral("observatory"), QStringLiteral("classic"), QStringLiteral("nebula")}));
    const auto descriptors = registry.descriptors();
    ASSERT_EQ(descriptors.size(), 3u);
    EXPECT_EQ(descriptors[0]->id, QStringLiteral("observatory"));
    EXPECT_EQ(descriptors[1]->id, QStringLiteral("classic"));
    EXPECT_EQ(descriptors[2]->id, QStringLiteral("nebula"));
    EXPECT_EQ(registry.size(), 3u);
}

TEST(ShellRegistry, AnEmptyRegistryListsNothing)
{
    const ShellRegistry registry;

    EXPECT_TRUE(registry.isEmpty());
    EXPECT_EQ(registry.size(), 0u);
    EXPECT_TRUE(registry.ids().isEmpty());
    EXPECT_TRUE(registry.descriptors().empty());
}

TEST(ShellRegistry, RefusesASecondDescriptorWithTheSameId)
{
    ShellRegistry registry;
    ASSERT_TRUE(registry.add(descriptorOf(QStringLiteral("classic"), QStringLiteral("First"))));

    EXPECT_FALSE(registry.add(descriptorOf(QStringLiteral("classic"), QStringLiteral("Second"))));

    EXPECT_EQ(registry.size(), 1u);
    const ShellDescriptor* kept = registry.find(QStringLiteral("classic"));
    ASSERT_NE(kept, nullptr);
    EXPECT_EQ(kept->displayName, QStringLiteral("First"));
}

TEST(ShellRegistry, RefusesAnIdThatIsNotALowercaseWord)
{
    ShellRegistry registry;
    for (const QString& id :
         {QString(), QStringLiteral("Classic"), QStringLiteral("two words"), QStringLiteral(" classic"),
          QStringLiteral("-classic"), QStringLiteral("9shell"), QStringLiteral("dark_sky"), QStringLiteral("céleste")})
    {
        EXPECT_FALSE(ShellRegistry::isValidId(id)) << id.toStdString();
        EXPECT_FALSE(registry.add(descriptorOf(id, QStringLiteral("A shell")))) << id.toStdString();
    }
    EXPECT_TRUE(registry.isEmpty());

    for (const QString& id : {QStringLiteral("classic"), QStringLiteral("dark-sky"), QStringLiteral("shell2")})
    {
        EXPECT_TRUE(ShellRegistry::isValidId(id)) << id.toStdString();
        EXPECT_TRUE(registry.add(descriptorOf(id))) << id.toStdString();
    }
}

TEST(ShellRegistry, RefusesADescriptorThatCannotMakeItsShell)
{
    ShellRegistry registry;
    ShellDescriptor descriptor = descriptorOf(QStringLiteral("classic"));
    descriptor.create = nullptr;

    EXPECT_FALSE(registry.add(std::move(descriptor)));
    EXPECT_TRUE(registry.isEmpty());
}

TEST(ShellRegistry, ADescriptorFoundEarlierStaysValidAsMoreAreRegistered)
{
    ShellRegistry registry;
    ASSERT_TRUE(registry.add(descriptorOf(QStringLiteral("classic"), QStringLiteral("Classic"))));
    const ShellDescriptor* classic = registry.find(QStringLiteral("classic"));
    ASSERT_NE(classic, nullptr);

    for (int index = 0; index < 64; ++index)
    {
        ASSERT_TRUE(registry.add(descriptorOf(QStringLiteral("shell%1").arg(index))));
    }

    EXPECT_EQ(registry.find(QStringLiteral("classic")), classic);
    EXPECT_EQ(classic->displayName, QStringLiteral("Classic"));
}

} // namespace
