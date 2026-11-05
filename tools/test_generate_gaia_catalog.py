#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import struct
import subprocess
import sys
import tempfile
import unittest
import urllib.error
from pathlib import Path
from unittest.mock import MagicMock, patch

from tools import generate_gaia_catalog as generator


class GenerateGaiaCatalogTests(unittest.TestCase):
    def test_data_query_joins_astrophysical_parameters(self) -> None:
        query = generator.data_query_for(14.0, 4, 123)

        self.assertIn("FROM gaiadr3.gaia_source AS s", query)
        self.assertIn("LEFT JOIN gaiadr3.astrophysical_parameters AS ap", query)
        self.assertIn("ON s.source_id = ap.source_id", query)
        self.assertIn("ap.teff_gspphot", query)
        self.assertIn("ap.logg_gspphot", query)
        self.assertIn("ap.mh_gspphot", query)
        self.assertIn("ap.ag_gspphot", query)
        self.assertIn("ap.ebpminrp_gspphot", query)
        self.assertIn("WHERE s.phot_g_mean_mag <= 14.000000", query)
        self.assertIn("GAIA_HEALPIX_INDEX(4, s.source_id) = 123", query)

    def test_cli_writes_chunked_v3_catalog(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            input_csv = temp_dir / "gaia.csv"
            output_dir = temp_dir / "catalog"

            first_tile_star = self._make_star_for_tile_range(0, 255, 101)
            second_tile_star = self._make_star_for_tile_range(256, 511, 202)
            sentinel_star_row = self._make_star_for_tile_range(0, 255, 303)

            with input_csv.open("w", encoding="utf-8", newline="") as handle:
                writer = csv.DictWriter(
                    handle,
                    fieldnames=[
                        "source_id",
                        "ra",
                        "dec",
                        "phot_g_mean_mag",
                        "phot_bp_mean_mag",
                        "phot_rp_mean_mag",
                        "pmra",
                        "pmdec",
                        "teff_gspphot",
                        "logg_gspphot",
                        "mh_gspphot",
                        "ag_gspphot",
                        "ebpminrp_gspphot",
                    ],
                )
                writer.writeheader()
                writer.writerow(self._csv_row(first_tile_star))
                writer.writerow(self._csv_row(second_tile_star))
                writer.writerow(
                    {
                        "source_id": "303",
                        "ra": f"{sentinel_star_row['_ra']:.1f}",
                        "dec": f"{sentinel_star_row['_dec']:.1f}",
                        "phot_g_mean_mag": "13.1",
                        "phot_bp_mean_mag": "13.4",
                        "phot_rp_mean_mag": "12.8",
                        "pmra": "",
                        "pmdec": "",
                        "teff_gspphot": "",
                        "logg_gspphot": "",
                        "mh_gspphot": "",
                        "ag_gspphot": "",
                        "ebpminrp_gspphot": "",
                    }
                )

            result = subprocess.run(
                [
                    sys.executable,
                    "tools/generate_gaia_catalog.py",
                    str(input_csv),
                    str(output_dir),
                    "--preset",
                    "g14",
                ],
                cwd=Path(__file__).resolve().parents[1],
                capture_output=True,
                text=True,
            )

            if result.returncode != 0:
                self.fail(f"generator failed:\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}")

            meta_path = output_dir / "catalog_meta.json"
            self.assertTrue(meta_path.exists())

            meta = json.loads(meta_path.read_text(encoding="utf-8"))
            self.assertEqual(meta["format_version"], 3)
            self.assertEqual(meta["storage"], "chunks")
            self.assertEqual(meta["record_size_bytes"], 64)
            self.assertEqual(meta["catalog_id"], "gaia_dr3_g14")
            self.assertEqual(meta["chunk_count"], 48)
            self.assertNotIn("tile_directory", meta)
            self.assertEqual(len(meta["chunks"]), 48)
            self.assertEqual(meta["total_stars"], 3)

            chunk0 = meta["chunks"][0]
            chunk1 = meta["chunks"][1]
            self.assertEqual(chunk0["first_tile"], 0)
            self.assertEqual(chunk0["tile_count"], 256)
            self.assertEqual(chunk1["first_tile"], 256)
            self.assertEqual(chunk1["tile_count"], 256)

            self.assertFalse((output_dir / "healpix_order5").exists())
            self.assertEqual(list(output_dir.rglob("tile_*.bin")), [])

            self._assert_chunk_manifest(output_dir, chunk0)
            self._assert_chunk_manifest(output_dir, chunk1)

            parsed_chunk0 = self._read_chunk(output_dir / chunk0["file"])
            parsed_chunk1 = self._read_chunk(output_dir / chunk1["file"])

            self.assertEqual(parsed_chunk0["header"]["record_version"], 3)
            self.assertEqual(parsed_chunk0["header"]["record_size_bytes"], 64)
            self.assertEqual(parsed_chunk0["header"]["chunk_index"], 0)
            self.assertEqual(parsed_chunk0["header"]["tile_count"], 256)
            self.assertEqual(parsed_chunk1["header"]["chunk_index"], 1)
            self.assertEqual(parsed_chunk1["header"]["first_tile"], 256)

            tile0 = int(first_tile_star["_tile_index"])
            tile1 = int(second_tile_star["_tile_index"])
            chunk0_star = self._star_by_source_id(parsed_chunk0["tiles"][tile0]["stars"], 101)
            chunk1_star = self._star_by_source_id(parsed_chunk1["tiles"][tile1 - 256]["stars"], 202)

            self.assertEqual(chunk0_star[0], first_tile_star["_ra"])
            self.assertEqual(chunk0_star[1], first_tile_star["_dec"])
            self.assertAlmostEqual(chunk0_star[2], 11.2, places=5)
            self.assertAlmostEqual(chunk0_star[3], 11.5, places=5)
            self.assertAlmostEqual(chunk0_star[4], 10.9, places=5)
            self.assertAlmostEqual(chunk0_star[5], 1.25, places=5)
            self.assertAlmostEqual(chunk0_star[6], -0.5, places=5)
            self.assertEqual(chunk0_star[7], 101)
            self.assertEqual(chunk0_star[8:13], (5772, 444, -15, 1234, 567))
            self.assertEqual(chunk1_star[0], second_tile_star["_ra"])
            self.assertEqual(chunk1_star[1], second_tile_star["_dec"])
            self.assertAlmostEqual(chunk1_star[2], 12.0, places=5)
            self.assertAlmostEqual(chunk1_star[3], 12.4, places=5)
            self.assertAlmostEqual(chunk1_star[4], 11.7, places=5)
            self.assertAlmostEqual(chunk1_star[5], -2.0, places=5)
            self.assertAlmostEqual(chunk1_star[6], 3.5, places=5)
            self.assertEqual(chunk1_star[7], 202)
            self.assertEqual(chunk1_star[8:13], (4800, 390, -80, 4321, -250))

            sentinel_tile_index = int(sentinel_star_row["_tile_index"])
            sentinel_star = self._star_by_source_id(parsed_chunk0["tiles"][sentinel_tile_index]["stars"], 303)
            self.assertEqual(sentinel_star[0], sentinel_star_row["_ra"])
            self.assertEqual(sentinel_star[1], sentinel_star_row["_dec"])
            self.assertAlmostEqual(sentinel_star[2], 13.1, places=5)
            self.assertAlmostEqual(sentinel_star[3], 13.4, places=5)
            self.assertAlmostEqual(sentinel_star[4], 12.8, places=5)
            self.assertAlmostEqual(sentinel_star[5], 0.0, places=5)
            self.assertAlmostEqual(sentinel_star[6], 0.0, places=5)
            self.assertEqual(sentinel_star[7], 303)
            self.assertEqual(sentinel_star[8:13], (0, -32768, -32768, 65535, -32768))

    def test_missing_bp_rp_stores_nan(self) -> None:
        _, payload = generator.pack_row_for_tile(
            {
                "source_id": "1",
                "ra": "10.0",
                "dec": "5.0",
                "phot_g_mean_mag": "12.0",
                "phot_bp_mean_mag": "",
                "phot_rp_mean_mag": "",
                "pmra": "",
                "pmdec": "",
                "teff_gspphot": "",
                "logg_gspphot": "",
                "mh_gspphot": "",
                "ag_gspphot": "",
                "ebpminrp_gspphot": "",
            },
            generator.DEFAULT_COLUMNS,
            5,
        )
        unpacked = generator.STAR_STRUCT_V3.unpack(payload)
        self.assertTrue(math.isnan(unpacked[3]))  # bp_mag
        self.assertTrue(math.isnan(unpacked[4]))  # rp_mag

    def test_short_csv_row_is_skipped_not_fatal(self) -> None:
        kept_rows, skipped_rows = generator.write_rows_from_csv_text(
            (
                "source_id,ra,dec,phot_g_mean_mag,phot_bp_mean_mag,phot_rp_mean_mag,pmra,pmdec,"
                "teff_gspphot,logg_gspphot,mh_gspphot,ag_gspphot,ebpminrp_gspphot\n"
                "1,10.0,5.0,12.0,12.5,11.5,0.1,0.2,5000,4.2,-0.1,0.05,0.03\n"
                "2,11.0\n"
            ),
            generator.DEFAULT_COLUMNS,
            5,
            _CollectingTileWriter(),
        )
        self.assertEqual(kept_rows, 1)
        self.assertEqual(skipped_rows, 1)

    def test_out_of_range_astrophysical_uses_sentinel_not_skips_star(self) -> None:
        """A star with an out-of-range astrophysical value must be packed using
        the sentinel value rather than being discarded entirely."""
        _, payload = generator.pack_row_for_tile(
            {
                "source_id": "2",
                "ra": "10.0",
                "dec": "5.0",
                "phot_g_mean_mag": "11.0",
                "phot_bp_mean_mag": "11.5",
                "phot_rp_mean_mag": "10.5",
                "pmra": "0.0",
                "pmdec": "0.0",
                "teff_gspphot": "70000",  # exceeds uint16 max 65535
                "logg_gspphot": "4.0",
                "mh_gspphot": "0.0",
                "ag_gspphot": "0.1",
                "ebpminrp_gspphot": "0.05",
            },
            generator.DEFAULT_COLUMNS,
            5,
        )
        unpacked = generator.STAR_STRUCT_V3.unpack(payload)
        self.assertEqual(unpacked[8], generator.TEFF_SENTINEL)  # teff → sentinel
        self.assertAlmostEqual(float(unpacked[2]), 11.0, places=4)  # g_mag preserved

    def _assert_chunk_manifest(self, output_dir: Path, chunk_meta: dict[str, object]) -> None:
        chunk_path = output_dir / str(chunk_meta["file"])
        self.assertTrue(chunk_path.exists())
        self.assertEqual(chunk_path.stat().st_size, int(chunk_meta["size_bytes"]))

        sha256 = hashlib.sha256(chunk_path.read_bytes()).hexdigest()
        self.assertEqual(sha256, chunk_meta["sha256"])

    def _read_chunk(self, path: Path) -> dict[str, object]:
        data = path.read_bytes()
        magic, version, flags, json_length, reserved = struct.unpack_from("<4sHHII", data, 0)
        self.assertEqual(magic, b"ANSP")
        self.assertEqual(version, 1)
        self.assertEqual(flags, 0)
        self.assertEqual(reserved, 0)

        json_start = 16
        json_end = json_start + json_length
        header = json.loads(data[json_start:json_end].decode("utf-8"))
        index_start = (json_end + 7) & ~7
        tile_count = int(header["tile_count"])
        index_entry_struct = struct.Struct("<IQ")
        record_struct = struct.Struct("<ddfffffQHhhHh10x")
        star_data_start = index_start + tile_count * index_entry_struct.size

        tiles: list[dict[str, object]] = []
        for tile_offset in range(tile_count):
            star_count, data_offset = index_entry_struct.unpack_from(data, index_start + tile_offset * index_entry_struct.size)
            stars = []
            cursor = star_data_start + data_offset
            for _ in range(star_count):
                stars.append(record_struct.unpack_from(data, cursor))
                cursor += record_struct.size
            tiles.append({"star_count": star_count, "stars": stars})

        return {"header": header, "tiles": tiles}

    def _make_star_for_tile_range(self, tile_start: int, tile_end: int, source_id: int) -> dict[str, object]:
        for dec_deg in range(-85, 86, 5):
            for ra_deg in range(0, 360, 5):
                tile_index = generator.ang2pix_ring(5, float(ra_deg), float(dec_deg))
                if tile_start <= tile_index <= tile_end:
                    return {
                        "source_id": str(source_id),
                        "ra": f"{ra_deg:.1f}",
                        "dec": f"{dec_deg:.1f}",
                        "phot_g_mean_mag": "11.2" if source_id == 101 else "12.0",
                        "phot_bp_mean_mag": "11.5" if source_id == 101 else "12.4",
                        "phot_rp_mean_mag": "10.9" if source_id == 101 else "11.7",
                        "pmra": "1.25" if source_id == 101 else "-2.0",
                        "pmdec": "-0.5" if source_id == 101 else "3.5",
                        "teff_gspphot": "5772" if source_id == 101 else "4800",
                        "logg_gspphot": "4.44" if source_id == 101 else "3.90",
                        "mh_gspphot": "-0.15" if source_id == 101 else "-0.80",
                        "ag_gspphot": "0.1234" if source_id == 101 else "0.4321",
                        "ebpminrp_gspphot": "0.0567" if source_id == 101 else "-0.0250",
                        "_tile_index": tile_index,
                        "_ra": float(ra_deg),
                        "_dec": float(dec_deg),
                    }
        self.fail(f"could not find coordinate for tile range {tile_start}..{tile_end}")

    def _csv_row(self, row: dict[str, object]) -> dict[str, object]:
        return {key: value for key, value in row.items() if not key.startswith("_")}

    def _star_by_source_id(self, stars: list[tuple[object, ...]], source_id: int) -> tuple[object, ...]:
        for star in stars:
            if star[7] == source_id:
                return star
        self.fail(f"missing star with source_id={source_id}")


class _CollectingTileWriter:
    def __init__(self) -> None:
        self.entries: list[tuple[int, bytes]] = []

    def write(self, tile_index: int, payload: bytes) -> None:
        self.entries.append((tile_index, payload))


# ---------------------------------------------------------------------------
# ang2pix_ring cross-validation
# ---------------------------------------------------------------------------

class TestAng2PixRing(unittest.TestCase):
    """Cross-validates ang2pix_ring against expected tile indices.

    Reference values were independently computed using the canonical HEALPix
    algorithm and also match an::healpix::ang2pix_ring in the C++ engine.
    Any divergence silently mis-routes stars to wrong tiles.
    """

    # (order, ra_deg, dec_deg, expected_pixel)
    # Values are pinned from this implementation and must be cross-checked
    # against an::healpix::ang2pix_ring (C++) whenever the algorithm changes.
    REFERENCE: list[tuple[int, float, float, int]] = [
        (5,   0.0,   0.0,  6080),
        (5, 180.0,   0.0,  6144),
        (5,  45.0,   0.0,  6096),
        (5, 270.0,   0.0,  6176),
        (5,   0.0,  90.0,     0),
        (5,   0.0, -90.0, 12284),
        (5,  90.0,  45.0,  1770),
        (5, 120.0, -35.0,  9706),
        (5, 300.0,  70.0,   410),
        (1, 180.0,   0.0,    24),
        (1,   0.0,  90.0,     0),
        (1,   0.0, -90.0,    44),
    ]

    def test_reference_coordinates(self) -> None:
        for order, ra, dec, expected in self.REFERENCE:
            with self.subTest(order=order, ra=ra, dec=dec):
                self.assertEqual(generator.ang2pix_ring(order, ra, dec), expected)

    def test_ra_wraps_360_same_as_0(self) -> None:
        for order in (3, 5):
            with self.subTest(order=order):
                self.assertEqual(
                    generator.ang2pix_ring(order, 0.0, 30.0),
                    generator.ang2pix_ring(order, 360.0, 30.0),
                )

    def test_negative_ra_normalises(self) -> None:
        self.assertEqual(
            generator.ang2pix_ring(5, -45.0, 0.0),
            generator.ang2pix_ring(5, 315.0, 0.0),
        )

    def test_dec_beyond_poles_does_not_error(self) -> None:
        north = generator.ang2pix_ring(5, 0.0, 90.01)
        south = generator.ang2pix_ring(5, 0.0, -90.01)
        self.assertGreaterEqual(north, 0)
        self.assertGreaterEqual(south, 0)

    def test_all_pixels_in_valid_range(self) -> None:
        npix = generator.pixel_count_for_order(5)
        for ra in range(0, 360, 45):
            for dec in range(-90, 91, 30):
                pixel = generator.ang2pix_ring(5, float(ra), float(dec))
                self.assertGreaterEqual(pixel, 0, f"ra={ra} dec={dec}")
                self.assertLess(pixel, npix, f"ra={ra} dec={dec}")


# ---------------------------------------------------------------------------
# chunk_file_name zero-padding
# ---------------------------------------------------------------------------

class TestChunkFileName(unittest.TestCase):
    def test_48_chunks_two_digit_padding(self) -> None:
        self.assertEqual(generator.chunk_file_name("cat", 0, 48),  "cat_chunk_00.ansp")
        self.assertEqual(generator.chunk_file_name("cat", 47, 48), "cat_chunk_47.ansp")

    def test_192_chunks_three_digit_padding(self) -> None:
        self.assertEqual(generator.chunk_file_name("cat", 0, 192),   "cat_chunk_000.ansp")
        self.assertEqual(generator.chunk_file_name("cat", 191, 192), "cat_chunk_191.ansp")

    def test_minimum_width_is_2(self) -> None:
        # chunk_count=10 -> max index len is 1 digit, but minimum width 2
        self.assertEqual(generator.chunk_file_name("cat", 9, 10), "cat_chunk_09.ansp")


# ---------------------------------------------------------------------------
# Skipped-row warning limit
# ---------------------------------------------------------------------------

class TestSkipRowWarnings(unittest.TestCase):
    _COLUMNS = generator.DEFAULT_COLUMNS

    def _bad_csv(self, n_bad: int) -> str:
        header = (
            "source_id,ra,dec,phot_g_mean_mag,phot_bp_mean_mag,phot_rp_mean_mag,"
            "pmra,pmdec,teff_gspphot,logg_gspphot,mh_gspphot,ag_gspphot,ebpminrp_gspphot"
        )
        rows = [f"{i},bad_ra,0.0,9.5,9.9,9.2,0.0,0.0,,,,," for i in range(n_bad)]
        return header + "\n" + "\n".join(rows)

    def test_exactly_max_warnings_printed(self) -> None:
        import io as _io
        from contextlib import redirect_stdout

        n_bad = generator._MAX_SKIP_WARNINGS + 3
        out = _io.StringIO()
        with redirect_stdout(out):
            kept, skipped = generator.write_rows_from_csv_text(
                self._bad_csv(n_bad), self._COLUMNS, 5, _CollectingTileWriter()
            )
        warnings = [l for l in out.getvalue().splitlines() if l.startswith("Warning:")]
        self.assertEqual(skipped, n_bad)
        self.assertEqual(kept, 0)
        self.assertEqual(len(warnings), generator._MAX_SKIP_WARNINGS)

    def test_warning_includes_error_detail(self) -> None:
        import io as _io
        from contextlib import redirect_stdout

        out = _io.StringIO()
        with redirect_stdout(out):
            generator.write_rows_from_csv_text(
                self._bad_csv(1), self._COLUMNS, 5, _CollectingTileWriter()
            )
        self.assertIn("Warning:", out.getvalue())


# ---------------------------------------------------------------------------
# TAP retry logic
# ---------------------------------------------------------------------------

class TestHttpRetry(unittest.TestCase):
    def setUp(self) -> None:
        self.throttle = generator._TapRequestThrottle()
        self.throttle_patch = patch.object(generator, "_TAP_REQUEST_THROTTLE", self.throttle)
        self.throttle_patch.start()
        self.outage_monitor = generator._TapRequestOutageMonitor()
        self.outage_monitor_patch = patch.object(
            generator,
            "_TAP_REQUEST_OUTAGE_MONITOR",
            self.outage_monitor,
        )
        self.outage_monitor_patch.start()

    def tearDown(self) -> None:
        self.outage_monitor_patch.stop()
        self.throttle_patch.stop()

    def _http_error(self, code: int) -> urllib.error.HTTPError:
        return urllib.error.HTTPError(
            url="http://tap", code=code, msg="error", hdrs=MagicMock(), fp=None
        )

    def _ok_response(self, body: bytes = b"ok") -> MagicMock:
        r = MagicMock()
        r.read.return_value = body
        r.geturl.return_value = "http://tap/job"
        r.__enter__ = lambda s: s
        r.__exit__ = MagicMock(return_value=False)
        return r

    def test_get_retries_503_then_succeeds(self) -> None:
        calls = []

        def fake_open(req, timeout):
            calls.append(req)
            if len(calls) < 3:
                raise self._http_error(503)
            return self._ok_response()

        with patch("urllib.request.urlopen", side_effect=fake_open), patch("time.sleep"):
            result = generator.http_get_text("http://tap", 10.0, "agent")
        self.assertEqual(result, "ok")
        self.assertEqual(len(calls), 3)

    def test_get_raises_after_all_retries_exhausted(self) -> None:
        with patch("urllib.request.urlopen", side_effect=self._http_error(503)), \
             patch("time.sleep"):
            with self.assertRaises(urllib.error.HTTPError):
                generator.http_get_text("http://tap", 10.0, "agent")

    def test_get_does_not_retry_404(self) -> None:
        """404 is a client error — must raise immediately without retrying."""
        calls = []

        def fake_open(req, timeout):
            calls.append(req)
            raise self._http_error(404)

        with patch("urllib.request.urlopen", side_effect=fake_open), \
             patch("time.sleep"):
            with self.assertRaises(urllib.error.HTTPError) as ctx:
                generator.http_get_text("http://tap", 10.0, "agent")
        self.assertEqual(ctx.exception.code, 404)
        self.assertEqual(len(calls), 1)

    def test_post_retries_502_then_succeeds(self) -> None:
        calls = []

        def fake_open(req, timeout):
            calls.append(req)
            if len(calls) < 2:
                raise self._http_error(502)
            return self._ok_response()

        with patch("urllib.request.urlopen", side_effect=fake_open), patch("time.sleep"):
            url, body = generator.http_post_form("http://tap", {"q": "v"}, 10.0, "agent")
        self.assertEqual(url, "http://tap/job")
        self.assertEqual(len(calls), 2)

    def test_url_error_is_retried(self) -> None:
        calls = []

        def fake_open(req, timeout):
            calls.append(req)
            if len(calls) < 2:
                raise urllib.error.URLError("connection refused")
            return self._ok_response()

        with patch("urllib.request.urlopen", side_effect=fake_open), patch("time.sleep"):
            result = generator.http_get_text("http://tap", 10.0, "agent")
        self.assertEqual(result, "ok")
        self.assertEqual(len(calls), 2)

    def test_get_raises_resumable_outage_error_after_sustained_failures(self) -> None:
        with self.outage_monitor._lock:
            self.outage_monitor._last_success_at -= 31.0

        with patch.object(generator, "_TAP_OUTAGE_THRESHOLD_SECONDS", 30.0), \
             patch("urllib.request.urlopen",
                   side_effect=urllib.error.URLError("[Errno -5] No address associated with hostname")), \
             patch("time.sleep"):
            with self.assertRaises(generator.TapTransientOutageError) as ctx:
                generator.http_get_text("http://tap/jobs/42/phase", 10.0, "agent")

        message = str(ctx.exception)
        self.assertIn("--resume", message)
        self.assertIn("--workers 1", message)

    def test_retry_after_header_delays_next_attempt(self) -> None:
        sleeps: list[float] = []
        calls = []
        error = urllib.error.HTTPError(
            url="http://tap",
            code=503,
            msg="error",
            hdrs={"Retry-After": "7"},
            fp=None,
        )

        def fake_open(req, timeout):
            calls.append(req)
            if len(calls) == 1:
                raise error
            return self._ok_response()

        with patch("urllib.request.urlopen", side_effect=fake_open), \
             patch("time.sleep", side_effect=lambda s: sleeps.append(s)):
            result = generator.http_get_text("http://tap", 10.0, "agent")

        self.assertEqual(result, "ok")
        self.assertEqual(len(calls), 2)
        self.assertTrue(any(abs(delay - 7.0) < 0.25 for delay in sleeps))

    def test_shared_throttle_delays_even_first_attempt_after_backoff(self) -> None:
        sleeps: list[float] = []
        self.throttle.defer(5.0)

        with patch("urllib.request.urlopen", return_value=self._ok_response()), \
             patch("time.sleep", side_effect=lambda s: sleeps.append(s)):
            result = generator.http_get_text("http://tap", 10.0, "agent")

        self.assertEqual(result, "ok")
        self.assertTrue(sleeps)
        self.assertGreaterEqual(sleeps[0], 4.5)


# ---------------------------------------------------------------------------
# Zero-star tile handling
# ---------------------------------------------------------------------------

class TestZeroStarTile(unittest.TestCase):
    """A chunk where some tiles have no stars must produce index entries with
    star_count=0 and correct cumulative data_offset values."""

    def test_zero_star_tile_index_entry(self) -> None:
        import shutil
        tmpdir = Path(tempfile.mkdtemp())
        try:
            tiledir = tmpdir / ".tmp_healpix_order5"
            tiledir.mkdir()
            outdir = tmpdir / "out"
            outdir.mkdir()

            # Put one star only in tile 6144 (chunk 24, local index 0).
            # Tile 6145 (local index 1) has no file — zero stars.
            payload = generator.STAR_STRUCT_V3.pack(
                180.0, 0.0, 9.5, 9.9, 9.2, 0.0, 0.0, 1, 0, -32768, -32768, 65535, -32768
            )
            (tiledir / "tile_06144.bin").write_bytes(payload)

            import io as _io
            from contextlib import redirect_stdout
            with redirect_stdout(_io.StringIO()):
                generator.build_chunked_catalog(tiledir, outdir, "T", "t", 5, 14.0)

            raw = (outdir / "t_chunk_24.ansp").read_bytes()
            jl = struct.unpack_from("<I", raw, 8)[0]
            padding = (-(16 + jl)) % 8
            idx = 16 + jl + padding
            entry_struct = struct.Struct("<IQ")

            sc0, off0 = entry_struct.unpack_from(raw, idx)
            sc1, off1 = entry_struct.unpack_from(raw, idx + entry_struct.size)

            self.assertEqual(sc0, 1)
            self.assertEqual(off0, 0)
            self.assertEqual(sc1, 0)
            # offset for tile 1 must equal the data used by tile 0
            self.assertEqual(off1, 1 * generator.STAR_STRUCT_V3.size)
        finally:
            shutil.rmtree(tmpdir, ignore_errors=True)


class TestTapPixelStore(unittest.TestCase):
    def test_reload_recovers_committed_pixel_files(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            store = generator.TapPixelStore(temp_dir)

            payload_a = generator.STAR_STRUCT_V3.pack(
                10.0, 5.0, 12.0, 12.5, 11.5, 0.1, 0.2, 1001, 5000, 420, -10, 200, 50
            )
            payload_b = generator.STAR_STRUCT_V3.pack(
                20.0, -3.0, 13.0, 13.4, 12.8, -0.2, 0.4, 1002, 5100, 430, -12, 210, 60
            )
            store.commit_data(
                3,
                17,
                row_count=2,
                kept_rows=2,
                skipped_rows=0,
                packed_rows=[(100, payload_a), (101, payload_b)],
            )
            store.commit_empty(3, 18)
            store.mark_subdivided(3, 19)

            reloaded = generator.TapPixelStore(temp_dir)
            self.assertEqual(reloaded.pixel_status(3, 17), "data")
            self.assertEqual(reloaded.pixel_status(3, 18), "empty")
            self.assertEqual(reloaded.pixel_status(3, 19), "subdivide")

            data_records = list(reloaded.iter_data_records())
            self.assertEqual(len(data_records), 1)
            self.assertEqual(data_records[0].healpix_level, 3)
            self.assertEqual(data_records[0].healpix_index, 17)
            self.assertEqual(data_records[0].row_count, 2)
            self.assertEqual(data_records[0].kept_rows, 2)
            self.assertEqual(data_records[0].skipped_rows, 0)
            self.assertEqual(data_records[0].packed_rows, [(100, payload_a), (101, payload_b)])

    def test_incomplete_temp_files_are_ignored_on_resume(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            store = generator.TapPixelStore(temp_dir)
            tmp_path = store.data_path(4, 23).with_suffix(".tmp")
            tmp_path.write_bytes(b"partial")

            reloaded = generator.TapPixelStore(temp_dir)
            self.assertIsNone(reloaded.pixel_status(4, 23))
            self.assertEqual(list(reloaded.iter_data_records()), [])

    def test_materialize_tiles_rebuilds_from_pixel_store(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            store = generator.TapPixelStore(temp_dir / "tap_store")
            tile_dir = temp_dir / "tiles"

            payload_a = generator.STAR_STRUCT_V3.pack(
                30.0, 15.0, 10.0, 10.4, 9.8, 0.0, 0.0, 2001, 4900, 410, -20, 100, 30
            )
            payload_b = generator.STAR_STRUCT_V3.pack(
                31.0, 15.5, 10.1, 10.5, 9.9, 0.1, 0.1, 2002, 4950, 415, -18, 110, 35
            )
            payload_c = generator.STAR_STRUCT_V3.pack(
                32.0, 16.0, 10.2, 10.6, 10.0, 0.2, 0.2, 2003, 5000, 420, -16, 120, 40
            )
            store.commit_data(
                3,
                5,
                row_count=3,
                kept_rows=3,
                skipped_rows=0,
                packed_rows=[(12, payload_a), (12, payload_b), (77, payload_c)],
            )

            rows_written = generator.materialize_tiles_from_tap_store(store, tile_dir, max_open_files=32)
            self.assertEqual(rows_written, 3)
            self.assertEqual((tile_dir / "tile_00012.bin").read_bytes(), payload_a + payload_b)
            self.assertEqual((tile_dir / "tile_00077.bin").read_bytes(), payload_c)


class TestChunkResume(unittest.TestCase):
    def test_resume_reuses_completed_chunks_without_catalog_meta(self) -> None:
        import io as _io
        import shutil
        from contextlib import redirect_stdout

        tmpdir = Path(tempfile.mkdtemp())
        try:
            tiledir = tmpdir / ".tmp_healpix_order5"
            tiledir.mkdir()
            outdir = tmpdir / "out"
            outdir.mkdir()

            payload0 = generator.STAR_STRUCT_V3.pack(
                0.0, 0.0, 9.5, 9.9, 9.2, 0.0, 0.0, 1, 0, -32768, -32768, 65535, -32768
            )
            payload1 = generator.STAR_STRUCT_V3.pack(
                180.0, 0.0, 10.5, 10.9, 10.2, 0.0, 0.0, 2, 0, -32768, -32768, 65535, -32768
            )
            (tiledir / "tile_00000.bin").write_bytes(payload0)
            (tiledir / "tile_00256.bin").write_bytes(payload1)

            checkpoint = generator.ChunkManifestCheckpoint(outdir)
            manifest0 = generator.build_chunk_manifest(tiledir, outdir, "T", "t", 5, 14.0, 0, 48, 0, 256)
            checkpoint.record_manifest(manifest0)
            chunk0_path = outdir / str(manifest0["file"])
            chunk0_before = chunk0_path.read_bytes()

            with redirect_stdout(_io.StringIO()):
                manifests = generator.build_chunked_catalog(
                    tiledir,
                    outdir,
                    "T",
                    "t",
                    5,
                    14.0,
                    workers=1,
                    resume=True,
                )

            self.assertEqual(len(manifests), 48)
            self.assertEqual(chunk0_path.read_bytes(), chunk0_before)
            self.assertEqual(manifests[0]["sha256"], manifest0["sha256"])
            self.assertEqual(manifests[0]["stars"], 1)
            self.assertEqual(manifests[1]["stars"], 1)
        finally:
            shutil.rmtree(tmpdir, ignore_errors=True)


class TestProgressReporting(unittest.TestCase):
    def test_overall_progress_tracker_reports_phase_and_total_percentage(self) -> None:
        tracker = generator.OverallProgressTracker(total_rows=300)

        tap_snapshot = tracker.snapshot("tap", 30, 300)
        self.assertAlmostEqual(tap_snapshot.percent, 10.0, places=6)
        self.assertAlmostEqual(tap_snapshot.overall_percent, (30 / 900) * 100.0, places=6)

        tile_snapshot = tracker.snapshot("tile", 150, 300)
        self.assertAlmostEqual(tile_snapshot.percent, 50.0, places=6)
        self.assertAlmostEqual(tile_snapshot.overall_percent, ((300 + 150) / 900) * 100.0, places=6)

        chunk_snapshot = tracker.snapshot("chunk", 300, 300)
        self.assertAlmostEqual(chunk_snapshot.percent, 100.0, places=6)
        self.assertAlmostEqual(chunk_snapshot.overall_percent, 100.0, places=6)

    def test_log_progress_prints_percent_fields_to_stdout(self) -> None:
        import io as _io
        from contextlib import redirect_stdout

        out = _io.StringIO()
        with patch.object(generator, "_STDOUT_IS_TTY", False), \
             patch.object(generator, "_STDERR_IS_TTY", False), \
             redirect_stdout(out):
            snapshot = generator.ProgressSnapshot(
                phase="tap",
                current=25,
                total=100,
                detail="healpix=3:7",
                percent=25.0,
                overall_percent=8.333333,
            )
            generator.log_progress(snapshot)

        line = out.getvalue().strip()
        self.assertIn("AFTERNIGHT_PROGRESS", line)
        self.assertIn("phase=tap", line)
        self.assertIn("current=25", line)
        self.assertIn("total=100", line)
        self.assertIn("percent=25.000", line)
        self.assertIn("overall_percent=8.333", line)
        self.assertIn("detail=healpix=3:7", line)

    def test_populate_tiles_from_official_tap_emits_heartbeat_when_waiting(self) -> None:
        import io as _io
        from contextlib import redirect_stdout

        class _FakeFuture:
            def __init__(self, result_value):
                self._result_value = result_value

            def result(self):
                return self._result_value

        class _FakeExecutor:
            def __init__(self, future):
                self._future = future

            def __enter__(self):
                return self

            def __exit__(self, exc_type, exc, tb):
                return False

            def submit(self, *args, **kwargs):
                return self._future

        with tempfile.TemporaryDirectory() as temp_dir_raw:
            temp_dir = Path(temp_dir_raw)
            pixel_store = generator.TapPixelStore(temp_dir / "tap_store")
            future = _FakeFuture(("empty", 3, 0, None))
            args = argparse.Namespace(
                tap_include_pixel=[0],
                tap_initial_healpix_level=3,
                workers=1,
                tap_heartbeat_seconds=60.0,
                tap_url="https://gea.esac.esa.int/tap-server/tap",
                tap_timeout_seconds=120.0,
                tap_poll_seconds=1.0,
                tap_job_timeout_seconds=1800.0,
                tap_user_agent="AfterNight Gaia Catalog Builder",
                tap_max_rows_per_query=200000,
                tap_max_healpix_level=6,
                healpix_order=5,
            )

            out = _io.StringIO()
            with patch.object(generator, "_STDOUT_IS_TTY", True), \
                 patch.object(generator, "_STDERR_IS_TTY", False), \
                 patch.object(generator.concurrent.futures, "ThreadPoolExecutor", return_value=_FakeExecutor(future)), \
                 patch.object(
                     generator.concurrent.futures,
                     "wait",
                     side_effect=[(set(), {future}), ({future}, set())],
                 ), \
                 redirect_stdout(out):
                generator.populate_tiles_from_official_tap(
                    args,
                    mag_limit=18.0,
                    columns=generator.DEFAULT_COLUMNS,
                    pixel_store=pixel_store,
                    tap_total_rows=304942094,
                    progress_tracker=None,
                )

        output = out.getvalue()
        self.assertIn("Info: TAP heartbeat:", output)
        self.assertIn("oldest healpix=3:0", output)


if __name__ == "__main__":
    unittest.main()
