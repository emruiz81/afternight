# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Failure-path tests for stretch evidence admission; no engines or capture corpus required."""

import copy
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import stretch_bench as bench


class StretchEvidenceTest(unittest.TestCase):
    def test_frozen_runtime_verification_survives_source_changes_but_not_runtime_changes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "runtime").mkdir()
            (root / "runtime" / "engine.dll").write_bytes(b"original engine")
            (root / "sources.zip").write_bytes(b"archived sources")
            bench.write_json(root / "frozen.json", {"schemaVersion": 1,
                "role": "frozen-pre-repair-runtime",
                "files": {"runtime/engine.dll": bench.digest(root / "runtime/engine.dll"),
                          "sources.zip": bench.digest(root / "sources.zip")}})
            with patch.object(bench, "source_identity", side_effect=AssertionError("must not consult current source")):
                bench.verify_frozen(root)
            (root / "runtime/engine.dll").write_bytes(b"changed engine")
            with self.assertRaisesRegex(ValueError, "frozen artifact"):
                bench.verify_frozen(root)

    def catalogue(self):
        ids = [f"stretch.function_{i}.f32" for i in range(10)]
        ids += [f"stretch.uniform_1mp.function_{i}.{profile}.mono_f32.ordinary"
                for i in (0, 1, 2, 3, 5, 6, 8, 9) for profile in ("audit", "interactive")]
        ids += [f"stretch.boundaries.function_{i}.{profile}.mono_f32.ordinary"
                for i in (2, 3) for profile in ("zero", "tiny", "large", "protected", "coincident", "symmetry_black", "symmetry_white", "max_float")]
        ids += [f"stretch.boundaries.function_{i}.{profile}.mono_f32.ordinary"
                for i in (0, 1) for profile in ["zero", "coincident"] + [
                    f"{limit}_{regime}" for regime in ("log", "near_log", "integral", "below_exponential", "exponential",
                                                       "above_exponential", "hyperbolic")
                    for limit in ("tiny", "protected", "large", "max_float", "symmetry_black", "symmetry_white")]]
        ids += [f"stretch.boundaries.function_8.{profile}.mono_f32.ordinary" for profile in bench.MTF_BOUNDARY_PROFILES]
        ids += [f"stretch.boundaries.function_9.{profile}.mono_f32.ordinary"
                for profile in bench.STATISTICAL_BOUNDARY_PROFILES]
        ids += [f"stretch.{dataset}.function_9.prepared.mono_f32.ordinary" for dataset in bench.CAPTURES]
        ids += [f"stretch.{dataset}.function_{i}.{profile}.mono_f32.ordinary"
                for dataset in bench.CAPTURES for i in range(10) for profile in ("audit", "interactive")
                if profile == "audit" or i != 7]
        return {"scenarios": [{"id": key, "group": "stretch", "operationVersion": 2,
                               "parameters": {"function": "0"}} for key in ids]}

    def test_catalogue_refuses_missing_duplicate_and_obsolete_rows(self):
        document = self.catalogue()
        self.assertEqual(len(bench.stretch_catalogue(document)), 195)
        for change in (lambda d: d["scenarios"].pop(),
                       lambda d: d["scenarios"].append(d["scenarios"][0]),
                       lambda d: d["scenarios"][0].update(operationVersion=1)):
            broken = copy.deepcopy(document)
            change(broken)
            with self.assertRaises(ValueError):
                bench.stretch_catalogue(broken)

    def test_identity_refuses_changed_sources_binaries_and_build_log(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "build.log").write_bytes(b"successful build")
            identity = {"schemaVersion": 1, "role": "same-checkout-pre-repair", "source": {"files": {"a": "old"}},
                        "binaries": {"runner": "old"}, "buildLogSha256": bench.digest(root / "build.log")}
            bench.write_json(root / "identity.json", identity)
            with patch.object(bench, "source_identity", return_value=identity["source"]), \
                    patch.object(bench, "binary_identity", return_value=identity["binaries"]):
                bench.verify_identity(root / "identity.json", root)
                identity["role"] = "same-checkout-repair"
                bench.write_json(root / "identity.json", identity)
                bench.verify_identity(root / "identity.json", root)
                with patch.object(bench, "source_identity", return_value={"files": {"a": "changed"}}):
                    with self.assertRaisesRegex(ValueError, "source"):
                        bench.verify_identity(root / "identity.json", root)
                with patch.object(bench, "binary_identity", return_value={"runner": "changed"}):
                    with self.assertRaisesRegex(ValueError, "binaries"):
                        bench.verify_identity(root / "identity.json", root)
                (root / "build.log").write_bytes(b"different build")
                with self.assertRaisesRegex(ValueError, "log"):
                    bench.verify_identity(root / "identity.json", root)

    def test_both_frozen_roles_still_require_every_archived_byte(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            payload = root / "runtime.bin"
            payload.write_bytes(b"original")
            for role in ("frozen-pre-repair-runtime", "frozen-repair-runtime"):
                payload.write_bytes(b"original")
                bench.write_json(root / "frozen.json", {"schemaVersion": 1, "role": role,
                                 "files": {"runtime.bin": bench.digest(payload)}})
                bench.verify_frozen(root)
                payload.write_bytes(b"changed")
                with self.assertRaisesRegex(ValueError, "changed"):
                    bench.verify_frozen(root)


    def test_prepared_manifest_change_is_not_a_new_measurement(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            bench.write_json(root / "scenarios.json", {"scenarios": []})
            bench.write_json(root / "corpus.json", {"datasets": []})
            bench.write_json(root / "inputs.json", {"scenariosSha256": bench.digest(root / "scenarios.json"),
                             "corpusManifestSha256": bench.digest(root / "corpus.json")})
            bench.write_json(root / "scenarios.json", {"scenarios": [{"parameters": {"amount": "5"}}]})
            with self.assertRaisesRegex(ValueError, "manifest changed"):
                bench.verify_prepared(root, [])


LDD = """\tlinux-vdso.so.1 (0x00007ffd5a1e2000)
\tlibAfterNightPlatform.so => /frozen/runtime/libAfterNightPlatform.so (0x00007f1a2c000000)
\tlibtbb.so.12 => /lib/x86_64-linux-gnu/libtbb.so.12 (0x00007f1a2bf00000)
\tlibstdc++.so.6 => /usr/lib/x86_64-linux-gnu/libstdc++.so.6 (0x00007f1a2bc00000)
\t/lib64/ld-linux-x86-64.so.2 (0x00007f1a2c800000)
"""


class HostLaunchTest(unittest.TestCase):
    def test_each_host_names_its_runners_its_machine_and_its_engine_build(self):
        self.assertEqual(bench.executable_name("AfterNightEngineBench", "nt"), "AfterNightEngineBench.exe")
        self.assertEqual(bench.executable_name("AfterNightEngineBench", "posix"), "AfterNightEngineBench")
        self.assertEqual((bench.host_id("nt"), bench.host_id("posix")), ("bench-windows", "bench-linux"))
        # The Microsoft release engines are linked with link-time optimization and the Linux ones are not.
        self.assertEqual((bench.engine_lto_flag("nt"), bench.engine_lto_flag("posix")),
                         ("--engine-lto", "--engine-no-lto"))

    def test_a_linux_runner_loads_one_runtime_and_nothing_it_inherited(self):
        inherited = {"LD_LIBRARY_PATH": "/home/me/afternight/bin/clang/release", "TMPDIR": "/scratch"}
        environment = bench.launch_environment(Path("/frozen/runtime"), "posix", inherited)
        self.assertEqual(environment["LD_LIBRARY_PATH"], str(Path("/frozen/runtime")))
        self.assertEqual(environment["TMPDIR"], "/scratch")
        self.assertEqual(inherited["LD_LIBRARY_PATH"], "/home/me/afternight/bin/clang/release")
        windows = bench.launch_environment(Path("C:/frozen/runtime"), "nt", {"SystemRoot": "C:\\Windows", "PATH": "x"})
        self.assertTrue(windows["PATH"].endswith("System32"))
        self.assertNotIn("LD_LIBRARY_PATH", windows)

    def test_a_catalogue_dump_states_this_host(self):
        # A Linux runner otherwise takes its host from HOSTNAME, which a shell does not export, and refuses the
        # manifest it writes as coming from an unknown host.
        command = bench.catalogue_dump_command(Path("/frozen/runtime/AfterNightEngineBench"), Path("out/dry.json"),
                                               Path("out/catalogue.json"), "head", "engine", "lock")
        self.assertEqual(command[:2], [str(Path("/frozen/runtime/AfterNightEngineBench")), "--dry-run"])
        self.assertEqual(command[command.index("--host-id") + 1], bench.host_id())
        for flag, value in (("--out", str(Path("out/dry.json"))), ("--dump-scenarios", str(Path("out/catalogue.json"))),
                            ("--commit", "head"), ("--engine-commit", "engine"), ("--dependency-lock-digest", "lock")):
            self.assertEqual(command[command.index(flag) + 1], value)

    def test_a_library_from_anywhere_but_the_runtime_or_the_system_is_refused(self):
        self.assertEqual(bench.unresolved_libraries(LDD, Path("/frozen/runtime")), [])
        stale = LDD.replace("/frozen/runtime/libAfterNightPlatform.so",
                            "/home/me/afternight/bin/clang/release/libAfterNightPlatform.so")
        self.assertEqual(bench.unresolved_libraries(stale, Path("/frozen/runtime")),
                         ["libAfterNightPlatform.so => /home/me/afternight/bin/clang/release/libAfterNightPlatform.so"])
        missing = LDD + "\tlibcgmic.so => not found\n"
        self.assertEqual(bench.unresolved_libraries(missing, Path("/frozen/runtime")), ["libcgmic.so => not found"])
        # A directory beside it whose name only starts with the runtime's is not the runtime.
        beside = LDD.replace("/frozen/runtime/", "/frozen/runtime-old/")
        self.assertEqual(len(bench.unresolved_libraries(beside, Path("/frozen/runtime"))), 1)
        local = LDD.replace("/lib/x86_64-linux-gnu/libtbb.so.12", "/usr/local/lib/libtbb.so.12")
        self.assertEqual(len(bench.unresolved_libraries(local, Path("/frozen/runtime"))), 1)


class FrozenRunnerTest(unittest.TestCase):
    def test_a_frozen_runtime_names_its_runner(self):
        self.assertEqual(bench.frozen_runner(Path("/frozen/runtime"), "nt"),
                         Path("/frozen/runtime") / (bench.RUNNER + ".exe"))
        self.assertEqual(bench.frozen_runner(Path("/frozen/runtime"), "posix"),
                         Path("/frozen/runtime") / bench.RUNNER)

    def test_a_freeze_copies_the_dependency_lock_its_source_identity_names(self):
        self.assertIn(bench.DEPENDENCY_LOCK, bench.SOURCE_ROOTS)
        self.assertTrue((bench.ROOT / bench.DEPENDENCY_LOCK).is_file())


if __name__ == "__main__":
    unittest.main()
