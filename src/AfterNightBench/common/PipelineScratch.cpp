// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "PipelineScratch.h"

#include <system_error>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace an::bench
{
namespace
{

/// A UTF-8 path as a filesystem path, without the narrow code page's interpretation.
std::filesystem::path pathOf(const std::string& utf8)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

/// This process's id.
unsigned long processId()
{
#if defined(_WIN32)
    return static_cast<unsigned long>(_getpid());
#else
    return static_cast<unsigned long>(getpid());
#endif
}

} // namespace

PipelineScratch::PipelineScratch(const std::string& prefix, const std::string& kept) : m_kept(!kept.empty())
{
    std::error_code status;
    if (m_kept)
    {
        m_path = pathOf(kept);
        if (std::filesystem::exists(m_path, status) && !std::filesystem::is_empty(m_path, status))
        {
            m_error = "the kept scratch directory '" + kept + "' is not empty";
            return;
        }
    }
    else
    {
        m_path = std::filesystem::temp_directory_path(status) / (prefix + std::to_string(processId()));
        std::filesystem::remove_all(m_path, status);
    }
    status.clear();
    std::filesystem::create_directories(m_path, status);
    if (status || !std::filesystem::is_directory(m_path))
    {
        m_error = "could not create the scratch directory '" + m_path.string() + "'";
        return;
    }
    m_ok = true;
}

PipelineScratch::~PipelineScratch()
{
    if (!m_kept && !m_path.empty())
    {
        std::error_code status;
        std::filesystem::remove_all(m_path, status);
    }
}

} // namespace an::bench
