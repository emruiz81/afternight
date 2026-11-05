// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "RawDump.h"

#include "Digest.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>

namespace an::bench
{
namespace
{

using Json = nlohmann::json;

/// Write `bytes` to a pending file beside `path` and rename it into place.
///
/// The same atomicity rule the engine's generated artifacts follow: a reader must never find a
/// file that exists but is only partly written, because it will read as valid and short.
bool writeAtomically(const std::filesystem::path& path, const void* bytes, std::size_t size, std::string& error)
{
    const std::filesystem::path pending = path.string() + ".pending";
    {
        std::ofstream file(pending, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            error = "cannot open '" + pending.string() + "' for writing";
            return false;
        }
        if (size != 0)
        {
            file.write(static_cast<const char*>(bytes), static_cast<std::streamsize>(size));
        }
        if (!file)
        {
            error = "write to '" + pending.string() + "' failed";
            return false;
        }
    }

    std::error_code code;
    std::filesystem::rename(pending, path, code);
    if (code)
    {
        std::filesystem::remove(path, code);
        std::filesystem::rename(pending, path, code);
    }
    if (code)
    {
        error = "cannot move '" + pending.string() + "' into place: " + code.message();
        std::filesystem::remove(pending, code);
        return false;
    }
    return true;
}

bool readWholeFile(const std::filesystem::path& path, std::uint64_t maximumBytes, std::vector<std::uint8_t>& out,
                   std::string& error)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        error = "cannot open '" + path.string() + "'";
        return false;
    }
    const std::streamoff size = file.tellg();
    if (size < 0)
    {
        error = "cannot size '" + path.string() + "'";
        return false;
    }
    if (static_cast<std::uint64_t>(size) > maximumBytes ||
        static_cast<std::uint64_t>(size) > (std::numeric_limits<std::size_t>::max)())
    {
        error = "'" + path.string() + "' is larger than the permitted size";
        return false;
    }
    file.seekg(0);
    out.resize(static_cast<std::size_t>(size));
    if (size != 0 && !file.read(reinterpret_cast<char*>(out.data()), size))
    {
        error = "cannot read '" + path.string() + "'";
        return false;
    }
    return true;
}

bool isContainedPayloadName(const std::string& name)
{
    const std::filesystem::path path(name);
    return !name.empty() && !path.is_absolute() && !path.has_root_name() && !path.has_root_directory() &&
           !path.has_parent_path() && path.filename() == path && path != "." && path != "..";
}

} // namespace

std::vector<std::string> RawDump::validate() const
{
    std::vector<std::string> problems;

    if (schemaVersion != kSchemaVersion)
    {
        problems.push_back("raw dump schemaVersion " + std::to_string(schemaVersion) + " is not the supported " +
                           std::to_string(kSchemaVersion));
    }
    if (!descriptor.isValid())
    {
        problems.push_back("raw dump has an invalid descriptor");
    }
    else if (payloadBytes != descriptor.totalBytes())
    {
        // The descriptor is the only thing that says what the payload means. If it describes a
        // different number of bytes than the payload holds, every read from it is out of bounds or
        // short, so this is a hard mismatch rather than a warning.
        problems.push_back("raw dump payload is " + std::to_string(payloadBytes) + " bytes but its descriptor " +
                           "implies " + std::to_string(descriptor.totalBytes()));
    }
    if (payloadFile.empty())
    {
        problems.push_back("raw dump names no payload file");
    }
    else if (!isContainedPayloadName(payloadFile))
    {
        problems.push_back("raw dump payload file must be one contained file name beside the descriptor");
    }
    const bool lowercaseHex = payloadDigest.size() == 64 &&
                              std::all_of(payloadDigest.begin(), payloadDigest.end(), [](unsigned char character) {
                                  return std::isdigit(character) != 0 || (character >= 'a' && character <= 'f');
                              });
    if (!lowercaseHex)
    {
        problems.push_back("raw dump has no valid payload digest");
    }
    if (descriptor.byteOrder != ByteOrder::Little)
    {
        problems.push_back("raw dump payload byte order must be canonical little endian");
    }
    for (const MetadataCard& card : cards)
    {
        if (card.key.empty())
        {
            problems.push_back("raw dump carries a metadata card with an empty key");
            break;
        }
    }

    return problems;
}

const MetadataCard* RawDump::findCard(const std::string& key) const
{
    for (const MetadataCard& card : cards)
    {
        if (card.key == key)
        {
            return &card;
        }
    }
    return nullptr;
}

bool writeRawDump(const std::string& descriptorPath, RawDump& dump, const std::vector<std::uint8_t>& payload,
                  std::string& error)
{
    dump.payloadBytes = payload.size();
    dump.payloadDigest = Sha256::of(payload.data(), payload.size());
    if (dump.payloadFile.empty())
    {
        dump.payloadFile = std::filesystem::path(descriptorPath).stem().string() + ".anraw";
    }

    const std::vector<std::string> problems = dump.validate();
    if (!problems.empty())
    {
        error = problems.front();
        return false;
    }

    Json cards = Json::array();
    for (const MetadataCard& card : dump.cards)
    {
        cards.push_back(Json{{"key", card.key}, {"value", card.value}, {"comment", card.comment}});
    }

    const SampleDescriptor& d = dump.descriptor;
    const Json document{{"schemaVersion", dump.schemaVersion},
                        {"descriptor", Json{{"width", d.width},
                                            {"height", d.height},
                                            {"channels", d.channels},
                                            {"sampleType", toString(d.sampleType)},
                                            {"layout", toString(d.layout)},
                                            {"byteOrder", toString(d.byteOrder)},
                                            {"rowStrideBytes", d.rowStrideBytes},
                                            {"planeStrideBytes", d.planeStrideBytes},
                                            {"cfaPattern", d.cfaPattern}}},
                        {"sourcePath", dump.sourcePath},
                        {"sourceFormat", dump.sourceFormat},
                        {"exporterVersion", dump.exporterVersion},
                        {"payloadFile", dump.payloadFile},
                        {"payloadBytes", dump.payloadBytes},
                        {"payloadDigest", dump.payloadDigest},
                        {"cards", cards}};

    const std::filesystem::path descriptor(descriptorPath);
    const std::filesystem::path payloadPath = descriptor.parent_path() / dump.payloadFile;

    // Payload first. A descriptor that exists without its payload is a broken pair a reader will
    // report; a payload without its descriptor is an orphan file nobody reads.
    if (!writeAtomically(payloadPath, payload.data(), payload.size(), error))
    {
        return false;
    }
    const std::string text = document.dump(2);
    if (!writeAtomically(descriptor, text.data(), text.size(), error))
    {
        std::error_code ignored;
        std::filesystem::remove(payloadPath, ignored);
        return false;
    }
    return true;
}

bool readRawDump(const std::string& descriptorPath, RawDump& out, std::vector<std::uint8_t>& payload,
                 std::string& error)
{
    std::vector<std::uint8_t> descriptorBytes;
    constexpr std::uint64_t kMaximumDescriptorBytes = 16ull * 1024ull * 1024ull;
    if (!readWholeFile(descriptorPath, kMaximumDescriptorBytes, descriptorBytes, error))
    {
        return false;
    }

    Json document = Json::parse(std::string(descriptorBytes.begin(), descriptorBytes.end()), nullptr, false);
    if (document.is_discarded() || !document.is_object())
    {
        error = "'" + descriptorPath + "' is not a raw dump descriptor";
        return false;
    }

    try
    {
        out = RawDump{};
        out.schemaVersion = document.value("schemaVersion", 0);
        const Json descriptorNode = document.value("descriptor", Json::object());
        out.descriptor.width = descriptorNode.value("width", 0u);
        out.descriptor.height = descriptorNode.value("height", 0u);
        out.descriptor.channels = descriptorNode.value("channels", 1u);
        out.descriptor.rowStrideBytes = descriptorNode.value("rowStrideBytes", std::uint64_t{0});
        out.descriptor.planeStrideBytes = descriptorNode.value("planeStrideBytes", std::uint64_t{0});
        out.descriptor.cfaPattern = descriptorNode.value("cfaPattern", std::string());
        if (!fromString(descriptorNode.value("sampleType", std::string()), out.descriptor.sampleType) ||
            !fromString(descriptorNode.value("layout", std::string()), out.descriptor.layout) ||
            !fromString(descriptorNode.value("byteOrder", std::string()), out.descriptor.byteOrder))
        {
            error = "'" + descriptorPath + "' names a sample type, layout, or byte order this build does not know";
            return false;
        }

        out.sourcePath = document.value("sourcePath", std::string());
        out.sourceFormat = document.value("sourceFormat", std::string());
        out.exporterVersion = document.value("exporterVersion", std::string());
        out.payloadFile = document.value("payloadFile", std::string());
        out.payloadBytes = document.value("payloadBytes", std::uint64_t{0});
        out.payloadDigest = document.value("payloadDigest", std::string());
        for (const Json& card : document.value("cards", Json::array()))
        {
            out.cards.push_back(MetadataCard{card.value("key", std::string()), card.value("value", std::string()),
                                             card.value("comment", std::string())});
        }
    }
    catch (const std::exception& failure)
    {
        error = "'" + descriptorPath + "' has a field with the wrong JSON type: " + failure.what();
        return false;
    }

    const std::vector<std::string> problems = out.validate();
    if (!problems.empty())
    {
        error = problems.front();
        return false;
    }

    const std::filesystem::path payloadPath = std::filesystem::path(descriptorPath).parent_path() / out.payloadFile;
    std::error_code statusError;
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(payloadPath, statusError)))
    {
        error = "payload '" + payloadPath.string() + "' is a symbolic link";
        return false;
    }
    if (!readWholeFile(payloadPath, out.payloadBytes, payload, error))
    {
        return false;
    }
    if (payload.size() != out.payloadBytes)
    {
        error = "payload '" + payloadPath.string() + "' is " + std::to_string(payload.size()) +
                " bytes, not the recorded " + std::to_string(out.payloadBytes);
        return false;
    }
    const std::string digest = Sha256::of(payload.data(), payload.size());
    if (digest != out.payloadDigest)
    {
        error =
            "payload '" + payloadPath.string() + "' hashes to " + digest + ", not the recorded " + out.payloadDigest;
        return false;
    }
    return true;
}

} // namespace an::bench
