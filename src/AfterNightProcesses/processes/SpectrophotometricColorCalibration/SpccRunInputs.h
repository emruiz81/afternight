// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/// @file
/// What one spectrophotometric calibration run reads once for every frame it calibrates.
///
/// An Apply over a sequence calibrates every frame, and most of what a frame needs is the run's rather than the
/// frame's: the installed catalog verified against the checksums its manifest states - every file of it, which for the
/// sampled Gaia XP catalog is two gigabytes - its manifest, the response repository with every throughput profile
/// composed, and the ImageIntegration hand-off of the target. A run reads each of them the first time a frame asks and
/// answers every later frame from that; the catalog's region around a field, and the catalog built from it, are kept
/// for the next frame on the same field and the same hand-off sources. The run's parameters carry one of these, so a
/// frame never pays again for what the frame before it read, and the next run - with whatever was installed since -
/// reads everything again.
///
/// Safe to ask from any thread; what it answers is never changed afterwards.

#include "processes/SpectrophotometricColorCalibration/SpccCatalogSource.h"
#include "processes/SpectrophotometricColorCalibration/SpccHandoffSubsetSpectrophotometricCatalog.h"
#include "processes/SpectrophotometricColorCalibration/SpccProcessReadinessPanel.h"
#include "processes/SpectrophotometricColorCalibration/SpccResponseRepository.h"

#include "app/SpccCatalogSettings.h"

#include <AfterNightEngine/Algorithms/Catalog/SpectrumCatalog.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/SkyCoordinate.h>

#include <QString>

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

namespace an
{
class ExecutionContext;
} // namespace an

namespace an::ui::processes
{

/// What one run reads once for all its frames, and the last field's region and catalog.
class AFTERNIGHTPROCESSES_API SpccRunInputs
{
public:
    /// How often each input was read from its files, for the tests that hold a run to reading each once.
    using Reads = SpccRunInputReads;

    SpccRunInputs() = default;
    SpccRunInputs(const SpccRunInputs&) = delete;
    SpccRunInputs& operator=(const SpccRunInputs&) = delete;

    /// The installed catalog inspected with its checksums verified, the first time it is asked for.
    ///
    /// @param directory the catalog's directory; every call of one run names the same one.
    /// @return the inspection.
    [[nodiscard]] an::ui::SpccCatalogDirectoryInspection verifiedCatalog(const QString& directory);

    /// The catalog's manifest, read the first time it is asked for.
    ///
    /// @param execution the operation reading it.
    /// @param directory the catalog's directory.
    /// @return the manifest; or what reading it refused, which is not kept.
    [[nodiscard]] an::Result<std::shared_ptr<const an::SpectrumCatalogManifest>>
    manifest(const an::ExecutionContext& execution, const QString& directory);

    /// The response repository, loaded the first time it is asked for.
    ///
    /// @param execution the operation loading it.
    /// @param directory the repository's directory.
    /// @return the repository; or what loading it refused, which is not kept.
    [[nodiscard]] an::Result<std::shared_ptr<const SpccResponseRepository>>
    repository(const an::ExecutionContext& execution, const QString& directory);

    /// The target's ImageIntegration hand-off, searched for the first time it is asked for.
    ///
    /// @param targetPath the target's file; every call of one run names the same one.
    /// @return the hand-off.
    [[nodiscard]] SpccIntegrationHandoff handoff(const QString& targetPath);

    /// The catalog's chunks a field and a set of hand-off sources need: the last region read when the field and the
    /// sources are the same, read otherwise.
    ///
    /// @param execution the operation reading them.
    /// @param directory the catalog's directory.
    /// @param manifest the catalog's manifest.
    /// @param center the field's centre.
    /// @param radiusDegrees the cone's radius.
    /// @param sourceIdentifiers the hand-off's sources.
    /// @return the region; or what `readSpccCatalogRegion` refused, which is not kept.
    [[nodiscard]] an::Result<std::shared_ptr<const SpccCatalogRegion>>
    region(const an::ExecutionContext& execution, const QString& directory, const an::SpectrumCatalogManifest& manifest,
           const an::SkyCoordinate& center, double radiusDegrees, std::span<const std::uint64_t> sourceIdentifiers);

    /// The catalog of a region `region` answered: the one already built from it, built otherwise.
    ///
    /// @param execution the operation building it.
    /// @param region the region.
    /// @return the catalog; or what `buildSpccCatalog` refused, which is not kept.
    [[nodiscard]] an::Result<std::shared_ptr<const an::SpectrumCatalog>>
    catalog(const an::ExecutionContext& execution, const std::shared_ptr<const SpccCatalogRegion>& region);

    /// How often each input was read so far.
    [[nodiscard]] Reads reads() const;

private:
    mutable std::mutex m_mutex;
    std::optional<an::ui::SpccCatalogDirectoryInspection> m_verified;
    std::shared_ptr<const an::SpectrumCatalogManifest> m_manifest;
    std::shared_ptr<const SpccResponseRepository> m_repository;
    std::optional<SpccIntegrationHandoff> m_handoff;
    double m_fieldRightAscension = 0.0;
    double m_fieldDeclination = 0.0;
    double m_fieldRadius = 0.0;
    std::vector<std::uint64_t> m_fieldSources;
    std::shared_ptr<const SpccCatalogRegion> m_region;
    std::shared_ptr<const an::SpectrumCatalog> m_catalog;
    Reads m_reads;
};

} // namespace an::ui::processes
