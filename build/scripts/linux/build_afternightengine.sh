#!/bin/bash
# Builds only the AfterNightEngine project, its optional adapters and its direct suites, to shorten
# the engine's edit/test loop. It is not a replacement for the numbered build scripts: it builds a
# few targets out of the normal graph and skips nothing else, so the full-product build is still the
# one that decides a change to the product.
#
# The Windows counterpart is build/scripts/msvc/build_afternightengine.bat.
#
# Usage: build_afternightengine.sh [release|debug] [--with-tests]
set -e
cd "$(dirname "${BASH_SOURCE[0]}")"

CONFIG="release"
WITH_TESTS=0
for argument in "$@"; do
    case "$argument" in
        release|debug) CONFIG="$argument" ;;
        --with-tests) WITH_TESTS=1 ;;
        *)
            echo "Usage: build_afternightengine.sh [release|debug] [--with-tests]"
            exit 2
            ;;
    esac
done

MAKE_CONFIG="${CONFIG}_x64"

# An instrumented generation and an ordinary one write their binaries to one directory and their
# objects to different ones, so after a switch make finds every output newer than the objects it
# would compile and rebuilds nothing at all - leaving the previous instrumentation in bin with
# nothing to say so. What the tree was generated for is recorded beside the binaries, and a change
# of it forces every target this script builds rather than trusting a timestamp.
TREE_SANITIZER="none"
if [ -f ../../make-linux/AfterNightEngine.make ]; then
    # The libFuzzer generation instruments the engine for coverage with the address sanitizer, so it
    # is read first: its flags name the address sanitizer too.
    if grep -q -- "-fsanitize=fuzzer-no-link" ../../make-linux/AfterNightEngine.make; then
        TREE_SANITIZER="fuzzer"
    elif grep -q -- "-fsanitize=address" ../../make-linux/AfterNightEngine.make; then
        TREE_SANITIZER="address"
    elif grep -q -- "-fsanitize=thread" ../../make-linux/AfterNightEngine.make; then
        TREE_SANITIZER="thread"
    fi
fi
# The fuzz targets carry an instrumentation of their own: a tree generated for libFuzzer links
# libFuzzer's `main` into them and leaves the standalone driver out, which nothing else in the tree
# does. A timestamp cannot see that either - the binaries from the previous generation are newer
# than the sources - so make rebuilds nothing and the campaign runs the driver it was told it had
# replaced. The record carries this half too, and a change of it alone forces the fuzz targets alone.
# The libFuzzer generation records `fuzzer+fuzzer`: the libraries' coverage instrumentation, then the
# targets' entry point. The Windows counterpart needs none of it: there the generation leaves the
# fuzz targets as they were.
TREE_FUZZ=""
if [ -f ../../make-linux/tests/Fuzz_EngineDescriptor.make ] &&
   grep -q -- "-fsanitize=fuzzer" ../../make-linux/tests/Fuzz_EngineDescriptor.make; then
    TREE_FUZZ="+fuzzer"
fi
SANITIZER_MARKER="../../../bin/clang/${CONFIG}/.engine-instrumentation"
BUILT_RECORD="none"
if [ -f "$SANITIZER_MARKER" ]; then
    BUILT_RECORD="$(cat "$SANITIZER_MARKER")"
fi
BUILT_SANITIZER="${BUILT_RECORD%%+*}"
BUILT_FUZZ=""
case "$BUILT_RECORD" in
    *+fuzzer) BUILT_FUZZ="+fuzzer" ;;
esac
MAKE_FORCE=""
if [ "$TREE_SANITIZER" != "$BUILT_SANITIZER" ]; then
    echo "Instrumentation changed from \"$BUILT_SANITIZER\" to \"$TREE_SANITIZER\"; rebuilding every target this script builds."
    MAKE_FORCE="-B"
fi
FUZZ_MAKE_FORCE="$MAKE_FORCE"
if [ -z "$FUZZ_MAKE_FORCE" ] && [ "$TREE_FUZZ" != "$BUILT_FUZZ" ]; then
    echo "Fuzz instrumentation changed from \"${BUILT_FUZZ:-none}\" to \"${TREE_FUZZ:-none}\"; rebuilding the fuzz targets."
    FUZZ_MAKE_FORCE="-B"
fi

echo "****************build_afternightengine_${CONFIG}****************"

# The generated graph is the only thing this script may build from, so a missing tree is a
# prerequisite error rather than something to work around by invoking make on a project file.
if [ ! -d ../../make-linux ]; then
    echo "Projects are not generated. Run 01-create_project_files.sh first."
    exit 1
fi

pushd ../../make-linux
make $MAKE_FORCE -j"$(nproc)" config="$MAKE_CONFIG" AfterNightEngine
# The optional adapters are separate shared libraries that link the engine's published surface.
# Nothing else links them, so building them here is how the loop keeps proving that the optional
# seams still compile against the surface they borrow.
make $MAKE_FORCE -j"$(nproc)" config="$MAKE_CONFIG" AfterNightEnginePythonAdapter
make $MAKE_FORCE -j"$(nproc)" config="$MAKE_CONFIG" AfterNightEngineGmicAdapter
popd

if [ "$WITH_TESTS" -eq 1 ]; then
    # The engine's direct suites, which link the engine and nothing else of AfterNight's. The rest
    # of the test workspace is deliberately not built here.
    pushd ../../make-linux/tests
    # EngineBenchCommon is named rather than left to the fixture-bridge suite's dependency: that
    # suite links the static library rather than compiling it, and a tree regenerated with or
    # without AFTERNIGHT_SANITIZE leaves the previous one in place otherwise.
    make $MAKE_FORCE -j"$(nproc)" config="$MAKE_CONFIG" EngineBenchCommon
    # Test_EngineCore replaces the global allocation functions, and the ThreadSanitizer runtime defines
    # the same ones, so a thread-instrumented build of it fails to link. The sanitizer matrix already
    # skips that suite in that configuration for the same reason; building it anyway would stop this
    # script before any of the suites the campaign does run had been built.
    if [ "$TREE_SANITIZER" = "thread" ]; then
        echo "Skipping Test_EngineCore: it replaces the global allocation functions the ThreadSanitizer runtime defines."
    else
        make $MAKE_FORCE -j"$(nproc)" config="$MAKE_CONFIG" Test_EngineCore
    fi
    make $MAKE_FORCE -j"$(nproc)" config="$MAKE_CONFIG" Test_EngineRuntime
    make $MAKE_FORCE -j"$(nproc)" config="$MAKE_CONFIG" Test_EngineImage
    make $MAKE_FORCE -j"$(nproc)" config="$MAKE_CONFIG" Test_EngineImageOps
    make $MAKE_FORCE -j"$(nproc)" config="$MAKE_CONFIG" Test_EngineCalibration
    make $MAKE_FORCE -j"$(nproc)" config="$MAKE_CONFIG" Test_EngineStars
    make $MAKE_FORCE -j"$(nproc)" config="$MAKE_CONFIG" Test_EngineRegistration
    make $MAKE_FORCE -j"$(nproc)" config="$MAKE_CONFIG" Test_EngineAstrometry
    make $MAKE_FORCE -j"$(nproc)" config="$MAKE_CONFIG" Test_EngineColor
    make $MAKE_FORCE -j"$(nproc)" config="$MAKE_CONFIG" Test_EngineStacking
    make $MAKE_FORCE -j"$(nproc)" config="$MAKE_CONFIG" Test_EngineInterop
    make $MAKE_FORCE -j"$(nproc)" config="$MAKE_CONFIG" Test_EngineKernels
    make $MAKE_FORCE -j"$(nproc)" config="$MAKE_CONFIG" Test_EngineFaults
    make $MAKE_FORCE -j"$(nproc)" config="$MAKE_CONFIG" Test_EngineFixtureBridge
    make $MAKE_FORCE -j"$(nproc)" config="$MAKE_CONFIG" Test_EnginePublicHeaders
    make $MAKE_FORCE -j"$(nproc)" config="$MAKE_CONFIG" Test_EngineConsumerProbes
    make $MAKE_FORCE -j"$(nproc)" config="$MAKE_CONFIG" Test_EngineLibraryParity
    make $MAKE_FORCE -j"$(nproc)" config="$MAKE_CONFIG" Test_EnginePythonAdapter
    make $MAKE_FORCE -j"$(nproc)" config="$MAKE_CONFIG" Test_EngineGmicAdapter
    # The fuzz targets, which are built by the same loop for the same reason the suites are:
    # they link the engine and nothing else, and a campaign whose targets stopped compiling
    # would otherwise be discovered only when somebody ran it.
    make $FUZZ_MAKE_FORCE -j"$(nproc)" config="$MAKE_CONFIG" Fuzz_EngineDescriptor
    make $FUZZ_MAKE_FORCE -j"$(nproc)" config="$MAKE_CONFIG" Fuzz_EngineConversion
    make $FUZZ_MAKE_FORCE -j"$(nproc)" config="$MAKE_CONFIG" Fuzz_EnginePixelMath
    make $FUZZ_MAKE_FORCE -j"$(nproc)" config="$MAKE_CONFIG" Fuzz_EngineMetadata
    make $FUZZ_MAKE_FORCE -j"$(nproc)" config="$MAKE_CONFIG" Fuzz_EngineCodec
    make $FUZZ_MAKE_FORCE -j"$(nproc)" config="$MAKE_CONFIG" Fuzz_EngineSequence
    popd
fi

# The record states what this invocation built. The fuzz half is claimed only when the fuzz targets
# were among them, so a build without the suites leaves the previous claim standing rather than
# asserting an instrumentation nothing here produced.
RECORD_FUZZ="$BUILT_FUZZ"
if [ "$WITH_TESTS" -eq 1 ]; then
    RECORD_FUZZ="$TREE_FUZZ"
fi
mkdir -p "$(dirname "$SANITIZER_MARKER")"
echo "${TREE_SANITIZER}${RECORD_FUZZ}" > "$SANITIZER_MARKER"

echo Done!
