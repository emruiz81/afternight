// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file JsonSerializer.h
 * @brief Standard JSON serialization utilities for the persistence layer.
 *
 * **Convention:** All new persistence serialization code MUST use nlohmann
 * JSON. Existing Qt JSON (QJsonDocument/QJsonObject) code should be moved to
 * nlohmann when touched. Plain-text delimiters for structured data are
 * prohibited in new code.
 *
 * Usage:
 * @code
 *     #include "controls/JsonSerializer.h"
 *     using an::ui::json_serializer::json;
 *
 *     // Serialize
 *     json doc;
 *     doc["version"] = 1;
 *     doc["params"]["threshold"] = 0.5;
 *     std::string output = an::ui::json_serializer::dump_compact(doc);
 *
 *     // Deserialize (safe)
 *     auto parsed = an::ui::json_serializer::safe_parse(output);
 *     if (parsed) {
 *         double threshold = (*parsed)["params"].value("threshold", 0.0);
 *     }
 * @endcode
 */
#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace an::ui::json_serializer
{

/// Standard JSON type alias — use this instead of `nlohmann::json` directly.
using json = nlohmann::json;

/**
 * @brief Safely parse a JSON string, returning std::nullopt on failure.
 *
 * Catches all nlohmann exceptions and returns nullopt rather than throwing.
 * Callers should log failures at the appropriate level.
 *
 * @param input  Raw JSON string (may be empty or malformed).
 * @return Parsed JSON object, or std::nullopt on any parse error.
 */
[[nodiscard]] inline std::optional<json> safe_parse(std::string_view input) noexcept
{
    if (input.empty())
        return std::nullopt;

    try
    {
        return json::parse(input);
    }
    catch (...)
    {
        return std::nullopt;
    }
}

/**
 * @brief Serialize a JSON object to a compact (no whitespace) string.
 *
 * This is the preferred format for database storage where human readability
 * is unnecessary.
 *
 * @param doc  JSON object to serialize.
 * @return Compact JSON string.
 */
[[nodiscard]] inline std::string dump_compact(const json& doc)
{
    return doc.dump(-1);
}

/**
 * @brief Serialize a JSON object to a pretty-printed string (2-space indent).
 *
 * Use for file export or debug output where readability matters.
 *
 * @param doc    JSON object to serialize.
 * @param indent Number of spaces per indent level (default: 2).
 * @return Pretty-printed JSON string.
 */
[[nodiscard]] inline std::string dump_pretty(const json& doc, int indent = 2)
{
    return doc.dump(indent);
}

} // namespace an::ui::json_serializer
