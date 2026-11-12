#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""The deep-sky catalog generator, and the catalog the product ships as what it writes.

NGC 2000.0 gives an object one size, its diameter. A catalog row states an axis ratio and a position angle only for
the objects whose extents are curated; any other object is a circle of its diameter, since a ratio invented for it
would also invent the orientation its outline is drawn at.
"""

from __future__ import annotations

import csv
import sys
import unittest
from pathlib import Path

# The generator is a module of the repository's `tools` package. Run as a file - `python
# tools/test_generate_deep_sky_catalog.py` - the interpreter knows this folder and not the repository's, so the
# repository's root is named here; `python -m unittest tools.test_generate_deep_sky_catalog` from the root finds
# it either way.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tools import generate_deep_sky_catalog as generator  # noqa: E402

BUNDLED_CATALOG = Path(__file__).resolve().parent.parent / "src" / "AfterNightControls" / "data" / "deep_sky_catalog.csv"

NGC2000_HEADER = "Name,Type,RAB2000,DEB2000,size,mag\n"


class ParseNgc2000Tests(unittest.TestCase):
    def test_a_galaxy_of_one_stated_size_is_a_circle_of_that_size(self) -> None:
        objects = generator.parse_ngc2000_csv(NGC2000_HEADER + "5236,Gx,204.25,-29.8667,11.2,7.6\n")

        self.assertEqual(len(objects), 1)
        m83 = objects[0]
        self.assertEqual(m83["id"], "NGC 5236")
        self.assertEqual(m83["major_axis_arcmin"], 11.2)
        self.assertEqual(m83["minor_axis_arcmin"], 11.2)
        self.assertEqual(m83["position_angle_deg"], 0.0)

    def test_every_type_of_one_stated_size_is_a_circle(self) -> None:
        objects = generator.parse_ngc2000_csv(
            NGC2000_HEADER + "891,Gx,35.6375,42.35,13.5,10.0\n"
            "6720,Pl,283.3958,33.0333,1.4,9.0\n"
            "I434,Nb,85.25,-2.4,60.0,\n")

        self.assertEqual([o["major_axis_arcmin"] for o in objects], [o["minor_axis_arcmin"] for o in objects])

    def test_a_curated_extent_states_its_own_axes_and_angle(self) -> None:
        objects = generator.parse_ngc2000_csv(NGC2000_HEADER + "5194,Gx,202.4708,47.1953,11.0,8.4\n")
        generator.add_messier_entries(objects)
        generator.apply_overrides(objects)

        by_id = {o["id"]: o for o in objects}
        for identifier in ("NGC 5194", "M 51"):
            self.assertEqual(by_id[identifier]["major_axis_arcmin"], 11.2)
            self.assertEqual(by_id[identifier]["minor_axis_arcmin"], 6.9)
            self.assertEqual(by_id[identifier]["position_angle_deg"], 163.0)


class BundledCatalogTests(unittest.TestCase):
    def setUp(self) -> None:
        with BUNDLED_CATALOG.open(encoding="utf-8", newline="") as handle:
            self.rows = list(csv.DictReader(handle))

    def test_an_object_without_a_curated_extent_is_a_circle(self) -> None:
        # The Caldwell rows are maintained by hand and the generator does not write them.
        uncurated = [
            row for row in self.rows
            if not row["id"].startswith("C ")
            and generator.MESSIER_TO_NGC.get(row["id"], row["id"]) not in generator.CURATED_EXTENTS
        ]
        self.assertGreater(len(uncurated), 10000)

        # A minor axis of zero is not stated, and the outline takes the major axis for it.
        drawn_as_ellipses = [row["id"] for row in uncurated
                             if row["minor_axis_arcmin"] not in (row["major_axis_arcmin"], "0.0")
                             or float(row["position_angle_deg"]) != 0.0]
        self.assertEqual(drawn_as_ellipses[:10], [], f"{len(drawn_as_ellipses)} rows state axes nobody curated")

    def test_m83_is_a_circle_of_its_ngc2000_diameter(self) -> None:
        by_id = {row["id"]: row for row in self.rows}
        for identifier in ("M 83", "NGC 5236"):
            self.assertEqual(by_id[identifier]["major_axis_arcmin"], "11.2")
            self.assertEqual(by_id[identifier]["minor_axis_arcmin"], "11.2")

    def test_a_curated_extent_is_shipped_as_curated(self) -> None:
        by_id = {row["id"]: row for row in self.rows}
        for messier, ngc in generator.MESSIER_TO_NGC.items():
            curated = generator.CURATED_EXTENTS.get(ngc)
            if curated is None:
                continue
            for identifier in (messier, ngc):
                row = by_id[identifier]
                self.assertAlmostEqual(float(row["major_axis_arcmin"]), curated["major_axis_arcmin"], places=1)
                self.assertAlmostEqual(float(row["minor_axis_arcmin"]), curated["minor_axis_arcmin"], places=1)
                self.assertAlmostEqual(float(row["position_angle_deg"]),
                                       curated.get("position_angle_deg", 0.0), places=0)


if __name__ == "__main__":
    unittest.main()
