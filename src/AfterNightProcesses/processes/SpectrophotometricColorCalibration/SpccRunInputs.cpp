// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What one spectrophotometric calibration run reads once for every frame it calibrates.

#include "stdafx.hpp"

#include "processes/SpectrophotometricColorCalibration/SpccRunInputs.h"

#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <algorithm>
#include <utility>

namespace an::ui::processes
{

an::ui::SpccCatalogDirectoryInspection SpccRunInputs::verifiedCatalog(const QString& directory)
{
    const std::lock_guard lock(m_mutex);
    if (!m_verified.has_value())
    {
        m_verified = an::ui::inspectSpccCatalogDirectoryDetailed(directory, true);
        ++m_reads.catalogVerifications;
    }
    return *m_verified;
}

an::Result<std::shared_ptr<const an::SpectrumCatalogManifest>>
SpccRunInputs::manifest(const an::ExecutionContext& execution, const QString& directory)
{
    const std::lock_guard lock(m_mutex);
    if (m_manifest == nullptr)
    {
        ++m_reads.manifests;
        an::Result<an::SpectrumCatalogManifest> read = readSpccCatalogManifest(execution, directory);
        if (!read.hasValue())
        {
            return read.status();
        }
        m_manifest = std::make_shared<const an::SpectrumCatalogManifest>(std::move(*read));
    }
    return m_manifest;
}

an::Result<std::shared_ptr<const SpccResponseRepository>>
SpccRunInputs::repository(const an::ExecutionContext& execution, const QString& directory)
{
    const std::lock_guard lock(m_mutex);
    if (m_repository == nullptr)
    {
        ++m_reads.repositories;
        an::Result<SpccResponseRepository> loaded = SpccResponseRepository::load(execution, directory);
        if (!loaded.hasValue())
        {
            return loaded.status();
        }
        m_repository = std::make_shared<const SpccResponseRepository>(std::move(*loaded));
    }
    return m_repository;
}

SpccIntegrationHandoff SpccRunInputs::handoff(const QString& targetPath)
{
    const std::lock_guard lock(m_mutex);
    if (!m_handoff.has_value())
    {
        m_handoff = locateSpccIntegrationHandoff(targetPath);
        ++m_reads.handoffs;
    }
    return *m_handoff;
}

an::Result<std::shared_ptr<const SpccCatalogRegion>>
SpccRunInputs::region(const an::ExecutionContext& execution, const QString& directory,
                      const an::SpectrumCatalogManifest& manifest, const an::SkyCoordinate& center,
                      const double radiusDegrees, const std::span<const std::uint64_t> sourceIdentifiers)
{
    const std::lock_guard lock(m_mutex);
    if (m_region != nullptr && m_fieldRightAscension == center.rightAscensionDegrees &&
        m_fieldDeclination == center.declinationDegrees && m_fieldRadius == radiusDegrees &&
        std::equal(m_fieldSources.begin(), m_fieldSources.end(), sourceIdentifiers.begin(), sourceIdentifiers.end()))
    {
        return m_region;
    }
    ++m_reads.regions;
    an::Result<SpccCatalogRegion> read =
        readSpccCatalogRegion(execution, directory, manifest, center, radiusDegrees, sourceIdentifiers);
    if (!read.hasValue())
    {
        return read.status();
    }
    m_region = std::make_shared<const SpccCatalogRegion>(std::move(*read));
    m_catalog.reset();
    m_fieldRightAscension = center.rightAscensionDegrees;
    m_fieldDeclination = center.declinationDegrees;
    m_fieldRadius = radiusDegrees;
    m_fieldSources.assign(sourceIdentifiers.begin(), sourceIdentifiers.end());
    return m_region;
}

an::Result<std::shared_ptr<const an::SpectrumCatalog>>
SpccRunInputs::catalog(const an::ExecutionContext& execution, const std::shared_ptr<const SpccCatalogRegion>& region)
{
    const std::lock_guard lock(m_mutex);
    if (m_catalog != nullptr && m_region == region)
    {
        return m_catalog;
    }
    ++m_reads.catalogs;
    an::Result<an::SpectrumCatalog> built = buildSpccCatalog(execution, *region);
    if (!built.hasValue())
    {
        return built.status();
    }
    std::shared_ptr<const an::SpectrumCatalog> made = std::make_shared<const an::SpectrumCatalog>(std::move(*built));
    if (m_region == region)
    {
        m_catalog = made;
    }
    return made;
}

SpccRunInputs::Reads SpccRunInputs::reads() const
{
    const std::lock_guard lock(m_mutex);
    return m_reads;
}

} // namespace an::ui::processes
