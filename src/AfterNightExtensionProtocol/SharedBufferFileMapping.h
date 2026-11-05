// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightExtensionProtocol.h"

#include <QFile>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace an::ui::extensions
{

struct SharedBufferFileSpec
{
    std::string session_id;
    std::string buffer_id;
    std::string symbolic_name;
    int width = 0;
    int height = 0;
    int channels = 0;
    SharedBufferPixelType pixel_type = SharedBufferPixelType::Float32;
    SharedBufferPixelFormat pixel_format = SharedBufferPixelFormat::Mono;
    SharedBufferColorSpace color_space = SharedBufferColorSpace::Linear;
    SharedBufferAccessMode access_mode = SharedBufferAccessMode::ReadWrite;
    SharedBufferRole buffer_role = SharedBufferRole::Destination;
    std::optional<int> offset_x;
    std::optional<int> offset_y;
    std::optional<int> source_width;
    std::optional<int> source_height;
    std::string metadata_sidecar;
};

class AFTERNIGHTEXTENSIONPROTOCOL_API SharedBufferFileMapping
{
public:
    SharedBufferFileMapping() = default;
    SharedBufferFileMapping(SharedBufferFileMapping&& other) noexcept;
    SharedBufferFileMapping& operator=(SharedBufferFileMapping&& other) noexcept;

    SharedBufferFileMapping(const SharedBufferFileMapping&) = delete;
    SharedBufferFileMapping& operator=(const SharedBufferFileMapping&) = delete;

    ~SharedBufferFileMapping();

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] const SharedBufferDescriptor& descriptor() const noexcept;
    [[nodiscard]] const std::filesystem::path& file_path() const noexcept;
    [[nodiscard]] std::uint64_t mapped_size() const noexcept;
    [[nodiscard]] std::byte* bytes() noexcept;
    [[nodiscard]] const std::byte* bytes() const noexcept;

private:
    friend class SharedBufferFileMapper;

    void reset() noexcept;

    SharedBufferDescriptor descriptor_;
    std::filesystem::path file_path_;
    std::unique_ptr<QFile> file_;
    uchar* mapped_bytes_ = nullptr;
    std::uint64_t mapped_size_ = 0;
};

class AFTERNIGHTEXTENSIONPROTOCOL_API SharedBufferFileMapper
{
public:
    explicit SharedBufferFileMapper(std::filesystem::path root_path);

    [[nodiscard]] const std::filesystem::path& root_path() const noexcept;
    [[nodiscard]] bool allocateBuffer(const SharedBufferFileSpec& spec, SharedBufferFileMapping* out_mapping,
                                      std::string* error = nullptr) const;
    [[nodiscard]] bool openBuffer(const SharedBufferDescriptor& descriptor, SharedBufferFileMapping* out_mapping,
                                  std::string* error = nullptr) const;

private:
    std::filesystem::path root_path_;
};

[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool sharedBufferPathIsWithinRoot(const std::filesystem::path& root,
                                                                                const std::filesystem::path& path);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool
sharedBufferRootPathForDescriptor(const SharedBufferDescriptor& descriptor, std::filesystem::path* out_root_path,
                                  std::string* error = nullptr);

} // namespace an::ui::extensions
