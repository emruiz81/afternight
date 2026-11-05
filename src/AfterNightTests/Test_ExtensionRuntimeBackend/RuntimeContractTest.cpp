// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The runtime SDK's whole contract, held to its records.
///
/// `runtime_contract.py` calls every native function and `_RuntimeImage` method with its defaults over small inputs it
/// builds, calls each once in a way it refuses, runs the path workflows over a few generated frames and records the
/// package's public signatures. Two records sit beside the other expectations: `runtime-bindings.json`, the recorded
/// reference contract; and `runtime-bindings-current.json`, what the runtime module answers. A fresh run is held to the
/// current record. The two records are held to `runtime-bindings-differences.json`, which names and classes every
/// place they differ.
///
/// With AFTERNIGHT_FIXTURE_CAPTURE naming a directory, a capture run writes the current record under its `sdk`
/// subdirectory instead, to be reviewed and checked in; AFTERNIGHT_FIXTURE_CAPTURE_COMMIT names the commit it was built
/// from.

#include "RuntimeBackendTestSupport.h"

#include <pybind11/eval.h>
#include <pybind11/stl.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace an::tests::runtime_backend
{
namespace
{

[[nodiscard]] std::filesystem::path suiteFile(const char* name)
{
    return repositoryFile(std::filesystem::path("src") / "AfterNightTests" / "Test_ExtensionRuntimeBackend" / name);
}

[[nodiscard]] std::filesystem::path expectation(const char* name)
{
    return repositoryFile(std::filesystem::path("src") / "AfterNightTests" / "fixtures" / "common" / "expectations" /
                          "sdk" / name);
}

[[nodiscard]] std::string report(const std::vector<std::string>& lines)
{
    std::string text;
    for (const std::string& line : lines)
    {
        text += "\n  " + line;
    }
    return text;
}

TEST(RuntimeContractTest, TheRuntimeBindingsKeepTheirRecordedContract)
{
    const std::string failure = requireSharedRuntime();
    ASSERT_TRUE(failure.empty()) << failure;
    const std::filesystem::path compareScript = suiteFile("contract_compare.py");
    const std::filesystem::path contractScript = suiteFile("runtime_contract.py");
    ASSERT_FALSE(compareScript.empty() || contractScript.empty()) << "the contract scripts were not found";
    const std::string storage = environmentText("AFTERNIGHT_TEST_STORAGE_ROOT");
    ASSERT_FALSE(storage.empty());
    const std::filesystem::path work = std::filesystem::path(storage) / "runtime-contract";
    std::error_code ignored;
    std::filesystem::remove_all(work, ignored);
    std::filesystem::create_directories(work);

    py::gil_scoped_acquire gil;
    py::dict scope;
    scope["__builtins__"] = py::module_::import("builtins");
    scope["work"] = work.string();
    py::eval_file(compareScript.string(), scope);
    py::eval_file(contractScript.string(), scope);
    const std::string produced = scope["contract_text"].cast<std::string>();

    const std::string captureDirectory = environmentText("AFTERNIGHT_FIXTURE_CAPTURE");
    if (!captureDirectory.empty())
    {
        const std::filesystem::path target = std::filesystem::path(captureDirectory) / "sdk";
        std::filesystem::create_directories(target);
        const std::string commit = environmentText("AFTERNIGHT_FIXTURE_CAPTURE_COMMIT");
        scope["capture_path"] = (target / "runtime-bindings-current.json").string();
        scope["capture_commit"] = commit.empty() ? std::string("-") : commit;
        py::exec(R"PY(
import datetime
import platform
record = {"provenance": {"producer": "AfterNightEngine through an_extension_runtime_backend",
                         "numpy": np.__version__, "python": platform.python_version(),
                         "platform": platform.system().lower() + "-" + platform.machine().lower(),
                         "captured": datetime.date.today().isoformat(), "commit": capture_commit},
          "contract": contract}
with open(capture_path, "w", encoding="utf-8", newline="\r\n") as stream:
    stream.write(json.dumps(record, indent=1, sort_keys=True) + "\n")
)PY",
                 scope);
        std::filesystem::remove_all(work, ignored);
        GTEST_SKIP() << "captured the runtime contract to " << (target / "runtime-bindings-current.json").string();
    }

    const std::filesystem::path recorded = expectation("runtime-bindings-current.json");
    ASSERT_FALSE(recorded.empty()) << "no current runtime contract is checked in; run a capture";
    scope["recorded_path"] = recorded.string();
    py::exec(R"PY(
with open(recorded_path, "r", encoding="utf-8") as stream:
    recorded_contract = json.load(stream)["contract"]
differences = compare(recorded_contract, json.loads(contract_text))
)PY",
             scope);
    const auto differences = scope["differences"].cast<std::vector<std::string>>();
    EXPECT_TRUE(differences.empty()) << differences.size()
                                     << " difference(s) from the recorded contract:" << report(differences);
    std::filesystem::remove_all(work, ignored);
}

TEST(RuntimeContractTest, EveryDifferenceFromTheReferenceRecordIsRegistered)
{
    const std::string failure = requireSharedRuntime();
    ASSERT_TRUE(failure.empty()) << failure;
    const std::filesystem::path compareScript = suiteFile("contract_compare.py");
    const std::filesystem::path reference = expectation("runtime-bindings.json");
    const std::filesystem::path current = expectation("runtime-bindings-current.json");
    const std::filesystem::path registered = expectation("runtime-bindings-differences.json");
    ASSERT_FALSE(compareScript.empty() || reference.empty() || current.empty() || registered.empty())
        << "a record or the register is missing";

    py::gil_scoped_acquire gil;
    py::dict scope;
    scope["__builtins__"] = py::module_::import("builtins");
    py::eval_file(compareScript.string(), scope);
    scope["reference_path"] = reference.string();
    scope["current_path"] = current.string();
    scope["register_path"] = registered.string();
    py::exec(R"PY(
import json
def _load(path):
    with open(path, "r", encoding="utf-8") as stream:
        return json.load(stream)
differences = compare(_load(reference_path)["contract"], _load(current_path)["contract"])
missing, stale, malformed = unexplained(differences, _load(register_path))
)PY",
             scope);
    const auto missing = scope["missing"].cast<std::vector<std::string>>();
    const auto stale = scope["stale"].cast<std::vector<std::string>>();
    const auto malformed = scope["malformed"].cast<std::vector<std::string>>();
    EXPECT_TRUE(missing.empty()) << missing.size() << " difference(s) no register entry names:" << report(missing);
    EXPECT_TRUE(stale.empty()) << stale.size() << " register entr(ies) naming no difference:" << report(stale);
    EXPECT_TRUE(malformed.empty()) << "register entries without a class or a reason:" << report(malformed);
}

} // namespace
} // namespace an::tests::runtime_backend
