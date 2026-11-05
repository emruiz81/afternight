#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Generate deep_sky_catalog.csv from public-domain VizieR catalog data.

Sources:
- NGC 2000.0 (VII/118) — Dreyer/Sinnott, public domain via Sky & Telescope
- Messier cross-identifications (hardcoded, well-known)

Output format matches an::object_catalog CSV parser:
  id, common_name, ra_deg, dec_deg, major_axis_arcmin, minor_axis_arcmin,
  position_angle_deg, object_type, magnitude
"""

import csv
import io
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

VIZIER_TAP = "https://tapvizier.cds.unistra.fr/TAPVizieR/tap/sync"

# Well-known Messier → NGC/IC cross-references (complete list)
MESSIER_TO_NGC = {
    "M 1": "NGC 1952",  "M 2": "NGC 7089",  "M 3": "NGC 5272",
    "M 4": "NGC 6121",  "M 5": "NGC 5904",  "M 6": "NGC 6405",
    "M 7": "NGC 6475",  "M 8": "NGC 6523",  "M 9": "NGC 6333",
    "M 10": "NGC 6254", "M 11": "NGC 6705", "M 12": "NGC 6218",
    "M 13": "NGC 6205", "M 14": "NGC 6402", "M 15": "NGC 7078",
    "M 16": "NGC 6611", "M 17": "NGC 6618", "M 18": "NGC 6613",
    "M 19": "NGC 6273", "M 20": "NGC 6514", "M 21": "NGC 6531",
    "M 22": "NGC 6656", "M 23": "NGC 6494", "M 24": "IC 4715",
    "M 25": "IC 4725",  "M 26": "NGC 6694", "M 27": "NGC 6853",
    "M 28": "NGC 6626", "M 29": "NGC 6913", "M 30": "NGC 7099",
    "M 31": "NGC 224",  "M 32": "NGC 221",  "M 33": "NGC 598",
    "M 34": "NGC 1039", "M 35": "NGC 2168", "M 36": "NGC 1960",
    "M 37": "NGC 2099", "M 38": "NGC 1912", "M 39": "NGC 7092",
    "M 40": None,        "M 41": "NGC 2287", "M 42": "NGC 1976",
    "M 43": "NGC 1982", "M 44": "NGC 2632", "M 45": None,
    "M 46": "NGC 2437", "M 47": "NGC 2422", "M 48": "NGC 2548",
    "M 49": "NGC 4472", "M 50": "NGC 2323", "M 51": "NGC 5194",
    "M 52": "NGC 7654", "M 53": "NGC 5024", "M 54": "NGC 6715",
    "M 55": "NGC 6809", "M 56": "NGC 6779", "M 57": "NGC 6720",
    "M 58": "NGC 4579", "M 59": "NGC 4621", "M 60": "NGC 4649",
    "M 61": "NGC 4303", "M 62": "NGC 6266", "M 63": "NGC 5055",
    "M 64": "NGC 4826", "M 65": "NGC 3623", "M 66": "NGC 3627",
    "M 67": "NGC 2682", "M 68": "NGC 4590", "M 69": "NGC 6637",
    "M 70": "NGC 6681", "M 71": "NGC 6838", "M 72": "NGC 6981",
    "M 73": "NGC 6994", "M 74": "NGC 628",  "M 75": "NGC 6864",
    "M 76": "NGC 650",  "M 77": "NGC 1068", "M 78": "NGC 2068",
    "M 79": "NGC 1904", "M 80": "NGC 6093", "M 81": "NGC 3031",
    "M 82": "NGC 3034", "M 83": "NGC 5236", "M 84": "NGC 4374",
    "M 85": "NGC 4382", "M 86": "NGC 4406", "M 87": "NGC 4486",
    "M 88": "NGC 4501", "M 89": "NGC 4552", "M 90": "NGC 4569",
    "M 91": "NGC 4548", "M 92": "NGC 6341", "M 93": "NGC 2447",
    "M 94": "NGC 4736", "M 95": "NGC 3351", "M 96": "NGC 3368",
    "M 97": "NGC 3587", "M 98": "NGC 4192", "M 99": "NGC 4254",
    "M 100": "NGC 4321", "M 101": "NGC 5457", "M 102": "NGC 5866",
    "M 103": "NGC 581", "M 104": "NGC 4594", "M 105": "NGC 3379",
    "M 106": "NGC 4258", "M 107": "NGC 6171", "M 108": "NGC 3556",
    "M 109": "NGC 3992", "M 110": "NGC 205",
}
NGC_TO_MESSIER = {}
for m, n in MESSIER_TO_NGC.items():
    if n is not None:
        NGC_TO_MESSIER[n] = m

# Well-known common names for popular deep-sky objects.
# Keyed by NGC/IC designation; Messier entries inherit these automatically.
# Only genuinely well-established names — not descriptive placeholders.
COMMON_NAMES = {
    # --- Galaxies ---
    "NGC 224":  "Andromeda Galaxy",
    "NGC 598":  "Triangulum Galaxy",
    "NGC 3031": "Bode's Galaxy",
    "NGC 3034": "Cigar Galaxy",
    "NGC 5194": "Whirlpool Galaxy",
    "NGC 5055": "Sunflower Galaxy",
    "NGC 4826": "Black Eye Galaxy",
    "NGC 5457": "Pinwheel Galaxy",
    "NGC 4594": "Sombrero Galaxy",
    "NGC 5236": "Southern Pinwheel Galaxy",
    "NGC 628":  "Phantom Galaxy",
    "NGC 4486": "Virgo A",
    "NGC 1068": "Cetus A",
    "NGC 3556": "Surfboard Galaxy",
    "NGC 4736": "Croc's Eye Galaxy",
    "NGC 5866": "Spindle Galaxy",
    "NGC 4258": "Starfish Galaxy",
    "NGC 253":  "Sculptor Galaxy",
    "NGC 55":   "Sculptor Dwarf Galaxy",
    "NGC 247":  "Cetus Dwarf Spiral",
    "NGC 300":  "Southern Pinwheel",
    "NGC 1365": "Great Barred Spiral Galaxy",
    "NGC 1275": "Perseus A",
    "NGC 1316": "Fornax A",
    "NGC 5128": "Centaurus A",
    "NGC 4038": "Antennae Galaxies",
    "NGC 4565": "Needle Galaxy",
    "NGC 4631": "Whale Galaxy",
    "NGC 4656": "Hockey Stick Galaxy",
    "NGC 891":  "Silver Sliver Galaxy",
    "NGC 7331": "Deer Lick Galaxy",
    "NGC 6946": "Fireworks Galaxy",
    "NGC 4244": "Silver Needle Galaxy",
    "NGC 2683": "UFO Galaxy",
    "NGC 5907": "Splinter Galaxy",
    "NGC 3115": "Spindle Galaxy (Sextans)",
    "NGC 4321": "Mirror Galaxy",
    "NGC 4254": "Coma Pinwheel",
    "NGC 3623": "Leo Triplet",
    "NGC 3627": "Leo Triplet",
    # --- Nebulae ---
    "NGC 1952": "Crab Nebula",
    "NGC 1976": "Orion Nebula",
    "NGC 1982": "De Mairan's Nebula",
    "NGC 2070": "Tarantula Nebula",
    "NGC 6523": "Lagoon Nebula",
    "NGC 6611": "Eagle Nebula",
    "NGC 6618": "Swan Nebula",
    "NGC 6514": "Trifid Nebula",
    "NGC 6853": "Dumbbell Nebula",
    "NGC 6720": "Ring Nebula",
    "NGC 3587": "Owl Nebula",
    "NGC 650":  "Little Dumbbell Nebula",
    "NGC 2068": "Reflection Nebula in Orion",
    "NGC 1499": "California Nebula",
    "NGC 2237": "Rosette Nebula",
    "NGC 2264": "Cone Nebula",
    "NGC 281":  "Pacman Nebula",
    "NGC 6888": "Crescent Nebula",
    "NGC 6960": "Western Veil Nebula",
    "NGC 6992": "Eastern Veil Nebula",
    "NGC 7000": "North America Nebula",
    "NGC 7293": "Helix Nebula",
    "NGC 7635": "Bubble Nebula",
    "NGC 2392": "Eskimo Nebula",
    "NGC 3372": "Carina Nebula",
    "NGC 7009": "Saturn Nebula",
    "NGC 6543": "Cat's Eye Nebula",
    "NGC 2359": "Thor's Helmet",
    "NGC 2024": "Flame Nebula",
    "NGC 6826": "Blinking Planetary",
    "NGC 7662": "Blue Snowball Nebula",
    "NGC 3242": "Ghost of Jupiter",
    "NGC 2261": "Hubble's Variable Nebula",
    "NGC 6302": "Butterfly Nebula",
    "NGC 6334": "Cat's Paw Nebula",
    "IC 434":   "Horsehead Region",
    "IC 1396":  "Elephant Trunk Nebula",
    "IC 1805":  "Heart Nebula",
    "IC 1848":  "Soul Nebula",
    "IC 5070":  "Pelican Nebula",
    "IC 405":   "Flaming Star Nebula",
    "IC 2118":  "Witch Head Nebula",
    "IC 4715":  "Sagittarius Star Cloud",
    # --- Clusters ---
    "NGC 6205": "Great Hercules Cluster",
    "NGC 6656": "Great Sagittarius Cluster",
    "NGC 2632": "Beehive Cluster",
    "NGC 6405": "Butterfly Cluster",
    "NGC 6475": "Ptolemy's Cluster",
    "NGC 6705": "Wild Duck Cluster",
    "NGC 7654": "Salt and Pepper Cluster",
    "NGC 869":  "Double Cluster (h Per)",
    "NGC 884":  "Double Cluster (χ Per)",
    "NGC 457":  "Owl Cluster",
    "NGC 7789": "Caroline's Rose Cluster",
    "NGC 2244": "Rosette Cluster",
    "NGC 5139": "Omega Centauri",
    "NGC 104":  "47 Tucanae",
    "NGC 6809": "Summer Rose Star",
    "NGC 7078": "Great Pegasus Cluster",
}

# NGC 2000.0 type codes → human-readable
TYPE_MAP = {
    "Gx":  "Galaxy",
    "OC":  "Open Cluster",
    "Gb":  "Globular Cluster",
    "Nb":  "Bright Nebula",
    "Pl":  "Planetary Nebula",
    "C+N": "Cluster with Nebulosity",
    "Ast": "Asterism",
    "Kt":  "Knot",
    "***": "Triple Star",
    "D*":  "Double Star",
    "*":   "Star",
    "?":   "Uncertain",
    "":    "Unknown",
    "-":   "Nonexistent",
    "PD":  "Plate Defect",
}


def safe_float(val, default=0.0):
    """Parse a float from a string, returning default on failure."""
    if val is None:
        return default
    val = str(val).strip()
    if not val or val in ("", "-", "?", "N/A"):
        return default
    try:
        return float(val)
    except ValueError:
        return default


def tap_query(endpoint, adql, fmt="csv", max_retries=3):
    """Execute a TAP/sync query and return the response text."""
    params = {
        "REQUEST": "doQuery",
        "LANG": "ADQL",
        "FORMAT": fmt,
        "QUERY": adql,
    }
    data = urllib.parse.urlencode(params).encode("utf-8")
    for attempt in range(max_retries):
        try:
            req = urllib.request.Request(endpoint, data=data, method="POST")
            req.add_header("User-Agent", "AfterNight-CatalogGen/1.0")
            with urllib.request.urlopen(req, timeout=120) as resp:
                return resp.read().decode("utf-8", errors="replace")
        except (urllib.error.URLError, TimeoutError) as e:
            print(f"  Attempt {attempt+1} failed: {e}", file=sys.stderr)
            if attempt < max_retries - 1:
                time.sleep(3 * (attempt + 1))
            else:
                raise


def fetch_ngc2000():
    """Fetch all NGC/IC objects from NGC 2000.0 (VII/118) via VizieR TAP."""
    print("Fetching objects from NGC 2000.0 (VII/118)...", file=sys.stderr)
    adql = 'SELECT Name, Type, RAB2000, DEB2000, size, mag FROM "VII/118/ngc2000"'
    return tap_query(VIZIER_TAP, adql)


def parse_ngc2000_csv(csv_text):
    """Parse NGC 2000.0 VizieR TAP CSV into catalog objects."""
    objects = []
    reader = csv.DictReader(io.StringIO(csv_text))
    for row in reader:
        name_raw = row.get("Name", "").strip()
        if not name_raw:
            continue

        # Determine prefix: IC entries start with "I", NGC entries are numeric
        if name_raw.startswith("I"):
            num_str = name_raw[1:].strip()
            try:
                num = int(num_str)
                obj_id = f"IC {num}"
            except ValueError:
                obj_id = f"IC {num_str}"
        else:
            try:
                num = int(name_raw)
                obj_id = f"NGC {num}"
            except ValueError:
                continue

        ra_str = row.get("RAB2000", "").strip()
        dec_str = row.get("DEB2000", "").strip()
        if not ra_str or not dec_str:
            continue

        try:
            ra_deg = float(ra_str)
            dec_deg = float(dec_str)
        except ValueError:
            continue

        type_code = row.get("Type", "").strip()
        obj_type = TYPE_MAP.get(type_code, type_code if type_code else "Unknown")

        # Skip nonexistent / plate defect / stellar entries
        if obj_type in ("Nonexistent", "Plate Defect"):
            continue

        major = safe_float(row.get("size", ""))
        vmag = safe_float(row.get("mag", ""), default=99.0)

        # For NGC 2000.0, only major axis (diameter) is provided.
        # Estimate minor axis based on object type:
        #   - Galaxies: minor = major * 0.5 (rough average axial ratio)
        #   - Everything else: minor = major (roughly circular)
        if obj_type == "Galaxy" and major > 0.0:
            minor = major * 0.5
        else:
            minor = major

        common_name = COMMON_NAMES.get(obj_id, "")

        objects.append({
            "id": obj_id,
            "common_name": common_name,
            "ra_deg": ra_deg,
            "dec_deg": dec_deg,
            "major_axis_arcmin": major,
            "minor_axis_arcmin": minor,
            "position_angle_deg": 0.0,
            "object_type": obj_type,
            "magnitude": vmag,
        })

    return objects


def add_messier_entries(objects):
    """Add Messier designation entries for NGC/IC objects that have them,
    plus M 40 and M 45 which have no NGC counterpart."""
    existing_ids = {o["id"] for o in objects}
    ngc_lookup = {o["id"]: o for o in objects}

    messier_entries = []
    for messier_id, ngc_id in sorted(MESSIER_TO_NGC.items(),
                                      key=lambda x: int(x[0][2:])):
        if messier_id in existing_ids:
            continue
        if ngc_id is None:
            continue
        ngc_obj = ngc_lookup.get(ngc_id)
        if ngc_obj is None:
            continue

        m_obj = dict(ngc_obj)
        m_obj["id"] = messier_id
        # Do not fall back to NGC/IC designation as common_name;
        # the NGC row already provides cross-referencing via search.
        messier_entries.append(m_obj)
        existing_ids.add(messier_id)

    objects.extend(messier_entries)

    # M 40 (Winnecke 4) — double star, no NGC counterpart
    if "M 40" not in existing_ids:
        objects.append({
            "id": "M 40",
            "common_name": "Winnecke 4",
            "ra_deg": 185.5500,
            "dec_deg": 58.0833,
            "major_axis_arcmin": 0.8,
            "minor_axis_arcmin": 0.0,
            "position_angle_deg": 0.0,
            "object_type": "Double Star",
            "magnitude": 8.4,
        })

    # M 45 (Pleiades) — not in NGC
    if "M 45" not in existing_ids:
        objects.append({
            "id": "M 45",
            "common_name": "Pleiades",
            "ra_deg": 56.7500,
            "dec_deg": 24.1167,
            "major_axis_arcmin": 110.0,
            "minor_axis_arcmin": 110.0,
            "position_angle_deg": 0.0,
            "object_type": "Open Cluster",
            "magnitude": 1.6,
        })


def apply_overrides(objects):
    """Apply curated data for well-known objects where NGC 2000.0 data
    is incomplete or inaccurate (e.g., large nebulae, galaxies with
    known axis ratios and position angles)."""
    overrides = {
        "NGC 224":  {"major_axis_arcmin": 190.0, "minor_axis_arcmin": 60.0,
                     "position_angle_deg": 35.0, "magnitude": 3.4},
        "NGC 598":  {"major_axis_arcmin": 70.8, "minor_axis_arcmin": 41.7,
                     "position_angle_deg": 23.0, "magnitude": 5.7},
        "NGC 221":  {"major_axis_arcmin": 8.7, "minor_axis_arcmin": 6.5,
                     "position_angle_deg": 165.0, "magnitude": 8.1},
        "NGC 205":  {"major_axis_arcmin": 21.9, "minor_axis_arcmin": 11.0,
                     "position_angle_deg": 35.0, "magnitude": 8.0},
        "NGC 1952": {"major_axis_arcmin": 7.0, "minor_axis_arcmin": 5.0,
                     "position_angle_deg": 0.0},
        "NGC 6853": {"major_axis_arcmin": 8.0, "minor_axis_arcmin": 5.7,
                     "position_angle_deg": 130.0},
        "NGC 5194": {"major_axis_arcmin": 11.2, "minor_axis_arcmin": 6.9,
                     "position_angle_deg": 163.0},
        "NGC 4594": {"major_axis_arcmin": 8.9, "minor_axis_arcmin": 4.1,
                     "position_angle_deg": 84.0},
        "NGC 3031": {"major_axis_arcmin": 26.9, "minor_axis_arcmin": 14.1,
                     "position_angle_deg": 157.0},
        "NGC 3034": {"major_axis_arcmin": 11.2, "minor_axis_arcmin": 4.3,
                     "position_angle_deg": 65.0},
        "NGC 5457": {"major_axis_arcmin": 28.8, "minor_axis_arcmin": 26.9,
                     "position_angle_deg": 39.0},
        "NGC 4826": {"major_axis_arcmin": 10.7, "minor_axis_arcmin": 5.1,
                     "position_angle_deg": 116.0},
        "NGC 5055": {"major_axis_arcmin": 12.6, "minor_axis_arcmin": 7.2,
                     "position_angle_deg": 105.0},
        "NGC 7000": {"major_axis_arcmin": 120.0, "minor_axis_arcmin": 100.0},
        "NGC 6523": {"major_axis_arcmin": 90.0, "minor_axis_arcmin": 40.0},
        "NGC 6611": {"major_axis_arcmin": 35.0, "minor_axis_arcmin": 28.0},
        "NGC 6618": {"major_axis_arcmin": 46.0, "minor_axis_arcmin": 37.0},
        "NGC 6514": {"major_axis_arcmin": 28.0, "minor_axis_arcmin": 28.0},
        "NGC 1976": {"major_axis_arcmin": 85.0, "minor_axis_arcmin": 60.0},
        "NGC 7293": {"major_axis_arcmin": 25.0, "minor_axis_arcmin": 21.0},
        "NGC 1499": {"major_axis_arcmin": 145.0, "minor_axis_arcmin": 40.0,
                     "position_angle_deg": 100.0},
        "NGC 2237": {"major_axis_arcmin": 80.0, "minor_axis_arcmin": 60.0},
        "NGC 6960": {"major_axis_arcmin": 70.0, "minor_axis_arcmin": 8.0,
                     "position_angle_deg": 52.0},
        "NGC 6992": {"major_axis_arcmin": 70.0, "minor_axis_arcmin": 8.0,
                     "position_angle_deg": 65.0},
        "NGC 6888": {"major_axis_arcmin": 18.0, "minor_axis_arcmin": 12.0,
                     "position_angle_deg": 45.0},
        "NGC 4565": {"major_axis_arcmin": 15.8, "minor_axis_arcmin": 2.1,
                     "position_angle_deg": 136.0},
        "NGC 4631": {"major_axis_arcmin": 15.5, "minor_axis_arcmin": 2.7,
                     "position_angle_deg": 86.0},
        "NGC 253": {"major_axis_arcmin": 27.5, "minor_axis_arcmin": 6.8,
                    "position_angle_deg": 52.0},
        "NGC 5128": {"major_axis_arcmin": 25.7, "minor_axis_arcmin": 20.0,
                     "position_angle_deg": 35.0},
        "NGC 4258": {"major_axis_arcmin": 18.6, "minor_axis_arcmin": 7.2,
                     "position_angle_deg": 162.0},
        "NGC 3556": {"major_axis_arcmin": 8.7, "minor_axis_arcmin": 2.2,
                     "position_angle_deg": 79.0},
        "NGC 3992": {"major_axis_arcmin": 7.6, "minor_axis_arcmin": 4.7,
                     "position_angle_deg": 150.0},
        "IC 1396":  {"major_axis_arcmin": 170.0, "minor_axis_arcmin": 140.0},
        "IC 1805":  {"major_axis_arcmin": 60.0, "minor_axis_arcmin": 60.0},
        "IC 1848":  {"major_axis_arcmin": 60.0, "minor_axis_arcmin": 40.0},
        "IC 5070":  {"major_axis_arcmin": 60.0, "minor_axis_arcmin": 50.0},
        "IC 434":   {"major_axis_arcmin": 60.0, "minor_axis_arcmin": 10.0},
    }

    by_id = {o["id"]: o for o in objects}
    for obj_id, overrides_dict in overrides.items():
        obj = by_id.get(obj_id)
        if obj:
            obj.update(overrides_dict)

    # Also apply overrides to corresponding Messier entries
    for messier_id, ngc_id in MESSIER_TO_NGC.items():
        if ngc_id and ngc_id in overrides:
            m_obj = by_id.get(messier_id)
            if m_obj:
                m_obj.update(overrides[ngc_id])


def build_catalog():
    csv_text = fetch_ngc2000()
    objects = parse_ngc2000_csv(csv_text)
    print(f"  Objects parsed: {len(objects)}", file=sys.stderr)

    add_messier_entries(objects)
    apply_overrides(objects)

    # Sort: Messier first (by number), then NGC (by number), then IC (by number)
    def sort_key(obj):
        oid = obj["id"]
        if oid.startswith("M "):
            try:
                return (0, int(oid[2:]))
            except ValueError:
                return (0, 9999)
        elif oid.startswith("NGC "):
            try:
                return (1, int(oid[4:]))
            except ValueError:
                return (1, 99999)
        elif oid.startswith("IC "):
            try:
                return (2, int(oid[3:]))
            except ValueError:
                return (2, 99999)
        return (3, 0)

    objects.sort(key=sort_key)
    return objects


def write_csv(objects, path):
    with open(path, "w", newline="", encoding="utf-8") as f:
        f.write("id,common_name,ra_deg,dec_deg,major_axis_arcmin,"
                "minor_axis_arcmin,position_angle_deg,object_type,magnitude\n")
        for obj in objects:
            common = obj.get("common_name", "")
            if "," in common or '"' in common:
                common = '"' + common.replace('"', '""') + '"'

            oid = obj["id"]
            if "," in oid or '"' in oid:
                oid = '"' + oid.replace('"', '""') + '"'

            otype = obj.get("object_type", "Unknown")
            if "," in otype or '"' in otype:
                otype = '"' + otype.replace('"', '""') + '"'

            f.write(f"{oid},{common},{obj['ra_deg']:.4f},{obj['dec_deg']:.4f},"
                    f"{obj['major_axis_arcmin']:.1f},{obj['minor_axis_arcmin']:.1f},"
                    f"{obj['position_angle_deg']:.0f},{otype},{obj['magnitude']:.1f}\n")

    print(f"Wrote {len(objects)} objects to {path}", file=sys.stderr)


if __name__ == "__main__":
    output_path = sys.argv[1] if len(sys.argv) > 1 else "deep_sky_catalog.csv"
    catalog = build_catalog()
    write_csv(catalog, output_path)
