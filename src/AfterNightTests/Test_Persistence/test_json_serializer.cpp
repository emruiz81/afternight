// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file test_json_serializer.cpp
 * @brief Packet 7C — Serialization standardization tests.
 *
 * Validates the JsonSerializer utility header:
 *  - safe_parse with valid, invalid, and empty input.
 *  - dump_compact and dump_pretty output formatting.
 *  - Round-trip fidelity (dump → parse → compare).
 */
#include <gtest/gtest.h>

#include "controls/JsonSerializer.h"

using namespace an::ui::json_serializer;

// ============================================================
// safe_parse
// ============================================================

TEST(JsonSerializer, SafeParse_ValidJson_ReturnsValue)
{
    const auto result = safe_parse(R"({"key":"value","num":42})");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ((*result)["key"].get<std::string>(), "value");
    EXPECT_EQ((*result)["num"].get<int>(), 42);
}

TEST(JsonSerializer, SafeParse_EmptyString_ReturnsNullopt)
{
    const auto result = safe_parse("");
    EXPECT_FALSE(result.has_value());
}

TEST(JsonSerializer, SafeParse_MalformedJson_ReturnsNullopt)
{
    const auto result = safe_parse("{not valid json");
    EXPECT_FALSE(result.has_value());
}

TEST(JsonSerializer, SafeParse_NullLiteral_ReturnsValue)
{
    const auto result = safe_parse("null");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->is_null());
}

TEST(JsonSerializer, SafeParse_ArrayInput_ReturnsValue)
{
    const auto result = safe_parse("[1,2,3]");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->is_array());
    EXPECT_EQ(result->size(), 3u);
}

// ============================================================
// dump_compact
// ============================================================

TEST(JsonSerializer, DumpCompact_ProducesNoWhitespace)
{
    json doc;
    doc["a"] = 1;
    doc["b"] = "hello";

    const std::string output = dump_compact(doc);

    // Compact format has no spaces or newlines between tokens
    EXPECT_EQ(output.find('\n'), std::string::npos);
    EXPECT_EQ(output.find("  "), std::string::npos);
    EXPECT_NE(output.find("\"a\":1"), std::string::npos);
}

TEST(JsonSerializer, DumpCompact_EmptyObject)
{
    const json doc = json::object();
    EXPECT_EQ(dump_compact(doc), "{}");
}

// ============================================================
// dump_pretty
// ============================================================

TEST(JsonSerializer, DumpPretty_ContainsNewlines)
{
    json doc;
    doc["key"] = "value";

    const std::string output = dump_pretty(doc);
    EXPECT_NE(output.find('\n'), std::string::npos);
}

TEST(JsonSerializer, DumpPretty_CustomIndent)
{
    json doc;
    doc["key"] = "value";

    const std::string output = dump_pretty(doc, 4);
    // 4-space indent for the key
    EXPECT_NE(output.find("    \"key\""), std::string::npos);
}

// ============================================================
// Round-trip
// ============================================================

TEST(JsonSerializer, RoundTrip_CompactDumpThenParse_Identical)
{
    json original;
    original["version"] = 1;
    original["params"]["threshold"] = 0.5;
    original["params"]["enabled"] = true;
    original["params"]["name"] = "test";

    const std::string compactStr = dump_compact(original);
    const auto parsed = safe_parse(compactStr);

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, original);
}

TEST(JsonSerializer, RoundTrip_PrettyDumpThenParse_Identical)
{
    json original;
    original["items"] = json::array({1, 2, 3});
    original["nested"]["deep"] = "value";

    const std::string prettyStr = dump_pretty(original);
    const auto parsed = safe_parse(prettyStr);

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, original);
}
