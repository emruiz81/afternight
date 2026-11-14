// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "core/ShellSelection.h"

#include "controls/IAppContext.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"
#include "core/IShell.h"
#include "core/ShellRegistry.h"

#include <AfterNightPlatform/Logging.h>

#include <optional>
#include <utility>

namespace an::ui::shell
{

namespace
{

[[nodiscard]] const char* sourceName(const ShellChoiceSource source) noexcept
{
    switch (source)
    {
    case ShellChoiceSource::CommandLine:
        return "the command line";
    case ShellChoiceSource::Environment:
        return "the environment";
    case ShellChoiceSource::Setting:
        return "the setting";
    case ShellChoiceSource::Default:
        break;
    }
    return "the default";
}

/// What a choice is made of while it is made: the outcome, and the registry it is made from.
class Chooser
{
public:
    explicit Chooser(const ShellRegistry& registry) : m_registry(registry)
    {
        const QStringList ids = registry.ids();
        m_known = ids.isEmpty() ? QStringLiteral("none is registered") : ids.join(QStringLiteral(", "));
    }

    /// Takes the shell `id` names when it is registered.
    [[nodiscard]] bool take(const QString& id, const ShellChoiceSource source)
    {
        const ShellDescriptor* found = m_registry.find(id);
        if (found == nullptr)
        {
            return false;
        }
        m_choice.descriptor = found;
        m_choice.source = source;
        LOG_INFO("The {} shell was chosen by {}", id.toStdString(), sourceName(source));
        return true;
    }

    void warn(const QString& warning)
    {
        LOG_WARNING("{}", warning.toStdString());
        m_choice.warnings.push_back(warning);
    }

    /// "the registered shells are ...", for a warning.
    [[nodiscard]] const QString& known() const noexcept
    {
        return m_known;
    }

    [[nodiscard]] ShellChoice result()
    {
        return std::move(m_choice);
    }

private:
    const ShellRegistry& m_registry;
    QString m_known;
    ShellChoice m_choice;
};

/// The id the first `--shell` option among `arguments` names, read after the program; nothing when no option names
/// one. Every other `--shell` option, and one with no value, is passed over with a warning.
[[nodiscard]] std::optional<QString> commandLineId(const QStringList& arguments, Chooser& chooser)
{
    const QString option = QString::fromLatin1(ShellSelection::kOption);
    const QString joined = option + QLatin1Char('=');
    std::optional<QString> id;
    bool seen = false;
    for (qsizetype index = 1; index < arguments.size(); ++index)
    {
        const QString& argument = arguments.at(index);
        const bool withValue = argument.startsWith(joined);
        if (!withValue && argument != option)
        {
            continue;
        }
        if (seen)
        {
            chooser.warn(QStringLiteral("%1 was not read: only the first %2 option is").arg(argument, option));
            continue;
        }
        seen = true;
        const QString value = withValue ? argument.mid(joined.size()).trimmed() : QString();
        if (value.isEmpty())
        {
            chooser.warn(QStringLiteral("%1 has no value and was passed over: the option is written %2=<id>, the "
                                        "value joined to it; the registered shells are %3")
                             .arg(argument, option, chooser.known()));
            continue;
        }
        id = value;
    }
    return id;
}

} // namespace

ShellChoice ShellSelection::resolve(const QStringList& arguments, const QProcessEnvironment& environment,
                                    IGlobalSettingsProvider* settings, const ShellRegistry& registry)
{
    Chooser chooser(registry);

    if (const std::optional<QString> id = commandLineId(arguments, chooser))
    {
        if (chooser.take(*id, ShellChoiceSource::CommandLine))
        {
            return chooser.result();
        }
        chooser.warn(QStringLiteral("%1=%2 names no registered shell and was passed over; the registered shells are %3")
                         .arg(QString::fromLatin1(kOption), *id, chooser.known()));
    }

    const QString variable = QString::fromLatin1(kEnvironmentVariable);
    if (const QString id = environment.value(variable).trimmed(); !id.isEmpty())
    {
        if (chooser.take(id, ShellChoiceSource::Environment))
        {
            return chooser.result();
        }
        chooser.warn(QStringLiteral("%1=%2 names no registered shell and was passed over; the registered shells are %3")
                         .arg(variable, id, chooser.known()));
    }

    if (settings != nullptr)
    {
        const QString key = QString::fromLatin1(SettingsKeys::INTERFACE_SHELL);
        if (const QString id = settings->selectOptional(key).value_or(QString()).trimmed(); !id.isEmpty())
        {
            if (chooser.take(id, ShellChoiceSource::Setting))
            {
                return chooser.result();
            }
            chooser.warn(QStringLiteral("The setting %1 is %2, which names no registered shell, and was passed over; "
                                        "the registered shells are %3")
                             .arg(key, id, chooser.known()));
        }
    }

    const QString defaultId = QString::fromLatin1(SettingsDefaults::INTERFACE_SHELL);
    if (!chooser.take(defaultId, ShellChoiceSource::Default))
    {
        chooser.warn(QStringLiteral("The default shell, %1, is not registered, so no shell was chosen; the registered "
                                    "shells are %2")
                         .arg(defaultId, chooser.known()));
    }
    return chooser.result();
}

} // namespace an::ui::shell
