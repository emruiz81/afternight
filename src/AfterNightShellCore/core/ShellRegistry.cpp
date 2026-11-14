// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "core/ShellRegistry.h"

#include <AfterNightPlatform/Logging.h>

#include <utility>

namespace an::ui::shell
{

namespace
{

[[nodiscard]] bool isLowercaseLetter(const QChar character) noexcept
{
    return character >= QLatin1Char('a') && character <= QLatin1Char('z');
}

[[nodiscard]] bool isDigit(const QChar character) noexcept
{
    return character >= QLatin1Char('0') && character <= QLatin1Char('9');
}

} // namespace

bool ShellRegistry::isValidId(const QStringView id)
{
    if (id.isEmpty() || !isLowercaseLetter(id.front()))
    {
        return false;
    }
    for (const QChar character : id)
    {
        if (!isLowercaseLetter(character) && !isDigit(character) && character != QLatin1Char('-'))
        {
            return false;
        }
    }
    return true;
}

bool ShellRegistry::add(ShellDescriptor descriptor)
{
    if (!isValidId(descriptor.id))
    {
        LOG_WARNING("A shell was not registered: its id \"{}\" is not a lowercase word", descriptor.id.toStdString());
        return false;
    }
    if (find(descriptor.id) != nullptr)
    {
        LOG_WARNING("A shell was not registered: the id \"{}\" is registered already", descriptor.id.toStdString());
        return false;
    }
    if (!descriptor.create)
    {
        LOG_WARNING("The shell \"{}\" was not registered: its descriptor cannot make it", descriptor.id.toStdString());
        return false;
    }
    m_descriptors.push_back(std::move(descriptor));
    return true;
}

const ShellDescriptor* ShellRegistry::find(const QStringView id) const
{
    for (const ShellDescriptor& descriptor : m_descriptors)
    {
        if (descriptor.id == id)
        {
            return &descriptor;
        }
    }
    return nullptr;
}

std::vector<const ShellDescriptor*> ShellRegistry::descriptors() const
{
    std::vector<const ShellDescriptor*> result;
    result.reserve(m_descriptors.size());
    for (const ShellDescriptor& descriptor : m_descriptors)
    {
        result.push_back(&descriptor);
    }
    return result;
}

QStringList ShellRegistry::ids() const
{
    QStringList result;
    result.reserve(static_cast<qsizetype>(m_descriptors.size()));
    for (const ShellDescriptor& descriptor : m_descriptors)
    {
        result.push_back(descriptor.id);
    }
    return result;
}

std::size_t ShellRegistry::size() const noexcept
{
    return m_descriptors.size();
}

bool ShellRegistry::isEmpty() const noexcept
{
    return m_descriptors.empty();
}

} // namespace an::ui::shell
