# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Validate the physical layering of AfterNightEngine, in its source rather than its build.

The engine is split into a published surface and an implementation, and the implementation
is split again into layers that may only depend downwards. None of that is visible to a compiler:
an include directory grants a whole tree at once, so a kernel that starts including a codec header,
or a public header that starts exposing an external library type, builds exactly as cleanly as one
that does not. The rules only exist while something reads the include lines.

What this refuses:

1. an include of any other first-party component, so the engine stays usable without the product
   built on it;
2. a public header that includes a private header, an external library, or anything by a quoted or
   relative path. The published surface is reachable from the public include root alone, and no
   external library type crosses it;
3. an include that violates the layer graph - a kernel reaching an algorithm, an algorithm domain
   reaching another domain's private headers, a layer reaching a library it does not declare;
4. a GPU SDK header anywhere but a GPU backend - the engine's own `backends/gpu` layer, or the opt-in
   backend module a default build omits entirely;
5. a file in a directory no layer declares, or a declared layer directory that has gone missing;
6. an umbrella header - a public header that only re-exports other public headers;
7. a public header without a self-compile translation unit, which is how the surface stays provably
   self-contained rather than accidentally order-dependent; and
8. an implementation file that does not include its own header first, or a header without a
   pragma-once guard; and
9. an optional adapter - a separate binary that puts one external library behind this engine's
   vocabulary - reaching anything but its own headers, the engine's published surface, and the one
   library it declares. Its own published surface carries no more of that library than the engine's
   does, which is what lets a consumer compile against an adapter it has not installed the library
   for. The opt-in GPU backend is held to the same rules, with a GPU SDK as its one library.

Usage:
    python build/scripts/tools/check_include_layers.py [--repo-root <dir>]

Exits 0 when every rule holds, 1 otherwise, printing one line per problem.

Tests: build/scripts/tools/check_include_layers_test.py
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]

ENGINE_DIR = 'src/AfterNightEngine'
PUBLIC_DIR = ENGINE_DIR + '/include/AfterNightEngine'
PRIVATE_DIR = ENGINE_DIR + '/src'
SELF_COMPILE_DIR = 'src/AfterNightTests/Test_EnginePublicHeaders/SelfCompile'

# The include prefix a public header is reached by: the engine's name, the directory below its include root.
PUBLIC_INCLUDE_PREFIX = 'AfterNightEngine/'

SOURCE_SUFFIXES = ('.h', '.hpp', '.inl', '.cpp')
HEADER_SUFFIXES = ('.h', '.hpp')

# Subject areas of the published surface. A public header lives in exactly one of them.
PUBLIC_AREAS = ('Algorithms', 'Core', 'IO', 'Image', 'Interop', 'Runtime')

# Areas whose contents are grouped one level further, by domain: Algorithms/Registration and so on.
PUBLIC_AREAS_WITH_DOMAINS = ('Algorithms',)

# Implementation layers, named relative to the private root.
PRIVATE_LAYERS = (
    'algorithms',
    'backends/cpu',
    'backends/gpu',
    'core',
    'image',
    'interop',
    'io',
    'kernels/reference',
    'memory',
    'ported',
    'runtime',
)

# Layers whose contents are grouped one level further: by domain, by adapted library, by codec.
PRIVATE_LAYERS_WITH_DOMAINS = ('algorithms', 'interop', 'io')

# Which public areas a public header of a given area may include.
#
# The image model sits above the runtime rather than beside it, and the direction is the one the
# memory says: an image's samples come from an engine context's memory resource, and no memory
# resource, scheduler or dispatch table has any reason to know what an image is. So Runtime may not
# include Image, and Image's *published* headers do not include Runtime either - `ImageStorage.h`
# names the context and the resource it borrows from without including either header, which is what
# keeps a kernel header that composes a storage from inheriting the runtime's include closure. The
# implementation is where that dependency is real, and it is declared below rather than here.
PUBLIC_AREA_DEPENDENCIES = {
    'Core': ('Core',),
    'Image': ('Core', 'Image'),
    'Runtime': ('Core', 'Runtime'),
    'Interop': ('Core', 'Image', 'Interop'),
    'Algorithms': ('Algorithms', 'Core', 'Image', 'Interop', 'Runtime'),
    'IO': ('Core', 'IO', 'Image', 'Interop', 'Runtime'),
}

# Which public areas each implementation layer may include.
PRIVATE_PUBLIC_DEPENDENCIES = {
    'algorithms': ('Algorithms', 'Core', 'Image', 'Interop', 'Runtime'),
    'backends/cpu': ('Core', 'Image', 'Runtime'),
    'backends/gpu': ('Core', 'Image', 'Runtime'),
    'core': ('Core',),
    'image': ('Core', 'Image', 'Runtime'),
    'interop': ('Core', 'Image', 'Interop', 'Runtime'),
    'io': ('Core', 'IO', 'Image', 'Interop', 'Runtime'),
    'kernels/reference': ('Algorithms', 'Core', 'Image', 'Runtime'),
    'memory': ('Core', 'Runtime'),
    'ported': ('Core', 'Image', 'Runtime'),
    'runtime': ('Core', 'Runtime'),
}

# Which implementation layers each implementation layer may include private headers from.
#
# Runtime and memory reach each other because the execution context owns the memory resources and
# an allocator has to see the context that owns it; that is one subsystem split across two
# directories rather than two layers with a cycle between them.
PRIVATE_PRIVATE_DEPENDENCIES = {
    'algorithms': ('algorithms', 'core', 'image', 'interop', 'kernels/reference', 'runtime'),
    'backends/cpu': ('backends/cpu', 'core', 'image', 'kernels/reference', 'runtime'),
    'backends/gpu': ('backends/gpu', 'core', 'image', 'kernels/reference', 'runtime'),
    'core': ('core',),
    'image': ('core', 'image'),
    'interop': ('core', 'image', 'interop', 'runtime'),
    'io': ('core', 'image', 'interop', 'io', 'runtime'),
    'kernels/reference': ('core', 'image', 'kernels/reference', 'runtime'),
    'memory': ('core', 'memory', 'runtime'),
    'ported': ('core', 'image', 'ported', 'runtime'),
    'runtime': ('core', 'memory', 'runtime'),
}

# Implementation headers one I/O domain shares with the others. The registry's operation-local read
# and write interfaces are what a format adapter implements - the reservation an output transaction asks an
# adapter for among them - and expose no registry implementation or external-library type; so is its rule of which forms of a payload an output intent states beyond
# its compression, which every writing adapter but XISF's refuses. The whole file a metadata parser
# reads is held the same way by every codec whose metadata addresses its file by absolute offset - the
# TIFF and camera RAW readers - and exposes no codec's type.
SHARED_IO_HEADERS = frozenset({
    'io/metadata/MetadataFile.h',
    'io/registry/PayloadForm.h',
    'io/registry/PreparedCodecRead.h',
    'io/registry/PreparedCodecWrite.h',
    'io/registry/ReservedCodecWrite.h',
})

# External libraries the engine may adapt, and the include spellings that identify each one. The
# name on the left is also the directory name an interop adapter for that library must use.
EXTERNAL_LIBRARIES = {
    'codecs': ('openfits/', 'tiff', 'png.h', 'jpeglib.h', 'jerror.h', 'libraw/', 'afternight-libraw/', 'jxl/',
               'openxisf/', 'zlib.h', 'librtprocess'),
    'eigen': ('Eigen/', 'unsupported/Eigen/'),
    # The metadata library the raster codecs read and write Exif through. It is an interop adapter
    # rather than a codec library on purpose: its types describe another library's metadata model
    # and stay inside src/interop/exiv2, so a codec domain sees cards and bytes and never an
    # Exiv2 type.
    'exiv2': ('exiv2/',),
    'fftw': ('fftw3',),
    # The video libraries the sequence source decodes through. Their allowance is a directory rather
    # than a layer: only io/sequences/ffmpeg may include them, so an FFmpeg type cannot reach another
    # format adapter, the sequence handles or any published header.
    'ffmpeg': ('libavcodec/', 'libavformat/', 'libavutil/', 'libswscale/', 'libswresample/', 'libavfilter/',
               'libavdevice/'),
    # The interpreter the optional G'MIC adapter puts behind this engine's vocabulary. The
    # directory spelling is what its C bridge is included as - `gmic/gmic_libc.h` - and the two bare
    # names are the interpreter's own header and CImg, which nothing in this repository's engine or
    # adapter tree includes.
    'gmic': ('gmic.h', 'gmic/', 'CImg.h'),
    'gpu': ('cuda', 'CL/', 'sycl/', 'hip/'),
    'gsl': ('gsl/',),
    # The JSON library two documents are read and written through: a star profile, and the
    # colour-calibration record a product's header carries hex-encoded. It is an interop adapter for
    # the reason Exiv2 is one - its value type stays inside src/interop/json, so the domains that own
    # those documents see plain records and text.
    'json': ('nlohmann/',),
    'onetbb': ('tbb/', 'oneapi/'),
    'opencv': ('opencv2/',),
    'python': ('Python.h', 'pybind11/'),
    # The operating system's own file and process headers, for what the standard library cannot
    # express: exclusive creation, durable flushes, bulk file I/O and private file mappings - Windows' and POSIX's
    # sys/mman.h, which a metadata parser reads a whole file through without a block of the budget. They are not
    # a third-party dependency either; grouping them keeps the allowance in one place, and the
    # I/O layer is the only layer that takes it.
    'platform': ('fcntl.h', 'io.h', 'process.h', 'share.h', 'unistd.h', 'sys/mman.h', 'sys/stat.h', 'sys/types.h',
                 'Windows.h'),
    # The compiler's own intrinsic and feature-query headers belong here too: intrin.h is MSVC's
    # umbrella for both, cpuid.h is the GCC/Clang half that answers what the processor supports, and
    # a translation unit that includes either is doing the same kind of thing as one that includes
    # immintrin.h. They are not a third-party dependency, so grouping them with the vector library
    # keeps the allowance in one place rather than adding a group with no library behind it.
    'simd': ('hwy/', 'xsimd', 'sleef', 'immintrin.h', 'emmintrin.h', 'xmmintrin.h', 'smmintrin.h',
             'nmmintrin.h', 'avx512', 'arm_neon.h', 'intrin.h', 'cpuid.h'),
    # The POSIX thread API, for the one thing std::thread cannot state: how much stack a thread gets.
    # No engine layer takes it - the engine's parallel work goes through its task scheduler - and an
    # optional adapter takes it only when the library it adapts needs more stack than the thread that
    # calls it can be assumed to have. The Windows half of the same call is `_beginthreadex`, whose
    # headers are already the platform group's.
    'threads': ('pthread.h',),
}

# Which external libraries each implementation layer may include.
#
# The interop layer is absent on purpose: an adapter's directory name decides its library, one per
# adapter, so an adapter under interop/opencv may include OpenCV and nothing else.
ALLOWED_EXTERNAL_LIBRARIES = {
    'algorithms': (),
    'backends/cpu': ('simd',),
    'backends/gpu': ('gpu', 'simd'),
    'core': (),
    'image': (),
    'io': ('codecs', 'platform'),
    'kernels/reference': (),
    'memory': ('onetbb',),
    'ported': (),
    'runtime': ('onetbb',),
}

# External libraries allowed below one implementation directory only, in addition to the directory's
# layer allowance. A library that belongs to one adapter inside a shared layer is quarantined here.
DIRECTORY_EXTERNAL_LIBRARIES = {
    'io/sequences/ffmpeg/': ('ffmpeg',),
}

# Other first-party components. None of them is reachable from this engine, which links only what it
# owns.
FOREIGN_COMPONENTS = (
    'AfterNightApp',
    'AfterNightBench',
    # The optional adapters. They include the engine and the engine never includes them: an engine
    # header that reached into one would have made an optional dependency a required one.
    'AfterNightEngineGmicAdapter',
    'AfterNightEnginePythonAdapter',
    # The opt-in GPU backend, for the same reason: it implements the engine's device seam from outside.
    'AfterNightEngineOpenClBackend',
    'AfterNightControls',
    'AfterNightExtensionHost',
    'AfterNightExtensionHostLite',
    'AfterNightExtensionProtocol',
    'AfterNightProcesses',
    'AfterNightShellClassic',
    'AfterNightShellCore',
)


# C standard headers, which carry a .h and would otherwise look like an undeclared library. The C++
# headers need no list: they have no extension at all.
C_STANDARD_HEADERS = frozenset([
    'assert.h', 'complex.h', 'ctype.h', 'errno.h', 'fenv.h', 'float.h', 'inttypes.h', 'iso646.h',
    'limits.h', 'locale.h', 'math.h', 'setjmp.h', 'signal.h', 'stdalign.h', 'stdarg.h',
    'stdatomic.h', 'stdbool.h', 'stddef.h', 'stdint.h', 'stdio.h', 'stdlib.h', 'stdnoreturn.h',
    'string.h', 'tgmath.h', 'threads.h', 'time.h', 'uchar.h', 'wchar.h', 'wctype.h',
])

PRAGMA_ONCE = '#pragma once'

INCLUDE_PATTERN = re.compile(r'^[ \t]*#[ \t]*include[ \t]*([<"])([^>"]+)[>"]')
BLOCK_COMMENT_PATTERN = re.compile(r'/\*.*?\*/', re.DOTALL)
LINE_COMMENT_PATTERN = re.compile(r'//[^\n]*')
PREPROCESSOR_PATTERN = re.compile(r'^[ \t]*#[^\n]*', re.MULTILINE)


class Include:
    """One include directive, with enough context to name it in a diagnostic."""

    def __init__(self, line, quoted, target):
        self.line = line
        self.quoted = quoted
        self.target = target

    def spelling(self):
        return '"%s"' % self.target if self.quoted else '<%s>' % self.target


class SourceFile:
    """One file of the engine, classified into the layer it belongs to."""

    def __init__(self, path, relative, public, area, layer, domain, includes, text):
        self.path = path
        self.relative = relative
        self.public = public
        self.area = area
        self.layer = layer
        self.domain = domain
        self.includes = includes
        self.text = text


def read_text(path):
    return path.read_bytes().decode('utf-8')


def parse_includes(text):
    """Every include directive in source order, comments already removed.

    An include line inside a block comment is not a directive, and a checker that reported one
    would be reporting prose. Removing comments first is cheaper and more accurate than trying to
    recognise them line by line, and the replacement keeps the line count so reported line numbers
    stay true.
    """
    def blank_out(match):
        return '\n' * match.group(0).count('\n')

    without_comments = LINE_COMMENT_PATTERN.sub('', BLOCK_COMMENT_PATTERN.sub(blank_out, text))
    includes = []
    for number, line in enumerate(without_comments.splitlines(), start=1):
        match = INCLUDE_PATTERN.match(line)
        if match:
            includes.append(Include(number, match.group(1) == '"', match.group(2).strip()))
    return includes


def declares_nothing(text):
    """True when a file contains no declaration of its own: only comments and preprocessor lines."""
    body = LINE_COMMENT_PATTERN.sub('', BLOCK_COMMENT_PATTERN.sub(' ', text))
    body = PREPROCESSOR_PATTERN.sub('', body)
    return body.strip() == ''


def public_area_of(relative_public_path):
    """The subject area and domain of a public header, from its path below the public root."""
    parts = relative_public_path.split('/')
    if len(parts) < 2:
        return None, None
    area = parts[0]
    if area not in PUBLIC_AREAS:
        return None, None
    domain = parts[1] if area in PUBLIC_AREAS_WITH_DOMAINS and len(parts) > 2 else None
    return area, domain


def private_layer_of(relative_private_path):
    """The layer and domain of an implementation file, from its path below the private root."""
    parts = relative_private_path.split('/')
    for layer in sorted(PRIVATE_LAYERS, key=len, reverse=True):
        prefix = layer.split('/')
        if parts[:len(prefix)] != prefix:
            continue
        remainder = parts[len(prefix):]
        if not remainder:
            return None, None
        domain = None
        if layer in PRIVATE_LAYERS_WITH_DOMAINS and len(remainder) > 1:
            domain = remainder[0]
        return layer, domain
    return None, None


def external_library_of(target):
    """The declared external library an include belongs to, or None."""
    for name in sorted(EXTERNAL_LIBRARIES):
        for spelling in EXTERNAL_LIBRARIES[name]:
            if target == spelling or target.startswith(spelling):
                return name
    return None


def is_standard_header(target):
    if '/' in target:
        return False
    if '.' not in target:
        return True
    return target in C_STANDARD_HEADERS


def foreign_component_of(target):
    for component in FOREIGN_COMPONENTS:
        if target.startswith(component + '/') or ('/' + component + '/') in target:
            return component
    return None


def resolve_public_include(target):
    """The public path an AfterNightEngine include names, or None when it names something else."""
    if not target.startswith(PUBLIC_INCLUDE_PREFIX):
        return None
    return target[len(PUBLIC_INCLUDE_PREFIX):]


def collect_files(repo_root):
    """Every source file of the engine, classified, plus the problems classification found."""
    files = []
    problems = []
    engine_root = repo_root / ENGINE_DIR
    public_root = repo_root / PUBLIC_DIR
    private_root = repo_root / PRIVATE_DIR

    if not engine_root.is_dir():
        return [], ['%s: the engine tree does not exist' % ENGINE_DIR]

    for path in sorted(engine_root.rglob('*')):
        if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
            continue
        relative = path.relative_to(repo_root).as_posix()
        if relative.startswith(PUBLIC_DIR + '/'):
            below = path.relative_to(public_root).as_posix()
            area, domain = public_area_of(below)
            if area is None:
                problems.append('%s: no public subject area owns this file; the areas are %s'
                                % (relative, ', '.join(PUBLIC_AREAS)))
                continue
            if path.suffix == '.cpp':
                problems.append('%s: the public include root carries headers only' % relative)
                continue
            text = read_text(path)
            files.append(SourceFile(path, relative, True, area, None, domain,
                                    parse_includes(text), text))
        elif relative.startswith(PRIVATE_DIR + '/'):
            below = path.relative_to(private_root).as_posix()
            layer, domain = private_layer_of(below)
            if layer is None:
                problems.append('%s: no implementation layer owns this file; the layers are %s'
                                % (relative, ', '.join(PRIVATE_LAYERS)))
                continue
            text = read_text(path)
            files.append(SourceFile(path, relative, False, None, layer, domain,
                                    parse_includes(text), text))
        elif relative.startswith(ENGINE_DIR + '/include/'):
            problems.append('%s: the public include root is %s and nothing else may sit beside it'
                            % (relative, PUBLIC_DIR))
        else:
            problems.append('%s: the engine has two roots, %s and %s, and this file is under '
                            'neither' % (relative, PUBLIC_DIR, PRIVATE_DIR))
    return files, problems


def check_layer_directories(repo_root):
    """Every declared layer has a directory, so a renamed or misspelled layer is not silent."""
    problems = []
    for area in PUBLIC_AREAS:
        if not (repo_root / PUBLIC_DIR / area).is_dir():
            problems.append('%s/%s: a declared public subject area has no directory'
                            % (PUBLIC_DIR, area))
    for layer in PRIVATE_LAYERS:
        if not (repo_root / PRIVATE_DIR / layer).is_dir():
            problems.append('%s/%s: a declared implementation layer has no directory'
                            % (PRIVATE_DIR, layer))
    return problems


def check_public_header(source, public_paths, problems):
    for include in source.includes:
        where = '%s:%d' % (source.relative, include.line)
        if include.quoted:
            problems.append('%s: %s is a quoted include in a public header; the published surface '
                            'is reachable through the %s include root alone'
                            % (where, include.spelling(), PUBLIC_INCLUDE_PREFIX))
            continue
        target = include.target
        if '..' in target.split('/'):
            problems.append('%s: %s leaves the engine tree' % (where, include.spelling()))
            continue
        foreign = foreign_component_of(target)
        if foreign is not None:
            problems.append('%s: %s belongs to %s' % (where, include.spelling(), foreign))
            continue
        public_path = resolve_public_include(target)
        if public_path is not None:
            if public_path not in public_paths:
                problems.append('%s: %s names no public header' % (where, include.spelling()))
                continue
            area, _ = public_area_of(public_path)
            allowed = PUBLIC_AREA_DEPENDENCIES[source.area]
            if area not in allowed:
                problems.append('%s: a %s header may not include %s; it may include %s'
                                % (where, source.area, area, ', '.join(allowed)))
            continue
        if is_standard_header(target):
            continue
        library = external_library_of(target)
        if library is not None:
            problems.append('%s: %s is a %s header, and no external library type crosses the '
                            'published surface' % (where, include.spelling(), library))
        else:
            problems.append('%s: %s is neither a standard header nor part of this engine'
                            % (where, include.spelling()))


def check_private_file(source, public_paths, private_paths, problems):
    allowed_public = PRIVATE_PUBLIC_DEPENDENCIES[source.layer]
    allowed_private = PRIVATE_PRIVATE_DEPENDENCIES[source.layer]
    if source.layer == 'interop':
        allowed_libraries = (source.domain,) if source.domain in EXTERNAL_LIBRARIES else ()
    else:
        allowed_libraries = ALLOWED_EXTERNAL_LIBRARIES[source.layer]
    below_private = source.relative[len(PRIVATE_DIR) + 1:]
    for directory, libraries in DIRECTORY_EXTERNAL_LIBRARIES.items():
        if below_private.startswith(directory):
            allowed_libraries = tuple(allowed_libraries) + libraries
    private_layer_roots = tuple(layer.split('/')[0] + '/' for layer in PRIVATE_LAYERS)

    for include in source.includes:
        where = '%s:%d' % (source.relative, include.line)
        target = include.target
        if '..' in target.split('/'):
            problems.append('%s: %s leaves the engine tree' % (where, include.spelling()))
            continue
        foreign = foreign_component_of(target)
        if foreign is not None:
            problems.append('%s: %s belongs to %s' % (where, include.spelling(), foreign))
            continue

        if include.quoted:
            if target not in private_paths:
                problems.append('%s: %s names no implementation header; a quoted include is a path '
                                'below %s' % (where, include.spelling(), PRIVATE_DIR))
                continue
            layer, domain = private_layer_of(target)
            if layer not in allowed_private:
                problems.append('%s: the %s layer may not include the %s layer; it may include %s'
                                % (where, source.layer, layer, ', '.join(allowed_private)))
            elif (layer == source.layer and layer in PRIVATE_LAYERS_WITH_DOMAINS
                  and domain != source.domain
                  and not (layer == 'io' and target in SHARED_IO_HEADERS)):
                problems.append('%s: %s is private to %s/%s, and %s/%s may not reach into it'
                                % (where, include.spelling(), layer, domain, layer, source.domain))
            continue

        public_path = resolve_public_include(target)
        if public_path is not None:
            if public_path not in public_paths:
                problems.append('%s: %s names no public header' % (where, include.spelling()))
                continue
            area, _ = public_area_of(public_path)
            # The two codecs that carry FITS header cards project them through the existing
            # astrometry bridge rather than growing a second parser whose crop and SIP semantics
            # could drift. XISF carries those cards natively, so it reaches the same one edge, and
            # so does the card-set metadata domain, whose combination keeps a source's world
            # coordinate cards by the bridge's own keyword set rather than a second list.
            card_bridge = (source.layer == 'io' and source.domain in ('fits', 'xisf', 'metadata')
                           and public_path == 'Algorithms/Astrometry/WcsCards.h')
            if area not in allowed_public and not card_bridge:
                problems.append('%s: the %s layer may not include %s; it may include %s'
                                % (where, source.layer, area, ', '.join(allowed_public)))
            continue
        if target.startswith(private_layer_roots):
            problems.append('%s: %s is an implementation header; include it with quotes, so that '
                            'the two include forms keep meaning different things'
                            % (where, include.spelling()))
            continue
        if is_standard_header(target):
            continue
        library = external_library_of(target)
        if library is None:
            problems.append('%s: %s is neither a standard header nor a library this engine declares'
                            % (where, include.spelling()))
        elif library == 'gpu' and source.layer != 'backends/gpu':
            problems.append('%s: %s is a GPU SDK header, and only the GPU backend may include one'
                            % (where, include.spelling()))
        elif library not in allowed_libraries:
            allowance = ', '.join(allowed_libraries) if allowed_libraries else 'no external library'
            problems.append('%s: the %s layer may not include %s; it declares %s'
                            % (where, source.layer, library, allowance))


def check_umbrella_headers(files, problems):
    for source in files:
        if not source.public:
            continue
        reexports = sum(1 for include in source.includes
                        if not include.quoted
                        and resolve_public_include(include.target) is not None)
        if reexports >= 2 and declares_nothing(source.text):
            problems.append('%s: this header declares nothing and re-exports %d others, which is '
                            'the umbrella header the engine does not have'
                            % (source.relative, reexports))


def check_header_guards(files, problems):
    for source in files:
        if source.path.suffix in HEADER_SUFFIXES and PRAGMA_ONCE not in source.text:
            problems.append('%s: a header needs a pragma-once guard' % source.relative)


def check_own_header_first(files, problems, public_dir=PUBLIC_DIR, private_dir=PRIVATE_DIR,
                          public_prefix=PUBLIC_INCLUDE_PREFIX):
    """An implementation file includes its own header first, so nothing else can hide a gap in it."""
    public_by_stem = {}
    private_by_stem = {}
    for source in files:
        if source.path.suffix not in HEADER_SUFFIXES:
            continue
        table = public_by_stem if source.public else private_by_stem
        table.setdefault(source.path.stem, []).append(source)

    for source in files:
        if source.path.suffix != '.cpp':
            continue
        expected = set()
        for header in private_by_stem.get(source.path.stem, []):
            if header.path.parent == source.path.parent:
                expected.add(header.relative.split(private_dir + '/', 1)[-1])
        for header in public_by_stem.get(source.path.stem, []):
            expected.add(public_prefix + header.relative.split(public_dir + '/', 1)[-1])
        if not expected:
            continue
        if not source.includes:
            problems.append('%s: includes nothing, but a header of its own name exists; include it '
                            'first so it is proved to compile alone' % source.relative)
            continue
        if source.includes[0].target not in expected:
            problems.append('%s: the first include is %s; it should be its own header, one of %s'
                            % (source.relative, source.includes[0].spelling(),
                               ', '.join(sorted(expected))))


def self_compile_unit_name(public_path):
    """The translation unit that proves one public header compiles on its own.

    Core/Version.h becomes CoreVersion.cpp: the path with its separators and extension removed, so
    the mapping is total and reversible and no two headers can claim one unit.
    """
    return ''.join(pathlib.PurePosixPath(public_path).with_suffix('').parts) + '.cpp'


def check_self_compile_coverage(repo_root, files, problems, self_compile_dir=SELF_COMPILE_DIR,
                               public_dir=PUBLIC_DIR, public_prefix=PUBLIC_INCLUDE_PREFIX):
    """One translation unit per public header, each including that header and nothing else."""
    directory = repo_root / self_compile_dir
    if not directory.is_dir():
        problems.append('%s: the self-compile directory does not exist' % self_compile_dir)
        return

    expected = {}
    for source in files:
        if not source.public:
            continue
        below = source.relative.split(public_dir + '/', 1)[-1]
        expected[self_compile_unit_name(below)] = public_prefix + below

    present = set(path.name for path in directory.glob('*.cpp'))
    for unit in sorted(present - set(expected)):
        problems.append('%s/%s: no public header corresponds to this self-compile unit'
                        % (self_compile_dir, unit))
    for unit in sorted(set(expected) - present):
        problems.append('%s: %s has no self-compile unit, so nothing proves it compiles on its own'
                        % (self_compile_dir + '/' + unit, expected[unit]))

    for unit in sorted(set(expected) & present):
        targets = [include.target for include in parse_includes(read_text(directory / unit))]
        if targets != [expected[unit], expected[unit]]:
            problems.append('%s/%s: must include %s exactly twice and nothing else, to prove that '
                            'header is both self-contained and idempotent'
                            % (self_compile_dir, unit, expected[unit]))




class OptionalAdapter:
    """One optional adapter component: a flat published surface over the engine and one library.

    An adapter is not an engine layer and does not have layers of its own. It is a separate binary
    with a separate include prefix, and it exists to put one external library behind the engine's
    vocabulary, so what it needs is a shorter rule set than the engine's and one rule the engine
    does not have: the library it adapts must not cross its *own* published surface either. That is
    what makes the adapter optional in a consumer's build rather than only in its packaging - a
    caller compiles against these headers with no Python toolchain installed, and acquires one only
    if it links the adapter.

    The opt-in GPU backend has exactly this shape - a flat surface over the engine and one SDK beneath
    it - so it is described by the same class, with `kind` saying which of the two a finding is about.
    """

    def __init__(self, name, library, suite, platform=(), kind='adapter'):
        self.name = name
        self.kind = kind
        self.library = library
        # Operating-system header groups the adapter's implementation may include beside its one
        # library. They are not a dependency, and they never reach the published surface either, which
        # a consumer compiles against on whatever platform it has.
        self.platform = platform
        self.root = 'src/' + name
        self.public_dir = self.root + '/include/' + name
        self.private_dir = self.root + '/src'
        self.prefix = name + '/'
        self.self_compile_dir = 'src/AfterNightTests/' + suite + '/SelfCompile'


# The header groups that belong to the operating system rather than to a library. An adapter may
# declare some of them for its implementation; its published surface may name none of them.
OPERATING_SYSTEM_GROUPS = ('platform', 'threads')


# The optional adapters, in the same form the build graph states them. One library per adapter, and
# it may only be included below that adapter's private root: the Python runtime for one, G'MIC for
# the other, and neither anywhere else.
OPTIONAL_ADAPTERS = (
    # The G'MIC adapter starts the thread each call into its library runs on, because the library's
    # interpreter needs more stack than the thread that calls the adapter can be assumed to have, and
    # std::thread cannot state a size: `_beginthreadex` on Windows, `pthread_create` elsewhere.
    OptionalAdapter('AfterNightEngineGmicAdapter', 'gmic', 'Test_EngineGmicAdapter',
                    platform=('platform', 'threads')),
    OptionalAdapter('AfterNightEnginePythonAdapter', 'python', 'Test_EnginePythonAdapter'),
)

# The opt-in GPU backends. The OpenCL one loads the loader its graphics driver installed from the
# operating system's directory before it calls anything, which is the platform group's API and not the
# SDK's; its published surface names neither.
OPTIONAL_BACKENDS = (
    OptionalAdapter('AfterNightEngineOpenClBackend', 'gpu', 'Test_EngineOpenClBackend',
                    platform=('platform',), kind='backend'),
)

# Every optional component whose tree this checker reads with the adapter rule set.
OPTIONAL_COMPONENTS = OPTIONAL_ADAPTERS + OPTIONAL_BACKENDS


def indefinite(noun):
    """`noun` with its indefinite article, for a finding that names an adapter or a backend."""
    return ('an ' if noun[:1] in 'aeiou' else 'a ') + noun


def collect_adapter_files(repo_root, adapter):
    """Every source file of one optional adapter, split into public and private, plus problems."""
    public = []
    private = []
    problems = []
    root = repo_root / adapter.root
    if not root.is_dir():
        return public, private, ['%s: the %s tree does not exist' % (adapter.root, adapter.kind)]
    for path in sorted(root.rglob('*')):
        if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
            continue
        relative = path.relative_to(repo_root).as_posix()
        text = read_text(path)
        entry = SourceFile(path, relative, True, None, None, None, parse_includes(text), text)
        if relative.startswith(adapter.public_dir + '/'):
            if path.suffix == '.cpp':
                problems.append('%s: the public include root carries headers only' % relative)
                continue
            public.append(entry)
        elif relative.startswith(adapter.private_dir + '/'):
            entry.public = False
            private.append(entry)
        elif relative.startswith(adapter.root + '/include/'):
            problems.append('%s: the public include root is %s and nothing else may sit beside it'
                            % (relative, adapter.public_dir))
        else:
            problems.append('%s: this %s has two roots, %s and %s, and this file is under '
                            'neither' % (relative, adapter.kind, adapter.public_dir, adapter.private_dir))
    return public, private, problems


def check_adapter_include(adapter, source, include, own_public, own_private, engine_public, problems):
    """Apply one adapter file's include rules to one include directive."""
    where = '%s:%d' % (source.relative, include.line)
    target = include.target
    if '..' in target.split('/'):
        problems.append('%s: %s leaves the %s tree' % (where, include.spelling(), adapter.kind))
        return
    foreign = foreign_component_of(target)
    if foreign is not None and foreign != adapter.name:
        problems.append('%s: %s belongs to %s' % (where, include.spelling(), foreign))
        return
    if include.quoted:
        if source.public:
            problems.append('%s: %s is a quoted include in a public header; this %s\'s surface '
                            'is reachable through the %s include root alone'
                            % (where, include.spelling(), adapter.kind, adapter.prefix))
        elif target not in own_private:
            problems.append('%s: %s names no implementation header of this %s; a quoted '
                            'include is a path below %s' % (where, include.spelling(), adapter.kind,
                                                            adapter.private_dir))
        return
    if target.startswith(adapter.prefix):
        below = target[len(adapter.prefix):]
        if below not in own_public:
            problems.append('%s: %s names no public header of this %s'
                            % (where, include.spelling(), adapter.kind))
        return
    if target.startswith(PUBLIC_INCLUDE_PREFIX):
        below = target[len(PUBLIC_INCLUDE_PREFIX):]
        if below not in engine_public:
            problems.append('%s: %s names no public header of the engine'
                            % (where, include.spelling()))
        return
    if is_standard_header(target):
        return
    library = external_library_of(target)
    if library is None:
        problems.append('%s: %s is neither a standard header nor part of this %s or the engine'
                        % (where, include.spelling(), adapter.kind))
    elif source.public and library in OPERATING_SYSTEM_GROUPS:
        problems.append('%s: %s is a %s header; %s\'s published surface carries no operating-system '
                        'header, because a consumer compiles against it on whatever platform it has'
                        % (where, include.spelling(), library, indefinite(adapter.kind)))
    elif not source.public and library in adapter.platform:
        return
    elif source.public:
        problems.append('%s: %s is a %s header, and the library %s adapts does not cross '
                        'the %s\'s own published surface either - a consumer compiles against '
                        'these headers without it' % (where, include.spelling(), library,
                                                      indefinite(adapter.kind), adapter.kind))
    elif library != adapter.library:
        problems.append('%s: %s is a %s header; this %s declares %s and nothing else'
                        % (where, include.spelling(), library, adapter.kind, adapter.library))


def check_optional_adapters(repo_root, engine_public, problems):
    """Every optional adapter's own layering, and its self-compile coverage.

    Returns how many adapter files were examined, so the run reports what it looked at rather than
    only what it found.
    """
    examined = 0
    for adapter in OPTIONAL_COMPONENTS:
        public, private, classification = collect_adapter_files(repo_root, adapter)
        problems.extend(classification)
        examined += len(public) + len(private)
        own_public = {source.relative.split(adapter.public_dir + '/', 1)[-1] for source in public}
        own_private = {source.relative.split(adapter.private_dir + '/', 1)[-1] for source in private}
        for source in public + private:
            for include in source.includes:
                check_adapter_include(adapter, source, include, own_public, own_private,
                                      engine_public, problems)
        check_umbrella_headers(public, problems)
        check_header_guards(public + private, problems)
        check_own_header_first(public + private, problems, adapter.public_dir, adapter.private_dir,
                              adapter.prefix)
        check_self_compile_coverage(repo_root, public, problems, adapter.self_compile_dir,
                                    adapter.public_dir, adapter.prefix)
    return examined


def check(repo_root):
    problems = []
    problems.extend(check_layer_directories(repo_root))
    files, classification_problems = collect_files(repo_root)
    problems.extend(classification_problems)

    public_paths = set()
    private_paths = set()
    for source in files:
        if source.public:
            public_paths.add(source.relative.split(PUBLIC_DIR + '/', 1)[-1])
        else:
            private_paths.add(source.relative.split(PRIVATE_DIR + '/', 1)[-1])

    for source in files:
        if source.public:
            check_public_header(source, public_paths, problems)
        else:
            check_private_file(source, public_paths, private_paths, problems)

    check_umbrella_headers(files, problems)
    check_header_guards(files, problems)
    check_own_header_first(files, problems)
    check_self_compile_coverage(repo_root, files, problems)
    examined = len(files) + check_optional_adapters(repo_root, public_paths, problems)
    return problems, examined


def main(argv=None, stream=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--repo-root', default=str(REPO_ROOT),
                        help='repository root to check; defaults to the one containing this script')
    arguments = parser.parse_args(argv)
    out = stream if stream is not None else sys.stdout

    problems, examined = check(pathlib.Path(arguments.repo_root).resolve())
    if problems:
        for problem in problems:
            print(problem, file=out)
        print('%d problem(s) across %d file(s) of %s and its optional adapters'
              % (len(problems), examined, ENGINE_DIR), file=out)
        return 1
    print('include layering holds across %d file(s) of %s and its optional adapters'
          % (examined, ENGINE_DIR), file=out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
