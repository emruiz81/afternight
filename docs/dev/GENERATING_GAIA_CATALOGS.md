# Generating Gaia DR3 Catalogs

This guide covers how to efficiently build the chunked `.ansp` catalog files that AfterNight downloads at runtime.

---

## Quick start

```bash
# From the repo root — requires Python 3.10+, no extra packages
python3 tools/generate_gaia_catalog.py --tap-official --preset g14 catalogs/gaia_dr3_g14
```

The generator queries the ESA Gaia DR3 Archive via TAP and writes a ready-to-deploy catalog directory.

---

## Available presets

| Preset | Magnitude limit | Approx. stars | Approx. output size |
|--------|----------------|---------------|----------------------|
| `g12`  | G ≤ 12         | ~3.1 M        | ~188 MB              |
| `g14`  | G ≤ 14         | ~17 M         | ~1.0 GB              |
| `g16`  | G ≤ 16         | ~78 M         | ~4.65 GB             |
| `g18`  | G ≤ 18         | ~305 M        | ~18 GB               |
| `g20`  | G ≤ 20         | ~1.5 B        | ~90 GB (estimated)   |

Star counts for g12–g18 are verified against ESA TAP COUNT queries. Sizes for g14 and g16 are measured from generated catalogs; others are computed from star counts × 64 bytes/record.

---

## System preparation

### File-descriptor limit

The generator auto-detects and uses the OS file-descriptor soft limit to size the tile-file handle cache. For large catalogs the default limit on Linux (1024) is often too low. Raise it before running:

```bash
ulimit -n 65536          # current shell only
```

For a permanent change add to `/etc/security/limits.conf`:

```
*    soft    nofile    65536
*    hard    nofile    65536
```

Verify with `ulimit -n`. If the OS limit is already ≥ 12 288 (the tile count for HEALPix order 5), no action is needed.

### Available disk space

During generation, pass 1 writes raw tile files to a temporary directory and pass 2 assembles them into `.ansp` chunks, deleting tiles as each chunk completes. Peak disk usage is roughly 2× the final catalog size.

| Catalog | Temp tile dir (pass 1) | Final chunks (pass 2) | Peak total |
|---------|----------------------|----------------------|------------|
| `g12`   | ~188 MB              | ~188 MB              | ~376 MB    |
| `g14`   | ~1.0 GB              | ~1.0 GB              | ~2.0 GB    |
| `g16`   | ~4.7 GB              | ~4.7 GB              | ~9.4 GB    |
| `g18`   | ~18 GB               | ~18 GB               | ~36 GB     |

The temporary tile directory is removed automatically on success.

---

## Performance tuning

### Parallel workers

The dominant cost is TAP round-trips to the ESA server: submit job → poll until complete → download results. These are all I/O-bound and run in parallel via `--workers`:

```bash
python3 tools/generate_gaia_catalog.py --tap-official --preset g14 \
    --workers 8 \
    catalogs/gaia_dr3_g14
```

| `--workers` | Typical speedup vs. default (4) | Notes |
|-------------|----------------------------------|-------|
| 4 (default) | 1×                               | Safe starting point |
| 8           | ~2×                              | Good for fast connections |
| 16          | ~4× up to ESA limit              | May hit ESA rate limits; increase `--tap-poll-seconds` if HTTP 429s appear |

`--workers` also controls how many `.ansp` chunk files are written in parallel during pass 2 (negligible compared to TAP time, but it completes much faster with g16).

### Partition tuning

The generator adaptively subdivides HEALPix pixels until each TAP query returns ≤ `--tap-max-rows-per-query` rows (default 200 000). Reducing this produces more, smaller queries — each faster but more HTTP overhead:

```bash
# Smaller queries; more parallel opportunities for g16
--tap-max-rows-per-query 100000
```

Raising the initial HEALPix level reduces the count queries needed before a refinement decision is made:

```bash
# Start subdivision at a finer level (fewer wasted count queries)
--tap-initial-healpix-level 4
```

### Polling interval

Each TAP job is polled until the ESA server reports `COMPLETED`. Reduce the polling interval to lower idle time per job:

```bash
--tap-poll-seconds 0.5    # default is 1.0
```

Do not go below 0.2 s — faster polling provides little benefit once network latency dominates.

If a dense region takes a long time and no HEALPix pixel completes, the generator emits a heartbeat every 60 seconds by default identifying the oldest in-flight pixel. Adjust with `--tap-heartbeat-seconds` or disable with `--tap-heartbeat-seconds 0`.

---

## Recommended commands by use case

### 1. Smoke test / validation (minutes, not hours)

Query a single HEALPix pixel at level 3. Produces a valid but partial catalog:

```bash
python3 tools/generate_gaia_catalog.py --tap-official --preset g14 \
    --tap-include-pixel 0 \
    catalogs/gaia_dr3_g14_test
```

Inspect the output before committing to a full build:
```bash
python3 -c "
import json, pathlib
meta = json.loads(pathlib.Path('catalogs/gaia_dr3_g14_test/catalog_meta.json').read_text())
print('format_version:', meta['format_version'])
print('chunks:', meta['chunk_count'])
print('total_stars:', meta['total_stars'])
"
```

### 2. Fast full g14 build (several hours)

```bash
ulimit -n 65536
python3 tools/generate_gaia_catalog.py --tap-official --preset g14 \
    --workers 8 \
    --tap-poll-seconds 0.5 \
    catalogs/gaia_dr3_g14
```

### 3. Full g16 build on a server (overnight)

```bash
ulimit -n 65536

# Run in a tmux/screen session so a disconnect doesn't kill it
python3 tools/generate_gaia_catalog.py --tap-official --preset g16 \
    --workers 16 \
    --tap-max-rows-per-query 100000 \
    --tap-poll-seconds 0.5 \
    --tap-job-timeout-seconds 3600 \
    catalogs/gaia_dr3_g16
```

### 4. Full g18 build (long running)

```bash
ulimit -n 65536

# Use --resume so an interruption doesn't lose progress
python3 tools/generate_gaia_catalog.py --tap-official --preset g18 \
    --resume \
    --workers 16 \
    --tap-max-rows-per-query 100000 \
    --tap-poll-seconds 0.5 \
    --tap-job-timeout-seconds 3600 \
    catalogs/gaia_dr3_g18
```

G18 downloads ~305 M stars (~18 GB). Expect 6–20+ hours depending on ESA server load and network speed. Using `--resume` is strongly recommended.

### 5. Generate from local Gaia CSV exports (no network)

If you have bulk CSV/CSV.GZ files downloaded from the ESA archive:

```bash
# Single file
python3 tools/generate_gaia_catalog.py GaiaSource_000.csv.gz catalogs/gaia_dr3_g14 --preset g14

# Directory of files
python3 tools/generate_gaia_catalog.py /data/gaia_dr3/ catalogs/gaia_dr3_g14 --preset g14

# Multiple explicit inputs
python3 tools/generate_gaia_catalog.py GaiaSource_000.csv.gz catalogs/gaia_dr3_g14 \
    --extra-input GaiaSource_001.csv.gz \
    --input-dir /data/more_gaia/ \
    --preset g14
```

CSV ingestion is single-threaded (Python CSV parsing is the bottleneck). For large CSV datasets, `--workers` only speeds up pass 2.

---

## Resuming an interrupted run

Pass `--resume` to continue a previously interrupted generation:

```bash
python3 tools/generate_gaia_catalog.py --tap-official --preset g14 --resume catalogs/gaia_dr3_g14
```

What `--resume` does:

| Phase | Behaviour |
|-------|----------|
| **TAP download (pass 1)** | Commits each completed HEALPix leaf pixel to its own durable file under `.tmp_tap_pixels_order<N>/`. Interrupted runs resume from those committed pixel files without re-appending rows into shared tile files. |
| **Tile rebuild (pass 1.5)** | Reconstructs the temporary per-tile files from the committed TAP pixel files before chunk assembly, so stale or partially written tile files are never trusted on resume. |
| **Chunk assembly (pass 2)** | Verifies SHA-256 of existing `.ansp` chunks against either the prior `catalog_meta.json` or the incremental chunk checkpoint (`.tmp_chunk_manifests.json`). Valid chunks are reused even if the final manifest was never written. |
| **Progress totals** | The generator persists the exact TAP total-row count and emits stdout progress lines with `percent=` and `overall_percent=` fields. |

The TAP pixel store, chunk checkpoint, and temporary tile directory are cleaned up automatically on success.

If the ESA TAP service or your DNS resolver goes unstable mid-run, the generator aborts with a resumable error after a sustained window of transient GET/POST failures instead of appearing hung indefinitely. The default outage threshold is 300 seconds and can be changed with `--tap-max-outage-seconds` or disabled with `--tap-max-outage-seconds 0`.

When resuming after an outage, prefer fewer workers until the service settles:

```bash
python3 tools/generate_gaia_catalog.py --tap-official --preset g14 --resume \
    --workers 1 \
    catalogs/gaia_dr3_g14
```

### Stdout progress

Non-interactive runs emit machine-readable progress lines on stdout:

```text
AFTERNIGHT_PROGRESS phase=tap current=123456 total=305000000 percent=0.040 overall_percent=0.013 detail=healpix=3:42
```

Fields:

- `current` / `total`: exact phase progress (rows for `tap` and `tile`; rows written for `chunk`)
- `percent`: exact phase percentage
- `overall_percent`: end-to-end percentage across TAP download, tile rebuild, and chunk assembly

### Without `--resume`

If the temp tile directory exists with tile files, the generator raises an error:

```
Temporary tile directory already exists with tile files: …
Remove it before re-running the generator, or pass --resume to continue.
```

Either pass `--resume` or delete the temp directory manually:

```bash
rm -rf catalogs/gaia_dr3_g14/.tmp_healpix_order5
```

Partially written `.ansp` chunk files are deleted automatically on error, so the output directory itself does not need manual cleanup.

---

## Output layout

A successful run produces:

```
catalogs/gaia_dr3_g14/
  catalog_meta.json
  gaia_dr3_g14_chunk_00.ansp
  gaia_dr3_g14_chunk_01.ansp
  ...
  gaia_dr3_g14_chunk_47.ansp
```

`catalog_meta.json` contains format version, chunk list, per-chunk star counts, file sizes, and SHA-256 hashes. The application reads this manifest: the installed-catalog inspection that Catalogs & Data shows finds and size-checks each chunk through it (`inspectGaiaCatalogDirectoryDetailed`, `GaiaCatalogSettings.h`), the downloader verifies each chunk's SHA-256 against it (`CatalogDownloadService`), and a plate solve parses it with the engine's `parseGaiaCatalogManifest` (`GaiaCatalogSource`).

Each `.ansp` chunk contains:
- 16-byte binary preamble (magic `ANSP` + version metadata)
- UTF-8 JSON chunk header (tile range, catalog identity, star count)
- Packed tile index table (256 entries × 12 bytes each)
- Contiguous 64-byte v3 Gaia star records

---

## Timing reference

Measured on a machine with an 80 Mbit/s connection to ESA, `--workers 8`, `--tap-poll-seconds 0.5`:

| Catalog | Wall time (approx.) |
|---------|---------------------|
| `g12`   | 10–20 minutes       |
| `g14`   | 1–3 hours           |
| `g16`   | 4–10 hours          |
| `g18`   | 6–20+ hours         |

ESA server load varies significantly; Friday afternoons and the first week after a Gaia data release are noticeably slower. For g18+, use `--resume` so interrupted runs don't lose progress.

---

## Custom catalog (non-standard magnitude limit)

```bash
python3 tools/generate_gaia_catalog.py --tap-official \
    --catalog-id gaia_dr3_g13 \
    --catalog-name "Gaia DR3 G<=13" \
    --mag-limit 13.0 \
    catalogs/gaia_dr3_g13
```

---

## Running the tests

```bash
python3 -m pytest tools/test_generate_gaia_catalog.py -v
```

The unit tests cover the v3 record format, chunk output, TAP query construction, retry and resumable-outage handling, the TAP pixel store, chunk resume, progress reporting, and edge cases (zero-star tiles, missing BP/RP, sentinel handling).
