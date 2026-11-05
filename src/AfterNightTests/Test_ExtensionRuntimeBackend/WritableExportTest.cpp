// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The writable NumPy export protocol, as the SDK relies on it: `to_numpy()` lends the image's own samples, writable
/// and never copied; an array and every slice of it keep the samples alive after the image is gone; NumPy's writes
/// reach the next engine call, which moves the pixel generation before it reads, as releasing the last array does;
/// replacing an image's samples gives it a new asset while old arrays keep the old ones; and calls from several Python
/// threads on one exported image are serialized.
///
/// Each case runs Python in the suite's shared runtime and states its expectations as assertions there, so a failure
/// reports the Python line that did not hold.

#include "RuntimeBackendTestSupport.h"

#include <gtest/gtest.h>

#include <string>

namespace an::tests::runtime_backend
{
namespace
{

/// Runs a case's Python, failing the case with the exception's text when an assertion in it does not hold.
void expectPython(const char* source)
{
    const std::string failure = requireSharedRuntime();
    ASSERT_TRUE(failure.empty()) << failure;
    py::gil_scoped_acquire gil;
    py::dict locals;
    try
    {
        runPython(source, locals);
    }
    catch (const std::exception& error)
    {
        FAIL() << error.what();
    }
    // A function the source defines holds the namespace as its globals, a cycle only the collector frees; clearing
    // the namespace lets every array and the samples it lends go before the case ends.
    locals.clear();
}

TEST(WritableExportTest, ToNumpyLendsTheImagesOwnSamplesWritableAndUncopied)
{
    expectPython(R"PY(
import numpy as np
import _afternight_runtime as rt
image = rt.image_from_numpy(np.zeros((4, 5, 3), np.float32))
first = image.to_numpy()
second = image.to_numpy()
assert first.flags.writeable and second.flags.writeable
assert np.shares_memory(first, second)
assert first.shape == (4, 5, 3) and first.strides == (20, 4, 80), first.strides
assert type(first.base).__name__ == "PyCapsule", type(first.base)
first[1, 2, 0] = 0.75
assert second[1, 2, 0] == np.float32(0.75)
mono = rt.image_from_numpy(np.zeros((3, 7), np.uint16)).to_numpy()
assert mono.flags.c_contiguous and mono.strides == (14, 2), mono.strides
)PY");
}

TEST(WritableExportTest, AnArrayAndItsSlicesKeepTheSamplesAfterTheImageIsGone)
{
    expectPython(R"PY(
import gc
import numpy as np
import _afternight_runtime as rt
image = rt.image_from_numpy(np.arange(30, dtype=np.float64).reshape(5, 6))
state = image._export_state()
array = image.to_numpy()
assert image._export_state()["writable_exports"] == state["writable_exports"] + 1
part = array[1:4, ::2]
del image, array
gc.collect()
assert part.tolist() == [[6.0, 8.0, 10.0], [12.0, 14.0, 16.0], [18.0, 20.0, 22.0]], part.tolist()
part[0, 0] = -1.0
assert part[0, 0] == -1.0
)PY");
}

TEST(WritableExportTest, SequentialEditsReachEachNextEngineCall)
{
    expectPython(R"PY(
import os, tempfile
import numpy as np
import _afternight_runtime as rt
image = rt.image_from_numpy(np.zeros((4, 4), np.float32))
array = image.to_numpy()
array[:] = 0.25
assert float(rt.convert_pixel_type(image, "float64").to_numpy()[2, 2]) == 0.25
array[2, 2] = 0.5
assert float(rt.convert_pixel_type(image, "float64").to_numpy()[2, 2]) == 0.5
assert float(image.clone().to_numpy()[2, 2]) == 0.5
with tempfile.TemporaryDirectory() as root:
    path = os.path.join(root, "edited.fits")
    array[0, 0] = 0.125
    rt.save_image(image, path)
    read = rt.load_image(path).to_numpy()
    assert float(read[0, 0]) == 0.125 and float(read[2, 2]) == 0.5, read
)PY");
}

TEST(WritableExportTest, ThePixelGenerationMovesAtEachEngineCallWhileExportedAndOnTheLastRelease)
{
    expectPython(R"PY(
import gc
import numpy as np
import _afternight_runtime as rt
image = rt.image_from_numpy(np.zeros((3, 3), np.float32))
def generation():
    return image._export_state()["pixel_generation"]
start = generation()
assert not image._export_state()["externally_mutable"]
image.clone()
assert generation() == start, "an engine call on an unexported image moves nothing"
array = image.to_numpy()
assert image._export_state()["externally_mutable"]
exported = generation()
image.clone()
assert generation() == exported + 1, "an engine call on an exported image moves the generation first"
array[0, 0] = 1.0
image.clone()
assert generation() == exported + 2
del array
gc.collect()
released = generation()
assert released == exported + 3, "releasing the last array moves the generation"
assert not image._export_state()["externally_mutable"]
image.clone()
assert generation() == released
)PY");
}

TEST(WritableExportTest, ReplacingTheSamplesGivesANewAssetAndOldArraysKeepTheirStorage)
{
    expectPython(R"PY(
import numpy as np
import _afternight_runtime as rt
image = rt.image_from_numpy(np.full((2, 3), 7, np.uint16), {"OBJECT": "M42"})
old = image.to_numpy()
asset = image._export_state()["asset"]
image.from_numpy(np.zeros((4, 4, 3), np.float32))
assert image._export_state()["asset"] != asset
assert old.shape == (2, 3) and int(old[1, 2]) == 7
old[0, 0] = 9
assert image.to_numpy().shape == (4, 4, 3)
assert float(image.to_numpy().max()) == 0.0
assert image.metadata()["OBJECT"] == "M42", "replacing the samples keeps the metadata"
)PY");
}

TEST(WritableExportTest, ALinearTransformWritesAnExportedDestinationInPlace)
{
    expectPython(R"PY(
import numpy as np
import _afternight_runtime as rt
source = rt.image_from_numpy(np.full((2, 2), 0.25, np.float32))
destination = rt.image_from_numpy(np.zeros((2, 2), np.float32))
view = destination.to_numpy()
rt.apply_linear_transform(source, destination, 2.0, 0.125)
assert np.allclose(view, 0.625), view
rt.apply_linear_transform(destination, destination, 2.0, 0.0)
assert np.allclose(view, 1.25), view
)PY");
}

TEST(WritableExportTest, CallsFromSeveralPythonThreadsOnOneExportedImageAreSerialized)
{
    expectPython(R"PY(
import threading
import numpy as np
import _afternight_runtime as rt
image = rt.image_from_numpy(np.linspace(0.0, 1.0, 64 * 64, dtype=np.float32).reshape(64, 64))
array = image.to_numpy()
errors = []
results = []
def work(index):
    try:
        for _ in range(10):
            converted = rt.convert_pixel_type(image, "float64").to_numpy()
            results.append(float(converted[10, 10]))
            image.clone()
    except Exception as error:
        errors.append(repr(error))
threads = [threading.Thread(target=work, args=(index,)) for index in range(4)]
for thread in threads:
    thread.start()
for thread in threads:
    thread.join()
assert not errors, errors
assert len(results) == 40
assert all(abs(value - float(array[10, 10])) < 1e-6 for value in results)
)PY");
}

} // namespace
} // namespace an::tests::runtime_backend
