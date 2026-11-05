# Catalog Distribution Guide

How to generate, upload, and serve AfterNight star catalogs (.ansp chunks).

---

## Hosting

Catalogs are hosted on **Hugging Face Hub** as a public dataset:

- **Repository:** `emruiz81/afternight-gaia-catalogs`
- **URL:** https://huggingface.co/datasets/emruiz81/afternight-gaia-catalogs
- **Direct download base:** `https://huggingface.co/datasets/emruiz81/afternight-gaia-catalogs/resolve/main/`

### Why Hugging Face?

- 300 GB free storage (fits all catalog tiers)
- No per-file size limit for Git LFS (chunks are ~22–380 MB)
- Direct download URLs with HTTPS CDN (no login required for public datasets)
- `QUrl::resolved()` relative chunk resolution works with the `/resolve/main/` URL scheme
- Built-in versioning via Git branches/tags

### Repository layout

```
afternight-gaia-catalogs/
├── README.md                              # Dataset card (HF renders this)
├── gaia_dr3_g12/
│   ├── catalog_meta.json
│   ├── gaia_dr3_g12_chunk_00.ansp
│   ├── ...
│   └── gaia_dr3_g12_chunk_47.ansp
├── gaia_dr3_g14/
│   ├── catalog_meta.json
│   ├── gaia_dr3_g14_chunk_00.ansp
│   ├── ...
│   └── gaia_dr3_g14_chunk_47.ansp
├── gaia_dr3_g16/
│   ├── catalog_meta.json
│   ├── gaia_dr3_g16_chunk_00.ansp
│   ├── ...
│   └── gaia_dr3_g16_chunk_47.ansp
└── gaia_dr3_g18/
    ├── catalog_meta.json
    ├── gaia_dr3_g18_chunk_00.ansp
    ├── ...
    └── gaia_dr3_g18_chunk_47.ansp
```

Each catalog pack lives in its own directory. The manifest (`catalog_meta.json`) and all chunk files must be siblings in the same directory. The app resolves chunk URLs relative to the manifest URL.

---

## Prerequisites

1. **Hugging Face account** with write access to the dataset repository.

2. **Hugging Face CLI** installed and authenticated.

   On Debian/Ubuntu and other PEP 668 environments, do not install into the system Python with `pip install ...` or you may hit `externally-managed-environment`.

   Option A: standalone CLI install
   ```bash
   curl -LsSf https://hf.co/cli/install.sh | bash
   source ~/.bashrc
   hf auth login    # Paste your HF access token (write scope)
   ```

   If you do not want to reload your shell, run it once with:
   ```bash
   env PATH="$HOME/.local/bin:$PATH" hf auth login
   ```

   Option B: isolated Python virtual environment
   ```bash
   sudo apt install python3-venv
   python3 -m venv .venv-hf
   . .venv-hf/bin/activate
   python -m pip install -U "huggingface_hub[cli]"
   hf auth login    # Paste your HF access token (write scope)
   ```

3. **Git LFS** installed (HF uses LFS for large files):
   ```bash
   sudo apt install git-lfs   # or: brew install git-lfs
   git lfs install
   ```

---

## Step 1: Generate the catalog

Use the Python generator with `--tap-official` and a preset:

```bash
cd /path/to/afternight

# G≤14 (recommended, ~17M stars, ~1.0 GB, takes several hours)
python3 tools/generate_gaia_catalog.py --tap-official --preset g14 catalogs/gaia_dr3_g14

# G≤16 (~78M stars, ~4.65 GB)
python3 tools/generate_gaia_catalog.py --tap-official --preset g16 catalogs/gaia_dr3_g16

# G≤18 (~305M stars, ~18 GB — long running)
python3 tools/generate_gaia_catalog.py --tap-official --preset g18 catalogs/gaia_dr3_g18
```

This produces a directory with `catalog_meta.json` + 48 `.ansp` chunk files.

### Validate the output

```bash
# Quick structural check
python3 -m pytest tools/test_generate_gaia_catalog.py -v

# Verify star counts against ESA TAP (optional)
python3 -c "
import json
with open('catalogs/gaia_dr3_g14/catalog_meta.json') as f:
    meta = json.load(f)
print(f'Stars: {meta[\"total_stars\"]:,}')
print(f'Chunks: {meta[\"chunk_count\"]}')
print(f'Skipped: {meta[\"skipped_rows\"]}')
"
```

---

## Step 2: Upload to Hugging Face

### Option A: Using `hf upload` (recommended)

```bash
# Upload a single catalog pack (e.g., G≤14)
hf upload emruiz81/afternight-gaia-catalogs \
    catalogs/gaia_dr3_g14 gaia_dr3_g14 \
    --repo-type dataset

# Upload another pack
hf upload emruiz81/afternight-gaia-catalogs \
    catalogs/gaia_dr3_g16 gaia_dr3_g16 \
    --repo-type dataset
```

Arguments:
- `emruiz81/afternight-gaia-catalogs` — the HF dataset repo
- `catalogs/gaia_dr3_g14` — local source directory
- `gaia_dr3_g14` — remote path prefix (directory in the repo)
- `--repo-type dataset` — required for dataset repos

### Option B: Using `huggingface_hub` Python API

```python
from huggingface_hub import HfApi

api = HfApi()

# Upload an entire catalog directory
api.upload_folder(
    repo_id="emruiz81/afternight-gaia-catalogs",
    folder_path="catalogs/gaia_dr3_g14",
    path_in_repo="gaia_dr3_g14",
    repo_type="dataset",
)
```

### Option C: Using Git LFS (for incremental updates)

```bash
# Clone the dataset repo (first time only)
git clone https://huggingface.co/datasets/emruiz81/afternight-gaia-catalogs
cd afternight-gaia-catalogs

# Track .ansp files with LFS
git lfs track "*.ansp"

# Copy catalog files into the repo
cp -r /path/to/catalogs/gaia_dr3_g14 gaia_dr3_g14/

# Commit and push
git add gaia_dr3_g14/
git commit -m "Add Gaia DR3 G<=14 catalog (v3, 48 chunks)"
git push
```

---

## Step 3: Verify the upload

After uploading, verify each catalog pack is accessible:

```bash
python3 << 'EOF'
import urllib.request, json

BASE = "https://huggingface.co/datasets/emruiz81/afternight-gaia-catalogs/resolve/main"

for pack in ["gaia_dr3_g14", "gaia_dr3_g16"]:
    url = f"{BASE}/{pack}/catalog_meta.json"
    print(f"Fetching {url} ...")
    req = urllib.request.Request(url, headers={"User-Agent": "AfterNight Validator"})
    with urllib.request.urlopen(req, timeout=30) as resp:
        meta = json.loads(resp.read())
    print(f"  OK: {meta['total_stars']:,} stars, {meta['chunk_count']} chunks")

    # Spot-check first chunk is downloadable (HEAD request)
    chunk_file = meta["chunks"][0]["file"]
    chunk_url = f"{BASE}/{pack}/{chunk_file}"
    req = urllib.request.Request(chunk_url, method="HEAD",
                                headers={"User-Agent": "AfterNight Validator"})
    with urllib.request.urlopen(req, timeout=15) as resp:
        print(f"  Chunk 0: {resp.status} OK")
    print()
EOF
```

---

## Step 4: Update the application (if adding a new pack)

If you add a new magnitude tier (e.g., G≤20, which the generator's `g20` preset builds), update
`availableGaiaCatalogPacks()` in `src/AfterNightControls/app/GaiaCatalogSettings.h`:

```cpp
CatalogPackDescriptor{
    .id = QStringLiteral("gaia_dr3_g20"),
    .displayName = QStringLiteral("Gaia DR3 G<=20"),
    .description = QStringLiteral("..."),
    .manifestUrl = QUrl(base_url + QStringLiteral("/gaia_dr3_g20/catalog_meta.json")),
    .distributionType = QStringLiteral("chunks"),
    .recommended = false,
    .downloadSizeBytes = 90LL * 1024LL * 1024LL * 1024LL,
    .installedSizeBytes = 90LL * 1024LL * 1024LL * 1024LL,
},
```

Key rules:
- `manifestUrl` must point to `catalog_meta.json` inside the pack's directory
- Chunk filenames in the manifest are resolved relative to the manifest URL
- `downloadSizeBytes` and `installedSizeBytes` are equal (no compression/extraction)
- After generating a new pack, update the sizes to match actual output

---

## Updating an existing catalog

To replace an existing catalog pack:

1. Regenerate the catalog locally
2. Re-upload the entire directory (HF overwrites files with the same path):
   ```bash
   hf upload emruiz81/afternight-gaia-catalogs \
       catalogs/gaia_dr3_g14 gaia_dr3_g14 \
       --repo-type dataset
   ```
3. The `sha256` values in `catalog_meta.json` will be different, so existing users' partial downloads will re-verify and re-download changed chunks

---

## Adding a mirror

The download service resolves chunk URLs relative to the manifest URL and has no host-specific logic. Mirror base URLs are configured in `catalog_mirrors.json` — no recompilation needed.

### `catalog_mirrors.json`

The app auto-generates this file in the application data directory on first use. If it already exists, it is left untouched — users can edit it to override the mirror URL.

**Location:**
- Linux: `~/.local/share/AfterNight/catalog_mirrors.json`
- Windows: `%LOCALAPPDATA%/AfterNight/catalog_mirrors.json`

File format:

```json
{
    "version": 1,
    "mirrors": [
        "https://huggingface.co/datasets/emruiz81/afternight-gaia-catalogs/resolve/main"
    ]
}
```

The first mirror URL in the array is used. The URL must be an HTTPS base URL without trailing slash. The app appends `/{pack_id}/catalog_meta.json` to form the manifest URL for each catalog pack.

The `version` field allows future migrations — if the schema changes, the app can detect the older version and update accordingly.

### Adding a new mirror

1. Upload the same directory structure to the mirror host (files must be directly downloadable via HTTPS)
2. Edit `catalog_mirrors.json` to replace (or prepend) the mirror URL
3. The mirror must support HTTPS and allow redirects that don't downgrade to HTTP (`NoLessSafeRedirectPolicy`)

Potential future mirrors: GitHub Releases, Internet Archive, Cloudflare R2, self-hosted.

---

## Catalog sizes (verified)

| Pack | Stars | Size | Chunks |
|------|-------|------|--------|
| G≤12 | 3.1M | ~188 MB | 48 |
| G≤14 | 16.8M | 1.0 GB | 48 |
| G≤16 | 78.0M | 4.65 GB | 48 |
| G≤18 | 305M | ~18 GB | 48 |

Sizes are for v3 records (64 bytes/star). G≤12 and G≤18 sizes are computed from verified TAP counts; G≤14 and G≤16 are measured from generated catalogs.
