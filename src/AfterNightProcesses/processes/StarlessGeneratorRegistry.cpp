// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "processes/StarlessGeneratorRegistry.h"

#include "controls/settings/SettingsKeys.h"
#include "controls/settings/StarNetCliTools.h"
#include "processes/ProcessCommonUtils.h"

#include <algorithm>
#include <tuple>

namespace
{

using an::ui::process_registry::ProcessDescriptor;
using an::ui::process_registry::ProcessRegistry;
using an::ui::starless_generators::StarlessGeneratorDescriptor;
using an::ui::starless_generators::StarlessGeneratorQuery;

[[nodiscard]] const ProcessRegistry& registryForQuery(const StarlessGeneratorQuery& query)
{
    return query.registry ? *query.registry : ProcessRegistry::instance();
}

[[nodiscard]] QString selectSetting(const StarlessGeneratorQuery& query, const QString& key,
                                    const QString& defaultValue)
{
    if (query.selectGlobalSetting)
    {
        return query.selectGlobalSetting(key, defaultValue);
    }

    return process_utils::selectGlobalSetting(key, defaultValue);
}

[[nodiscard]] std::string displayNameForDescriptor(const ProcessDescriptor& descriptor)
{
    if (!descriptor.display_name.empty())
    {
        return descriptor.display_name;
    }
    if (!descriptor.process_name.empty())
    {
        return descriptor.process_name;
    }
    return descriptor.id;
}

[[nodiscard]] std::string unavailableReasonForDescriptor(const ProcessDescriptor& descriptor)
{
    const auto availability = an::ui::process_registry::evaluateProcessAvailability(descriptor);
    return availability.ok() ? std::string{} : availability.message;
}

[[nodiscard]] bool isStarnetDescriptor(const ProcessDescriptor& descriptor)
{
    return descriptor.id == an::ui::starless_generators::kStarnetProcessId;
}

[[nodiscard]] bool validateStarnetExecutable(const StarlessGeneratorQuery& query, const QString& path)
{
    if (query.validateStarnetExecutable)
    {
        return query.validateStarnetExecutable(path);
    }

    return an::ui::starnet::checkExecutable(path, 3000).supported;
}

} // namespace

namespace an::ui::starless_generators
{

bool isStarNetConfigured(const StarlessGeneratorQuery& query)
{
    const QString path = selectSetting(query, SettingsKeys::TOOLS_STARNET_PATH, {}).trimmed();
    return !path.isEmpty() && validateStarnetExecutable(query, path);
}

std::vector<StarlessGeneratorDescriptor> listStarlessGenerators(const StarlessGeneratorQuery& query)
{
    std::vector<StarlessGeneratorDescriptor> generators;

    for (const auto& descriptor : registryForQuery(query).all())
    {
        if (!descriptor.provides_starless_output)
        {
            continue;
        }

        StarlessGeneratorDescriptor generator;
        generator.process_id = descriptor.id;
        generator.display_name = displayNameForDescriptor(descriptor);
        generator.available = descriptor.is_available();
        generator.unavailable_reason = unavailableReasonForDescriptor(descriptor);
        generator.is_native_starnet = isStarnetDescriptor(descriptor);
        generator.is_extension_process = descriptor.is_extension_process;

        if (generator.is_native_starnet && !isStarNetConfigured(query))
        {
            generator.available = false;
            generator.unavailable_reason =
                "Configure StarNet2 CLI 2.5.3 or newer in Settings > External Tools to generate a starless view.";
        }

        generators.push_back(std::move(generator));
    }

    std::sort(generators.begin(), generators.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.available != rhs.available)
        {
            return lhs.available;
        }
        return std::tie(lhs.display_name, lhs.process_id) < std::tie(rhs.display_name, rhs.process_id);
    });
    return generators;
}

const process_registry::ProcessDescriptor* findStarlessGeneratorProcess(std::string_view processId,
                                                                        const StarlessGeneratorQuery& query)
{
    const auto* descriptor = registryForQuery(query).find(processId);
    if (!descriptor || !descriptor->provides_starless_output)
    {
        return nullptr;
    }

    return descriptor;
}

} // namespace an::ui::starless_generators
