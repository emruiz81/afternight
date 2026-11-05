// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the star profilers' cases share: star fields whose stars are planted where the case knows, plate-solved, a Gaia
/// catalog of those stars written as the product installs one, the targets the windows open on, the windows
/// themselves, and the waiting a case does for a detection, a read or a write to land.
///
/// **A field's stars are where the case put them.** Each star is a round Gaussian rendered at its pixel centres, its
/// position stated in engine pixel coordinates - the first pixel's centre at one half - so a detection that reports a
/// position half a pixel off, or a document read back shifted, misses its star.
#pragma once

#include "Test_ProcessesShared/ProcessesSharedTestSupport.h"

#include "processes/StarProfilerProcessV1.h"
#include "processes/StarProfilerProcessV2.h"

#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/SkyCoordinate.h>
#include <AfterNightEngine/Interop/StarProfileDocument.h>

#include <QCoreApplication>
#include <QString>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace an::tests::processes::stars
{

using an::tests::processes::ProcessTestContext;
using an::tests::processes::ViewHandle;

/// One planted star.
struct PlantedStar
{
    /// Where it is, in engine pixel coordinates.
    double x = 0.0;
    double y = 0.0;

    /// Its peak above the sky, as a fraction of full scale.
    double amplitude = 0.4;

    /// Its standard deviation, in pixels.
    double sigma = 1.6;

    /// Where the field's solution puts it on the sky.
    an::SkyCoordinate direction{};

    /// Its Gaia G magnitude and identifier.
    float magnitude = 10.0f;
    std::uint64_t sourceIdentifier = 0u;
};

/// A rendered star field and the solution that places it.
struct StarField
{
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t channels = 1u;
    std::optional<an::WcsSolution> solution;
    std::vector<PlantedStar> stars;

    /// The sky, as a fraction of full scale.
    double background = 0.05;

    /// Planar samples, as fractions of full scale.
    std::vector<float> samples;

    [[nodiscard]] float value(std::uint32_t x, std::uint32_t y, std::uint32_t channel) const
    {
        return samples[(static_cast<std::size_t>(channel) * height + y) * width + x];
    }
};

/// A field of round stars of a few amplitudes, scattered apart from one another and from the edges, over a sky with a
/// small deterministic speckle, plate-solved at 2.4 arcseconds a pixel.
///
/// @param width the field's width.
/// @param height the field's height.
/// @param channels one for a grey field, three for a colour one whose channels are the same.
/// @return the field.
[[nodiscard]] StarField makeStarField(std::uint32_t width = 320u, std::uint32_t height = 240u,
                                      std::uint32_t channels = 1u);

/// The planted star nearest a position, and how far it is.
struct NearestStar
{
    const PlantedStar* star = nullptr;
    double distance = 0.0;
};

/// The planted star nearest a position.
[[nodiscard]] NearestStar nearestPlanted(const StarField& field, double x, double y);

/// How many planted stars a list holds a star within `tolerance` pixels of.
[[nodiscard]] std::size_t plantedStarsFound(const StarField& field, const an::ui::StarList& stars, double tolerance);

/// Writes a Gaia chunk catalog of a field's stars into a directory, as the downloader installs one.
[[nodiscard]] bool writeGaiaCatalog(const QString& directory, const StarField& field);

/// The metadata a field's image carries: its solution in its cards when `solved`, and an observation date.
[[nodiscard]] an::ImageMetadata fieldMetadata(const StarField& field, bool solved = true);

/// A field as a new asset of a session in the representation `SampleT` - an integer as a fraction of its range.
template <typename SampleT>
[[nodiscard]] std::optional<an::consumer::ImageAsset> fieldAsset(const an::consumer::EngineSession& session,
                                                                 const StarField& field, an::ImageMetadata metadata);

/// A view over a field's image in a representation, its history begun.
template <typename SampleT>
[[nodiscard]] ViewHandle fieldView(const an::consumer::EngineSession& session, const StarField& field,
                                   const an::ImageMetadata& metadata, const std::string& name = "target");

/// A view over a SER recording of a field's frames - each the field a little brighter than the one before - showing
/// its first frame.
[[nodiscard]] ViewHandle fieldSequenceView(const an::consumer::EngineSession& session, const QString& path,
                                           const StarField& field, std::uint32_t frameCount);

/// A window of a process, closed and deleted with the case once its runs have landed.
template <typename WindowT> struct WindowDeleter
{
    void operator()(WindowT* window) const
    {
        if (window == nullptr)
        {
            return;
        }
        (void)pumpUntil([window]() { return window->isRunIdle(); });
        delete window;
        QCoreApplication::processEvents();
    }
};
template <typename WindowT> using WindowHandle = std::unique_ptr<WindowT, WindowDeleter<WindowT>>;

/// Opens a profiler over a target as the entry point opens it, without the delete-on-close.
template <typename WindowT> [[nodiscard]] WindowHandle<WindowT> openProfiler(ImageViewControl& target)
{
    WindowHandle<WindowT> window(new WindowT(&target));
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    return window;
}

/// Turns the event loop until a window's detections, reads and writes have landed.
[[nodiscard]] bool settleProfiler(const StarProfilerWindow& window);

/// Starts a detection and waits for it to land.
[[nodiscard]] bool detectAndSettle(StarProfilerWindow& window);

/// The stars a window holds now.
[[nodiscard]] an::ui::StarList heldStars(StarProfilerWindow& window);

/// The text of a table cell of a window.
[[nodiscard]] QString cellText(const TableWidget& table, int row, int column);

/// The directory a case installs its catalog in, named in the settings the second profiler reads.
class InstalledCatalog
{
public:
    explicit InstalledCatalog(ProcessTestContext& context);

    [[nodiscard]] QString directory() const;

    /// Writes a field's catalog and names it in the settings.
    [[nodiscard]] bool install(const StarField& field);

private:
    ProcessTestContext& m_context;
    QTemporaryDir m_root;
};

/// A profile document of a generation over a field's planted stars, as a profiler states one: every star where it was
/// planted - a first-generation star by `x` and `y`, a second-generation one by `xpos` and `ypos` and its fitted box -
/// with the figures a fit over it would give, under the header a profiler's defaults give.
///
/// @param generation the document's generation.
/// @param field the field whose stars it lists.
/// @return the document.
[[nodiscard]] an::StarProfileFile plantedProfile(an::StarProfileGeneration generation, const StarField& field);

/// A case's context over the processes' session, installed as the application's.
class ProfilerCase : public ::testing::Test
{
protected:
    void SetUp() override;
    void TearDown() override;

public:
    [[nodiscard]] ProcessTestContext& context() const
    {
        return *m_context;
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_context->engineSession();
    }

    [[nodiscard]] QString temporaryPath(const QString& name) const;

    /// Writes a profile document through the engine's writer to the case's own directory.
    ///
    /// @param name the file's name.
    /// @param document the document.
    /// @return the file's path, or an empty string when it cannot be written.
    [[nodiscard]] QString writtenProfile(const QString& name, const an::StarProfileFile& document) const;

private:
    std::unique_ptr<ProcessTestContext> m_context;
    std::optional<InstalledContext> m_installed;
    QTemporaryDir m_files;
};

} // namespace an::tests::processes::stars
