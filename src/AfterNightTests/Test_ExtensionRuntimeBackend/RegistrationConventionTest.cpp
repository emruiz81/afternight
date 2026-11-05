// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The pixel convention of the registrations the runtime SDK states and reads: a matrix maps a frame's pixel centres,
/// at integer coordinates, onto the reference's. The engine places a pixel's centre half a pixel further on, so the
/// backend moves every matrix between the two conventions; a translation reads the same in both, so these cases use a
/// scale and a quarter turn, where a matrix stated in the wrong convention lands its pixels half a pixel or a whole
/// pixel away.

#include "RuntimeBackendTestSupport.h"

#include <gtest/gtest.h>

#include <string>

namespace an::tests::runtime_backend
{
namespace
{

void expectPython(const char* source)
{
    const std::string failure = requireSharedRuntime();
    ASSERT_TRUE(failure.empty()) << failure;
    py::gil_scoped_acquire gil;
    py::dict scope;
    try
    {
        runPython(source, scope);
    }
    catch (const std::exception& error)
    {
        FAIL() << error.what();
    }
}

TEST(RegistrationConventionTest, AStoredMatrixMapsPixelCentresAtIntegerCoordinates)
{
    expectPython(R"PY(
import os, tempfile
import numpy as np
import _afternight_runtime as rt
with tempfile.TemporaryDirectory() as root:
    frame = np.zeros((33, 33), np.float32)
    frame[7, 5] = 1.0
    paths = [os.path.join(root, "reference.fits"), os.path.join(root, "frame.fits")]
    rt.save_image(rt.image_from_numpy(np.zeros((33, 33), np.float32), {}), paths[0])
    rt.save_image(rt.image_from_numpy(frame, {}), paths[1])
    doubled = {"matrix": [[2.0, 0.0, 0.0], [0.0, 2.0, 0.0], [0.0, 0.0, 1.0]], "transform_type": "similarity"}
    result = rt.apply_registration_paths(paths, [None, doubled], os.path.join(root, "aligned"),
                                         interpolation="bilinear")
    assert result["succeeded_count"] == 2, result
    aligned = rt.load_image(result["frames"][1]["output_path"]).to_numpy()
    # The pixel whose centre is (5, 7) lands on the one whose centre is (10, 14), whole: a matrix read in the engine's
    # convention would put it between four pixels.
    peak = tuple(int(index) for index in np.unravel_index(int(np.argmax(aligned)), aligned.shape))
    assert peak == (14, 10), peak
    assert abs(float(aligned[14, 10]) - 1.0) < 1e-5, float(aligned[14, 10])
)PY");
}

TEST(RegistrationConventionTest, AFoundMatrixMapsAFramesStarsOntoTheReferencesAtIntegerCentres)
{
    expectPython(R"PY(
import os, tempfile
import numpy as np
import _afternight_runtime as rt
size = 160
y, x = np.mgrid[0:size, 0:size].astype(np.float32)
stars = [(30.0, 34.0, 0.55), (78.0, 24.0, 0.45), (122.0, 31.0, 0.50), (48.0, 82.0, 0.62), (112.0, 72.0, 0.48),
         (140.0, 95.0, 0.42), (32.0, 132.0, 0.44), (88.0, 120.0, 0.58), (126.0, 140.0, 0.52), (64.0, 56.0, 0.46)]
reference = np.full((size, size), 0.02, np.float32)
sigma = 3.5 / 2.35482004503
for cx, cy, amplitude in stars:
    reference += amplitude * np.exp(-((x - cx) ** 2 + (y - cy) ** 2) / (2.0 * sigma * sigma))
reference = np.clip(reference, 0.0, 1.0).astype(np.float32)
# A quarter turn: the frame's pixel (column c, row r) is the reference's (size - 1 - r, c), so the frame-to-reference
# matrix with centres at integer coordinates is [[0, -1, size - 1], [1, 0, 0]].
frame = np.ascontiguousarray(np.rot90(reference))
with tempfile.TemporaryDirectory() as root:
    paths = [os.path.join(root, "reference.fits"), os.path.join(root, "frame.fits")]
    rt.save_image(rt.image_from_numpy(reference, {}), paths[0])
    rt.save_image(rt.image_from_numpy(frame, {}), paths[1])
    result = rt.register_sequence_paths(paths, os.path.join(root, "registered"), min_stars=6,
                                        params={"radius": 8, "sigma": 1.5, "profile": "gaussian"})
    registration = result["frames"][1]["registration"]
    assert result["frames"][1]["succeeded"], result["frames"][1]["error_message"]
    matrix = np.array(registration["matrix"], np.float64)
    expected = np.array([[0.0, -1.0, size - 1.0], [1.0, 0.0, 0.0], [0.0, 0.0, 1.0]])
    assert np.allclose(matrix, expected, atol=0.05), matrix
)PY");
}

} // namespace
} // namespace an::tests::runtime_backend
