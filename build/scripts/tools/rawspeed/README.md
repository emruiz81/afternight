# RAW backend qualification

These repository-owned tools qualify the RawSpeed-accelerated RAW decoder against the native LibRaw
path: a hash-verified corpus gate, the runtime and fault checks, the native-only rollback and an
actual-engine benchmark. They use deployed engines and private packages, never local experimental
binaries or sources.
All inputs are read-only. RAW files stay outside Git. Missing files, hash mismatches, missing tools,
unexpected routes and incomplete output fail the gate instead of becoming skips.

## Inputs and independent comparisons

Supply `--root <corpus-root>` matching the manifest's relative paths or filenames, or `--paths
<local.json>`, a JSON object from manifest ID to literal path. The six established environment paths
are also recognized. `verify --download --root ...` can fetch the manifest's licensed public URLs;
it cannot supply the maintainer's six Canon/Nikon originals. All 37 identities must be available.
The manifest hash is canonical JSON, so Windows/Linux checkout line endings do not change identity.

`RawStockOracle` and `RawPrivateOracle` are separate executables linked to different libraries.
They compare complete uncropped mosaic rows and final visible samples, calibrated metadata and
CFA tables. `RawEngineBench` calls the real prepared codec through `decodeImage`. Neither oracle
uses the engine's extraction implementation. The corpus
gate checks the independent reference every time and also enforces checked-in semantic/sample
goldens. Updating packages, camera definitions, correction or numerical compiler flags requires
new qualification and a port revision change; do not regenerate goldens from the candidate alone.

RawSpeed is compiled in by default. `AFTERNIGHT_RAWSPEED=0` is only a dependency/rollback build
option. Runtime callers set `CodecReadRequest::rawDecoderPreference` to Automatic or NativeLibRaw
for each new read; an omitted override inherits the adapter default. Neither a rebuild nor context
replacement is needed. The engine owns no global setting; the application copies its synchronized
`enableRawSpeed` flag into the request.

The JSON-line `RawEngineBench` host accepts `enableRawSpeed: false/true` for runtime disable/enable,
or `preference: "native"/"automatic"/"required"` for the explicit read override. `adapterPreference`
separately sets the immutable default. `testRuntimeToggle: true` qualifies three reads in one
context while retaining their frames: on, off during the first unpack, then on again. The first
read finishes accelerated and the second uses native. The corpus gate runs this on every eligible
input, together with the ordinary independent sensor/metadata, failure and cancellation checks.

## Windows

Follow the numbered generator, dependency, third-party, dependency-deploy and main/test build
scripts in AGENTS.md for Release and Debug. `02` calls `install_raw_deps.bat`; the dedicated
`x64-windows-raw` subtree uses MSVC LibRaw and Visual Studio's clang-cl for RawSpeed. `04` validates
the complete private PE import closure before copying. Then, from the repository root:

```text
build\scripts\msvc\build_raw_qualification.bat Release
build\scripts\msvc\build_raw_qualification.bat Debug
python build/scripts/tools/rawspeed/qualify.py qualify --bin bin/msvc/release --paths <local.json> --output <evidence>/release.json
python build/scripts/tools/rawspeed/qualify.py qualify --bin bin/msvc/debug --paths <local.json> --output <evidence>/debug.json
python build/scripts/tools/rawspeed/check_runtime.py --bin bin/msvc/release --config release --paths <local.json> --fault-library obj/raw-qualification/Release/fault/afternight_rawspeed.dll --output <evidence>/runtime-release.json
python build/scripts/tools/rawspeed/check_runtime.py --bin bin/msvc/debug --config debug --paths <local.json> --fault-library obj/raw-qualification/Debug/fault/afternight_rawspeedd.dll --output <evidence>/runtime-debug.json
build\scripts\msvc\check_raw_cache.bat
```

`check_raw_cache.bat` deliberately reinstalls only the two private packages. It proves clean source
builds, cache-only restore and cache misses after changing wrapper content or actual compiler bytes
at a fixed path. The compiler probe uses an isolated copy with an inert PE overlay; it never modifies
the installed VS compiler. It restores the checked-in wrapper and production package at completion.
Run `04a`/`04b` again if package bytes changed. Its logs and hashes are `obj/raw-cache-proof/`.

`check_runtime.py` copies the deployed dependency closure into an isolated temporary directory,
loads the engine with a native loader probe and checks independent stock/private symbols.
Its fault DLL exists only under `obj/.../fault`, is never installed/deployed, and replaces a DLL
only in the disposable directory. Seven typed faults exercise Automatic and Required: recoverable
failures reopen native once, whereas allocation/invalid/internal failures publish nothing and stop.

## Linux host

Run the numbered Linux generator, dependency, third-party, main build, deploy and test scripts in
AGENTS.md in both configurations. The private triplet pins `/usr/bin/clang-21` and `clang++-21`.
Run these commands on the Linux benchmark host:

```text
./build/scripts/linux/build_raw_qualification.sh Release
./build/scripts/linux/build_raw_qualification.sh Debug
python3 build/scripts/tools/rawspeed/qualify.py qualify --bin bin/clang/release --root <corpus-root> --output <evidence>/release.json
python3 build/scripts/tools/rawspeed/qualify.py qualify --bin bin/clang/debug --root <corpus-root> --output <evidence>/debug.json
python3 build/scripts/tools/rawspeed/check_runtime.py --bin bin/clang/release --config release --output <evidence>/runtime-release.json
python3 build/scripts/tools/rawspeed/check_runtime.py --bin bin/clang/debug --config debug --output <evidence>/runtime-debug.json
./build/scripts/linux/check_raw_sanitizers.sh --root <corpus-root>
```

The runtime check rejects exported private C/C++ RAW or JPEG/zlib symbols and unexpected shared
RAW/OpenMP dependencies; it loads the engine using `RTLD_GLOBAL`. The sanitizer helper recompiles
the installed patched source and wrapper into `obj/raw-sanitizers`, with ASan/UBSan and Debug
assertions at `-O1`, then runs the fixture-free native integer regression and complete independent boundary corpus and real decoder allocation injections. It does not edit a
package or vcpkg buildtree. Private package directories are pinned explicitly because vcpkg
prepends its ordinary install tree to CMake searches; an incomplete instrumented prefix is refused.
LeakSanitizer is disabled consistently with the engine host workflow;
this is not TSan evidence. Run the ordinary engine sanitizer and applicable TSan gates as documented
in `LOCAL_VALIDATION.md` as well. Keep the outputs with the evidence; Linux completion is not
inferred from a Windows run. Automatic routes every qualified mode on Windows and every one but Sony
ARW2 on Linux, where that mode did not meet the timing criterion; `qualify` asserts that route per host.

Prove the Linux source build and local cache restore before deployment. From the repository root,
retain the three install logs below; removal touches only the private RAW subtree. The Windows
proof separately exercises wrapper and same-path clang-cl byte invalidation, the mixed-compiler
cache requirement. Linux's triplet also hashes both pinned compiler executables.

```bash
set -euo pipefail
mkdir -p obj/raw-cache-linux
./vcpkg/vcpkg remove afternight-rawspeed:x64-linux-raw --recurse
./build/scripts/linux/install_raw_deps.sh --binarysource=clear > obj/raw-cache-linux/source.log 2>&1
./vcpkg/vcpkg remove afternight-rawspeed:x64-linux-raw --recurse
./build/scripts/linux/install_raw_deps.sh > obj/raw-cache-linux/populate.log 2>&1
./vcpkg/vcpkg remove afternight-rawspeed:x64-linux-raw --recurse
./build/scripts/linux/install_raw_deps.sh --only-binarycaching > obj/raw-cache-linux/restore.log 2>&1
```

## Actual-engine performance

Use the same deployed binaries as functional qualification, an otherwise idle host and the fixed
host power profile. The default is one thread per operation, two warmups, nine randomized rounds,
and width-two throughput through independent contexts. Reports retain each trial, median, range,
MAD, phase traces, actual decoder selection, engine bytes and fresh-process peak working set.
The Automatic criterion is exactness followed by at least 10% median improvement and a difference
greater than three times the sum of native/accelerated MADs. Unknown and marginal profiles stay native.

```text
python build/scripts/tools/rawspeed/qualify.py benchmark --bin bin/msvc/release --paths <local.json> --rounds 9 --output <evidence>/warm.json
python3 build/scripts/tools/rawspeed/qualify.py benchmark --bin bin/clang/release --root <corpus-root> --rounds 9 --output <evidence>/warm.json
python3 build/scripts/tools/rawspeed/qualify.py benchmark --bin bin/clang/release --root <corpus-root> --rounds 9 --cold-cache --output <evidence>/cold.json
```

Windows timings are warm OS-cache observations; a fresh process does not imply a cold file.
`--cold-cache` is Linux-only: it flushes/evicts just the fixture, then requires `mincore` to report
zero resident pages before every measured decode. It never purges the whole machine's cache and
fails if eviction cannot be proved. Eviction time is excluded; code pages remain warm. File hashing
is outside timing. `--only-qualified` labels a timing rerun of the 24 accelerated inputs while
still verifying all 37 inputs first; it cannot reduce a functional qualification set.

## Native-only rollback and common gates

Set `AFTERNIGHT_RAWSPEED=0` in the environment before generation, then build the engine with
`--with-tests` and rebuild the qualification tools. Both helpers omit private-package oracle
targets in this mode. Keep the normal stock dependency/deployment prerequisites. Run:

```text
python build/scripts/tools/rawspeed/native_only.py --bin bin/msvc/release --paths <local.json> --output <evidence>/native-only.json
```

Use the corresponding Linux paths/`python3` there. This compares all native/Automatic outputs to
the 37 goldens and requires unavailable-acceleration failures; Windows also checks the engine's
stock import identity. Unset the variable, regenerate, deploy the private dependency closure and
rebuild to restore acceleration. Run Release/Debug RAW/image/public-header and broader affected
tests, the common checks of `LOCAL_VALIDATION.md`, deployment checker tests, include-layer and budget
tooling tests, clang-tidy and repository formatting. Record new compile units only for the host that
measured them, without replacing its existing records.
