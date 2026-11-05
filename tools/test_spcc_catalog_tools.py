#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

from __future__ import annotations

import csv
import gzip
import json
import subprocess
import sys
import tempfile
import unittest
import urllib.error
import zipfile
from pathlib import Path

from tools import export_spcc_gaia_xp_metadata as spcc_metadata_export
from tools import fetch_spcc_gaia_validation_fixture as spcc_fetch
from tools import spcc_tool_common as spcc


REPO_ROOT = Path(__file__).resolve().parents[1]


class SpccCatalogToolTests(unittest.TestCase):
    def run_tool(self, *args: str, check: bool = True) -> subprocess.CompletedProcess[str]:
        result = subprocess.run(
            [sys.executable, *args],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
        )
        if check and result.returncode != 0:
            self.fail(
                f"tool failed with exit code {result.returncode}: {' '.join(args)}\n"
                f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
            )
        return result

    def test_fixture_generator_writes_valid_catalog_and_validator_passes(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            catalog_dir = temp_dir / "spcc_fixture"

            self.run_tool(
                "tools/generate_spcc_catalog.py",
                str(catalog_dir),
                "--fixture",
                "--fixture-source-count",
                "6",
                "--manifest-id",
                "spcc-fixture-manifest-2026a",
                "--access-profile",
                "local-full",
                "--rights-review-status",
                "private-local",
            )

            self.run_tool("tools/validate_spcc_catalog.py", str(catalog_dir), "--sample-count", "3")

            meta = json.loads((catalog_dir / "catalog_meta.json").read_text(encoding="utf-8"))
            self.assertEqual(meta["catalog_family_id"], "spcc_gaia_dr3_xp_sampled")
            self.assertEqual(meta["manifest_id"], "spcc-fixture-manifest-2026a")
            self.assertEqual(meta["format"]["magic"], "ANXP")
            self.assertEqual(meta["format"]["record_size_bytes"], 1536)
            self.assertEqual(meta["wavelength_grid"]["sample_count"], 343)
            self.assertEqual(meta["uncertainty_payload"]["flux_error_samples"], "present")
            self.assertEqual(meta["rights"]["review_status"], "private-local")
            self.assertEqual(meta["rights"]["mirror_hosting_policy"], "staging-only")
            self.assertIn("reviewed_at_utc", meta["rights"])
            self.assertEqual(meta["provenance"]["output_manifest_id"], "spcc-fixture-manifest-2026a")
            self.assertIn("source_query_timestamp_utc", meta["provenance"])
            self.assertEqual(meta["total_records"], 6)
            self.assertEqual(len(meta["chunks"]), 1)
            self.assertEqual(len(meta["chunks"][0]["sha256"]), 64)

            coverage = json.loads((catalog_dir / "coverage_summary.json").read_text(encoding="utf-8"))
            self.assertEqual(coverage["catalog_family_id"], meta["catalog_family_id"])
            self.assertEqual(coverage["manifest_id"], meta["manifest_id"])
            self.assertEqual(coverage["total_records"], 6)
            self.assertGreaterEqual(coverage["estimated_usable_candidates"], 6)

    def test_generator_stamps_regional_request_coverage_and_sidecar(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            catalog_dir = temp_dir / "spcc_regional"
            input_jsonl = temp_dir / "regional_sources.jsonl"
            transcript_path = temp_dir / "gaia_validation_transcript.json"
            request_path = temp_dir / "spcc_regional_request.json"
            self._write_sample_spcc_jsonl(input_jsonl)
            transcript_path.write_text(
                json.dumps(
                    {
                        "generated_at_utc": "2026-05-12T00:00:00Z",
                        "tap": {
                            "service_url": "https://gea.esac.esa.int/tap-server/tap",
                            "adql": "SELECT source_id FROM gaiadr3.gaia_source",
                        },
                        "spectrum_retrieval": {"files": []},
                    }
                ),
                encoding="utf-8",
            )
            request_path.write_text(
                json.dumps(
                    {
                        "schema_version": "spcc-regional-cache-request-v1",
                        "catalog_family_id": spcc.CATALOG_FAMILY_ID,
                        "access_profile": "installed-regional-cache",
                        "target_name": "M31 Core",
                        "created_at_utc": "2026-05-12T00:00:00Z",
                        "region": {
                            "center_ra_deg": 10.6847083,
                            "center_dec_deg": 41.26875,
                            "radius_deg": 1.5,
                        },
                        "selection": {"magnitude_limit": 16.0},
                        "image": {"width": 6248, "height": 4176},
                    }
                ),
                encoding="utf-8",
            )

            self.run_tool(
                "tools/generate_spcc_catalog.py",
                str(catalog_dir),
                "--input-jsonl",
                str(input_jsonl),
                "--source-transcript",
                str(transcript_path),
                "--regional-request",
                str(request_path),
                "--manifest-id",
                "spcc-gaia-dr3-xp-sampled-m31-region-20260512",
                "--preset",
                "sampled-region",
                "--rights-review-status",
                "approved-for-managed-download",
            )
            self.run_tool("tools/validate_spcc_catalog.py", str(catalog_dir), "--sample-count", "2")

            meta = json.loads((catalog_dir / "catalog_meta.json").read_text(encoding="utf-8"))
            coverage = json.loads((catalog_dir / "coverage_summary.json").read_text(encoding="utf-8"))
            request_sidecar = catalog_dir / "regional_cache_request.json"
            request_sha = spcc.sha256_file(request_sidecar)

            self.assertEqual(meta["access_profile"], "installed-regional-cache")
            self.assertEqual(meta["coverage"]["center_ra_deg"], 10.6847083)
            self.assertEqual(meta["coverage"]["center_dec_deg"], 41.26875)
            self.assertEqual(meta["coverage"]["radius_deg"], 1.5)
            self.assertEqual(meta["coverage"]["magnitude_limit"], 16.0)
            self.assertEqual(meta["files"]["regional_cache_request"], "regional_cache_request.json")
            self.assertEqual(meta["regional_cache_request_sha256"], request_sha)
            self.assertEqual(meta["provenance"]["regional_cache_request_sha256"], request_sha)
            self.assertEqual(coverage["coverage"], meta["coverage"])
            self.assertEqual(coverage["regional_cache_request"]["target_name"], "M31 Core")
            self.assertEqual(coverage["regional_cache_request"]["sha256"], request_sha)

            dataset_dir = temp_dir / "hf_dataset"
            self.run_tool("tools/build_spcc_hf_dataset.py", str(dataset_dir), str(catalog_dir), "--clean")
            dataset_manifest = json.loads((dataset_dir / "spcc_dataset_manifest.json").read_text(encoding="utf-8"))
            mirror_manifest = json.loads((dataset_dir / "spcc_mirror_manifest.json").read_text(encoding="utf-8"))
            app_config = json.loads((dataset_dir / "spcc_catalog_mirrors.json").read_text(encoding="utf-8"))
            self.assertEqual(dataset_manifest["catalogs"][0]["coverage"], meta["coverage"])
            self.assertEqual(mirror_manifest["catalogs"][0]["coverage"], meta["coverage"])
            self.assertEqual(app_config["catalog_packs"][0]["coverage"], meta["coverage"])

    def test_generator_resume_state_survives_interruption(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            catalog_dir = temp_dir / "spcc_fixture"

            interrupted = self.run_tool(
                "tools/generate_spcc_catalog.py",
                str(catalog_dir),
                "--fixture",
                "--fixture-source-count",
                "4",
                "--manifest-id",
                "spcc-fixture-resume-2026a",
                "--simulate-interruption-after",
                "staged-sources",
                check=False,
            )

            self.assertEqual(interrupted.returncode, 2)
            self.assertTrue((catalog_dir / ".spcc_resume_state.json").exists())

            self.run_tool("tools/generate_spcc_catalog.py", str(catalog_dir), "--fixture", "--resume")
            self.assertFalse((catalog_dir / ".spcc_resume_state.json").exists())
            self.run_tool("tools/validate_spcc_catalog.py", str(catalog_dir))

    def test_full_xp_sampled_builder_chunks_caps_and_validates(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            input_jsonl = temp_dir / "full_export_fixture.jsonl"
            output_dir = temp_dir / "full_sampled_catalog"
            self._write_full_xp_sampled_export_fixture(input_jsonl)

            build_result = self.run_tool(
                "tools/build_spcc_full_xp_sampled_catalog.py",
                str(output_dir),
                "--input-jsonl",
                str(input_jsonl),
                "--manifest-id",
                "spcc-gaia-dr3-xp-sampled-full-fixture-2026a",
                "--cap-per-healpix8",
                "2",
                "--rights-review-status",
                "private-local",
                "--allow-partial-full-sky",
            )
            self.assertIn("SPCC full XP sampled catalog build", build_result.stdout)
            self.assertIn("input_jsonl: 1 partitioned=0", build_result.stdout)
            self.assertIn("[partition 1/1] starting full_export_fixture.jsonl", build_result.stdout)
            self.assertIn("[chunk] discovered", build_result.stdout)
            self.assertIn("[chunk 1/", build_result.stdout)
            self.assertIn("eta=", build_result.stdout)
            self.run_tool("tools/validate_spcc_catalog.py", str(output_dir), "--sample-count", "8")

            meta = json.loads((output_dir / "catalog_meta.json").read_text(encoding="utf-8"))
            self.assertEqual(meta["access_profile"], "local-full")
            self.assertEqual(meta["total_records"], 4)
            self.assertEqual(len(meta["chunks"]), 2)
            self.assertEqual({chunk["chunk_index"] for chunk in meta["chunks"]}, {0, 1})

            records_by_source: dict[int, dict[str, object]] = {}
            for chunk in meta["chunks"]:
                _, records = spcc.read_chunk(output_dir / chunk["file"])
                records_by_source.update({int(record["source_id"]): record for record in records})

            rejected_dim_source = self._source_id_for_healpix8(0, 1)
            self.assertNotIn(rejected_dim_source, records_by_source)
            self.assertIn(self._source_id_for_healpix8(0, 2), records_by_source)
            self.assertIn(self._source_id_for_healpix8(1024, 1), records_by_source)

            coverage = json.loads((output_dir / "coverage_summary.json").read_text(encoding="utf-8"))
            self.assertEqual(coverage["total_records"], 4)
            self.assertEqual(coverage["chunk_status"][0]["chunk_index"], 0)

    def test_full_xp_sampled_builder_rejects_partial_local_full_without_override(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            input_jsonl = temp_dir / "full_export_fixture.jsonl"
            output_dir = temp_dir / "full_sampled_catalog"
            self._write_full_xp_sampled_export_fixture(input_jsonl)

            result = self.run_tool(
                "tools/build_spcc_full_xp_sampled_catalog.py",
                str(output_dir),
                "--input-jsonl",
                str(input_jsonl),
                "--manifest-id",
                "spcc-gaia-dr3-xp-sampled-full-partial-rejected-2026a",
                "--cap-per-healpix8",
                "2",
                check=False,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("local-full SPCC catalog declares 2/768 parent chunks", result.stderr)
            self.assertIn("--allow-partial-full-sky only for test fixtures", result.stderr)
            self.assertFalse((output_dir / "catalog_meta.json").exists())

    def test_catalog_validator_rejects_approved_partial_local_full(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            input_jsonl = temp_dir / "full_export_fixture.jsonl"
            output_dir = temp_dir / "full_sampled_catalog"
            self._write_full_xp_sampled_export_fixture(input_jsonl)

            self.run_tool(
                "tools/build_spcc_full_xp_sampled_catalog.py",
                str(output_dir),
                "--input-jsonl",
                str(input_jsonl),
                "--manifest-id",
                "spcc-gaia-dr3-xp-sampled-full-approved-partial-2026a",
                "--cap-per-healpix8",
                "2",
                "--rights-review-status",
                "approved-for-managed-download",
                "--allow-partial-full-sky",
            )

            result = self.run_tool(
                "tools/validate_spcc_catalog.py",
                str(output_dir),
                "--sample-count",
                "8",
                check=False,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("local-full SPCC catalog declares 2/768 parent chunks", result.stderr)

    def test_catalog_rights_approval_rejects_partial_gaia_local_full(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            input_jsonl = temp_dir / "full_export_fixture.jsonl"
            output_dir = temp_dir / "full_sampled_catalog"
            self._write_full_xp_sampled_export_fixture(input_jsonl)

            self.run_tool(
                "tools/build_spcc_full_xp_sampled_catalog.py",
                str(output_dir),
                "--input-jsonl",
                str(input_jsonl),
                "--manifest-id",
                "spcc-gaia-dr3-xp-sampled-full-approval-partial-2026a",
                "--cap-per-healpix8",
                "2",
                "--allow-partial-full-sky",
            )

            result = self.run_tool(
                "tools/approve_spcc_catalog_rights.py",
                str(output_dir),
                "--reviewer",
                "Unit Test",
                check=False,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("not ready for managed download approval", result.stderr)
            self.assertIn("2/768 parent chunks", result.stderr)

    def test_hf_dataset_builder_rejects_approved_partial_gaia_local_full_without_validation(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            input_jsonl = temp_dir / "full_export_fixture.jsonl"
            output_dir = temp_dir / "full_sampled_catalog"
            dataset_dir = temp_dir / "hf_dataset"
            self._write_full_xp_sampled_export_fixture(input_jsonl)

            self.run_tool(
                "tools/build_spcc_full_xp_sampled_catalog.py",
                str(output_dir),
                "--input-jsonl",
                str(input_jsonl),
                "--manifest-id",
                "spcc-gaia-dr3-xp-sampled-full-staging-partial-2026a",
                "--cap-per-healpix8",
                "2",
                "--rights-review-status",
                "approved-for-managed-download",
                "--allow-partial-full-sky",
            )

            result = self.run_tool(
                "tools/build_spcc_hf_dataset.py",
                str(dataset_dir),
                str(output_dir),
                "--no-validate",
                "--clean",
                check=False,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("not ready for managed download", result.stderr)
            self.assertIn("2/768 parent chunks", result.stderr)

    def test_full_xp_sampled_builder_resumes_after_partition(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            input_jsonl = temp_dir / "full_export_fixture.jsonl"
            output_dir = temp_dir / "full_sampled_catalog"
            self._write_full_xp_sampled_export_fixture(input_jsonl)

            interrupted = self.run_tool(
                "tools/build_spcc_full_xp_sampled_catalog.py",
                str(output_dir),
                "--input-jsonl",
                str(input_jsonl),
                "--manifest-id",
                "spcc-gaia-dr3-xp-sampled-full-resume-2026a",
                "--cap-per-healpix8",
                "2",
                "--allow-partial-full-sky",
                "--simulate-interruption-after",
                "partition",
                check=False,
            )

            self.assertEqual(interrupted.returncode, 2)
            self.assertTrue((output_dir / ".spcc_full_export_state.json").exists())

            self.run_tool("tools/build_spcc_full_xp_sampled_catalog.py", str(output_dir), "--resume")
            self.assertFalse((output_dir / ".spcc_full_export_state.json").exists())
            self.run_tool("tools/validate_spcc_catalog.py", str(output_dir), "--sample-count", "8")

    def test_full_xp_sampled_source_exporter_joins_csv_gz_and_feeds_builder(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            metadata_csv = temp_dir / "gaia_source_xp_summary.csv"
            spectra_csv_gz = temp_dir / "xp_sampled_mean_spectrum.csv.gz"
            export_dir = temp_dir / "prejoined"
            catalog_dir = temp_dir / "catalog"
            self._write_full_xp_metadata_csv(metadata_csv)
            self._write_full_xp_spectra_csv_gz(spectra_csv_gz)

            export_result = self.run_tool(
                "tools/export_spcc_gaia_full_xp_sampled_sources.py",
                str(export_dir),
                "--metadata-csv",
                str(metadata_csv),
                "--xp-sampled-csv",
                str(spectra_csv_gz),
                "--partition-count",
                "4",
                "--part-prefix",
                "fixture_sources",
            )
            self.assertIn("SPCC full XP sampled source export", export_result.stdout)
            self.assertIn("metadata_inputs: 1 completed=0", export_result.stdout)
            self.assertIn("[metadata 1/1] starting gaia_source_xp_summary.csv", export_result.stdout)
            self.assertIn("[spectra 1/1] starting xp_sampled_mean_spectrum.csv.gz", export_result.stdout)
            self.assertIn("[join 1/4] starting partition", export_result.stdout)
            self.assertIn("joined_rows=", export_result.stdout)
            self.assertIn("eta=", export_result.stdout)

            export_manifest_path = export_dir / "spcc_full_xp_sampled_source_export_manifest.json"
            export_manifest = json.loads(export_manifest_path.read_text(encoding="utf-8"))
            self.assertEqual(export_manifest["joined_rows"], 5)
            self.assertEqual(export_manifest["metadata_rows"], 5)
            self.assertEqual(export_manifest["spectrum_rows"], 6)
            self.assertEqual(export_manifest["unmatched_spectrum_rows"], 1)
            self.assertGreaterEqual(len(export_manifest["part_files"]), 1)

            first_part = export_dir / export_manifest["part_files"][0]["path"]
            first_row = json.loads(first_part.read_text(encoding="utf-8").splitlines()[0])
            self.assertEqual(len(first_row["flux"]), spcc.SAMPLED_XP_SAMPLE_COUNT)
            self.assertIn("phot_g_mean_flux_over_error", first_row)
            self.assertIn("bp_n_relevant_bases", first_row)

            self.run_tool(
                "tools/build_spcc_full_xp_sampled_catalog.py",
                str(catalog_dir),
                "--input-manifest",
                str(export_manifest_path),
                "--manifest-id",
                "spcc-gaia-dr3-xp-sampled-full-exporter-fixture-2026a",
                "--cap-per-healpix8",
                "2",
                "--rights-review-status",
                "private-local",
                "--allow-partial-full-sky",
            )
            self.run_tool("tools/validate_spcc_catalog.py", str(catalog_dir), "--sample-count", "8")

            meta = json.loads((catalog_dir / "catalog_meta.json").read_text(encoding="utf-8"))
            self.assertEqual(meta["total_records"], 4)

    def test_full_xp_sampled_source_exporter_resumes_after_metadata_partition(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            metadata_csv = temp_dir / "gaia_source_xp_summary.csv"
            spectra_csv_gz = temp_dir / "xp_sampled_mean_spectrum.csv.gz"
            export_dir = temp_dir / "prejoined"
            self._write_full_xp_metadata_csv(metadata_csv)
            self._write_full_xp_spectra_csv_gz(spectra_csv_gz)

            interrupted = self.run_tool(
                "tools/export_spcc_gaia_full_xp_sampled_sources.py",
                str(export_dir),
                "--metadata-csv",
                str(metadata_csv),
                "--xp-sampled-csv",
                str(spectra_csv_gz),
                "--partition-count",
                "4",
                "--simulate-interruption-after",
                "metadata",
                check=False,
            )

            self.assertEqual(interrupted.returncode, 2)
            self.assertTrue((export_dir / ".spcc_full_xp_source_export_state.json").exists())

            self.run_tool("tools/export_spcc_gaia_full_xp_sampled_sources.py", str(export_dir), "--resume")
            self.assertFalse((export_dir / ".spcc_full_xp_source_export_state.json").exists())
            export_manifest = json.loads(
                (export_dir / "spcc_full_xp_sampled_source_export_manifest.json").read_text(encoding="utf-8")
            )
            self.assertEqual(export_manifest["joined_rows"], 5)

    def test_full_xp_sampled_source_exporter_parallel_workers(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            metadata_csv = temp_dir / "gaia_source_xp_summary.csv"
            spectra_csv_gz = temp_dir / "xp_sampled_mean_spectrum.csv.gz"
            export_dir = temp_dir / "prejoined"
            self._write_full_xp_metadata_csv(metadata_csv)
            self._write_full_xp_spectra_csv_gz(spectra_csv_gz)

            result = self.run_tool(
                "tools/export_spcc_gaia_full_xp_sampled_sources.py",
                str(export_dir),
                "--metadata-csv",
                str(metadata_csv),
                "--xp-sampled-csv",
                str(spectra_csv_gz),
                "--partition-count",
                "4",
                "--workers",
                "2",
            )

            self.assertIn("SPCC full XP sampled source export", result.stdout)
            export_manifest = json.loads(
                (export_dir / "spcc_full_xp_sampled_source_export_manifest.json").read_text(encoding="utf-8")
            )
            self.assertEqual(export_manifest["joined_rows"], 5)
            self.assertEqual(export_manifest["metadata_rows"], 5)
            self.assertEqual(export_manifest["spectrum_rows"], 6)

    def test_gaia_xp_metadata_exporter_splits_csv_and_feeds_prejoin(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            metadata_csv = temp_dir / "gaia_source_xp_summary.csv"
            spectra_csv_gz = temp_dir / "xp_sampled_mean_spectrum.csv.gz"
            metadata_dir = temp_dir / "metadata_export"
            prejoin_dir = temp_dir / "prejoined"
            self._write_full_xp_metadata_csv(metadata_csv)
            self._write_full_xp_spectra_csv_gz(spectra_csv_gz)

            metadata_result = self.run_tool(
                "tools/export_spcc_gaia_xp_metadata.py",
                str(metadata_dir),
                "--source-csv",
                str(metadata_csv),
                "--healpix8-start",
                "0",
                "--healpix8-end",
                "2048",
                "--healpix8-step",
                "1024",
                "--output-prefix",
                "fixture_metadata",
            )
            self.assertIn("SPCC Gaia XP metadata export", metadata_result.stdout)
            self.assertIn("ranges=2 completed=0 remaining=2", metadata_result.stdout)
            self.assertIn("[1/2] starting hp8 000000-001023", metadata_result.stdout)
            self.assertIn("rows=3", metadata_result.stdout)
            self.assertIn("eta=", metadata_result.stdout)

            metadata_manifest_path = metadata_dir / "spcc_gaia_xp_metadata_export_manifest.json"
            metadata_manifest = json.loads(metadata_manifest_path.read_text(encoding="utf-8"))
            self.assertEqual(metadata_manifest["total_rows"], 5)
            self.assertEqual(len(metadata_manifest["part_files"]), 2)
            self.assertEqual([entry["row_count"] for entry in metadata_manifest["part_files"]], [3, 2])
            first_metadata_part = metadata_dir / metadata_manifest["part_files"][0]["path"]
            with gzip.open(first_metadata_part, "rt", encoding="utf-8", newline="") as handle:
                exported_rows = list(csv.DictReader(handle))
            blank_pm_source_id = str(self._source_id_for_healpix8(0, 1))
            blank_pm_export = next(row for row in exported_rows if row["source_id"] == blank_pm_source_id)
            self.assertEqual(blank_pm_export["pmra"], "0.0")
            self.assertEqual(blank_pm_export["pmdec"], "0.0")

            self.run_tool(
                "tools/export_spcc_gaia_full_xp_sampled_sources.py",
                str(prejoin_dir),
                "--metadata-manifest",
                str(metadata_manifest_path),
                "--xp-sampled-csv",
                str(spectra_csv_gz),
                "--partition-count",
                "4",
            )

            prejoin_manifest = json.loads(
                (prejoin_dir / "spcc_full_xp_sampled_source_export_manifest.json").read_text(encoding="utf-8")
            )
            self.assertEqual(prejoin_manifest["joined_rows"], 5)
            self.assertEqual(prejoin_manifest["metadata_rows"], 5)

    def test_gaia_xp_metadata_exporter_tap_query_filters_null_quality_fields(self) -> None:
        adql = spcc_metadata_export.build_tap_adql(0, 64)

        self.assertIn("s.ruwe IS NOT NULL", adql)
        self.assertIn("s.phot_g_mean_flux_over_error IS NOT NULL", adql)
        self.assertIn("x.bp_n_relevant_bases IS NOT NULL", adql)
        self.assertNotIn("s.pmra IS NOT NULL", adql)
        self.assertNotIn("s.pmdec IS NOT NULL", adql)

    def test_gaia_xp_metadata_exporter_retries_transient_tap_errors(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            part_path = temp_dir / "metadata_part.csv.gz"
            attempts = 0
            original_writer = spcc_metadata_export.write_metadata_part

            def flaky_writer(*args: object, **kwargs: object) -> int:
                nonlocal attempts
                del args, kwargs
                attempts += 1
                if attempts == 1:
                    part_path.write_text("partial", encoding="utf-8")
                    raise urllib.error.HTTPError("https://gea.esac.esa.int/tap-server/tap", 500, "server error", None, None)
                part_path.write_bytes(b"complete")
                return 7

            try:
                spcc_metadata_export.write_metadata_part = flaky_writer
                row_count = spcc_metadata_export.write_metadata_part_with_retries(
                    temp_dir,
                    {
                        "mode": "official-gaia-tap",
                        "tap_max_retries": 1,
                        "tap_retry_delay_seconds": 0.0,
                    },
                    243136,
                    243200,
                    part_path,
                    "SELECT 1",
                )
            finally:
                spcc_metadata_export.write_metadata_part = original_writer

            self.assertEqual(row_count, 7)
            self.assertEqual(attempts, 2)
            self.assertEqual(part_path.read_bytes(), b"complete")

    def test_gaia_xp_sampled_downloader_writes_manifest_and_feeds_prejoin(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            metadata_csv = temp_dir / "gaia_source_xp_summary.csv"
            metadata_dir = temp_dir / "metadata_export"
            xp_source_dir = temp_dir / "xp_source"
            xp_download_dir = temp_dir / "xp_download"
            prejoin_dir = temp_dir / "prejoined"
            xp_source_dir.mkdir()
            xp_source = xp_source_dir / "XpSampledMeanSpectrum_000000-003111.csv.gz"
            xp_index = temp_dir / "xp_index.json"
            self._write_full_xp_metadata_csv(metadata_csv)
            self._write_full_xp_spectra_csv_gz(xp_source)
            spcc.write_json(
                xp_index,
                {
                    "files": [
                        {
                            "key": "Gaia/gdr3/Spectroscopy/xp_sampled_mean_spectrum/" + xp_source.name,
                            "url": xp_source.resolve().as_uri(),
                            "healpix_start": 0,
                            "healpix_end": 3111,
                            "size_bytes": xp_source.stat().st_size,
                            "sha256": spcc.sha256_file(xp_source),
                            "last_modified": "2026-05-13T00:00:00Z",
                        }
                    ]
                },
            )

            self.run_tool(
                "tools/export_spcc_gaia_xp_metadata.py",
                str(metadata_dir),
                "--source-csv",
                str(metadata_csv),
                "--healpix8-start",
                "0",
                "--healpix8-end",
                "2048",
                "--healpix8-step",
                "1024",
            )

            download_result = self.run_tool(
                "tools/download_spcc_gaia_xp_sampled_files.py",
                str(xp_download_dir),
                "--index-json",
                str(xp_index),
            )
            self.assertIn("SPCC Gaia XP sampled download", download_result.stdout)
            self.assertIn("completed XpSampledMeanSpectrum_000000-003111.csv.gz", download_result.stdout)
            xp_manifest_path = xp_download_dir / "spcc_gaia_xp_sampled_download_manifest.json"
            xp_manifest = json.loads(xp_manifest_path.read_text(encoding="utf-8"))
            self.assertEqual(xp_manifest["file_count"], 1)
            self.assertEqual(xp_manifest["part_files"][0]["sha256"], spcc.sha256_file(xp_source))

            self.run_tool(
                "tools/export_spcc_gaia_full_xp_sampled_sources.py",
                str(prejoin_dir),
                "--metadata-manifest",
                str(metadata_dir / "spcc_gaia_xp_metadata_export_manifest.json"),
                "--xp-sampled-manifest",
                str(xp_manifest_path),
                "--partition-count",
                "4",
            )
            prejoin_manifest = json.loads(
                (prejoin_dir / "spcc_full_xp_sampled_source_export_manifest.json").read_text(encoding="utf-8")
            )
            self.assertEqual(prejoin_manifest["joined_rows"], 5)
            self.assertEqual(prejoin_manifest["spectrum_rows"], 6)
            self.assertEqual(len(prejoin_manifest["xp_sampled_manifests"]), 1)

    def test_gaia_xp_sampled_downloader_resumes_after_first_file(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            xp_source = temp_dir / "XpSampledMeanSpectrum_000000-003111.csv.gz"
            xp_download_dir = temp_dir / "xp_download"
            xp_index = temp_dir / "xp_index.json"
            self._write_full_xp_spectra_csv_gz(xp_source)
            spcc.write_json(
                xp_index,
                {
                    "files": [
                        {
                            "key": xp_source.name,
                            "url": xp_source.resolve().as_uri(),
                            "size_bytes": xp_source.stat().st_size,
                            "sha256": spcc.sha256_file(xp_source),
                        }
                    ]
                },
            )

            interrupted = self.run_tool(
                "tools/download_spcc_gaia_xp_sampled_files.py",
                str(xp_download_dir),
                "--index-json",
                str(xp_index),
                "--simulate-interruption-after",
                "first-file",
                check=False,
            )
            self.assertEqual(interrupted.returncode, 2)
            self.assertTrue((xp_download_dir / ".spcc_gaia_xp_sampled_download_state.json").exists())

            self.run_tool("tools/download_spcc_gaia_xp_sampled_files.py", str(xp_download_dir), "--resume")
            self.assertFalse((xp_download_dir / ".spcc_gaia_xp_sampled_download_state.json").exists())
            xp_manifest = json.loads(
                (xp_download_dir / "spcc_gaia_xp_sampled_download_manifest.json").read_text(encoding="utf-8")
            )
            self.assertEqual(xp_manifest["file_count"], 1)

    def test_gaia_xp_sampled_downloader_redownloads_invalid_completed_marker(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            xp_source = temp_dir / "XpSampledMeanSpectrum_000000-003111.csv.gz"
            xp_download_dir = temp_dir / "xp_download"
            xp_index = temp_dir / "xp_index.json"
            self._write_full_xp_spectra_csv_gz(xp_source)
            expected_sha = spcc.sha256_file(xp_source)
            spcc.write_json(
                xp_index,
                {
                    "files": [
                        {
                            "key": xp_source.name,
                            "url": xp_source.resolve().as_uri(),
                            "size_bytes": xp_source.stat().st_size,
                            "sha256": expected_sha,
                        }
                    ]
                },
            )

            interrupted = self.run_tool(
                "tools/download_spcc_gaia_xp_sampled_files.py",
                str(xp_download_dir),
                "--index-json",
                str(xp_index),
                "--simulate-interruption-after",
                "first-file",
                check=False,
            )
            self.assertEqual(interrupted.returncode, 2)
            downloaded_file = xp_download_dir / xp_source.name
            self.assertTrue(downloaded_file.exists())
            downloaded_file.unlink()

            resumed = self.run_tool("tools/download_spcc_gaia_xp_sampled_files.py", str(xp_download_dir), "--resume")
            self.assertIn(f"completed {xp_source.name}", resumed.stdout)
            self.assertEqual(spcc.sha256_file(downloaded_file), expected_sha)
            xp_manifest = json.loads(
                (xp_download_dir / "spcc_gaia_xp_sampled_download_manifest.json").read_text(encoding="utf-8")
            )
            self.assertEqual(xp_manifest["part_files"][0]["sha256"], expected_sha)

    def test_gaia_xp_metadata_exporter_resumes_after_first_range(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            metadata_csv = temp_dir / "gaia_source_xp_summary.csv"
            metadata_dir = temp_dir / "metadata_export"
            self._write_full_xp_metadata_csv(metadata_csv)

            interrupted = self.run_tool(
                "tools/export_spcc_gaia_xp_metadata.py",
                str(metadata_dir),
                "--source-csv",
                str(metadata_csv),
                "--healpix8-start",
                "0",
                "--healpix8-end",
                "2048",
                "--healpix8-step",
                "1024",
                "--simulate-interruption-after",
                "first-range",
                check=False,
            )
            self.assertEqual(interrupted.returncode, 2)
            self.assertTrue((metadata_dir / ".spcc_gaia_xp_metadata_export_state.json").exists())

            self.run_tool("tools/export_spcc_gaia_xp_metadata.py", str(metadata_dir), "--resume")
            self.assertFalse((metadata_dir / ".spcc_gaia_xp_metadata_export_state.json").exists())
            metadata_manifest = json.loads(
                (metadata_dir / "spcc_gaia_xp_metadata_export_manifest.json").read_text(encoding="utf-8")
            )
            self.assertEqual(metadata_manifest["total_rows"], 5)

    def test_catalog_validator_rejects_checksum_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            catalog_dir = temp_dir / "spcc_fixture"

            self.run_tool("tools/generate_spcc_catalog.py", str(catalog_dir), "--fixture", "--fixture-source-count", "3")

            meta = json.loads((catalog_dir / "catalog_meta.json").read_text(encoding="utf-8"))
            chunk_path = catalog_dir / meta["chunks"][0]["file"]
            with chunk_path.open("ab") as handle:
                handle.write(b"corruption")

            result = self.run_tool("tools/validate_spcc_catalog.py", str(catalog_dir), check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("checksum", result.stderr.lower())

    def test_catalog_validator_checks_gaia_transcript_record_hashes(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            catalog_dir = temp_dir / "spcc_fixture"

            self.run_tool("tools/generate_spcc_catalog.py", str(catalog_dir), "--fixture", "--fixture-source-count", "2")

            meta = json.loads((catalog_dir / "catalog_meta.json").read_text(encoding="utf-8"))
            chunk_path = catalog_dir / meta["chunks"][0]["file"]
            _, records = spcc.read_chunk(chunk_path)
            first_record = records[0]
            first_payload_hash = self._packed_record_hash_by_source(chunk_path, str(first_record["source_id"]))

            transcript = {
                "tap": {"service_url": "https://gea.esac.esa.int/tap-server/tap"},
                "sources": [
                    {
                        "source_id": str(first_record["source_id"]),
                        "flux_sample_count": spcc.SAMPLED_XP_SAMPLE_COUNT,
                        "flux_error_sample_count": spcc.SAMPLED_XP_SAMPLE_COUNT,
                        "tap_quality_fields": {"has_xp_sampled": "true"},
                        "packed_record_sha256": first_payload_hash,
                    }
                ],
            }
            transcript_path = temp_dir / "gaia_validation_transcript.json"
            transcript_path.write_text(json.dumps(transcript), encoding="utf-8")

            self.run_tool(
                "tools/validate_spcc_catalog.py",
                str(catalog_dir),
                "--gaia-validation-transcript",
                str(transcript_path),
            )

            transcript["sources"][0]["packed_record_sha256"] = "0" * 64
            transcript_path.write_text(json.dumps(transcript), encoding="utf-8")
            result = self.run_tool(
                "tools/validate_spcc_catalog.py",
                str(catalog_dir),
                "--gaia-validation-transcript",
                str(transcript_path),
                check=False,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("packed record hash mismatch", result.stderr)

    def test_hf_dataset_builder_stages_catalog_card_and_mirror_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            catalog_dir = temp_dir / "spcc_fixture"
            dataset_dir = temp_dir / "hf_dataset"

            self.run_tool(
                "tools/generate_spcc_catalog.py",
                str(catalog_dir),
                "--fixture",
                "--fixture-source-count",
                "5",
                "--manifest-id",
                "spcc-fixture-hf-2026a",
            )

            result = self.run_tool(
                "tools/build_spcc_hf_dataset.py",
                str(dataset_dir),
                str(catalog_dir),
                "--repo-id",
                "emruiz81/afternight-spcc-catalogs",
                "--clean",
            )
            self.assertIn("SPCC Hugging Face dataset staging", result.stdout)
            self.assertIn("[catalog 1/1] validating source catalog", result.stdout)
            self.assertIn("[catalog 1/1] copying", result.stdout)
            self.assertIn("[catalog 1/1] copied", result.stdout)
            self.assertIn("[catalog 1/1] validating staged catalog", result.stdout)
            self.assertIn("[manifest] writing dataset and mirror manifests", result.stdout)

            dataset_manifest = json.loads((dataset_dir / "spcc_dataset_manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(dataset_manifest["schema_version"], "spcc-hf-dataset-manifest-v1")
            self.assertEqual(dataset_manifest["dataset_repo_id"], "emruiz81/afternight-spcc-catalogs")
            self.assertEqual(len(dataset_manifest["catalogs"]), 1)

            entry = dataset_manifest["catalogs"][0]
            expected_path = "spcc_gaia_dr3_xp_sampled/local-full/spcc-fixture-hf-2026a"
            self.assertEqual(entry["path"], expected_path)
            self.assertEqual(entry["rights_review_status"], "private-local")
            self.assertIn(expected_path + "/catalog_meta.json", entry["manifest_url"])

            staged_catalog = dataset_dir / expected_path
            self.run_tool("tools/validate_spcc_catalog.py", str(staged_catalog), "--sample-count", "3")
            self.assertTrue((dataset_dir / ".gitattributes").exists())

            readme = (dataset_dir / "README.md").read_text(encoding="utf-8")
            self.assertIn("hf upload emruiz81/afternight-spcc-catalogs", readme)
            self.assertIn("spcc-fixture-hf-2026a", readme)

            mirror_manifest = json.loads((dataset_dir / "spcc_mirror_manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(mirror_manifest["schema_version"], "spcc-remote-mirror-manifest-v1")
            self.assertEqual(mirror_manifest["mirror_hosting_policy"], "staging-only")
            self.assertEqual(len(mirror_manifest["catalogs"]), 1)
            self.assertEqual(mirror_manifest["catalogs"][0]["upstream_manifest_id"], "spcc-fixture-hf-2026a")
            app_config = json.loads((dataset_dir / "spcc_catalog_mirrors.json").read_text(encoding="utf-8"))
            self.assertEqual(app_config["schema_version"], "spcc-catalog-mirror-config-v1")
            self.assertEqual(app_config["catalog_packs"], [])

            self.run_tool(
                "tools/verify_spcc_hf_dataset.py",
                str(dataset_dir / "spcc_dataset_manifest.json"),
                "--base-url-override",
                dataset_dir.resolve().as_uri(),
                "--chunk-policy",
                "first",
            )

    def test_hf_dataset_builder_blocks_public_upload_without_rights_review(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            catalog_dir = temp_dir / "spcc_fixture"
            dataset_dir = temp_dir / "hf_dataset"

            self.run_tool("tools/generate_spcc_catalog.py", str(catalog_dir), "--fixture")

            result = self.run_tool(
                "tools/build_spcc_hf_dataset.py",
                str(dataset_dir),
                str(catalog_dir),
                "--publication-mode",
                "public-production",
                check=False,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("approved", result.stderr.lower())

    def test_catalog_rights_approval_tool_records_public_production_gate(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            catalog_dir = temp_dir / "spcc_fixture"
            dataset_dir = temp_dir / "hf_dataset"

            self.run_tool(
                "tools/generate_spcc_catalog.py",
                str(catalog_dir),
                "--fixture",
                "--fixture-source-count",
                "5",
                "--manifest-id",
                "spcc-fixture-approved-rights-2026a",
            )
            self.run_tool(
                "tools/approve_spcc_catalog_rights.py",
                str(catalog_dir),
                "--reviewer",
                "Unit Test",
                "--reviewed-at-utc",
                "2026-05-14T00:00:00Z",
            )
            self.run_tool(
                "tools/validate_spcc_catalog.py",
                str(catalog_dir),
                "--strict-production-review",
                "--sample-count",
                "2",
            )

            meta = json.loads((catalog_dir / "catalog_meta.json").read_text(encoding="utf-8"))
            self.assertEqual(meta["rights"]["review_status"], "approved-for-managed-download")
            self.assertEqual(meta["rights"]["mirror_hosting_policy"], "approved-public")
            self.assertEqual(meta["rights"]["reviewed_by"], "Unit Test")
            self.assertEqual(meta["rights"]["reviewed_at_utc"], "2026-05-14T00:00:00Z")
            self.assertIn("required_citations", meta["rights"])
            self.assertIn("source_terms_urls", meta["provenance"])

            self.run_tool(
                "tools/build_spcc_hf_dataset.py",
                str(dataset_dir),
                str(catalog_dir),
                "--publication-mode",
                "public-production",
                "--mirror-hosting-policy",
                "approved-public",
                "--strict-production-review",
                "--clean",
            )
            app_config = json.loads((dataset_dir / "spcc_catalog_mirrors.json").read_text(encoding="utf-8"))
            self.assertEqual(len(app_config["catalog_packs"]), 1)
            self.assertEqual(app_config["publication_policy"]["channel"], "stable")
            self.assertEqual(app_config["catalog_packs"][0]["id"], "spcc-fixture-approved-rights-2026a")
            self.run_tool(
                "tools/verify_spcc_catalog_mirror_config.py",
                str(dataset_dir / "spcc_catalog_mirrors.json"),
                "--base-url-override",
                dataset_dir.resolve().as_uri(),
                "--chunk-policy",
                "first",
                "--no-token",
            )

    def test_catalog_mirror_config_verifier_checks_approved_descriptors_and_manifests(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            catalog_dir = temp_dir / "spcc_fixture"
            dataset_dir = temp_dir / "hf_dataset"
            config_path = temp_dir / "spcc_catalog_mirrors.json"

            self.run_tool(
                "tools/generate_spcc_catalog.py",
                str(catalog_dir),
                "--fixture",
                "--fixture-source-count",
                "5",
                "--manifest-id",
                "spcc-fixture-mirror-config-2026a",
                "--rights-review-status",
                "approved-for-managed-download",
            )
            self.run_tool(
                "tools/build_spcc_hf_dataset.py",
                str(dataset_dir),
                str(catalog_dir),
                "--repo-id",
                "emruiz81/afternight-spcc-catalogs",
                "--direct-base-url",
                dataset_dir.resolve().as_uri(),
                "--clean",
            )

            dataset_manifest = json.loads((dataset_dir / "spcc_dataset_manifest.json").read_text(encoding="utf-8"))
            entry = dataset_manifest["catalogs"][0]
            config_path = dataset_dir / "spcc_catalog_mirrors.json"
            config = json.loads(config_path.read_text(encoding="utf-8"))
            self.assertEqual(config["mirrors"], [dataset_dir.resolve().as_uri()])
            self.assertEqual(config["publication_policy"]["channel"], "preview")
            self.assertEqual(config["publication_policy"]["update_policy"], "immutable-versioned")
            self.assertEqual(len(config["catalog_packs"]), 1)
            self.assertEqual(config["catalog_packs"][0]["id"], entry["manifest_id"])
            self.assertEqual(config["catalog_packs"][0]["manifest_path"], entry["manifest_file"])
            self.assertEqual(config["catalog_packs"][0]["catalog_meta_sha256"], entry["catalog_meta_sha256"])
            self.assertEqual(config["catalog_packs"][0]["release_status"], "current")
            self.assertTrue(config["catalog_packs"][0]["recommended"])

            verified = self.run_tool(
                "tools/verify_spcc_catalog_mirror_config.py",
                str(config_path),
                "--chunk-policy",
                "first",
                "--no-token",
            )

            self.assertIn("verification passed: 1 descriptor", verified.stdout)

            config["catalog_packs"][0]["rights_review_status"] = "private-local"
            config_path.write_text(json.dumps(config), encoding="utf-8")

            rejected = self.run_tool(
                "tools/verify_spcc_catalog_mirror_config.py",
                str(config_path),
                "--chunk-policy",
                "none",
                "--no-token",
                check=False,
            )

            self.assertNotEqual(rejected.returncode, 0)
            self.assertIn("rights_review_status", rejected.stderr)

            config["catalog_packs"][0]["rights_review_status"] = "approved-for-managed-download"
            config["catalog_packs"][0]["id"] = "../escaped-spcc-catalog"
            config_path.write_text(json.dumps(config), encoding="utf-8")

            rejected = self.run_tool(
                "tools/verify_spcc_catalog_mirror_config.py",
                str(config_path),
                "--chunk-policy",
                "none",
                "--no-token",
                check=False,
            )

            self.assertNotEqual(rejected.returncode, 0)
            self.assertIn("safe path segment", rejected.stderr)

    def test_catalog_mirror_config_verifier_enforces_publication_update_policy(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            catalog_2026a = temp_dir / "spcc_fixture_2026a"
            catalog_2026b = temp_dir / "spcc_fixture_2026b"
            dataset_dir = temp_dir / "hf_dataset"

            for version, catalog_dir in (("2026a", catalog_2026a), ("2026b", catalog_2026b)):
                self.run_tool(
                    "tools/generate_spcc_catalog.py",
                    str(catalog_dir),
                    "--fixture",
                    "--fixture-source-count",
                    "5",
                    "--manifest-id",
                    f"spcc-fixture-mirror-config-{version}",
                    "--rights-review-status",
                    "approved-for-managed-download",
                )

            self.run_tool(
                "tools/build_spcc_hf_dataset.py",
                str(dataset_dir),
                str(catalog_2026a),
                str(catalog_2026b),
                "--repo-id",
                "emruiz81/afternight-spcc-catalogs",
                "--direct-base-url",
                dataset_dir.resolve().as_uri(),
                "--clean",
            )

            config_path = dataset_dir / "spcc_catalog_mirrors.json"
            config = json.loads(config_path.read_text(encoding="utf-8"))
            self.assertEqual(config["publication_policy"]["update_policy"], "immutable-versioned")
            self.assertEqual(len(config["catalog_packs"]), 2)
            self.assertEqual({descriptor["release_status"] for descriptor in config["catalog_packs"]}, {"current"})

            verified_multiple_current = self.run_tool(
                "tools/verify_spcc_catalog_mirror_config.py",
                str(config_path),
                "--chunk-policy",
                "first",
                "--no-token",
            )
            self.assertIn("verification passed: 2 descriptor", verified_multiple_current.stdout)

            superseded_config = json.loads(json.dumps(config))
            superseded_config["catalog_packs"][0]["release_status"] = "superseded"
            superseded_config["catalog_packs"][0]["recommended"] = False
            superseded_config["catalog_packs"][0]["superseded_by"] = superseded_config["catalog_packs"][1]["id"]
            superseded_config["catalog_packs"][1]["supersedes"] = [superseded_config["catalog_packs"][0]["id"]]
            config_path.write_text(json.dumps(superseded_config), encoding="utf-8")

            verified_supersession = self.run_tool(
                "tools/verify_spcc_catalog_mirror_config.py",
                str(config_path),
                "--chunk-policy",
                "first",
                "--no-token",
            )
            self.assertIn("verification passed: 2 descriptor", verified_supersession.stdout)

            missing_policy = json.loads(json.dumps(config))
            missing_policy.pop("publication_policy")
            config_path.write_text(json.dumps(missing_policy), encoding="utf-8")
            rejected_policy = self.run_tool(
                "tools/verify_spcc_catalog_mirror_config.py",
                str(config_path),
                "--chunk-policy",
                "none",
                "--no-token",
                check=False,
            )
            self.assertNotEqual(rejected_policy.returncode, 0)
            self.assertIn("publication_policy", rejected_policy.stderr)

            bad_update_policy = json.loads(json.dumps(config))
            bad_update_policy["publication_policy"]["update_policy"] = "mutable-overwrite"
            config_path.write_text(json.dumps(bad_update_policy), encoding="utf-8")
            rejected_update_policy = self.run_tool(
                "tools/verify_spcc_catalog_mirror_config.py",
                str(config_path),
                "--chunk-policy",
                "none",
                "--no-token",
                check=False,
            )
            self.assertNotEqual(rejected_update_policy.returncode, 0)
            self.assertIn("immutable-versioned", rejected_update_policy.stderr)

            bad_superseded = json.loads(json.dumps(superseded_config))
            bad_superseded["catalog_packs"][0]["recommended"] = True
            config_path.write_text(json.dumps(bad_superseded), encoding="utf-8")
            rejected_recommended = self.run_tool(
                "tools/verify_spcc_catalog_mirror_config.py",
                str(config_path),
                "--chunk-policy",
                "none",
                "--no-token",
                check=False,
            )
            self.assertNotEqual(rejected_recommended.returncode, 0)
            self.assertIn("superseded descriptor must not be recommended", rejected_recommended.stderr)

            bad_superseded_by = json.loads(json.dumps(superseded_config))
            bad_superseded_by["catalog_packs"][0]["superseded_by"] = "missing-current-descriptor"
            config_path.write_text(json.dumps(bad_superseded_by), encoding="utf-8")
            rejected_superseded_by = self.run_tool(
                "tools/verify_spcc_catalog_mirror_config.py",
                str(config_path),
                "--chunk-policy",
                "none",
                "--no-token",
                check=False,
            )
            self.assertNotEqual(rejected_superseded_by.returncode, 0)
            self.assertIn("superseded_by", rejected_superseded_by.stderr)

    def test_size_estimator_writes_reproducibility_transcript(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            counts_fixture = temp_dir / "counts.json"
            transcript = temp_dir / "spcc_size_transcript.json"
            counts_fixture.write_text(
                json.dumps(
                    {
                        "sampled-full": 34468373,
                        "continuous-g16": 72450000,
                    }
                ),
                encoding="utf-8",
            )

            result = self.run_tool(
                "tools/estimate_spcc_catalog_size.py",
                "--counts-fixture",
                str(counts_fixture),
                "--output-transcript",
                str(transcript),
            )

            self.assertIn("sampled-full", result.stdout)
            data = json.loads(transcript.read_text(encoding="utf-8"))
            self.assertEqual(data["tool"], "estimate_spcc_catalog_size.py")
            self.assertEqual(data["service_url"], "fixture://spcc-counts")
            self.assertEqual(len(data["queries"]), 2)
            sampled = next(query for query in data["queries"] if query["preset"] == "sampled-full")
            self.assertIn("gaiadr3.gaia_source", sampled["adql"])
            self.assertIn("gaiadr3.xp_sampled_mean_spectrum", sampled["gaia_tables"])
            self.assertEqual(sampled["returned_count"], 34468373)
            self.assertEqual(sampled["estimated_raw_size_bytes"], 34468373 * 1536)

    def test_support_envelope_checker_marks_approved_supported_candidate_ready(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            catalog_dir = temp_dir / "spcc_candidate"
            metrics_path = temp_dir / "validation_metrics.json"
            report_path = temp_dir / "go_no_go_report.json"

            self.run_tool(
                "tools/generate_spcc_catalog.py",
                str(catalog_dir),
                "--fixture",
                "--fixture-source-count",
                "90",
                "--manifest-id",
                "spcc-fixture-support-ready-2026a",
                "--rights-review-status",
                "approved-for-managed-download",
            )
            metrics_path.write_text(
                json.dumps(
                    {
                        "schema_version": "spcc-support-envelope-validation-v1",
                        "runs": [
                            {
                                "manifest_id": "spcc-fixture-support-ready-2026a",
                                "runtime_used_stars": 64,
                                "validation_has_stable_gains": True,
                                "validation_residuals_acceptable": True,
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )

            result = self.run_tool(
                "tools/check_spcc_support_envelope.py",
                str(catalog_dir),
                "--validation-metrics",
                str(metrics_path),
                "--output-json",
                str(report_path),
            )

            self.assertIn("production_ready", result.stdout)
            report = json.loads(report_path.read_text(encoding="utf-8"))
            self.assertTrue(report["all_production_ready"])
            self.assertEqual(report["results"][0]["status"], "production_ready")
            self.assertEqual(report["support_envelope"]["minimum_runtime_used_stars"], 35)

    def test_support_envelope_checker_writes_validation_manifest_entry(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            catalog_dir = temp_dir / "spcc_candidate"
            metrics_path = temp_dir / "validation_metrics.json"
            report_path = temp_dir / "go_no_go_report.json"
            entry_path = temp_dir / "support_manifest_entry.json"
            validation_manifest_path = temp_dir / "spcc-validation-data-manifest.json"

            self.run_tool(
                "tools/generate_spcc_catalog.py",
                str(catalog_dir),
                "--fixture",
                "--fixture-source-count",
                "90",
                "--manifest-id",
                "spcc-fixture-support-entry-2026a",
                "--rights-review-status",
                "approved-for-managed-download",
            )
            metrics_path.write_text(
                json.dumps(
                    {
                        "schema_version": "spcc-support-envelope-validation-v1",
                        "runs": [
                            {
                                "manifest_id": "spcc-fixture-support-entry-2026a",
                                "runtime_used_stars": 64,
                                "validation_has_stable_gains": True,
                                "validation_residuals_acceptable": True,
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )

            self.run_tool(
                "tools/check_spcc_support_envelope.py",
                str(catalog_dir),
                "--validation-metrics",
                str(metrics_path),
                "--output-json",
                str(report_path),
                "--manifest-entry-json",
                str(entry_path),
                "--manifest-entry-id",
                "sampled-xp-support-envelope-entry",
                "--validation-command",
                "python tools/check_spcc_support_envelope.py <catalog> --validation-metrics validation_metrics.json --output-json go_no_go_report.json",
            )

            entry = json.loads(entry_path.read_text(encoding="utf-8"))
            self.assertEqual(entry["id"], "sampled-xp-support-envelope-entry")
            self.assertEqual(entry["status"], "production-ready")
            self.assertEqual(entry["catalog_family_id"], spcc.CATALOG_FAMILY_ID)
            self.assertEqual(entry["catalog_manifest_ids"], ["spcc-fixture-support-entry-2026a"])
            self.assertEqual(entry["rights_review_status"], "approved-for-managed-download")
            self.assertEqual(entry["validation_metrics"]["sha256"], spcc.sha256_file(metrics_path))
            self.assertEqual(entry["go_no_go_report"]["sha256"], spcc.sha256_file(report_path))
            self.assertEqual(
                entry["expected_catalog_statuses"],
                {"spcc-fixture-support-entry-2026a": "production_ready"},
            )

            validation_manifest = {
                "schema_version": "spcc-validation-data-manifest-v1",
                "updated_at_utc": "2026-05-12T00:00:00Z",
                "purpose": "unit test",
                "fixture_catalogs": [],
                "response_repositories": [],
                "gaia_validation_artifacts": [],
                "real_image_reference_sets": [],
                "external_comparison_outputs": [],
                "packaged_qa_reports": [],
                "support_envelope_reports": [entry],
            }
            validation_manifest_path.write_text(json.dumps(validation_manifest), encoding="utf-8")
            self.run_tool("tools/validate_spcc_validation_manifest.py", str(validation_manifest_path))

    def test_support_envelope_checker_blocks_missing_validation_and_insufficient_support(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            supported_catalog = temp_dir / "spcc_supported_candidate"
            sparse_catalog = temp_dir / "spcc_sparse_candidate"
            metrics_path = temp_dir / "validation_metrics.json"

            self.run_tool(
                "tools/generate_spcc_catalog.py",
                str(supported_catalog),
                "--fixture",
                "--fixture-source-count",
                "90",
                "--manifest-id",
                "spcc-fixture-validation-missing-2026a",
                "--rights-review-status",
                "approved-for-managed-download",
            )
            missing_metrics = self.run_tool(
                "tools/check_spcc_support_envelope.py",
                str(supported_catalog),
                check=False,
            )
            self.assertNotEqual(missing_metrics.returncode, 0)
            self.assertIn("validation_missing", missing_metrics.stdout)

            self.run_tool(
                "tools/generate_spcc_catalog.py",
                str(sparse_catalog),
                "--fixture",
                "--fixture-source-count",
                "20",
                "--manifest-id",
                "spcc-fixture-sparse-2026a",
                "--rights-review-status",
                "approved-for-managed-download",
            )
            metrics_path.write_text(
                json.dumps(
                    {
                        "schema_version": "spcc-support-envelope-validation-v1",
                        "runs": [
                            {
                                "manifest_id": "spcc-fixture-sparse-2026a",
                                "runtime_used_stars": 40,
                                "validation_has_stable_gains": True,
                                "validation_residuals_acceptable": True,
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )

            sparse_result = self.run_tool(
                "tools/check_spcc_support_envelope.py",
                str(sparse_catalog),
                "--validation-metrics",
                str(metrics_path),
                check=False,
            )
            self.assertNotEqual(sparse_result.returncode, 0)
            self.assertIn("insufficient_support", sparse_result.stdout)
            self.assertIn("estimated usable candidates", sparse_result.stdout)

    def test_support_envelope_checker_blocks_private_catalog_rights(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            catalog_dir = temp_dir / "spcc_private_candidate"
            metrics_path = temp_dir / "validation_metrics.json"

            self.run_tool(
                "tools/generate_spcc_catalog.py",
                str(catalog_dir),
                "--fixture",
                "--fixture-source-count",
                "90",
                "--manifest-id",
                "spcc-fixture-private-rights-2026a",
                "--rights-review-status",
                "private-local",
            )
            metrics_path.write_text(
                json.dumps(
                    {
                        "schema_version": "spcc-support-envelope-validation-v1",
                        "runs": [
                            {
                                "manifest_id": "spcc-fixture-private-rights-2026a",
                                "runtime_used_stars": 64,
                                "validation_has_stable_gains": True,
                                "validation_residuals_acceptable": True,
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )

            result = self.run_tool(
                "tools/check_spcc_support_envelope.py",
                str(catalog_dir),
                "--validation-metrics",
                str(metrics_path),
                check=False,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("blocked_rights", result.stdout)
            self.assertIn("rights review", result.stdout)

    def test_support_envelope_checker_rejects_invalid_metrics_schema(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            catalog_dir = temp_dir / "spcc_candidate"
            metrics_path = temp_dir / "validation_metrics.json"

            self.run_tool(
                "tools/generate_spcc_catalog.py",
                str(catalog_dir),
                "--fixture",
                "--fixture-source-count",
                "90",
                "--manifest-id",
                "spcc-fixture-invalid-metrics-schema-2026a",
                "--rights-review-status",
                "approved-for-managed-download",
            )
            metrics_path.write_text(
                json.dumps(
                    {
                        "schema_version": "spcc-support-envelope-validation-v0",
                        "runs": [
                            {
                                "manifest_id": "spcc-fixture-invalid-metrics-schema-2026a",
                                "runtime_used_stars": 64,
                                "validation_has_stable_gains": True,
                                "validation_residuals_acceptable": True,
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )

            result = self.run_tool(
                "tools/check_spcc_support_envelope.py",
                str(catalog_dir),
                "--validation-metrics",
                str(metrics_path),
                check=False,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("schema_version", result.stderr)
            self.assertNotIn("Traceback", result.stderr)

    def test_support_envelope_checker_rejects_non_boolean_metrics(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            catalog_dir = temp_dir / "spcc_candidate"
            metrics_path = temp_dir / "validation_metrics.json"

            self.run_tool(
                "tools/generate_spcc_catalog.py",
                str(catalog_dir),
                "--fixture",
                "--fixture-source-count",
                "90",
                "--manifest-id",
                "spcc-fixture-invalid-metrics-types-2026a",
                "--rights-review-status",
                "approved-for-managed-download",
            )
            metrics_path.write_text(
                json.dumps(
                    {
                        "schema_version": "spcc-support-envelope-validation-v1",
                        "runs": [
                            {
                                "manifest_id": "spcc-fixture-invalid-metrics-types-2026a",
                                "runtime_used_stars": 64,
                                "validation_has_stable_gains": "true",
                                "validation_residuals_acceptable": True,
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )

            result = self.run_tool(
                "tools/check_spcc_support_envelope.py",
                str(catalog_dir),
                "--validation-metrics",
                str(metrics_path),
                check=False,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("validation_has_stable_gains", result.stderr)
            self.assertNotIn("production_ready", result.stdout)

    def test_gaia_fetcher_builds_regional_tap_query_without_first_file_constraint(self) -> None:
        args = spcc_fetch.parse_args(
            [
                "out",
                "--source-count",
                "80",
                "--region-name",
                "m31-core",
                "--ra-deg",
                "10.6847083",
                "--dec-deg",
                "41.26875",
                "--radius-deg",
                "1.5",
                "--mag-min",
                "9",
                "--mag-max",
                "16",
                "--min-bp-rp-snr",
                "20",
            ]
        )

        adql = spcc_fetch.build_tap_adql(args)

        self.assertIn("SELECT TOP 80", adql)
        self.assertIn("CIRCLE('ICRS', 10.68470830, 41.26875000, 1.50000000)", adql)
        self.assertIn("s.phot_g_mean_mag BETWEEN 9 AND 16", adql)
        self.assertIn("s.phot_bp_mean_flux_over_error > 20", adql)
        self.assertIn("ORDER BY s.phot_g_mean_mag ASC", adql)
        self.assertNotIn("s.source_id <", adql)

    def test_response_repository_normalizes_and_validates_deployed_path(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            source_dir = temp_dir / "source"
            source_dir.mkdir()
            output_dir = temp_dir / "normalized_responses"

            self._write_curve(source_dir / "sensor_r.csv", [(400.0, 0.1), (500.0, 0.8), (700.0, 0.2)])
            self._write_curve(source_dir / "white_d65.csv", [(400.0, 0.6), (500.0, 1.0), (700.0, 0.7)])

            manifest = {
                "repository_id": "spcc-response-fixture",
                "version": "2026a",
                "rights": {"review_status": "private-local", "license_name": "synthetic-fixture"},
                "components": [
                    {
                        "component_id": "sensor.synthetic.r",
                        "version": "1",
                        "role": "sensor-red-response",
                        "curve_csv": "sensor_r.csv",
                        "wavelength_unit": "nm",
                        "value_semantics": "dimensionless-throughput",
                        "review_status": "private-local",
                        "license_name": "synthetic-fixture",
                    }
                ],
                "white_references": [
                    {
                        "white_reference_id": "white.synthetic.d65",
                        "version": "1",
                        "curve_csv": "white_d65.csv",
                        "wavelength_unit": "nm",
                        "value_semantics": "relative-sed",
                        "review_status": "private-local",
                        "license_name": "synthetic-fixture",
                    }
                ],
                "throughput_profiles": [
                    {
                        "profile_id": "profile.synthetic.rgb",
                        "version": "1",
                        "component_ids": ["sensor.synthetic.r"],
                        "white_reference_id": "white.synthetic.d65",
                        "compatible_channel_bases": ["camera-rgb", "linear-rgb"],
                        "models_raw_white_balance": False,
                        "models_camera_matrix": False,
                    }
                ],
            }
            manifest_path = source_dir / "response_source_manifest.json"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

            self.run_tool(
                "tools/normalize_spcc_response_repository.py",
                str(manifest_path),
                str(output_dir),
            )
            self.run_tool("tools/validate_spcc_response_repository.py", str(output_dir), "--runtime-root")

            response_meta = json.loads((output_dir / "response_meta.json").read_text(encoding="utf-8"))
            self.assertEqual(response_meta["repository_id"], "spcc-response-fixture")
            self.assertEqual(len(response_meta["components"]), 1)
            self.assertEqual(len(response_meta["white_references"]), 1)
            self.assertEqual(len(response_meta["throughput_profiles"]), 1)
            self.assertEqual(len(response_meta["components"][0]["data_sha256"]), 64)

    def test_response_repository_strict_review_requires_asset_provenance(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            source_dir = temp_dir / "source"
            source_dir.mkdir()
            output_dir = temp_dir / "normalized_responses"
            self._write_curve(source_dir / "sensor_r.csv", [(400.0, 0.1), (500.0, 0.8), (700.0, 0.2)])
            self._write_curve(source_dir / "white_d65.csv", [(400.0, 0.6), (500.0, 1.0), (700.0, 0.7)])

            manifest = {
                "repository_id": "spcc-response-missing-provenance",
                "version": "2026a",
                "rights": {
                    "review_status": "approved-for-managed-download",
                    "license_name": "synthetic-fixture",
                },
                "components": [
                    {
                        "component_id": "sensor.synthetic.r",
                        "version": "1",
                        "role": "sensor-red-response",
                        "curve_csv": "sensor_r.csv",
                        "review_status": "approved-for-managed-download",
                        "license_name": "synthetic-fixture",
                        "source_url": "local://synthetic/sensor-r",
                    }
                ],
                "white_references": [
                    {
                        "white_reference_id": "white.synthetic.d65",
                        "version": "1",
                        "curve_csv": "white_d65.csv",
                        "value_semantics": "relative-sed",
                        "review_status": "approved-for-managed-download",
                        "license_name": "synthetic-fixture",
                        "source_url": "local://synthetic/white-d65",
                    }
                ],
                "throughput_profiles": [
                    {
                        "profile_id": "profile.synthetic.rgb",
                        "version": "1",
                        "component_ids": ["sensor.synthetic.r"],
                        "white_reference_id": "white.synthetic.d65",
                        "compatible_channel_bases": ["linear-rgb"],
                    }
                ],
            }
            manifest_path = source_dir / "response_source_manifest.json"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            self.run_tool("tools/normalize_spcc_response_repository.py", str(manifest_path), str(output_dir))

            result = self.run_tool(
                "tools/validate_spcc_response_repository.py",
                str(output_dir),
                "--strict-production-review",
                check=False,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("source_citation", result.stderr)
            self.assertIn("attribution_text", result.stderr)
            self.assertIn("transformation_notes", result.stderr)

    def test_response_baseline_source_manifest_template_carries_release_provenance_fields(self) -> None:
        template_path = REPO_ROOT / "docs/dev/spcc/spcc-response-baseline-2026a-source-manifest.template.json"
        template = json.loads(template_path.read_text(encoding="utf-8"))

        self.assertEqual(template["repository_id"], "spcc-response-baseline")
        self.assertEqual(template["rights"]["review_status"], "license-review-needed")
        for asset in template["components"] + template["white_references"]:
            self.assertEqual(asset["review_status"], "license-review-needed")
            self.assertIn("license_name", asset)
            self.assertIn("source_url", asset)
            self.assertIn("source_citation", asset)
            self.assertIn("attribution_text", asset)
            self.assertIn("transformation_notes", asset)

    def test_response_source_manifest_validator_rejects_template_placeholders(self) -> None:
        result = self.run_tool(
            "tools/validate_spcc_response_source_manifest.py",
            "docs/dev/spcc/spcc-response-baseline-2026a-source-manifest.template.json",
            "--strict-production-review",
            check=False,
        )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("placeholder", result.stderr)
        self.assertIn("curve_csv invalid", result.stderr)

    def test_response_source_manifest_validator_writes_audit_report_for_template(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            report_path = Path(temp_dir_raw) / "response_source_audit.json"
            result = self.run_tool(
                "tools/validate_spcc_response_source_manifest.py",
                "docs/dev/spcc/spcc-response-baseline-2026a-source-manifest.template.json",
                "--strict-production-review",
                "--report-json",
                str(report_path),
                check=False,
            )

            self.assertNotEqual(result.returncode, 0)
            report = json.loads(report_path.read_text(encoding="utf-8"))
            self.assertEqual(report["schema_version"], "spcc-response-source-manifest-audit-v1")
            self.assertFalse(report["passed"])
            self.assertEqual(report["repository_id"], "spcc-response-baseline")
            self.assertEqual(report["counts"]["components"], 3)
            self.assertEqual(report["counts"]["white_references"], 1)
            self.assertIn("source_manifest.rights.license_name", report["placeholder_paths"])
            self.assertEqual(len(report["curve_artifacts"]), 4)
            self.assertTrue(all(not artifact["exists"] for artifact in report["curve_artifacts"]))

    def test_response_source_manifest_validator_accepts_reviewed_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            source_dir = temp_dir / "source"
            source_dir.mkdir()
            self._write_curve(source_dir / "sensor_r.csv", [(400.0, 0.1), (500.0, 0.8), (700.0, 0.2)])
            self._write_curve(source_dir / "white_d65.csv", [(400.0, 0.6), (500.0, 1.0), (700.0, 0.7)])
            manifest = {
                "repository_id": "spcc-response-reviewed",
                "version": "2026a",
                "rights": {
                    "review_status": "approved-for-managed-download",
                    "license_name": "reviewed-license",
                    "license_url": "https://example.test/license",
                    "reviewer": "Unit Test",
                    "reviewed_at_utc": "2026-05-12T00:00:00Z",
                    "redistribution_permission": "allowed for managed download",
                    "derivative_permission": "allowed for normalized response payloads",
                    "required_attribution": "Synthetic reviewed fixture attribution.",
                },
                "components": [
                    self._reviewed_response_source_asset(
                        "component_id",
                        "sensor.synthetic.r",
                        "sensor-red-response",
                        "sensor_r.csv",
                        "dimensionless-throughput",
                    )
                ],
                "white_references": [
                    self._reviewed_response_source_asset(
                        "white_reference_id",
                        "white.synthetic.d65",
                        "white-reference",
                        "white_d65.csv",
                        "relative-sed",
                    )
                ],
                "throughput_profiles": [
                    {
                        "profile_id": "profile.synthetic.rgb",
                        "version": "1",
                        "component_ids": ["sensor.synthetic.r"],
                        "white_reference_id": "white.synthetic.d65",
                        "compatible_channel_bases": ["linear-rgb"],
                    }
                ],
            }
            manifest_path = source_dir / "response_source_manifest.json"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

            self.run_tool(
                "tools/validate_spcc_response_source_manifest.py",
                str(manifest_path),
                "--strict-production-review",
            )
            self.run_tool(
                "tools/normalize_spcc_response_repository.py",
                str(manifest_path),
                str(temp_dir / "normalized"),
                "--strict-production-review",
            )

    def test_spcc_response_database_importer_writes_reviewed_response_source_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            source_database_dir = temp_dir / "source-spcc-database"
            source_dir = temp_dir / "afternight_response_source"
            manifest_path = source_dir / "response_source_manifest.json"
            normalized_dir = temp_dir / "normalized"

            self._write_spcc_response_fixture_database(source_database_dir)

            self.run_tool(
                "tools/import_spcc_response_database.py",
                str(source_database_dir),
                str(source_dir),
                "--repository-id",
                "afternight-spcc-response-fixture",
                "--version",
                "2026a",
                "--source-revision",
                "abcdef1234567890",
                "--source-acquired-at-utc",
                "2026-05-12T00:00:00Z",
                "--review-status",
                "approved-for-managed-download",
                "--reviewer",
                "Unit Test",
            )

            self.run_tool(
                "tools/validate_spcc_response_source_manifest.py",
                str(manifest_path),
                "--strict-production-review",
            )
            self.run_tool(
                "tools/normalize_spcc_response_repository.py",
                str(manifest_path),
                str(normalized_dir),
                "--strict-production-review",
            )
            self.run_tool(
                "tools/validate_spcc_response_repository.py",
                str(normalized_dir),
                "--strict-production-review",
            )

            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            self.assertEqual(manifest["repository_id"], "afternight-spcc-response-fixture")
            self.assertEqual(manifest["rights"]["license_name"], "GPL-3.0-only")
            self.assertEqual(len(manifest["white_references"]), 1)
            self.assertGreaterEqual(len(manifest["throughput_profiles"]), 2)
            white_reference = manifest["white_references"][0]
            self.assertEqual(white_reference["value_semantics"], "relative-photon-count")
            self.assertTrue(white_reference["photon_count_weighted"])
            self.assertEqual(white_reference["normalization"], "white-reference-wavelength-weighted-550nm-value")
            with (source_dir / white_reference["curve_csv"]).open("r", encoding="utf-8", newline="") as handle:
                rows = list(csv.DictReader(handle))
            self.assertAlmostEqual(float(rows[0]["value"]), 240.0, places=8)
            self.assertAlmostEqual(float(rows[1]["value"]), 400.0, places=8)
            self.assertAlmostEqual(float(rows[2]["value"]), 600.0, places=8)
            self.assertAlmostEqual(float(rows[3]["value"]), 490.0, places=8)

            profile_by_id = {profile["profile_id"]: profile for profile in manifest["throughput_profiles"]}
            osc_profile = profile_by_id["afternight.spcc.profile.osc_sensor.toy_osc_sensor.default"]
            self.assertEqual(
                osc_profile["channel_component_ids"],
                {
                    "red": ["afternight.spcc.osc_sensor.toy_osc_sensor.red"],
                    "green": ["afternight.spcc.osc_sensor.toy_osc_sensor.green"],
                    "blue": ["afternight.spcc.osc_sensor.toy_osc_sensor.blue"],
                },
            )
            osc_filter_lpf_profile = profile_by_id[
                "afternight.spcc.profile.osc_sensor.toy_osc_sensor.osc_filter.toy_broadband_filter."
                "osc_lpf.toy_dslr_lpf.default"
            ]
            self.assertEqual(
                osc_filter_lpf_profile["channel_component_ids"],
                {
                    "red": [
                        "afternight.spcc.osc_sensor.toy_osc_sensor.red",
                        "afternight.spcc.osc_filter.toy_broadband_filter",
                        "afternight.spcc.osc_lpf.toy_dslr_lpf",
                    ],
                    "green": [
                        "afternight.spcc.osc_sensor.toy_osc_sensor.green",
                        "afternight.spcc.osc_filter.toy_broadband_filter",
                        "afternight.spcc.osc_lpf.toy_dslr_lpf",
                    ],
                    "blue": [
                        "afternight.spcc.osc_sensor.toy_osc_sensor.blue",
                        "afternight.spcc.osc_filter.toy_broadband_filter",
                        "afternight.spcc.osc_lpf.toy_dslr_lpf",
                    ],
                },
            )
            self.assertEqual(osc_filter_lpf_profile["aliases"][0]["filter"], "Toy Broadband Filter + Toy DSLR LPF")
            mono_sensor_profile = profile_by_id["afternight.spcc.profile.mono_sensor.toy_mono_sensor.default"]
            self.assertEqual(
                mono_sensor_profile["channel_component_ids"],
                {
                    "red": ["afternight.spcc.mono_sensor.toy_mono_sensor"],
                    "green": ["afternight.spcc.mono_sensor.toy_mono_sensor"],
                    "blue": ["afternight.spcc.mono_sensor.toy_mono_sensor"],
                },
            )
            self.assertEqual(mono_sensor_profile["aliases"][0]["filter"], "")
            mono_profile = profile_by_id[
                "afternight.spcc.profile.mono_sensor.toy_mono_sensor.mono_filter.toy_rgb_filters.default"
            ]
            self.assertEqual(
                mono_profile["channel_component_ids"]["red"],
                ["afternight.spcc.mono_sensor.toy_mono_sensor", "afternight.spcc.mono_filter.toy_rgb_filters.red"],
            )
            mono_clear_profile = profile_by_id[
                "afternight.spcc.profile.mono_sensor.toy_mono_sensor.mono_filter.toy_clear_filter.default"
            ]
            self.assertEqual(
                mono_clear_profile["channel_component_ids"],
                {
                    "red": [
                        "afternight.spcc.mono_sensor.toy_mono_sensor",
                        "afternight.spcc.mono_filter.toy_clear_filter.all",
                    ],
                    "green": [
                        "afternight.spcc.mono_sensor.toy_mono_sensor",
                        "afternight.spcc.mono_filter.toy_clear_filter.all",
                    ],
                    "blue": [
                        "afternight.spcc.mono_sensor.toy_mono_sensor",
                        "afternight.spcc.mono_filter.toy_clear_filter.all",
                    ],
                },
            )

            normalized_meta = json.loads((normalized_dir / "response_meta.json").read_text(encoding="utf-8"))
            normalized_profile = {
                profile["profile_id"]: profile for profile in normalized_meta["throughput_profiles"]
            }["afternight.spcc.profile.osc_sensor.toy_osc_sensor.default"]
            self.assertIn("channel_component_ids", normalized_profile)
            component_payload = json.loads(
                (normalized_dir / "components" / "afternight_spcc_osc_sensor_toy_osc_sensor_red.json").read_text(
                    encoding="utf-8"
                )
            )
            self.assertEqual(component_payload["channel"], "red")
            self.assertEqual(component_payload["source_upstream_data_quality_marker"], 4)
            normalized_clear_payload = json.loads(
                (
                    normalized_dir
                    / "components"
                    / "afternight_spcc_mono_filter_toy_clear_filter_all.json"
                ).read_text(encoding="utf-8")
            )
            self.assertEqual(normalized_clear_payload["channel"], "all")
            normalized_white_payload = json.loads(
                (
                    normalized_dir
                    / "white_references"
                    / "afternight_spcc_white_reference_average_spiral_galaxy.json"
                ).read_text(encoding="utf-8")
            )
            self.assertTrue(normalized_white_payload["photon_count_weighted"])

    def test_spcc_response_database_importer_reads_zip_archive_case_collisions(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            archive_path = temp_dir / "source-spcc-database.zip"
            source_dir = temp_dir / "afternight_response_source"
            wavelength = [400, 500, 600, 700]
            sensor_entries = [
                {
                    "model": "Toy OSC Sensor",
                    "name": f"Toy OSC Sensor {channel.title()}",
                    "type": "OSC_SENSOR",
                    "dataQualityMarker": 4,
                    "dataSource": "Unit test OEM tabulated sensor data",
                    "manufacturer": "Toy Cameras",
                    "version": 1,
                    "channel": channel.upper(),
                    "units": "nm",
                    "range": 100,
                    "wavelength": wavelength,
                    "values": values,
                }
                for channel, values in {
                    "red": [5, 65, 92, 30],
                    "green": [20, 88, 72, 8],
                    "blue": [80, 70, 15, 2],
                }.items()
            ]

            def white_reference(model: str, values: list[float]) -> list[dict[str, object]]:
                return [
                    {
                        "model": model,
                        "name": model,
                        "type": "WB_REF",
                        "dataQualityMarker": 3,
                        "dataSource": "Unit test white reference",
                        "manufacturer": "Upstream",
                        "version": 1,
                        "units": "nm",
                        "range": 1,
                        "wavelength": wavelength,
                        "values": values,
                    }
                ]

            with zipfile.ZipFile(archive_path, "w") as archive:
                archive.writestr(
                    "source-spcc-database-main/osc_sensors/Toy_OSC_Sensor.json",
                    json.dumps(sensor_entries),
                )
                archive.writestr(
                    "source-spcc-database-main/wb_refs/Star_M9iii.json",
                    json.dumps(white_reference("Star M9III", [0.6, 0.8, 1.0, 0.7])),
                )
                archive.writestr(
                    "source-spcc-database-main/wb_refs/Star_m9iii.json",
                    json.dumps(white_reference("Star m9III", [0.5, 0.7, 0.9, 0.8])),
                )

            self.run_tool(
                "tools/import_spcc_response_database.py",
                str(archive_path),
                str(source_dir),
                "--repository-id",
                "afternight-spcc-response-zip-fixture",
                "--version",
                "2026a",
                "--source-revision",
                "abcdef1234567890",
                "--source-acquired-at-utc",
                "2026-05-12T00:00:00Z",
                "--review-status",
                "approved-for-managed-download",
                "--reviewer",
                "Unit Test",
            )
            self.run_tool(
                "tools/validate_spcc_response_source_manifest.py",
                str(source_dir / "response_source_manifest.json"),
                "--strict-production-review",
            )

            manifest = json.loads((source_dir / "response_source_manifest.json").read_text(encoding="utf-8"))
            white_reference_ids = {
                entry["white_reference_id"] for entry in manifest["white_references"]
            }
            self.assertEqual(len(manifest["components"]), 3)
            self.assertEqual(len(manifest["white_references"]), 2)
            self.assertEqual(
                white_reference_ids,
                {"afternight.spcc.white_reference.star_m9iii", "afternight.spcc.white_reference.star_m9iii.2"},
            )
            self.assertEqual(len(manifest["throughput_profiles"]), 1)

    def test_real_image_validation_source_manifest_template_carries_release_fields(self) -> None:
        template_path = REPO_ROOT / "docs/dev/spcc/spcc-real-image-validation-source-manifest.template.json"
        template = json.loads(template_path.read_text(encoding="utf-8"))

        self.assertEqual(template["schema_version"], "spcc-real-image-validation-source-v1")
        field_classes = {entry["field_class"] for entry in template["real_image_reference_sets"]}
        self.assertEqual(field_classes, {"small-fov", "medium-fov", "wide-fov"})
        real_image_ids = {entry["id"] for entry in template["real_image_reference_sets"]}
        for entry in template["real_image_reference_sets"]:
            self.assertEqual(entry["rights_review_status"], "license-review-needed")
            self.assertIn("minimum_accepted_samples", entry["expected_metrics"])
            self.assertIn("maximum_median_color_residual", entry["expected_metrics"])
            for item in entry["files"]:
                self.assertFalse(Path(item["path"]).is_absolute())
                self.assertIn("role", item)

        for entry in template["external_comparison_outputs"]:
            self.assertIn(entry["reference_set_id"], real_image_ids)
            self.assertEqual(entry["rights_review_status"], "license-review-needed")
            self.assertIn("maximum_gain_delta", entry["expected_metrics"])
            for item in entry["output_files"]:
                self.assertFalse(Path(item["path"]).is_absolute())
                self.assertIn("role", item)

    def test_real_image_validation_entry_builder_rejects_template_placeholders(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            output_json = Path(temp_dir_raw) / "real_image_manifest_entries.json"
            result = self.run_tool(
                "tools/build_spcc_real_image_validation_entries.py",
                "docs/dev/spcc/spcc-real-image-validation-source-manifest.template.json",
                str(output_json),
                "--strict-production-review",
                check=False,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("placeholder", result.stderr)
            self.assertIn("artifact file is missing", result.stderr)

    def test_response_repository_config_verifier_checks_approved_descriptors_and_assets(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            source_dir = temp_dir / "source"
            source_dir.mkdir()
            output_dir = temp_dir / "normalized_responses"
            config_path = temp_dir / "spcc_response_repositories.json"

            self._write_curve(source_dir / "sensor_r.csv", [(400.0, 0.1), (500.0, 0.8), (700.0, 0.2)])
            self._write_curve(source_dir / "white_d65.csv", [(400.0, 0.6), (500.0, 1.0), (700.0, 0.7)])

            manifest = {
                "repository_id": "spcc-response-fixture",
                "version": "2026a",
                "rights": {
                    "review_status": "approved-for-managed-download",
                    "license_name": "synthetic-fixture",
                },
                "components": [
                    {
                        "component_id": "sensor.synthetic.r",
                        "version": "1",
                        "role": "sensor-red-response",
                        "curve_csv": "sensor_r.csv",
                        "wavelength_unit": "nm",
                        "value_semantics": "dimensionless-throughput",
                        "review_status": "approved-for-managed-download",
                        "license_name": "synthetic-fixture",
                        "source_url": "local://synthetic/sensor-r",
                        "source_citation": "Synthetic sensor response fixture",
                        "attribution_text": "Synthetic response data generated for AfterNight tests.",
                        "transformation_notes": "Unit-test curve samples are normalized as authored.",
                    }
                ],
                "white_references": [
                    {
                        "white_reference_id": "white.synthetic.d65",
                        "version": "1",
                        "curve_csv": "white_d65.csv",
                        "wavelength_unit": "nm",
                        "value_semantics": "relative-sed",
                        "review_status": "approved-for-managed-download",
                        "license_name": "synthetic-fixture",
                        "source_url": "local://synthetic/white-d65",
                        "source_citation": "Synthetic white reference fixture",
                        "attribution_text": "Synthetic white-reference data generated for AfterNight tests.",
                        "transformation_notes": "Unit-test curve samples are normalized as authored.",
                    }
                ],
                "throughput_profiles": [
                    {
                        "profile_id": "profile.synthetic.rgb",
                        "version": "1",
                        "component_ids": ["sensor.synthetic.r"],
                        "white_reference_id": "white.synthetic.d65",
                        "compatible_channel_bases": ["camera-rgb", "linear-rgb"],
                        "models_raw_white_balance": False,
                        "models_camera_matrix": False,
                    }
                ],
            }
            manifest_path = source_dir / "response_source_manifest.json"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

            self.run_tool(
                "tools/normalize_spcc_response_repository.py",
                str(manifest_path),
                str(output_dir),
            )

            config = {
                "schema_version": "spcc-response-repository-config-v1",
                "publication_policy": {
                    "channel": "stable",
                    "published_at_utc": "2026-05-11T00:00:00Z",
                    "update_policy": "immutable-versioned",
                },
                "mirrors": [output_dir.resolve().as_uri()],
                "response_repositories": [
                    {
                        "id": "spcc-response-fixture-2026a",
                        "repository_id": "spcc-response-fixture",
                        "version": "2026a",
                        "display_name": "Approved response fixture",
                        "description": "Local response repository verifier smoke descriptor.",
                        "manifest_path": "response_meta.json",
                        "rights_review_status": "approved-for-managed-download",
                        "deployment_scope": "managed-cache",
                        "response_meta_sha256": spcc.sha256_file(output_dir / "response_meta.json"),
                        "release_status": "current",
                        "recommended": True,
                    }
                ],
            }
            config_path.write_text(json.dumps(config), encoding="utf-8")

            verified = self.run_tool(
                "tools/verify_spcc_response_repository_config.py",
                str(config_path),
                "--no-token",
            )

            self.assertIn("verification passed: 1 descriptor", verified.stdout)

            config["response_repositories"][0]["rights_review_status"] = "private-local"
            config_path.write_text(json.dumps(config), encoding="utf-8")

            rejected = self.run_tool(
                "tools/verify_spcc_response_repository_config.py",
                str(config_path),
                "--no-token",
                check=False,
            )

            self.assertNotEqual(rejected.returncode, 0)
            self.assertIn("rights_review_status", rejected.stderr)

            config["response_repositories"][0]["rights_review_status"] = "approved-for-managed-download"
            config_path.write_text(json.dumps(config), encoding="utf-8")
            response_meta = json.loads((output_dir / "response_meta.json").read_text(encoding="utf-8"))
            component_path = output_dir / response_meta["components"][0]["file"]
            component_payload = json.loads(component_path.read_text(encoding="utf-8"))
            component_payload["samples"][1]["value"] = 0.7
            component_path.write_text(json.dumps(component_payload), encoding="utf-8")

            bad_asset = self.run_tool(
                "tools/verify_spcc_response_repository_config.py",
                str(config_path),
                "--no-token",
                check=False,
            )

            self.assertNotEqual(bad_asset.returncode, 0)
            self.assertIn("file_sha256 mismatch", bad_asset.stderr)

    def test_response_repository_config_verifier_enforces_publication_update_policy(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            response_2026a = self._normalize_response_repository_fixture(temp_dir, "2026a")
            response_2026b = self._normalize_response_repository_fixture(temp_dir, "2026b")
            config_path = temp_dir / "spcc_response_repositories.json"

            def descriptor(version: str, output_dir: Path, *, release_status: str, recommended: bool) -> dict:
                descriptor_id = f"spcc-response-fixture-{version}"
                return {
                    "id": descriptor_id,
                    "repository_id": "spcc-response-fixture",
                    "version": version,
                    "display_name": f"Approved response fixture {version}",
                    "description": "Local response repository publication-policy fixture.",
                    "manifest_url": (output_dir / "response_meta.json").resolve().as_uri(),
                    "rights_review_status": "approved-for-managed-download",
                    "deployment_scope": "managed-cache",
                    "response_meta_sha256": spcc.sha256_file(output_dir / "response_meta.json"),
                    "release_status": release_status,
                    "recommended": recommended,
                }

            superseded = descriptor("2026a", response_2026a, release_status="superseded", recommended=False)
            superseded["superseded_by"] = "spcc-response-fixture-2026b"
            current = descriptor("2026b", response_2026b, release_status="current", recommended=True)
            current["supersedes"] = ["spcc-response-fixture-2026a"]

            config = {
                "schema_version": "spcc-response-repository-config-v1",
                "publication_policy": {
                    "channel": "stable",
                    "published_at_utc": "2026-05-11T00:00:00Z",
                    "update_policy": "immutable-versioned",
                },
                "response_repositories": [superseded, current],
            }
            config_path.write_text(json.dumps(config), encoding="utf-8")

            verified = self.run_tool(
                "tools/verify_spcc_response_repository_config.py",
                str(config_path),
                "--no-token",
            )

            self.assertIn("verification passed: 2 descriptor", verified.stdout)

            missing_policy = dict(config)
            missing_policy.pop("publication_policy")
            config_path.write_text(json.dumps(missing_policy), encoding="utf-8")
            rejected_policy = self.run_tool(
                "tools/verify_spcc_response_repository_config.py",
                str(config_path),
                "--no-token",
                check=False,
            )
            self.assertNotEqual(rejected_policy.returncode, 0)
            self.assertIn("publication_policy", rejected_policy.stderr)

            config["publication_policy"]["update_policy"] = "mutable-overwrite"
            config_path.write_text(json.dumps(config), encoding="utf-8")
            rejected_update_policy = self.run_tool(
                "tools/verify_spcc_response_repository_config.py",
                str(config_path),
                "--no-token",
                check=False,
            )
            self.assertNotEqual(rejected_update_policy.returncode, 0)
            self.assertIn("immutable-versioned", rejected_update_policy.stderr)

            config["publication_policy"]["update_policy"] = "immutable-versioned"
            config["response_repositories"][0]["recommended"] = True
            config_path.write_text(json.dumps(config), encoding="utf-8")
            rejected_recommended = self.run_tool(
                "tools/verify_spcc_response_repository_config.py",
                str(config_path),
                "--no-token",
                check=False,
            )
            self.assertNotEqual(rejected_recommended.returncode, 0)
            self.assertIn("superseded descriptor must not be recommended", rejected_recommended.stderr)

            config["response_repositories"][0]["recommended"] = False
            config["response_repositories"][0]["superseded_by"] = "missing-current-descriptor"
            config_path.write_text(json.dumps(config), encoding="utf-8")
            rejected_superseded_by = self.run_tool(
                "tools/verify_spcc_response_repository_config.py",
                str(config_path),
                "--no-token",
                check=False,
            )
            self.assertNotEqual(rejected_superseded_by.returncode, 0)
            self.assertIn("superseded_by", rejected_superseded_by.stderr)

    def test_response_repository_dataset_builder_stages_config_and_verifies(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            response_2026a = self._normalize_response_repository_fixture(temp_dir, "2026a")
            response_2026b = self._normalize_response_repository_fixture(temp_dir, "2026b")
            dataset_dir = temp_dir / "response_dataset"
            entries_path = temp_dir / "response_validation_manifest_entries.json"

            self.run_tool(
                "tools/build_spcc_response_repository_dataset.py",
                str(dataset_dir),
                str(response_2026a),
                str(response_2026b),
                "--repo-id",
                "emruiz81/afternight-spcc-responses",
                "--direct-base-url",
                dataset_dir.resolve().as_uri(),
                "--validation-manifest-entries-json",
                str(entries_path),
                "--clean",
            )

            dataset_manifest = json.loads(
                (dataset_dir / "spcc_response_dataset_manifest.json").read_text(encoding="utf-8")
            )
            self.assertEqual(dataset_manifest["schema_version"], "spcc-response-dataset-manifest-v1")
            self.assertEqual(dataset_manifest["dataset_repo_id"], "emruiz81/afternight-spcc-responses")
            self.assertEqual(len(dataset_manifest["response_repositories"]), 2)

            config_path = dataset_dir / "spcc_response_repositories.json"
            config = json.loads(config_path.read_text(encoding="utf-8"))
            self.assertEqual(config["schema_version"], "spcc-response-repository-config-v1")
            self.assertEqual(config["publication_policy"]["update_policy"], "immutable-versioned")
            self.assertEqual(len(config["response_repositories"]), 2)

            descriptors = {descriptor["version"]: descriptor for descriptor in config["response_repositories"]}
            self.assertEqual(descriptors["2026a"]["release_status"], "superseded")
            self.assertFalse(descriptors["2026a"]["recommended"])
            self.assertEqual(descriptors["2026a"]["superseded_by"], descriptors["2026b"]["id"])
            self.assertEqual(descriptors["2026b"]["release_status"], "current")
            self.assertTrue(descriptors["2026b"]["recommended"])
            self.assertEqual(descriptors["2026b"]["supersedes"], [descriptors["2026a"]["id"]])

            staged_repo = dataset_dir / "responses" / "spcc-response-fixture" / "2026b"
            self.run_tool("tools/validate_spcc_response_repository.py", str(staged_repo), "--strict-production-review")
            self.run_tool("tools/verify_spcc_response_repository_config.py", str(config_path), "--no-token")

            readme = (dataset_dir / "README.md").read_text(encoding="utf-8")
            self.assertIn("hf upload emruiz81/afternight-spcc-responses", readme)
            self.assertIn("spcc-response-fixture", readme)

            entries = json.loads(entries_path.read_text(encoding="utf-8"))
            self.assertEqual(entries["schema_version"], "spcc-response-validation-manifest-entries-v1")
            self.assertEqual(len(entries["response_repositories"]), 2)
            current_entry = {
                entry["version"]: entry for entry in entries["response_repositories"]
            }["2026b"]
            self.assertEqual(current_entry["rights_review_status"], "approved-for-managed-download")
            self.assertEqual(current_entry["license"], "synthetic-fixture")
            self.assertEqual(current_entry["response_meta"]["sha256"], descriptors["2026b"]["response_meta_sha256"])
            self.assertEqual(
                current_entry["descriptor_config"]["sha256"],
                spcc.sha256_file(config_path),
            )

            validation_manifest = {
                "schema_version": "spcc-validation-data-manifest-v1",
                "updated_at_utc": "2026-05-12T00:00:00Z",
                "purpose": "unit test",
                "fixture_catalogs": [],
                "response_repositories": entries["response_repositories"],
                "gaia_validation_artifacts": [],
                "support_envelope_reports": [],
                "real_image_reference_sets": [],
                "external_comparison_outputs": [],
                "packaged_qa_reports": [],
            }
            validation_manifest_path = temp_dir / "spcc-validation-data-manifest.json"
            validation_manifest_path.write_text(json.dumps(validation_manifest), encoding="utf-8")
            self.run_tool("tools/validate_spcc_validation_manifest.py", str(validation_manifest_path))

    def test_validation_manifest_validator_accepts_checked_in_manifest(self) -> None:
        self.run_tool(
            "tools/validate_spcc_validation_manifest.py",
            "docs/dev/spcc/spcc-validation-data-manifest.json",
        )

    def test_validation_manifest_release_ready_mode_reports_missing_production_evidence(self) -> None:
        result = self.run_tool(
            "tools/validate_spcc_validation_manifest.py",
            "docs/dev/spcc/spcc-validation-data-manifest.json",
            "--release-ready",
            check=False,
        )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("release-ready manifest requires", result.stderr)

    def test_validation_manifest_release_ready_mode_accepts_complete_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            manifest_path = temp_dir / "spcc-validation-data-manifest.json"
            metrics_hash = spcc.sha256_bytes(b"production support envelope metrics")
            report_hash = spcc.sha256_bytes(b"production support envelope report")
            image_hash = spcc.sha256_bytes(b"production linear image reference")
            comparison_hash = spcc.sha256_bytes(b"production external comparison output")
            manifest = {
                "schema_version": "spcc-validation-data-manifest-v1",
                "updated_at_utc": "2026-05-12T00:00:00Z",
                "purpose": "unit test",
                "fixture_catalogs": [],
                "response_repositories": [
                    {
                        "id": "spcc-response-production-2026a",
                        "generator": "tools/normalize_spcc_response_repository.py",
                        "validation_command": (
                            "python tools/validate_spcc_response_repository.py <repo> --strict-production-review"
                        ),
                        "license": "reviewed-production-license",
                        "rights_review_status": "approved-for-managed-download",
                        "expected_metrics": {"minimum_throughput_profiles": 1},
                        "tolerance_version": "spcc-response-production-v1",
                    }
                ],
                "gaia_validation_artifacts": [],
                "support_envelope_reports": [
                    {
                        "id": "sampled-xp-production-envelope-2026a",
                        "status": "production-ready",
                        "catalog_family_id": "spcc_gaia_dr3_xp_sampled",
                        "catalog_manifest_ids": ["spcc-gaia-dr3-xp-sampled-full-2026a"],
                        "rights_review_status": "approved-for-managed-download",
                        "validation_metrics": {
                            "path": "validation/spcc-support-metrics-2026a.json",
                            "sha256": metrics_hash,
                            "schema_version": "spcc-support-envelope-validation-v1",
                        },
                        "go_no_go_report": {
                            "path": "validation/spcc-support-go-no-go-2026a.json",
                            "sha256": report_hash,
                            "schema_version": "spcc-support-envelope-go-no-go-v1",
                        },
                        "expected_catalog_statuses": {
                            "spcc-gaia-dr3-xp-sampled-full-2026a": "production_ready",
                        },
                        "validation_command": (
                            "python tools/check_spcc_support_envelope.py <catalog> --validation-metrics "
                            "validation/spcc-support-metrics-2026a.json --output-json "
                            "validation/spcc-support-go-no-go-2026a.json"
                        ),
                        "tolerance_version": "spcc-support-envelope-v1",
                    }
                ],
                "real_image_reference_sets": [
                    {
                        "id": "broadband-validation-production",
                        "status": "validated",
                        "source_url_or_acquisition_path": "validation/broadband-production",
                        "license": "reviewed-production-license",
                        "rights_review_status": "approved-for-managed-download",
                        "target_name": "Production broadband validation field",
                        "camera_or_sensor": "Reviewed OSC Sensor",
                        "field_class": "mixed-fov",
                        "files": [
                            {
                                "path": "images/broadband-validation.fit",
                                "role": "linear-rgb-input",
                                "sha256": image_hash,
                            }
                        ],
                        "expected_metrics": {
                            "minimum_accepted_samples": 35,
                            "maximum_median_color_residual": 0.05,
                        },
                        "tolerance_version": "spcc-real-image-v1",
                    }
                ],
                "external_comparison_outputs": [
                    {
                        "id": "broadband-validation-reference",
                        "reference_set_id": "broadband-validation-production",
                        "tool": "reference-tool",
                        "tool_version": "reference-version",
                        "run_command_or_notes": "SPCC reference run notes",
                        "license": "reviewed-production-license",
                        "rights_review_status": "approved-for-managed-download",
                        "output_files": [
                            {
                                "path": "external/reference/broadband-validation.fit",
                                "role": "calibrated-output",
                                "sha256": comparison_hash,
                            }
                        ],
                        "expected_metrics": {"maximum_gain_delta": 0.03},
                        "tolerance_version": "spcc-external-comparison-v1",
                    }
                ],
                "packaged_qa_reports": [self._packaged_qa_report_entry()],
            }
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

            self.run_tool(
                "tools/validate_spcc_validation_manifest.py",
                str(manifest_path),
                "--release-ready",
            )

    def test_release_readiness_runner_reports_missing_required_evidence(self) -> None:
        result = self.run_tool("tools/check_spcc_release_readiness.py", "--no-token", check=False)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("FAIL: validation manifest release evidence", result.stdout)
        self.assertIn("non-empty catalog mirror config is required", result.stdout)
        self.assertIn("non-empty response repository config is required", result.stdout)
        self.assertIn("at least one reviewed response source manifest is required", result.stdout)
        self.assertIn("at least one reviewed real-image validation source manifest is required", result.stdout)

    def test_release_readiness_runner_accepts_complete_fixture_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            catalog_dir = temp_dir / "catalog"
            catalog_dataset = temp_dir / "catalog_dataset"
            response_source_dir = temp_dir / "response_source"
            response_source_dir.mkdir()
            response_repo = temp_dir / "response_repo"
            response_dataset = temp_dir / "response_dataset"
            real_image_source_dir = temp_dir / "real_image_validation"
            real_image_source_dir.mkdir()
            validation_manifest_path = temp_dir / "spcc-validation-data-manifest.json"

            self.run_tool(
                "tools/generate_spcc_catalog.py",
                str(catalog_dir),
                "--fixture",
                "--fixture-source-count",
                "90",
                "--manifest-id",
                "spcc-fixture-release-ready-2026a",
                "--rights-review-status",
                "approved-for-managed-download",
            )
            self.run_tool(
                "tools/build_spcc_hf_dataset.py",
                str(catalog_dataset),
                str(catalog_dir),
                "--clean",
                "--direct-base-url",
                catalog_dataset.resolve().as_uri(),
            )

            self._write_curve(response_source_dir / "sensor_r.csv", [(400.0, 0.1), (500.0, 0.8), (700.0, 0.2)])
            self._write_curve(response_source_dir / "white_d65.csv", [(400.0, 0.6), (500.0, 1.0), (700.0, 0.7)])
            response_source_manifest = self._write_reviewed_response_source_manifest(response_source_dir)
            self.run_tool(
                "tools/normalize_spcc_response_repository.py",
                str(response_source_manifest),
                str(response_repo),
                "--strict-production-review",
            )
            self.run_tool(
                "tools/build_spcc_response_repository_dataset.py",
                str(response_dataset),
                str(response_repo),
                "--clean",
                "--direct-base-url",
                response_dataset.resolve().as_uri(),
            )
            real_image_source_manifest = self._write_reviewed_real_image_validation_source_manifest(
                real_image_source_dir
            )

            validation_manifest_path.write_text(
                json.dumps(
                    self._complete_release_validation_manifest("spcc-fixture-release-ready-2026a"),
                    sort_keys=True,
                ),
                encoding="utf-8",
            )

            result = self.run_tool(
                "tools/check_spcc_release_readiness.py",
                "--validation-manifest",
                str(validation_manifest_path),
                "--catalog-mirror-config",
                str(catalog_dataset / "spcc_catalog_mirrors.json"),
                "--response-repository-config",
                str(response_dataset / "spcc_response_repositories.json"),
                "--response-source-manifest",
                str(response_source_manifest),
                "--real-image-validation-source-manifest",
                str(real_image_source_manifest),
                "--chunk-policy",
                "first",
                "--no-token",
            )

            self.assertIn("PASS: validation manifest release evidence", result.stdout)
            self.assertIn("PASS: catalog mirror config", result.stdout)
            self.assertIn("PASS: response repository config", result.stdout)
            self.assertIn("PASS: reviewed response source manifests", result.stdout)
            self.assertIn("PASS: reviewed real-image validation source manifests", result.stdout)

    def test_real_image_validation_entry_builder_writes_manifest_entries(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            source_manifest = self._write_reviewed_real_image_validation_source_manifest(temp_dir)
            output_json = temp_dir / "real_image_manifest_entries.json"

            self.run_tool(
                "tools/build_spcc_real_image_validation_entries.py",
                str(source_manifest),
                str(output_json),
                "--strict-production-review",
            )

            entries = json.loads(output_json.read_text(encoding="utf-8"))
            self.assertEqual(entries["schema_version"], "spcc-real-image-validation-manifest-entries-v1")
            self.assertEqual(len(entries["real_image_reference_sets"]), 1)
            self.assertEqual(len(entries["external_comparison_outputs"]), 1)
            real_image_file = entries["real_image_reference_sets"][0]["files"][0]
            external_file = entries["external_comparison_outputs"][0]["output_files"][0]
            self.assertEqual(real_image_file["path"], "images/release-broadband-reference.fit")
            self.assertEqual(real_image_file["sha256"], spcc.sha256_bytes(b"release real image"))
            self.assertEqual(external_file["sha256"], spcc.sha256_bytes(b"release external comparison"))

            validation_manifest = {
                "schema_version": "spcc-validation-data-manifest-v1",
                "updated_at_utc": "2026-05-12T00:00:00Z",
                "purpose": "unit test",
                "fixture_catalogs": [],
                "response_repositories": [],
                "gaia_validation_artifacts": [],
                "support_envelope_reports": [],
                "real_image_reference_sets": entries["real_image_reference_sets"],
                "external_comparison_outputs": entries["external_comparison_outputs"],
                "packaged_qa_reports": [],
            }
            validation_manifest_path = temp_dir / "spcc-validation-data-manifest.json"
            validation_manifest_path.write_text(json.dumps(validation_manifest), encoding="utf-8")
            self.run_tool(
                "tools/validate_spcc_validation_manifest.py",
                str(validation_manifest_path),
                "--strict-production-review",
            )

    def test_real_image_validation_entry_builder_rejects_placeholders_and_unknown_reference(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            (temp_dir / "images").mkdir()
            (temp_dir / "external" / "reference").mkdir(parents=True)
            (temp_dir / "images" / "reference.fit").write_bytes(b"reference image")
            (temp_dir / "external" / "reference" / "reference.fit").write_bytes(b"external output")
            source_manifest = temp_dir / "real_image_validation_source_manifest.json"
            source_manifest.write_text(
                json.dumps(
                    {
                        "schema_version": "spcc-real-image-validation-source-v1",
                        "real_image_reference_sets": [
                            {
                                "id": "placeholder-reference",
                                "status": "validated",
                                "source_url_or_acquisition_path": "local://unit-test/reference",
                                "license": "reviewed-license",
                                "rights_review_status": "approved-for-managed-download",
                                "target_name": "TODO reviewed field",
                                "camera_or_sensor": "Reviewed OSC Sensor",
                                "field_class": "small-fov",
                                "files": [
                                    {
                                        "path": "images/reference.fit",
                                        "role": "linear-rgb-input",
                                    }
                                ],
                                "expected_metrics": {"minimum_accepted_samples": 20},
                                "tolerance_version": "spcc-real-image-v1",
                            }
                        ],
                        "external_comparison_outputs": [
                            {
                                "id": "unknown-reference-output",
                                "reference_set_id": "missing-reference",
                                "tool": "reference-tool",
                                "tool_version": "reference-version",
                                "run_command_or_notes": "SPCC reference run notes",
                                "license": "reviewed-license",
                                "rights_review_status": "approved-for-managed-download",
                                "output_files": [
                                    {
                                        "path": "external/reference/reference.fit",
                                        "role": "calibrated-output",
                                    }
                                ],
                                "expected_metrics": {"maximum_gain_delta": 0.03},
                                "tolerance_version": "spcc-external-comparison-v1",
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )

            result = self.run_tool(
                "tools/build_spcc_real_image_validation_entries.py",
                str(source_manifest),
                str(temp_dir / "entries.json"),
                "--strict-production-review",
                check=False,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("unresolved placeholder", result.stderr)
            self.assertIn("unknown real image set", result.stderr)

    def test_release_readiness_runner_rejects_real_image_manifest_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            source_manifest = self._write_reviewed_real_image_validation_source_manifest(temp_dir)
            validation_manifest = self._complete_release_validation_manifest("spcc-fixture-release-ready-2026a")
            validation_manifest["real_image_reference_sets"][0]["files"][0]["sha256"] = spcc.sha256_bytes(
                b"stale real-image checksum"
            )
            validation_manifest_path = temp_dir / "spcc-validation-data-manifest.json"
            validation_manifest_path.write_text(json.dumps(validation_manifest), encoding="utf-8")

            result = self.run_tool(
                "tools/check_spcc_release_readiness.py",
                "--validation-manifest",
                str(validation_manifest_path),
                "--real-image-validation-source-manifest",
                str(source_manifest),
                "--no-token",
                check=False,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("reviewed real-image validation source manifests", result.stdout)
            self.assertIn("does not match entries generated", result.stdout)

    def test_validation_manifest_entry_merger_combines_generated_sections(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            base_manifest_path = temp_dir / "base-validation-manifest.json"
            support_entry_path = temp_dir / "support_manifest_entry.json"
            response_entries_path = temp_dir / "response_manifest_entries.json"
            real_image_entries_path = temp_dir / "real_image_manifest_entries.json"
            packaged_qa_entries_path = temp_dir / "packaged_qa_manifest_entries.json"
            merged_manifest_path = temp_dir / "merged-validation-manifest.json"

            base_manifest = {
                "schema_version": "spcc-validation-data-manifest-v1",
                "updated_at_utc": "2026-05-12T00:00:00Z",
                "purpose": "unit test",
                "fixture_catalogs": [],
                "response_repositories": [],
                "gaia_validation_artifacts": [],
                "support_envelope_reports": [],
                "real_image_reference_sets": [],
                "external_comparison_outputs": [],
                "packaged_qa_reports": [],
            }
            release_manifest = self._complete_release_validation_manifest("spcc-fixture-release-ready-2026a")
            base_manifest_path.write_text(json.dumps(base_manifest), encoding="utf-8")
            support_entry_path.write_text(
                json.dumps(release_manifest["support_envelope_reports"][0]),
                encoding="utf-8",
            )
            response_entries_path.write_text(
                json.dumps(
                    {
                        "schema_version": "spcc-response-validation-manifest-entries-v1",
                        "response_repositories": release_manifest["response_repositories"],
                    }
                ),
                encoding="utf-8",
            )
            packaged_qa_entries_path.write_text(
                json.dumps({"packaged_qa_reports": release_manifest["packaged_qa_reports"]}),
                encoding="utf-8",
            )

            source_manifest = self._write_reviewed_real_image_validation_source_manifest(temp_dir)
            self.run_tool(
                "tools/build_spcc_real_image_validation_entries.py",
                str(source_manifest),
                str(real_image_entries_path),
                "--strict-production-review",
            )

            self.run_tool(
                "tools/merge_spcc_validation_manifest_entries.py",
                str(base_manifest_path),
                str(response_entries_path),
                str(support_entry_path),
                str(real_image_entries_path),
                str(packaged_qa_entries_path),
                "--output",
                str(merged_manifest_path),
                "--release-ready",
            )

            merged = json.loads(merged_manifest_path.read_text(encoding="utf-8"))
            self.assertEqual(len(merged["response_repositories"]), 1)
            self.assertEqual(len(merged["support_envelope_reports"]), 1)
            self.assertEqual(len(merged["real_image_reference_sets"]), 1)
            self.assertEqual(len(merged["external_comparison_outputs"]), 1)
            self.assertEqual(len(merged["packaged_qa_reports"]), 1)
            self.run_tool(
                "tools/validate_spcc_validation_manifest.py",
                str(merged_manifest_path),
                "--release-ready",
            )

    def test_validation_manifest_entry_merger_rejects_duplicate_incoming_ids(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            base_manifest_path = temp_dir / "base-validation-manifest.json"
            first_entries_path = temp_dir / "response_manifest_entries_a.json"
            second_entries_path = temp_dir / "response_manifest_entries_b.json"
            merged_manifest_path = temp_dir / "merged-validation-manifest.json"

            base_manifest = {
                "schema_version": "spcc-validation-data-manifest-v1",
                "updated_at_utc": "2026-05-12T00:00:00Z",
                "purpose": "unit test",
                "fixture_catalogs": [],
                "response_repositories": [],
                "gaia_validation_artifacts": [],
                "support_envelope_reports": [],
                "real_image_reference_sets": [],
                "external_comparison_outputs": [],
                "packaged_qa_reports": [],
            }
            response_entry = self._complete_release_validation_manifest("spcc-fixture-release-ready-2026a")[
                "response_repositories"
            ][0]
            entries = {
                "schema_version": "spcc-response-validation-manifest-entries-v1",
                "response_repositories": [response_entry],
            }
            base_manifest_path.write_text(json.dumps(base_manifest), encoding="utf-8")
            first_entries_path.write_text(json.dumps(entries), encoding="utf-8")
            second_entries_path.write_text(json.dumps(entries), encoding="utf-8")

            result = self.run_tool(
                "tools/merge_spcc_validation_manifest_entries.py",
                str(base_manifest_path),
                str(first_entries_path),
                str(second_entries_path),
                "--output",
                str(merged_manifest_path),
                check=False,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("duplicate incoming response_repositories id", result.stderr)

    def test_hf_upload_instruction_writer_writes_final_commands(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            catalog_dataset = temp_dir / "catalog_dataset"
            response_dataset = temp_dir / "response_dataset"
            catalog_dataset.mkdir()
            response_dataset.mkdir()
            output_markdown = temp_dir / "spcc_hf_upload_instructions.md"
            self._write_upload_instruction_fixture_datasets(catalog_dataset, response_dataset)

            self.run_tool(
                "tools/write_spcc_hf_upload_instructions.py",
                "--catalog-dataset-dir",
                str(catalog_dataset),
                "--response-dataset-dir",
                str(response_dataset),
                "--response-source-manifest",
                "reviewed-response-source.json",
                "--real-image-validation-source-manifest",
                "reviewed-real-image-source.json",
                "--response-manifest-entries",
                "response-entries.json",
                "--support-envelope-manifest-entry",
                "support-entry.json",
                "--real-image-manifest-entries",
                "real-image-entries.json",
                "--merged-validation-manifest",
                "merged-validation-manifest.json",
                "--output-markdown",
                str(output_markdown),
            )

            instructions = output_markdown.read_text(encoding="utf-8")
            self.assertIn("hf upload emruiz81/afternight-spcc-catalogs", instructions)
            self.assertIn("hf upload emruiz81/afternight-spcc-responses", instructions)
            self.assertIn(
                "https://huggingface.co/datasets/emruiz81/afternight-spcc-catalogs/resolve/release-2026a/spcc_dataset_manifest.json",
                instructions,
            )
            self.assertIn("tools/merge_spcc_validation_manifest_entries.py", instructions)
            self.assertIn("tools/check_spcc_release_readiness.py", instructions)
            self.assertIn("reviewed-response-source.json", instructions)
            self.assertIn("reviewed-real-image-source.json", instructions)

    def test_hf_upload_instruction_writer_rejects_empty_descriptor_configs(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            catalog_dataset = temp_dir / "catalog_dataset"
            response_dataset = temp_dir / "response_dataset"
            catalog_dataset.mkdir()
            response_dataset.mkdir()
            self._write_upload_instruction_fixture_datasets(catalog_dataset, response_dataset)
            config = json.loads((catalog_dataset / "spcc_catalog_mirrors.json").read_text(encoding="utf-8"))
            config["catalog_packs"] = []
            (catalog_dataset / "spcc_catalog_mirrors.json").write_text(json.dumps(config), encoding="utf-8")

            result = self.run_tool(
                "tools/write_spcc_hf_upload_instructions.py",
                "--catalog-dataset-dir",
                str(catalog_dataset),
                "--response-dataset-dir",
                str(response_dataset),
                check=False,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("spcc_catalog_mirrors.json must contain a non-empty catalog_packs array", result.stderr)

    def test_validation_manifest_requires_real_image_checksums_and_metric_contracts(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            manifest_path = temp_dir / "spcc-validation-data-manifest.json"
            file_hash = spcc.sha256_bytes(b"synthetic reference image placeholder")
            output_hash = spcc.sha256_bytes(b"synthetic external output placeholder")

            manifest = {
                "schema_version": "spcc-validation-data-manifest-v1",
                "updated_at_utc": "2026-05-11T00:00:00Z",
                "purpose": "unit test",
                "fixture_catalogs": [],
                "response_repositories": [],
                "gaia_validation_artifacts": [],
                "support_envelope_reports": [],
                "real_image_reference_sets": [
                    {
                        "id": "broadband-small-fov-fixture",
                        "status": "ready-for-private-validation",
                        "source_url_or_acquisition_path": "local://validation/broadband-small-fov",
                        "license": "private-validation",
                        "rights_review_status": "private-local",
                        "target_name": "Synthetic M31 crop",
                        "camera_or_sensor": "Unit Test OSC",
                        "field_class": "small-fov",
                        "files": [
                            {
                                "path": "images/m31-small-fov.fit",
                                "role": "linear-rgb-input",
                                "sha256": file_hash,
                            }
                        ],
                        "expected_metrics": {
                            "minimum_accepted_samples": 20,
                            "maximum_median_color_residual": 0.05,
                        },
                        "tolerance_version": "spcc-real-image-v1",
                    }
                ],
                "external_comparison_outputs": [
                    {
                        "id": "broadband-small-fov-reference",
                        "reference_set_id": "broadband-small-fov-fixture",
                        "tool": "reference-tool",
                        "tool_version": "reference-version",
                        "run_command_or_notes": "SPCC reference run notes",
                        "license": "private-validation",
                        "rights_review_status": "private-local",
                        "output_files": [
                            {
                                "path": "external/reference/m31-small-fov.fit",
                                "role": "calibrated-output",
                                "sha256": output_hash,
                            }
                        ],
                        "expected_metrics": {
                            "maximum_gain_delta": 0.03,
                        },
                        "tolerance_version": "spcc-external-comparison-v1",
                    }
                ],
                "packaged_qa_reports": [],
            }
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

            self.run_tool("tools/validate_spcc_validation_manifest.py", str(manifest_path))

            manifest["real_image_reference_sets"][0]["files"][0]["sha256"] = "missing"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

            result = self.run_tool("tools/validate_spcc_validation_manifest.py", str(manifest_path), check=False)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("sha256", result.stderr)

    def test_validation_manifest_rejects_external_output_without_known_real_image_set(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            manifest_path = temp_dir / "spcc-validation-data-manifest.json"
            output_hash = spcc.sha256_bytes(b"synthetic external output placeholder")
            manifest = {
                "schema_version": "spcc-validation-data-manifest-v1",
                "updated_at_utc": "2026-05-11T00:00:00Z",
                "purpose": "unit test",
                "fixture_catalogs": [],
                "response_repositories": [],
                "gaia_validation_artifacts": [],
                "support_envelope_reports": [],
                "real_image_reference_sets": [],
                "external_comparison_outputs": [
                    {
                        "id": "orphan-reference",
                        "reference_set_id": "missing-real-image-set",
                        "tool": "reference-tool",
                        "tool_version": "reference-version",
                        "run_command_or_notes": "SPCC reference run notes",
                        "license": "private-validation",
                        "rights_review_status": "private-local",
                        "output_files": [
                            {
                                "path": "external/reference/orphan.fit",
                                "role": "calibrated-output",
                                "sha256": output_hash,
                            }
                        ],
                        "expected_metrics": {
                            "maximum_gain_delta": 0.03,
                        },
                        "tolerance_version": "spcc-external-comparison-v1",
                    }
                ],
                "packaged_qa_reports": [],
            }
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

            result = self.run_tool("tools/validate_spcc_validation_manifest.py", str(manifest_path), check=False)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("unknown real image set", result.stderr)

    def test_validation_manifest_requires_support_envelope_reports_section(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            manifest_path = temp_dir / "spcc-validation-data-manifest.json"
            manifest = {
                "schema_version": "spcc-validation-data-manifest-v1",
                "updated_at_utc": "2026-05-11T00:00:00Z",
                "purpose": "unit test",
                "fixture_catalogs": [],
                "response_repositories": [],
                "gaia_validation_artifacts": [],
                "real_image_reference_sets": [],
                "external_comparison_outputs": [],
                "packaged_qa_reports": [],
            }
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

            result = self.run_tool("tools/validate_spcc_validation_manifest.py", str(manifest_path), check=False)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("support_envelope_reports", result.stderr)

    def test_validation_manifest_validates_support_envelope_report_artifacts(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            manifest_path = temp_dir / "spcc-validation-data-manifest.json"
            metrics_hash = spcc.sha256_bytes(b"support envelope metrics placeholder")
            report_hash = spcc.sha256_bytes(b"support envelope report placeholder")
            manifest = {
                "schema_version": "spcc-validation-data-manifest-v1",
                "updated_at_utc": "2026-05-11T00:00:00Z",
                "purpose": "unit test",
                "fixture_catalogs": [],
                "response_repositories": [],
                "gaia_validation_artifacts": [],
                "real_image_reference_sets": [],
                "external_comparison_outputs": [],
                "packaged_qa_reports": [],
                "support_envelope_reports": [
                    {
                        "id": "sampled-xp-support-envelope-unit",
                        "status": "private-validation",
                        "catalog_family_id": "spcc_gaia_dr3_xp_sampled",
                        "catalog_manifest_ids": ["spcc-fixture-support-2026a"],
                        "rights_review_status": "private-local",
                        "validation_metrics": {
                            "path": "validation/spcc_support_metrics.json",
                            "sha256": metrics_hash,
                            "schema_version": "spcc-support-envelope-validation-v1",
                        },
                        "go_no_go_report": {
                            "path": "validation/spcc_support_go_no_go.json",
                            "sha256": report_hash,
                            "schema_version": "spcc-support-envelope-go-no-go-v1",
                        },
                        "expected_catalog_statuses": {
                            "spcc-fixture-support-2026a": "blocked_rights",
                        },
                        "validation_command": "python tools/check_spcc_support_envelope.py <catalog> --validation-metrics validation/spcc_support_metrics.json --output-json validation/spcc_support_go_no_go.json",
                        "tolerance_version": "spcc-support-envelope-v1",
                    }
                ],
            }
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

            self.run_tool("tools/validate_spcc_validation_manifest.py", str(manifest_path))

            manifest["support_envelope_reports"][0]["expected_catalog_statuses"]["spcc-fixture-support-2026a"] = (
                "looks-good"
            )
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            bad_status = self.run_tool("tools/validate_spcc_validation_manifest.py", str(manifest_path), check=False)

            self.assertNotEqual(bad_status.returncode, 0)
            self.assertIn("unsupported support-envelope status", bad_status.stderr)

            manifest["support_envelope_reports"][0]["expected_catalog_statuses"]["spcc-fixture-support-2026a"] = (
                "blocked_rights"
            )
            manifest["support_envelope_reports"][0]["validation_metrics"][
                "schema_version"
            ] = "spcc-support-envelope-validation-v0"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            bad_schema = self.run_tool("tools/validate_spcc_validation_manifest.py", str(manifest_path), check=False)

            self.assertNotEqual(bad_schema.returncode, 0)
            self.assertIn("validation_metrics schema_version", bad_schema.stderr)

    def test_validation_manifest_validates_packaged_qa_reports(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            manifest_path = temp_dir / "spcc-validation-data-manifest.json"
            manifest = {
                "schema_version": "spcc-validation-data-manifest-v1",
                "updated_at_utc": "2026-05-12T00:00:00Z",
                "purpose": "unit test",
                "fixture_catalogs": [],
                "response_repositories": [],
                "gaia_validation_artifacts": [],
                "support_envelope_reports": [],
                "real_image_reference_sets": [],
                "external_comparison_outputs": [],
                "packaged_qa_reports": [self._packaged_qa_report_entry()],
            }
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

            self.run_tool("tools/validate_spcc_validation_manifest.py", str(manifest_path))

            manifest["packaged_qa_reports"][0]["checklist"]["visual_qa_720p"] = "yes"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

            result = self.run_tool("tools/validate_spcc_validation_manifest.py", str(manifest_path), check=False)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("visual_qa_720p must be a boolean", result.stderr)

    def test_packaged_qa_manifest_entry_template_carries_required_checks(self) -> None:
        template_path = REPO_ROOT / "docs/dev/spcc/spcc-packaged-qa-manifest-entry.template.json"
        template = json.loads(template_path.read_text(encoding="utf-8"))

        self.assertIn("packaged_qa_reports", template)
        entry = template["packaged_qa_reports"][0]
        self.assertEqual(entry["status"], "not-run")
        self.assertEqual(entry["catalog_mirror_config"]["schema_version"], "spcc-catalog-mirror-config-v1")
        self.assertEqual(
            entry["response_repository_config"]["schema_version"],
            "spcc-response-repository-config-v1",
        )
        self.assertEqual(entry["qa_report"]["schema_version"], "spcc-packaged-qa-report-v1")
        for check_name in [
            "hosted_catalog_download_smoke",
            "hosted_response_download_smoke",
            "runtime_response_discovery",
            "runtime_response_checksum_validation",
            "catalog_repair_resume_cancel_smoke",
            "install_marker_eviction_smoke",
            "visual_qa_1080p",
            "visual_qa_720p",
        ]:
            self.assertIn(check_name, entry["checklist"])
            self.assertFalse(entry["checklist"][check_name])

    def _normalize_response_repository_fixture(self, temp_dir: Path, version: str) -> Path:
        source_dir = temp_dir / f"response_source_{version}"
        source_dir.mkdir()
        output_dir = temp_dir / f"normalized_responses_{version}"

        self._write_curve(source_dir / "sensor_r.csv", [(400.0, 0.1), (500.0, 0.8), (700.0, 0.2)])
        self._write_curve(source_dir / "white_d65.csv", [(400.0, 0.6), (500.0, 1.0), (700.0, 0.7)])

        manifest = {
            "repository_id": "spcc-response-fixture",
            "version": version,
            "rights": {
                "review_status": "approved-for-managed-download",
                "license_name": "synthetic-fixture",
            },
            "components": [
                {
                    "component_id": "sensor.synthetic.r",
                    "version": "1",
                    "role": "sensor-red-response",
                    "curve_csv": "sensor_r.csv",
                    "wavelength_unit": "nm",
                    "value_semantics": "dimensionless-throughput",
                    "review_status": "approved-for-managed-download",
                    "license_name": "synthetic-fixture",
                    "source_url": "local://synthetic/sensor-r",
                    "source_citation": "Synthetic sensor response fixture",
                    "attribution_text": "Synthetic response data generated for AfterNight tests.",
                    "transformation_notes": "Unit-test curve samples are normalized as authored.",
                }
            ],
            "white_references": [
                {
                    "white_reference_id": "white.synthetic.d65",
                    "version": "1",
                    "curve_csv": "white_d65.csv",
                    "wavelength_unit": "nm",
                    "value_semantics": "relative-sed",
                    "review_status": "approved-for-managed-download",
                    "license_name": "synthetic-fixture",
                    "source_url": "local://synthetic/white-d65",
                    "source_citation": "Synthetic white reference fixture",
                    "attribution_text": "Synthetic white-reference data generated for AfterNight tests.",
                    "transformation_notes": "Unit-test curve samples are normalized as authored.",
                }
            ],
            "throughput_profiles": [
                {
                    "profile_id": "profile.synthetic.rgb",
                    "version": "1",
                    "component_ids": ["sensor.synthetic.r"],
                    "white_reference_id": "white.synthetic.d65",
                    "compatible_channel_bases": ["camera-rgb", "linear-rgb"],
                    "models_raw_white_balance": False,
                    "models_camera_matrix": False,
                }
            ],
        }
        manifest_path = source_dir / "response_source_manifest.json"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

        self.run_tool(
            "tools/normalize_spcc_response_repository.py",
            str(manifest_path),
            str(output_dir),
        )
        return output_dir

    @staticmethod
    def _reviewed_response_source_asset(
        id_key: str,
        asset_id: str,
        role: str,
        curve_csv: str,
        value_semantics: str,
    ) -> dict[str, str]:
        return {
            id_key: asset_id,
            "version": "1",
            "role": role,
            "curve_csv": curve_csv,
            "wavelength_unit": "nm",
            "value_semantics": value_semantics,
            "review_status": "approved-for-managed-download",
            "license_name": "reviewed-license",
            "license_url": "https://example.test/license",
            "source_url": f"local://synthetic/{asset_id}",
            "source_citation": f"Synthetic reviewed fixture for {asset_id}",
            "attribution_text": "Synthetic response data generated for AfterNight tests.",
            "source_acquired_at_utc": "2026-05-12T00:00:00Z",
            "reviewer": "Unit Test",
            "reviewed_at_utc": "2026-05-12T00:00:00Z",
            "redistribution_permission": "allowed for managed download",
            "derivative_permission": "allowed for normalized response payloads",
            "transformation_notes": "Unit-test curve samples are normalized as authored.",
        }

    def _write_reviewed_response_source_manifest(self, source_dir: Path) -> Path:
        manifest = {
            "repository_id": "spcc-response-reviewed",
            "version": "2026a",
            "rights": {
                "review_status": "approved-for-managed-download",
                "license_name": "reviewed-license",
                "license_url": "https://example.test/license",
                "reviewer": "Unit Test",
                "reviewed_at_utc": "2026-05-12T00:00:00Z",
                "redistribution_permission": "allowed for managed download",
                "derivative_permission": "allowed for normalized response payloads",
                "required_attribution": "Synthetic reviewed fixture attribution.",
            },
            "components": [
                self._reviewed_response_source_asset(
                    "component_id",
                    "sensor.synthetic.r",
                    "sensor-red-response",
                    "sensor_r.csv",
                    "dimensionless-throughput",
                )
            ],
            "white_references": [
                self._reviewed_response_source_asset(
                    "white_reference_id",
                    "white.synthetic.d65",
                    "white-reference",
                    "white_d65.csv",
                    "relative-sed",
                )
            ],
            "throughput_profiles": [
                {
                    "profile_id": "profile.synthetic.rgb",
                    "version": "1",
                    "component_ids": ["sensor.synthetic.r"],
                    "white_reference_id": "white.synthetic.d65",
                    "compatible_channel_bases": ["linear-rgb"],
                }
            ],
        }
        manifest_path = source_dir / "response_source_manifest.json"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        return manifest_path

    @staticmethod
    def _write_reviewed_real_image_validation_source_manifest(source_dir: Path) -> Path:
        (source_dir / "images").mkdir(parents=True, exist_ok=True)
        (source_dir / "external" / "reference").mkdir(parents=True, exist_ok=True)
        (source_dir / "images" / "release-broadband-reference.fit").write_bytes(b"release real image")
        (source_dir / "external" / "reference" / "release-broadband-reference.fit").write_bytes(
            b"release external comparison"
        )

        manifest = {
            "schema_version": "spcc-real-image-validation-source-v1",
            "real_image_reference_sets": [
                {
                    "id": "release-broadband-reference",
                    "status": "validated",
                    "source_url_or_acquisition_path": "validation/release-broadband-reference",
                    "license": "reviewed-license",
                    "rights_review_status": "approved-for-managed-download",
                    "target_name": "Release validation field",
                    "camera_or_sensor": "Reviewed OSC Sensor",
                    "field_class": "mixed-fov",
                    "files": [
                        {
                            "path": "images/release-broadband-reference.fit",
                            "role": "linear-rgb-input",
                        }
                    ],
                    "expected_metrics": {
                        "minimum_accepted_samples": 35,
                        "maximum_median_color_residual": 0.05,
                    },
                    "tolerance_version": "spcc-real-image-v1",
                }
            ],
            "external_comparison_outputs": [
                {
                    "id": "release-broadband-reference",
                    "reference_set_id": "release-broadband-reference",
                    "tool": "reference-tool",
                    "tool_version": "reference-version",
                    "run_command_or_notes": "SPCC reference run notes",
                    "license": "reviewed-license",
                    "rights_review_status": "approved-for-managed-download",
                    "output_files": [
                        {
                            "path": "external/reference/release-broadband-reference.fit",
                            "role": "calibrated-output",
                        }
                    ],
                    "expected_metrics": {"maximum_gain_delta": 0.03},
                    "tolerance_version": "spcc-external-comparison-v1",
                }
            ],
        }
        manifest_path = source_dir / "real_image_validation_source_manifest.json"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        return manifest_path

    @staticmethod
    def _write_spcc_response_fixture_database(root: Path) -> None:
        (root / "osc_sensors").mkdir(parents=True, exist_ok=True)
        (root / "osc_filters").mkdir(parents=True, exist_ok=True)
        (root / "mono_sensors").mkdir(parents=True, exist_ok=True)
        (root / "mono_filters").mkdir(parents=True, exist_ok=True)
        (root / "wb_refs").mkdir(parents=True, exist_ok=True)
        (root / "LICENSE.md").write_text("GNU General Public License Version 3 fixture\n", encoding="utf-8")

        wavelength = [400, 500, 600, 700]
        (root / "osc_sensors" / "Toy_OSC_Sensor.json").write_text(
            json.dumps(
                [
                    {
                        "model": "Toy OSC Sensor",
                        "name": "Toy OSC Sensor Red",
                        "type": "OSC_SENSOR",
                        "dataQualityMarker": 4,
                        "dataSource": "Unit test OEM tabulated sensor data",
                        "manufacturer": "Toy Cameras",
                        "version": 1,
                        "channel": "RED",
                        "units": "nm",
                        "range": 100,
                        "wavelength": wavelength,
                        "values": [5, 65, 92, 30],
                    },
                    {
                        "model": "Toy OSC Sensor",
                        "name": "Toy OSC Sensor Green",
                        "type": "OSC_SENSOR",
                        "dataQualityMarker": 4,
                        "dataSource": "Unit test OEM tabulated sensor data",
                        "manufacturer": "Toy Cameras",
                        "version": 1,
                        "channel": "GREEN",
                        "units": "nm",
                        "range": 100,
                        "wavelength": wavelength,
                        "values": [20, 88, 72, 8],
                    },
                    {
                        "model": "Toy OSC Sensor",
                        "name": "Toy OSC Sensor Blue",
                        "type": "OSC_SENSOR",
                        "dataQualityMarker": 4,
                        "dataSource": "Unit test OEM tabulated sensor data",
                        "manufacturer": "Toy Cameras",
                        "version": 1,
                        "channel": "BLUE",
                        "units": "nm",
                        "range": 100,
                        "wavelength": wavelength,
                        "values": [80, 70, 15, 2],
                    },
                ]
            ),
            encoding="utf-8",
        )
        (root / "osc_filters" / "Toy_Broadband_Filter.json").write_text(
            json.dumps(
                [
                    {
                        "model": "Toy Broadband Filter",
                        "name": "Toy Broadband Filter",
                        "type": "OSC_FILTER",
                        "dataQualityMarker": 4,
                        "dataSource": "Unit test OSC filter data",
                        "manufacturer": "Toy Filters",
                        "version": 1,
                        "units": "nm",
                        "range": 100,
                        "wavelength": wavelength,
                        "values": [20, 90, 95, 80],
                    }
                ]
            ),
            encoding="utf-8",
        )
        (root / "osc_filters" / "Toy_DSLR_LPF.json").write_text(
            json.dumps(
                [
                    {
                        "model": "Toy DSLR LPF",
                        "name": "Toy DSLR LPF",
                        "type": "OSC_LPF",
                        "dataQualityMarker": 4,
                        "dataSource": "Unit test DSLR low-pass filter data",
                        "manufacturer": "Toy Filters",
                        "version": 1,
                        "units": "nm",
                        "range": 100,
                        "wavelength": wavelength,
                        "values": [15, 85, 96, 72],
                    }
                ]
            ),
            encoding="utf-8",
        )
        (root / "mono_sensors" / "Toy_Mono_Sensor.json").write_text(
            json.dumps(
                [
                    {
                        "model": "Toy Mono Sensor",
                        "name": "Toy Mono Sensor",
                        "type": "MONO_SENSOR",
                        "dataQualityMarker": 3,
                        "dataSource": "Unit test mono QE data",
                        "manufacturer": "Toy Cameras",
                        "version": 1,
                        "units": "nm",
                        "range": 1,
                        "wavelength": wavelength,
                        "values": [0.2, 0.8, 0.9, 0.4],
                    }
                ]
            ),
            encoding="utf-8",
        )
        (root / "mono_filters" / "Toy_RGB_Filters.json").write_text(
            json.dumps(
                [
                    {
                        "model": "Toy RGB Filters",
                        "name": "Toy Red Filter",
                        "type": "MONO_FILTER",
                        "dataQualityMarker": 4,
                        "dataSource": "Unit test OEM filter data",
                        "manufacturer": "Toy Filters",
                        "version": 1,
                        "channel": "RED",
                        "units": "nm",
                        "range": 100,
                        "wavelength": wavelength,
                        "values": [0, 15, 90, 95],
                    },
                    {
                        "model": "Toy RGB Filters",
                        "name": "Toy Green Filter",
                        "type": "MONO_FILTER",
                        "dataQualityMarker": 4,
                        "dataSource": "Unit test OEM filter data",
                        "manufacturer": "Toy Filters",
                        "version": 1,
                        "channel": "GREEN",
                        "units": "nm",
                        "range": 100,
                        "wavelength": wavelength,
                        "values": [5, 95, 65, 2],
                    },
                    {
                        "model": "Toy RGB Filters",
                        "name": "Toy Blue Filter",
                        "type": "MONO_FILTER",
                        "dataQualityMarker": 4,
                        "dataSource": "Unit test OEM filter data",
                        "manufacturer": "Toy Filters",
                        "version": 1,
                        "channel": "BLUE",
                        "units": "nm",
                        "range": 100,
                        "wavelength": wavelength,
                        "values": [90, 75, 4, 0],
                    },
                ]
            ),
            encoding="utf-8",
        )
        (root / "mono_filters" / "Toy_Clear_Filter.json").write_text(
            json.dumps(
                [
                    {
                        "model": "Toy Clear Filter",
                        "name": "Toy Clear Filter",
                        "type": "MONO_FILTER",
                        "dataQualityMarker": 4,
                        "dataSource": "Unit test all-channel filter data",
                        "manufacturer": "Toy Filters",
                        "version": 1,
                        "channel": "ALL",
                        "units": "nm",
                        "range": 100,
                        "wavelength": wavelength,
                        "values": [55, 90, 88, 60],
                    }
                ]
            ),
            encoding="utf-8",
        )
        (root / "wb_refs" / "Average_Spiral_Galaxy.json").write_text(
            json.dumps(
                [
                    {
                        "model": "Average Spiral Galaxy",
                        "name": "Average Spiral Galaxy",
                        "type": "WB_REF",
                        "dataQualityMarker": 3,
                        "dataSource": "Unit test SWIRE-like white reference",
                        "manufacturer": "Upstream",
                        "version": 1,
                        "units": "nm",
                        "range": 1,
                        "wavelength": wavelength,
                        "values": [0.6, 0.8, 1.0, 0.7],
                    }
                ]
            ),
            encoding="utf-8",
        )

    @staticmethod
    def _write_upload_instruction_fixture_datasets(catalog_dataset: Path, response_dataset: Path) -> None:
        publication_policy = {
            "channel": "stable",
            "published_at_utc": "2026-05-12T00:00:00Z",
            "update_policy": "immutable-versioned",
        }
        (catalog_dataset / "spcc_dataset_manifest.json").write_text(
            json.dumps(
                {
                    "schema_version": "spcc-hf-dataset-manifest-v1",
                    "dataset_repo_id": "emruiz81/afternight-spcc-catalogs",
                    "revision": "release-2026a",
                    "catalogs": [{"manifest_id": "spcc-gaia-dr3-xp-sampled-full-2026a"}],
                }
            ),
            encoding="utf-8",
        )
        (catalog_dataset / "spcc_catalog_mirrors.json").write_text(
            json.dumps(
                {
                    "schema_version": "spcc-catalog-mirror-config-v1",
                    "publication_policy": publication_policy,
                    "mirrors": [
                        "https://huggingface.co/datasets/emruiz81/afternight-spcc-catalogs/resolve/release-2026a"
                    ],
                    "catalog_packs": [{"id": "spcc-gaia-dr3-xp-sampled-full-2026a"}],
                }
            ),
            encoding="utf-8",
        )
        (response_dataset / "spcc_response_dataset_manifest.json").write_text(
            json.dumps(
                {
                    "schema_version": "spcc-response-dataset-manifest-v1",
                    "dataset_repo_id": "emruiz81/afternight-spcc-responses",
                    "revision": "release-2026a",
                    "response_repositories": [{"id": "spcc-response-baseline-2026a"}],
                }
            ),
            encoding="utf-8",
        )
        (response_dataset / "spcc_response_repositories.json").write_text(
            json.dumps(
                {
                    "schema_version": "spcc-response-repository-config-v1",
                    "publication_policy": publication_policy,
                    "mirrors": [
                        "https://huggingface.co/datasets/emruiz81/afternight-spcc-responses/resolve/release-2026a"
                    ],
                    "response_repositories": [{"id": "spcc-response-baseline-2026a"}],
                }
            ),
            encoding="utf-8",
        )

    @staticmethod
    def _packaged_qa_report_entry() -> dict[str, object]:
        return {
            "id": "spcc-packaged-qa-release",
            "status": "passed",
            "platform": "windows-msvc",
            "package_type": "installer",
            "build_configuration": "release",
            "build_artifact": "installer/AfterNight-setup-release.exe",
            "catalog_mirror_config": {
                "path": "spcc_catalog_mirrors.json",
                "sha256": spcc.sha256_bytes(b"catalog mirror config"),
                "schema_version": "spcc-catalog-mirror-config-v1",
            },
            "response_repository_config": {
                "path": "spcc_response_repositories.json",
                "sha256": spcc.sha256_bytes(b"response repository config"),
                "schema_version": "spcc-response-repository-config-v1",
            },
            "qa_report": {
                "path": "validation/spcc-packaged-qa-report.json",
                "sha256": spcc.sha256_bytes(b"packaged qa report"),
                "schema_version": "spcc-packaged-qa-report-v1",
            },
            "checklist": {
                "hosted_catalog_download_smoke": True,
                "hosted_response_download_smoke": True,
                "runtime_response_discovery": True,
                "runtime_response_checksum_validation": True,
                "catalog_repair_resume_cancel_smoke": True,
                "install_marker_eviction_smoke": True,
                "visual_qa_1080p": True,
                "visual_qa_720p": True,
            },
            "artifacts": [
                {
                    "path": "validation/screenshots/spcc-ready-1920x1080.png",
                    "role": "visual-qa-1080p",
                    "sha256": spcc.sha256_bytes(b"spcc 1080p screenshot"),
                },
                {
                    "path": "validation/screenshots/spcc-ready-1280x720.png",
                    "role": "visual-qa-720p",
                    "sha256": spcc.sha256_bytes(b"spcc 720p screenshot"),
                },
            ],
            "validation_command": "python tools/check_spcc_packaged_qa.py <qa-report>",
            "tolerance_version": "spcc-packaged-qa-v1",
        }

    @staticmethod
    def _complete_release_validation_manifest(catalog_manifest_id: str) -> dict[str, object]:
        return {
            "schema_version": "spcc-validation-data-manifest-v1",
            "updated_at_utc": "2026-05-12T00:00:00Z",
            "purpose": "unit test",
            "fixture_catalogs": [],
            "response_repositories": [
                {
                    "id": "spcc-response-reviewed-2026a",
                    "generator": "tools/normalize_spcc_response_repository.py",
                    "validation_command": (
                        "python tools/validate_spcc_response_repository.py <repo> --strict-production-review"
                    ),
                    "license": "reviewed-license",
                    "rights_review_status": "approved-for-managed-download",
                    "expected_metrics": {"minimum_throughput_profiles": 1},
                    "tolerance_version": "spcc-response-production-v1",
                }
            ],
            "gaia_validation_artifacts": [],
            "support_envelope_reports": [
                {
                    "id": "sampled-xp-support-envelope-release",
                    "status": "production-ready",
                    "catalog_family_id": spcc.CATALOG_FAMILY_ID,
                    "catalog_manifest_ids": [catalog_manifest_id],
                    "rights_review_status": "approved-for-managed-download",
                    "validation_metrics": {
                        "path": "validation/spcc-support-metrics-release.json",
                        "sha256": spcc.sha256_bytes(b"release support metrics"),
                        "schema_version": "spcc-support-envelope-validation-v1",
                    },
                    "go_no_go_report": {
                        "path": "validation/spcc-support-go-no-go-release.json",
                        "sha256": spcc.sha256_bytes(b"release support report"),
                        "schema_version": "spcc-support-envelope-go-no-go-v1",
                    },
                    "expected_catalog_statuses": {catalog_manifest_id: "production_ready"},
                    "validation_command": "python tools/check_spcc_support_envelope.py <catalog> --validation-metrics <metrics> --output-json <report>",
                    "tolerance_version": "spcc-support-envelope-v1",
                }
            ],
            "real_image_reference_sets": [
                {
                    "id": "release-broadband-reference",
                    "status": "validated",
                    "source_url_or_acquisition_path": "validation/release-broadband-reference",
                    "license": "reviewed-license",
                    "rights_review_status": "approved-for-managed-download",
                    "target_name": "Release validation field",
                    "camera_or_sensor": "Reviewed OSC Sensor",
                    "field_class": "mixed-fov",
                    "files": [
                        {
                            "path": "images/release-broadband-reference.fit",
                            "role": "linear-rgb-input",
                            "sha256": spcc.sha256_bytes(b"release real image"),
                        }
                    ],
                    "expected_metrics": {
                        "minimum_accepted_samples": 35,
                        "maximum_median_color_residual": 0.05,
                    },
                    "tolerance_version": "spcc-real-image-v1",
                }
            ],
            "external_comparison_outputs": [
                {
                    "id": "release-broadband-reference",
                    "reference_set_id": "release-broadband-reference",
                    "tool": "reference-tool",
                    "tool_version": "reference-version",
                    "run_command_or_notes": "SPCC reference run notes",
                    "license": "reviewed-license",
                    "rights_review_status": "approved-for-managed-download",
                    "output_files": [
                        {
                            "path": "external/reference/release-broadband-reference.fit",
                            "role": "calibrated-output",
                            "sha256": spcc.sha256_bytes(b"release external comparison"),
                        }
                    ],
                    "expected_metrics": {"maximum_gain_delta": 0.03},
                    "tolerance_version": "spcc-external-comparison-v1",
                }
            ],
            "packaged_qa_reports": [SpccCatalogToolTests._packaged_qa_report_entry()],
        }

    @staticmethod
    def _write_sample_spcc_jsonl(path: Path) -> None:
        rows = []
        for index in range(3):
            base_flux = 1.0e-16 * (1.0 + index * 0.1)
            rows.append(
                {
                    "source_id": 940000000000000000 + index,
                    "solution_id": "gaia-dr3",
                    "ra": 10.65 + index * 0.02,
                    "dec": 41.20 + index * 0.03,
                    "ref_epoch": 2016.0,
                    "pmra": 0.0,
                    "pmdec": 0.0,
                    "phot_g_mean_mag": 13.0 + index * 0.2,
                    "phot_bp_mean_mag": 13.4 + index * 0.25,
                    "phot_rp_mean_mag": 12.7 + index * 0.15,
                    "flux": [base_flux] * spcc.SAMPLED_XP_SAMPLE_COUNT,
                    "flux_error": [base_flux * 0.02] * spcc.SAMPLED_XP_SAMPLE_COUNT,
                    "quality_flags": 0,
                    "spectrum_source_kind": 1,
                }
            )
        with path.open("w", encoding="utf-8") as handle:
            for row in rows:
                handle.write(json.dumps(row, sort_keys=True, separators=(",", ":")) + "\n")

    @staticmethod
    def _source_id_for_healpix8(healpix8: int, ordinal: int) -> int:
        return ((healpix8 * 4**4) * 2**35) + ordinal

    @classmethod
    def _write_full_xp_sampled_export_fixture(cls, path: Path) -> None:
        rows = []
        specs = [
            (0, 1, 14.0),
            (0, 2, 12.0),
            (0, 3, 13.0),
            (1024, 1, 11.0),
            (1025, 1, 12.2),
        ]
        for index, (healpix8, ordinal, g_mag) in enumerate(specs):
            base_flux = 1.0e-16 * (1.0 + index * 0.1)
            rows.append(
                {
                    "source_id": cls._source_id_for_healpix8(healpix8, ordinal),
                    "solution_id": "gaia-dr3",
                    "ra": (10.0 + index * 0.1) % 360.0,
                    "dec": -5.0 + index * 0.2,
                    "ref_epoch": 2016.0,
                    "pmra": 0.0,
                    "pmdec": 0.0,
                    "phot_g_mean_mag": g_mag,
                    "phot_bp_mean_mag": g_mag + 0.35 + index * 0.1,
                    "phot_rp_mean_mag": g_mag - 0.25 - index * 0.05,
                    "flux": [base_flux] * spcc.SAMPLED_XP_SAMPLE_COUNT,
                    "flux_error": [base_flux * 0.02] * spcc.SAMPLED_XP_SAMPLE_COUNT,
                    "quality_flags": 0,
                    "spectrum_source_kind": 1,
                }
            )
        with path.open("w", encoding="utf-8") as handle:
            for row in rows:
                handle.write(json.dumps(row, sort_keys=True, separators=(",", ":")) + "\n")

    @classmethod
    def _write_full_xp_metadata_csv(cls, path: Path) -> None:
        fieldnames = list(dict.fromkeys(spcc.REQUIRED_GAIA_SOURCE_FIELDS + spcc.REQUIRED_XP_SUMMARY_FIELDS))
        rows = []
        specs = [
            (0, 1, 14.0),
            (0, 2, 12.0),
            (0, 3, 13.0),
            (1024, 1, 11.0),
            (1025, 1, 12.2),
        ]
        for index, (healpix8, ordinal, g_mag) in enumerate(specs):
            row = {
                "source_id": str(cls._source_id_for_healpix8(healpix8, ordinal)),
                "ra": f"{(10.0 + index * 0.1) % 360.0:.8f}",
                "dec": f"{-5.0 + index * 0.2:.8f}",
                "ref_epoch": "2016.0",
                "pmra": "" if index == 0 else "0.0",
                "pmdec": "" if index == 0 else "0.0",
                "phot_g_mean_mag": f"{g_mag:.3f}",
                "phot_bp_mean_mag": f"{g_mag + 0.35 + index * 0.1:.3f}",
                "phot_rp_mean_mag": f"{g_mag - 0.25 - index * 0.05:.3f}",
                "phot_g_mean_flux_over_error": "120.0",
                "phot_bp_mean_flux_over_error": "90.0",
                "phot_rp_mean_flux_over_error": "95.0",
                "phot_bp_rp_excess_factor": "1.2",
                "phot_bp_n_contaminated_transits": "0",
                "phot_bp_n_blended_transits": "0",
                "phot_rp_n_contaminated_transits": "0",
                "phot_rp_n_blended_transits": "0",
                "ruwe": "1.0",
                "duplicated_source": "false",
                "non_single_star": "0",
                "phot_variable_flag": "NOT_AVAILABLE",
                "ipd_frac_multi_peak": "0",
                "has_xp_sampled": "true",
                "bp_n_relevant_bases": "55",
                "bp_n_measurements": "50",
                "bp_n_rejected_measurements": "0",
                "bp_chi_squared": "1.0",
                "bp_n_transits": "20",
                "bp_n_contaminated_transits": "0",
                "bp_n_blended_transits": "0",
                "rp_n_relevant_bases": "55",
                "rp_n_measurements": "50",
                "rp_n_rejected_measurements": "0",
                "rp_chi_squared": "1.0",
                "rp_n_transits": "20",
                "rp_n_contaminated_transits": "0",
                "rp_n_blended_transits": "0",
            }
            rows.append(row)
        with path.open("w", encoding="utf-8", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(rows)

    @classmethod
    def _write_full_xp_spectra_csv_gz(cls, path: Path) -> None:
        specs = [
            (0, 1),
            (0, 2),
            (0, 3),
            (1024, 1),
            (1025, 1),
            (2048, 99),
        ]
        with gzip.open(path, "wt", encoding="utf-8", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=["source_id", "solution_id", "ra", "dec", "flux", "flux_error"])
            writer.writeheader()
            for index, (healpix8, ordinal) in enumerate(specs):
                base_flux = 1.0e-16 * (1.0 + index * 0.1)
                writer.writerow(
                    {
                        "source_id": str(cls._source_id_for_healpix8(healpix8, ordinal)),
                        "solution_id": "gaia-dr3",
                        "ra": f"{(10.0 + index * 0.1) % 360.0:.8f}",
                        "dec": f"{-5.0 + index * 0.2:.8f}",
                        "flux": json.dumps([base_flux] * spcc.SAMPLED_XP_SAMPLE_COUNT),
                        "flux_error": json.dumps([base_flux * 0.02] * spcc.SAMPLED_XP_SAMPLE_COUNT),
                    }
                )

    @staticmethod
    def _write_curve(path: Path, rows: list[tuple[float, float]]) -> None:
        with path.open("w", encoding="utf-8", newline="") as handle:
            writer = csv.writer(handle)
            writer.writerow(["wavelength_nm", "value"])
            writer.writerows(rows)

    @staticmethod
    def _packed_record_hash_by_source(chunk_path: Path, source_id: str) -> str:
        with chunk_path.open("rb") as handle:
            preamble = handle.read(spcc.CHUNK_PREAMBLE_STRUCT.size)
            _, _, _, header_size, record_count = spcc.CHUNK_PREAMBLE_STRUCT.unpack(preamble)
            handle.seek(header_size, 1)
            for _ in range(record_count):
                payload = handle.read(spcc.ANXP_RECORD_SIZE_BYTES)
                if str(spcc.unpack_record(payload)["source_id"]) == source_id:
                    return spcc.sha256_bytes(payload)
        raise AssertionError(f"source {source_id} not found")


if __name__ == "__main__":
    unittest.main()
