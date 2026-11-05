// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "SharedBufferFileMapping.h"

#include <QFileInfo>
#include <QUuid>

#include <system_error>
#include <utility>

namespace an::ui::extensions
{

namespace
{

int bytesPerSample(const SharedBufferPixelType pixel_type) noexcept
{
    switch (pixel_type)
    {
    case SharedBufferPixelType::UInt8:
        return 1;
    case SharedBufferPixelType::UInt16:
        return 2;
    case SharedBufferPixelType::Float32:
        return 4;
    case SharedBufferPixelType::Float64:
        return 8;
    }

    return 0;
}

bool ensureRootExists(const std::filesystem::path& root_path, std::string* error)
{
    if (root_path.empty())
    {
        if (error)
        {
            *error = "Shared buffer root path is empty";
        }
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(root_path, ec);
    if (ec)
    {
        if (error)
        {
            *error = "Failed to create shared buffer root path: " + root_path.generic_string();
        }
        return false;
    }

    return true;
}

} // namespace

SharedBufferFileMapping::SharedBufferFileMapping(SharedBufferFileMapping&& other) noexcept
{
    *this = std::move(other);
}

SharedBufferFileMapping& SharedBufferFileMapping::operator=(SharedBufferFileMapping&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }

    reset();
    descriptor_ = std::move(other.descriptor_);
    file_path_ = std::move(other.file_path_);
    file_ = std::move(other.file_);
    mapped_bytes_ = other.mapped_bytes_;
    mapped_size_ = other.mapped_size_;
    other.mapped_bytes_ = nullptr;
    other.mapped_size_ = 0;
    other.file_path_.clear();
    return *this;
}

SharedBufferFileMapping::~SharedBufferFileMapping()
{
    reset();
}

bool SharedBufferFileMapping::valid() const noexcept
{
    return file_ != nullptr && mapped_bytes_ != nullptr && mapped_size_ != 0;
}

const SharedBufferDescriptor& SharedBufferFileMapping::descriptor() const noexcept
{
    return descriptor_;
}

const std::filesystem::path& SharedBufferFileMapping::file_path() const noexcept
{
    return file_path_;
}

std::uint64_t SharedBufferFileMapping::mapped_size() const noexcept
{
    return mapped_size_;
}

std::byte* SharedBufferFileMapping::bytes() noexcept
{
    return reinterpret_cast<std::byte*>(mapped_bytes_);
}

const std::byte* SharedBufferFileMapping::bytes() const noexcept
{
    return reinterpret_cast<const std::byte*>(mapped_bytes_);
}

void SharedBufferFileMapping::reset() noexcept
{
    if (file_ && mapped_bytes_)
    {
        file_->unmap(mapped_bytes_);
    }

    mapped_bytes_ = nullptr;
    mapped_size_ = 0;
    file_.reset();
    file_path_.clear();
    descriptor_ = SharedBufferDescriptor{};
}

SharedBufferFileMapper::SharedBufferFileMapper(std::filesystem::path root_path) : root_path_(std::move(root_path)) {}

const std::filesystem::path& SharedBufferFileMapper::root_path() const noexcept
{
    return root_path_;
}

bool SharedBufferFileMapper::allocateBuffer(const SharedBufferFileSpec& spec, SharedBufferFileMapping* out_mapping,
                                            std::string* error) const
{
    if (!out_mapping)
    {
        if (error)
        {
            *error = "Shared buffer mapping output is null";
        }
        return false;
    }
    if (spec.session_id.empty())
    {
        if (error)
        {
            *error = "Shared buffer allocation requires a session_id";
        }
        return false;
    }
    if (spec.width <= 0 || spec.height <= 0 || spec.channels <= 0)
    {
        if (error)
        {
            *error = "Shared buffer allocation dimensions must be positive";
        }
        return false;
    }
    if (!ensureRootExists(root_path_, error))
    {
        return false;
    }

    const int sample_bytes = bytesPerSample(spec.pixel_type);
    if (sample_bytes <= 0)
    {
        if (error)
        {
            *error = "Shared buffer allocation uses an unsupported pixel type";
        }
        return false;
    }

    SharedBufferDescriptor descriptor;
    descriptor.buffer_id =
        spec.buffer_id.empty() ? QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString() : spec.buffer_id;
    descriptor.symbolic_name = spec.symbolic_name;
    descriptor.width = spec.width;
    descriptor.height = spec.height;
    descriptor.channels = spec.channels;
    descriptor.pixel_type = spec.pixel_type;
    descriptor.pixel_format = spec.pixel_format;
    descriptor.color_space = spec.color_space;
    descriptor.row_stride = static_cast<std::uint64_t>(spec.width) * static_cast<std::uint64_t>(sample_bytes);
    descriptor.plane_stride = descriptor.row_stride * static_cast<std::uint64_t>(spec.height);
    descriptor.byte_size = descriptor.plane_stride * static_cast<std::uint64_t>(spec.channels);
    descriptor.access_mode = spec.access_mode;
    descriptor.buffer_role = spec.buffer_role;
    descriptor.offset_x = spec.offset_x;
    descriptor.offset_y = spec.offset_y;
    descriptor.source_width = spec.source_width;
    descriptor.source_height = spec.source_height;
    descriptor.metadata_sidecar = spec.metadata_sidecar;
    descriptor.mapping_name = spec.session_id + "." + descriptor.buffer_id;

    const auto validation = validateSharedBufferDescriptor(descriptor);
    if (!validation.ok())
    {
        if (error)
        {
            *error = validation.error;
        }
        return false;
    }

    const std::filesystem::path session_root = root_path_ / spec.session_id;
    std::error_code ec;
    std::filesystem::create_directories(session_root, ec);
    if (ec)
    {
        if (error)
        {
            *error = "Failed to create shared buffer session path: " + session_root.generic_string();
        }
        return false;
    }

    const std::filesystem::path file_path = (session_root / (descriptor.buffer_id + ".bin")).lexically_normal();
    QFile file(QString::fromStdString(file_path.string()));
    if (file.exists())
    {
        file.remove();
    }
    if (!file.open(QIODevice::ReadWrite))
    {
        if (error)
        {
            *error = "Failed to open shared buffer file for writing: " + file_path.generic_string();
        }
        return false;
    }
    if (!file.resize(static_cast<qint64>(descriptor.byte_size)))
    {
        if (error)
        {
            *error = "Failed to size shared buffer file: " + file_path.generic_string();
        }
        return false;
    }

    SharedBufferFileMapping mapping;
    mapping.descriptor_ = descriptor;
    mapping.descriptor_.mapped_file_path = file_path.generic_string();
    mapping.file_path_ = file_path;
    mapping.file_ = std::make_unique<QFile>(file.fileName());
    if (!mapping.file_->open(QIODevice::ReadWrite))
    {
        if (error)
        {
            *error = "Failed to reopen shared buffer file for ownership transfer: " + file_path.generic_string();
        }
        return false;
    }

    mapping.mapped_bytes_ = mapping.file_->map(0, mapping.descriptor_.byte_size);
    if (!mapping.mapped_bytes_)
    {
        if (error)
        {
            *error = "Failed to memory-map shared buffer file: " + file_path.generic_string();
        }
        return false;
    }
    mapping.mapped_size_ = mapping.descriptor_.byte_size;

    *out_mapping = std::move(mapping);
    return true;
}

bool sharedBufferPathIsWithinRoot(const std::filesystem::path& root, const std::filesystem::path& path)
{
    if (root.empty())
    {
        return false;
    }

    std::error_code ec;
    const auto normalized_root = std::filesystem::weakly_canonical(root, ec);
    if (ec)
    {
        return false;
    }

    const auto normalized_path = std::filesystem::weakly_canonical(path, ec);
    if (ec)
    {
        return false;
    }

    auto root_it = normalized_root.begin();
    auto path_it = normalized_path.begin();
    for (; root_it != normalized_root.end(); ++root_it, ++path_it)
    {
        if (path_it == normalized_path.end() || *root_it != *path_it)
        {
            return false;
        }
    }

    return true;
}

bool sharedBufferRootPathForDescriptor(const SharedBufferDescriptor& descriptor, std::filesystem::path* out_root_path,
                                       std::string* error)
{
    if (!out_root_path)
    {
        if (error)
        {
            *error = "Shared buffer root path output is null";
        }
        return false;
    }
    if (descriptor.mapped_file_path.empty())
    {
        if (error)
        {
            *error = "Shared buffer descriptor does not define a mapped_file_path";
        }
        return false;
    }

    const std::filesystem::path file_path = std::filesystem::path(descriptor.mapped_file_path).lexically_normal();
    const std::filesystem::path session_root = file_path.parent_path();
    const std::filesystem::path root_path = session_root.parent_path();
    if (session_root.empty() || root_path.empty())
    {
        if (error)
        {
            *error = "Shared buffer descriptor did not expose a usable managed root";
        }
        return false;
    }

    *out_root_path = root_path.lexically_normal();
    return true;
}

bool SharedBufferFileMapper::openBuffer(const SharedBufferDescriptor& descriptor, SharedBufferFileMapping* out_mapping,
                                        std::string* error) const
{
    if (!out_mapping)
    {
        if (error)
        {
            *error = "Shared buffer mapping output is null";
        }
        return false;
    }
    if (root_path_.empty())
    {
        if (error)
        {
            *error = "Shared buffer root path is empty";
        }
        return false;
    }

    const auto validation = validateSharedBufferDescriptor(descriptor);
    if (!validation.ok())
    {
        if (error)
        {
            *error = validation.error;
        }
        return false;
    }
    if (descriptor.mapped_file_path.empty())
    {
        if (error)
        {
            *error = "Shared buffer descriptor does not define a mapped_file_path";
        }
        return false;
    }

    const std::filesystem::path file_path = std::filesystem::path(descriptor.mapped_file_path).lexically_normal();
    if (!sharedBufferPathIsWithinRoot(root_path_, file_path))
    {
        if (error)
        {
            *error = "Shared buffer descriptor path is outside the managed root";
        }
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::exists(file_path, ec) || !std::filesystem::is_regular_file(file_path, ec))
    {
        if (error)
        {
            *error = "Shared buffer file does not exist: " + file_path.generic_string();
        }
        return false;
    }

    SharedBufferFileMapping mapping;
    mapping.descriptor_ = descriptor;
    mapping.file_path_ = file_path;
    mapping.file_ = std::make_unique<QFile>(QString::fromStdString(file_path.string()));
    const auto open_mode =
        descriptor.access_mode == SharedBufferAccessMode::ReadOnly ? QIODevice::ReadOnly : QIODevice::ReadWrite;
    if (!mapping.file_->open(open_mode))
    {
        if (error)
        {
            *error = "Failed to open shared buffer file: " + file_path.generic_string();
        }
        return false;
    }

    const QFileInfo file_info(*mapping.file_);
    if (static_cast<std::uint64_t>(file_info.size()) < descriptor.byte_size)
    {
        if (error)
        {
            *error = "Shared buffer file is smaller than the descriptor byte_size";
        }
        return false;
    }

    mapping.mapped_bytes_ = mapping.file_->map(0, descriptor.byte_size);
    if (!mapping.mapped_bytes_)
    {
        if (error)
        {
            *error = "Failed to memory-map shared buffer file: " + file_path.generic_string();
        }
        return false;
    }
    mapping.mapped_size_ = descriptor.byte_size;

    *out_mapping = std::move(mapping);
    return true;
}

} // namespace an::ui::extensions
