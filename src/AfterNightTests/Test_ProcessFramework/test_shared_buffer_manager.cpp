// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the shared buffer manager does beyond what the extension seam's suite holds: a buffer whose path is a link out
/// of the managed root is refused, and the sidecar of a plate-solved image states the solution's distortion model and
/// names the solver from the card an older solver wrote. Every buffer lives under a scratch root of the case's own,
/// removed with the case; the image is an asset of the suite's session.

#include "Test_ControlsExtensionSeam/ExtensionSeamTestSupport.h"
#include "Test_ProcessFramework/ProcessFrameworkSuiteSupport.h"

#include "app/SharedBufferManager.h"

#include <AfterNightEngine/Core/ErrorCode.h>

#include <QJsonObject>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>

namespace
{

using namespace an::tests::seam;
using an::ui::extensions::ImageExportRequest;
using an::ui::extensions::SharedBufferAccessMode;
using an::ui::extensions::SharedBufferColorSpace;
using an::ui::extensions::SharedBufferDescriptor;
using an::ui::extensions::SharedBufferManager;
using an::ui::extensions::SharedBufferPixelFormat;
using an::ui::extensions::SharedBufferPixelType;
using an::ui::extensions::SharedBufferRole;
using an::ui::extensions::SharedImageBuffer;
using an::ui::extensions::SharedImageBufferSpec;

[[nodiscard]] SharedImageBufferSpec monoSpec(const std::string& sessionId, const std::string& bufferId)
{
    SharedImageBufferSpec spec;
    spec.sessionId = sessionId;
    spec.bufferId = bufferId;
    spec.symbolicName = "selection";
    spec.width = 4;
    spec.height = 4;
    spec.channels = 1;
    spec.pixelType = SharedBufferPixelType::Float32;
    spec.pixelFormat = SharedBufferPixelFormat::Mono;
    spec.colorSpace = SharedBufferColorSpace::Grayscale;
    spec.accessMode = SharedBufferAccessMode::ReadWrite;
    spec.bufferRole = SharedBufferRole::Preview;
    return spec;
}

} // namespace

// A descriptor whose file is a link inside the managed root to a file outside it is refused, by opening and by
// importing alike: the root is judged by where the file resolves to, not by how its path is spelled.
TEST(SharedBufferManagerTest, ALinkOutOfTheManagedRootIsRefused)
{
    SeamRoot root;
    QTemporaryDir outside;
    ASSERT_TRUE(outside.isValid());

    std::error_code ec;
    std::filesystem::create_directories(root.path() / "session-sym", ec);
    ASSERT_FALSE(ec) << ec.message();
    const std::filesystem::path outsideFile = std::filesystem::path(outside.path().toStdWString()) / "secret.bin";
    {
        std::ofstream out(outsideFile, std::ios::binary);
        out << std::string(64u, 'x');
    }
    const std::filesystem::path link = root.path() / "session-sym" / "escape.bin";
    std::filesystem::create_symlink(outsideFile, link, ec);
    if (ec)
    {
        GTEST_SKIP() << "This account may not create symbolic links: " << ec.message();
    }

    const SharedBufferManager manager(root.path());
    an::Result<SharedImageBuffer> legitimate = manager.allocateBuffer(monoSpec("session-sym", "buffer-sym"));
    ASSERT_TRUE(legitimate.hasValue()) << legitimate.status().message();

    SharedBufferDescriptor escaping = legitimate->descriptor();
    escaping.mapped_file_path = link.generic_string();
    escaping.metadata_sidecar.clear();
    legitimate->release();

    an::Result<SharedImageBuffer> opened = manager.openBuffer(escaping);
    ASSERT_FALSE(opened.hasValue());
    EXPECT_EQ(opened.status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_NE(std::string(opened.status().message()).find("outside"), std::string::npos) << opened.status().message();

    an::Result<an::ui::extensions::ImportedImage> imported =
        manager.importImage(an::tests::processes::framework::suiteContext().engineSession(), escaping);
    ASSERT_FALSE(imported.hasValue());
    EXPECT_EQ(imported.status().code(), an::ErrorCode::InvalidArgument);
}

// The sidecar of an image whose cards state a tangent-plane solution with a SIP distortion carries that model under
// its astrometry record, and names the solver from the card an older solver spelled without the O.
TEST(SharedBufferManagerTest, ASolvedImagesSidecarStatesItsSipDistortionAndItsSolver)
{
    SeamRoot root;
    SeamImageSpec image;
    image.width = 4u;
    image.height = 3u;
    image.fitsCards = {{"NAXIS", "2"},
                       {"NAXIS1", "4"},
                       {"NAXIS2", "3"},
                       {"CTYPE1", "'RA---TAN-SIP'"},
                       {"CTYPE2", "'DEC--TAN-SIP'"},
                       {"CUNIT1", "'deg'"},
                       {"CUNIT2", "'deg'"},
                       {"CRPIX1", "2.0"},
                       {"CRPIX2", "2.0"},
                       {"CRVAL1", "210.123456"},
                       {"CRVAL2", "-12.25"},
                       {"CD1_1", "-0.0002777777778"},
                       {"CD1_2", "0.0"},
                       {"CD2_1", "0.0"},
                       {"CD2_2", "0.0002777777778"},
                       {"EQUINOX", "2000.0"},
                       {"RADESYS", "'ICRS'"},
                       {"PLTSLVR", "'ASTAP'"},
                       {"A_ORDER", "2"},
                       {"B_ORDER", "2"},
                       {"A_2_0", "1.0e-5"},
                       {"B_0_2", "-2.0e-5"}};
    image.exifCards = {{"Exif.Image.Make", "Canon"}, {"Exif.Photo.ExposureTime", "120/1"}};
    std::optional<an::consumer::ImageAsset> asset =
        makeSeamAsset<float>(an::tests::processes::framework::suiteContext().engineSession(), image,
                             [](std::uint32_t, std::uint32_t, std::uint32_t channel) { return 0.1f + 0.4f * channel; });
    ASSERT_TRUE(asset.has_value());

    ImageExportRequest request;
    request.sessionId = "session-solved";
    request.role = SharedBufferRole::Source;
    request.bufferId = "source-buffer";
    request.symbolicName = "source";
    request.fileName = "solved.fit";
    an::Result<an::consumer::AssetReadLease> lease = asset->read();
    ASSERT_TRUE(lease.hasValue()) << lease.status().message();
    an::Result<SharedImageBuffer> buffer = SharedBufferManager(root.path()).exportImage(lease->image(), request);
    ASSERT_TRUE(buffer.hasValue()) << buffer.status().message();
    ASSERT_FALSE(buffer->descriptor().metadata_sidecar.empty());

    const QJsonObject sidecar = readSidecar(buffer->descriptor().metadata_sidecar);
    EXPECT_EQ(sidecar.value(QStringLiteral("CRVAL1")).toString(), QStringLiteral("210.123456"));
    const QJsonObject header = sidecar.value(QStringLiteral("fits_header")).toObject();
    EXPECT_EQ(header.value(QStringLiteral("CTYPE1")).toString(), QStringLiteral("'RA---TAN-SIP'"));
    EXPECT_EQ(
        sidecar.value(QStringLiteral("exif_header")).toObject().value(QStringLiteral("Exif.Image.Make")).toString(),
        QStringLiteral("Canon"));

    const QJsonObject astrometry = sidecar.value(QStringLiteral("astrometry")).toObject();
    ASSERT_TRUE(astrometry.value(QStringLiteral("has_solution")).toBool());
    // The solver is a name, not a FITS string: it is stated without the card's quotes.
    EXPECT_EQ(astrometry.value(QStringLiteral("solver")).toString(), QStringLiteral("ASTAP"));
    EXPECT_NEAR(astrometry.value(QStringLiteral("crval_ra_deg")).toDouble(), 210.123456, 1e-6);
    EXPECT_NEAR(astrometry.value(QStringLiteral("image_scale_arcsec_per_pixel")).toDouble(), 1.0, 1e-3);
    const QJsonObject sip = astrometry.value(QStringLiteral("sip")).toObject();
    ASSERT_FALSE(sip.isEmpty()) << "a solution with a distortion states its SIP model";
    EXPECT_TRUE(sip.value(QStringLiteral("has_forward_model")).toBool());
    EXPECT_EQ(sip.value(QStringLiteral("a_order")).toInt(), 2);
    EXPECT_EQ(sip.value(QStringLiteral("b_order")).toInt(), 2);
}
