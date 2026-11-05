// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"
#include "app/ProcessRegistry.h"

#include <QString>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace an::ui::starless_generators
{

inline constexpr std::string_view kStarnetProcessId = "star_object.actionStarnet";

struct StarlessGeneratorDescriptor
{
    std::string process_id;
    std::string display_name;
    bool available = false;
    std::string unavailable_reason;
    bool is_native_starnet = false;
    bool is_extension_process = false;
};

struct StarlessGeneratorQuery
{
    const process_registry::ProcessRegistry* registry = nullptr;
    std::function<QString(const QString& key, const QString& defaultValue)> selectGlobalSetting;
    std::function<bool(const QString& executablePath)> validateStarnetExecutable;
};

[[nodiscard]] AFTERNIGHTPROCESSES_API bool isStarNetConfigured(const StarlessGeneratorQuery& query = {});

[[nodiscard]] AFTERNIGHTPROCESSES_API std::vector<StarlessGeneratorDescriptor>
listStarlessGenerators(const StarlessGeneratorQuery& query = {});

[[nodiscard]] AFTERNIGHTPROCESSES_API const process_registry::ProcessDescriptor*
findStarlessGeneratorProcess(std::string_view processId, const StarlessGeneratorQuery& query = {});

} // namespace an::ui::starless_generators
