# SIMD candidate spike

Evaluation code from choosing the engine's vector abstraction. It measures Highway, xsimd, SLEEF,
OpenCV's universal intrinsics, and plain compiler auto-vectorization against an unvectorized scalar
reference, over five kernel shapes at two sizes. Highway was adopted; xsimd and SLEEF were declined.

## This is not part of the build

There is no Premake project here and no entry in either workspace. Nothing under `src/` includes it,
no generated solution references it, and `build/scripts/tools/check_build_graph.py` would not see it
if it did. It compiles through its own driver:

```
python build/spike/simd/run_spike.py [--compiler msvc|clang] [--out results.csv]
```

The driver builds each candidate as its own translation unit - which is what makes the object sizes
comparable - links one executable, runs it, and writes `results.csv`, `objectsizes.csv` and the
pipeline drift table.

It detects the platform rather than being told. Windows offers both compilers the project ships
there; Linux offers `clang`, which is what its build scripts use. The Linux run that produced
`results-linux-clang.csv` was:

```
python3 build/spike/simd/run_spike.py --compiler clang \
    --out build/spike/simd/results-linux-clang.csv \
    --sizes-out build/spike/simd/objectsizes-linux-clang.csv \
    --drift-out build/spike/simd/drift-linux-clang.csv \
    --settle-seconds 60 --thermal-warmup-runs 1
```

`--settle-seconds` and `--thermal-warmup-runs` exist for the laptop: they idle after the build and
then burn one discarded pass, so the measured run starts from thermal steady state instead of from
whatever the compile left behind. `--drift-out` keeps one host's drift table from overwriting
another's. On that host the measured runs were also pinned to one physical core with `taskset`,
which halves the cache-resident run-to-run spread.

It requires `highway[contrib]`, `xsimd` and `sleef` in the vcpkg tree, at the same port versions on
both platforms (1.4.0, 14.3.0 and 3.9.0). `highway[contrib]` is in both dependency scripts,
because the engine adopted it and its vector kernels compile against it. **`xsimd` and `sleef`
remain installed on the benchmark hosts only and deliberately absent from `02-install_msvc_deps.bat`
and `02-install_linux_deps.sh`**: measuring a library is not adopting it, and both were declined.
OpenCV is the exception in the other direction - it is already a product dependency, and the Linux
build takes it from `libopencv-dev` through `pkg-config`, which is where the Linux product takes it
from.

## Deleting this directory

The choice it informed is made and applied, so this has no further purpose. Removing it must leave
the product build untouched; if it ever does not, something has grown a dependency on evaluation
code, which is itself the bug.

## Reading the exit code

The driver's executable returns non-zero when a candidate disagrees with the scalar reference by
more than a combined 1e-6 absolute plus 1e-5 relative tolerance. It is not a benchmark threshold -
timings are never asserted - only a guard that a candidate measured as fast is also computing the
right answer. A kernel that is fast because it is wrong is the failure mode worth catching
automatically.
