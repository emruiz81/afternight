// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The `afternight` package's surface an extension writes against, beside the native module: the sequence helpers, the
/// extension base classes and their documented defaults, the core and I/O facades over the runtime, and the refusals
/// the workflow helpers make before any native call. The package itself did not change when the backend moved; these
/// cases hold that it still behaves as documented over the new backend.

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

TEST(PackageSurfaceTest, SequencesHoldTheirFramesAndReadPixelsThroughTheRuntime)
{
    expectPython(R"PY(
import os, tempfile
import numpy as np
from afternight import core, io, sequences
with tempfile.TemporaryDirectory() as root:
    path = os.path.join(root, "frame.fits")
    io.save(core.from_numpy(np.full((3, 4), 0.5, np.float32), {"FILTER": "R"}), path)
    sequence = sequences.create("surface", frames=[path])
    assert sequence.name == "surface" and len(sequence) == 1
    held = sequence.add_frame(image=core.from_numpy(np.zeros((3, 4), np.float32)), metadata={"IMAGETYP": "LIGHT"},
                              registration={"dx": 1.0})
    assert held.index == 1
    record = sequence.add_frame({"path": path, "metadata": {"FILTER": "R"}, "included": False})
    assert [frame.index for frame in sequence.frames()] == [0, 1, 2]
    assert [frame.index for frame in sequence.included_frames()] == [0, 1]
    sequence.include_frame(2)
    sequence.exclude_frame(0)
    sequence.set_frame_included(1, False)
    assert [frame.index for frame in sequence.included_frames()] == [2]
    assert sequence.get_frame(2) is record
    metadata = sequence.get_frame_metadata(1)
    metadata["IMAGETYP"] = "changed"
    assert sequence.get_frame_metadata(1) == {"IMAGETYP": "LIGHT"}, "the metadata is a copy"
    assert sequence.get_frame_registration(1) == {"dx": 1.0}
    assert sequence.get_frame_pixels(1) is held.image
    loaded = sequence.get_frame_pixels(0)
    assert float(loaded.to_numpy()[1, 1]) == 0.5
    sequence.add_frame(path=os.path.join(root, "missing.fits"))
    try:
        sequence.get_frame_pixels(3)
    except FileNotFoundError:
        pass
    else:
        raise AssertionError("a frame whose file is missing has no pixels")
    listed = sequences.from_paths([path, path], name="listed", load_metadata=True)
    assert listed.get_frame_metadata(1) == {"path": path}
    assert sequence in sequences.list_sequences() and listed in sequences.list_sequences()
    try:
        sequence.get_frame(9)
    except IndexError:
        pass
    else:
        raise AssertionError("a frame past the sequence is refused")
)PY");
}

TEST(PackageSurfaceTest, TheExtensionBasesAnswerTheirDocumentedDefaults)
{
    expectPython(R"PY(
import afternight
from afternight import runtime
for base in (runtime.ProcessExtension, runtime.RTPreviewExtension, runtime.WorkflowExtension):
    extension = base(afternight.Settings())
    assert extension.get_settings_params() == []
    assert extension.get_params() == []
    answer = extension.handle_settings_action("reset", {})
    assert answer["ok"] is False and "reset" in answer["message"] and answer["tone"] == "warning"
    assert extension.validate_process({}, {}) == []
    for hook, arguments in (("on_process_launch", ()), ("on_process_close", ("closed",)),
                            ("on_parameters_changed", ({}, [], "user")), ("on_input_changed", ({},)),
                            ("on_execution_cancelled", ()), ("on_preset_loaded", ({}, {})),
                            ("on_preset_saved", ({}, {}))):
        assert getattr(extension, hook)(*arguments) is None, hook
process = runtime.ProcessExtension(None)
for call in (lambda: process.execute(None, None, None, {}, None),
             lambda: runtime.RTPreviewExtension(None).execute_preview(None, None, None, {}, None),
             lambda: runtime.WorkflowExtension(None).execute_workflow(None, {}, None)):
    try:
        call()
    except NotImplementedError:
        pass
    else:
        raise AssertionError("an unimplemented execution is refused")
)PY");
}

TEST(PackageSurfaceTest, TheCoreAndIoFacadesReachTheRuntime)
{
    expectPython(R"PY(
import os, tempfile
import numpy as np
from afternight import core, io
assert "float32" in core.available_pixel_types()
image = core.from_numpy(np.linspace(0.0, 1.0, 12, dtype=np.float32).reshape(3, 4), {"OBJECT": "M1"})
assert image.metadata["OBJECT"] == "M1"
wide = core.convert_pixel_type(image, "uint16")
assert wide.to_numpy().dtype == np.uint16 and int(wide.to_numpy()[2, 3]) == 65535
colour = core.compose_rgb(image, image, image)
assert colour.to_numpy().shape == (3, 4, 3)
assert len(core.extract_channels(colour)) == 3
assert float(core.extract_channel(colour, 2).to_numpy()[2, 3]) == 1.0
target = core.from_numpy(np.zeros((3, 4), np.float32))
core.apply_linear_transform(image, target, scale=0.5, offset=0.25)
assert abs(float(target.to_numpy()[2, 3]) - 0.75) < 1e-6
assert io.is_supported_path("frame.xisf") and not io.is_supported_path("frame.txt")
with tempfile.TemporaryDirectory() as root:
    path = os.path.join(root, "facade.xisf")
    io.save(image, path)
    assert io.load(path).metadata["OBJECT"] == "M1"
    for refused in (lambda: io.load(os.path.join(root, "missing.fits")),
                    lambda: core.convert_pixel_type(image, "int64"),
                    lambda: core.extract_channel(colour, 3)):
        try:
            refused()
        except RuntimeError:
            pass
        else:
            raise AssertionError("the runtime's refusal reaches the caller as RuntimeError")
)PY");
}

TEST(PackageSurfaceTest, AnImageHandleStatesWhatItsHolderSaysAndGuessesNothing)
{
    expectPython(R"PY(
import numpy as np
from afternight import core
from afternight.protocol import ImageHandle
colour = core.from_numpy(np.zeros((3, 4, 3), np.uint16))
assert isinstance(colour, ImageHandle)
properties = colour.properties
assert properties["backend"] == "runtime", properties
assert (properties["width"], properties["height"], properties["channels"]) == (4, 3, 3), properties
assert properties["pixel_type"] == "uint16" and properties["pixel_format"] == "rgb", properties
assert core.from_numpy(np.zeros((3, 4), np.float32)).properties["pixel_format"] == "gray"
# A handle that neither the host nor the runtime holds has no samples and no description to state.
for read in (lambda handle: handle.properties, lambda handle: handle.to_numpy()):
    try:
        read(ImageHandle(metadata={"OBJECT": "M1"}))
    except NotImplementedError:
        pass
    else:
        raise AssertionError("a handle nothing holds states no image")
)PY");
}

TEST(PackageSurfaceTest, TheWorkflowHelpersRefuseWhatTheyCannotRunBeforeAnyNativeCall)
{
    expectPython(R"PY(
import os, tempfile
import numpy as np
from afternight import calibration, core, registration, sequences, stacking
with tempfile.TemporaryDirectory() as root:
    empty = sequences.create("empty")
    assert calibration.create_masters(empty, os.path.join(root, "masters")) == {}
    assert len(calibration.calibrate_lights(empty, {}, os.path.join(root, "calibrated"))) == 0
    for refused, kind in ((lambda: stacking.stack(empty, os.path.join(root, "s.fits")), ValueError),
                          (lambda: registration.register(empty, os.path.join(root, "r")), ValueError),
                          (lambda: registration.apply(empty, os.path.join(root, "a")), ValueError)):
        try:
            refused()
        except kind:
            pass
        else:
            raise AssertionError("an empty sequence is refused")
    frames = sequences.create("frames")
    for index in range(2):
        frames.add_frame(image=core.from_numpy(np.full((8, 8), 0.1 * (index + 1), np.float32)))
    try:
        stacking.drizzle(frames, os.path.join(root, "d.fits"), reference_index=5)
    except ValueError:
        pass
    else:
        raise AssertionError("a reference outside the sequence is refused")
    try:
        stacking.drizzle(frames, os.path.join(root, "d.fits"))
    except ValueError as error:
        assert "registration" in str(error)
    else:
        raise AssertionError("a drizzle of frames without registrations is refused")
    result = stacking.average(frames, os.path.join(root, "average.fits"))
    assert result["frame_count"] == 2 and result["method"] == "average"
)PY");
}

} // namespace
} // namespace an::tests::runtime_backend
