# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Refuse constructs AfterNightEngine has decided against, in its source rather than in review.

The layering checker next door answers what one file may reach. This answers a different question:
which spellings the engine has decided not to use anywhere, whatever the layer rules allow. They are
separate scripts because they fail for separate reasons and a reader of one failure should not have
to work out which kind of rule was broken.

Each rule below is a decision with a reason, not a style preference, and each is here because the
mistake is easy to make and costly to find:

1. `tbb::global_control`. It is a process-wide parallelism cap: constructing one inside a helper
   resizes the thread pool for every other part of the application, including parts that never asked
   and parts that are running. An engine context owns a `task_arena` instead, which is local by
   construction, and the engine's whole concurrency contract depends on nobody reaching for the
   process-wide answer. `Runtime/TaskScheduler.h` is where the local one lives.

2. `tbb::task_scheduler_init`. Removed from oneTBB, and the same process-wide mistake in an older
   spelling. Refused so that code copied from an older codebase fails here rather than compiling
   against whatever compatibility shim happens to exist.

3. `CALL_TEMPLATE_BY_PIXEL_TYPE`, a macro that routes an untyped address to a template per pixel
   type. The engine has a closed sample table and resolves a kernel once before its inner loop; the
   macro would recreate an open-ended generic type switch.

4. A `void*` or `std::any` in the typed-image core. `Image`, `ImageView`, `PlaneView` and
   `SampleTraits` are the part of the model a typed kernel sees. Raw buffers, the untyped allocation
   owner and the compiled dispatch boundary deliberately sit outside that set, but a typed view or
   image must never erase its sample type and recover it later.

5. A dynamic owner in a sample-movement hot path. Converting, copying, filling, clearing,
   repacking, byte-reversing and moving a channel are fused walks from the caller's source storage
   into the caller's destination storage; an owning buffer in one of them could silently turn an
   integer-storage operation into a full-frame floating allocation, and in the vector kernels it
   would put an allocation inside a per-row call. Persistent image ownership lives above the
   kernels, and operation-local temporary memory must be an explicit, reviewed scratch request
   rather than a hidden container.

6. An exported declaration inside a template, anywhere in the published surface. Templates there are
   limited to small value and view types, and an algorithm is compiled behind a non-template entry
   point that names the types it supports. A templated export compiles everywhere and then means
   whatever each caller instantiated it with, so the library and its consumers disagree about what
   ran with nothing to detect it. The typed convenience wrappers the
   surface does carry - `computeBasicStatistics` over an `ImageView`, for instance - are inline and
   forward to the exported untyped entry point, which is exactly the shape this rule permits.

7. A second eight-way sample dispatch. `Image/SampleTraits.h` publishes `dispatchSampleType`, the one
   switch from a sample value to its C++ type, and holds it to the sample tables at compile time. A
   private copy is free to fall behind the tables - to forget a representation or map one to a type of
   the wrong width - and nothing would say so until an image of that type arrived. A function declared
   with that name and a `SampleType` parameter anywhere but that header is a copy.

The engine's own explanations of these decisions mention the forbidden spelling by name, which is
what a comment explaining a ban has to do. A line that is a comment is therefore not a finding, and
the checker strips comments before it looks - the same way the layering checker does before reading
include lines.

Usage:
    python build/scripts/tools/check_forbidden_constructs.py [--repo-root <dir>]

Exits 0 when nothing forbidden appears, 1 otherwise, printing one line per finding.

Tests: build/scripts/tools/check_forbidden_constructs_test.py
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]

# The trees the rules apply to: the engine, its adapters and its backend, and the suites and the
# benchmark runner that link them directly.
SCANNED_DIRS = (
    'src/AfterNightEngine',
    'src/AfterNightEngineGmicAdapter',
    'src/AfterNightEngineOpenClBackend',
    'src/AfterNightEnginePythonAdapter',
    'src/AfterNightTests/Test_EngineAstrometry',
    'src/AfterNightTests/Test_EngineConsumerProbes',
    'src/AfterNightTests/Test_EngineCalibration',
    'src/AfterNightTests/Test_EngineColor',
    'src/AfterNightTests/Test_EngineCore',
    'src/AfterNightTests/Test_EngineFaults',
    'src/AfterNightTests/Test_EngineFixtureBridge',
    'src/AfterNightTests/Test_EngineGmicAdapter',
    'src/AfterNightTests/Test_EngineImage',
    'src/AfterNightTests/Test_EngineImageOps',
    'src/AfterNightTests/Test_EngineInterop',
    'src/AfterNightTests/Test_EngineKernels',
    'src/AfterNightTests/Test_EngineLibraryParity',
    'src/AfterNightTests/Test_EngineOpenClBackend',
    'src/AfterNightTests/Test_EngineRegistration',
    'src/AfterNightTests/Test_EngineRuntime',
    'src/AfterNightTests/Test_EngineStacking',
    'src/AfterNightTests/Test_EngineStars',
    'src/AfterNightTests/Test_EnginePublicHeaders',
    'src/AfterNightTests/Test_EnginePythonAdapter',
    'src/AfterNightBench/engine',
)

SOURCE_SUFFIXES = ('.h', '.hpp', '.inl', '.cpp')

BLOCK_COMMENT_PATTERN = re.compile(r'/\*.*?\*/', re.DOTALL)
LINE_COMMENT_PATTERN = re.compile(r'//[^\n]*')

# One entry per refused construct: the pattern that finds it, and the sentence a reader gets.
FORBIDDEN_CONSTRUCTS = (
    (re.compile(r'\bglobal_control\b'),
     'tbb::global_control caps parallelism for the whole process, including work this engine did '
     'not start. An engine context owns a task_arena instead; run parallel work through '
     'Runtime/TaskScheduler.h.'),
    (re.compile(r'\btask_scheduler_init\b'),
     'tbb::task_scheduler_init is the removed spelling of the same process-wide cap. An engine '
     'context owns a task_arena instead; run parallel work through Runtime/TaskScheduler.h.'),
    (re.compile(r'\bCALL_TEMPLATE_BY_PIXEL_TYPE\b'),
     'CALL_TEMPLATE_BY_PIXEL_TYPE is an open-ended generic pixel-type switch. Use the closed sample '
     'table and resolve a typed kernel once before its inner loop.'),
)

# The published sample dispatch, and the one header allowed to define it. A definition is the name followed
# by a `SampleType` parameter; a call passes a value and never spells the type.
SAMPLE_DISPATCH_HOME = 'src/AfterNightEngine/include/AfterNightEngine/Image/SampleTraits.h'
SAMPLE_DISPATCH_DEFINITION = (
    re.compile(r'\bdispatchSampleType\s*\(\s*(?:an::)?SampleType\s+\w+'),
    'a second sample-type dispatch can fall behind the sample tables unnoticed. Call dispatchSampleType '
    'from Image/SampleTraits.h, which is held to them at compile time.')

# FFmpeg's process-wide settings. Its log callback and level, its network and registration state and
# its CPU flags belong to the whole process, including an application that configured them for its own
# use. The engine reads video through FFmpeg and changes none of them; a test process, which owns its
# own log, may.
ENGINE_TREE = 'src/AfterNightEngine/'

ENGINE_ONLY_CONSTRUCTS = (
    (re.compile(r'\b(?:av_log_set_callback|av_log_set_level|av_log_set_flags|avformat_network_init|'
                r'avformat_network_deinit|av_register_all|avcodec_register_all|av_force_cpu_flags|'
                r'av_set_cpu_flags_mask|av_max_alloc)\s*\('),
     'a process-wide FFmpeg setting changes the library for the whole application, including code '
     'that configured it for its own use. Leave FFmpeg\'s global state to the process.'),
    # The standard regular-expression engine. Parsing a library's version banner, a catalogue
    # identifier or a header keyword with one costs a compiled pattern with process lifetime, the
    # locale the process happens to own, and a matcher whose stack depth its input decides. This
    # engine parses them with bounded scans instead, which is a decision rather than an accident and
    # is therefore stated here rather than only observed. The rule is the engine's alone: a test that
    # wants a regular expression may have one.
    (re.compile(r'\bstd::(?:regex|wregex|regex_search|regex_match|regex_replace)\b|<regex>'),
     'std::regex compiles a pattern into an object with process lifetime, reads the process locale, '
     'and matches with a stack depth its input decides. Parse with a bounded scan instead; '
     'Core/DependencyReport.h and its contributors are the worked example.'),
)

# The published surface, and the marker that says a declaration crosses the library boundary.
#
# Templates in the published surface are limited to small value and view types, and algorithms are
# compiled behind non-template entry points with explicit supported types. That rule is invisible to a
# compiler: a templated exported function builds cleanly and then means whatever the caller
# instantiated it with, which is how an engine ends up with a different implementation per consumer
# and no way to tell which one ran. The mechanical form of the rule is that the export marker never
# appears inside a template.
# Every published surface in the tree, with the marker that says a declaration crosses that
# library's boundary. An optional adapter is a second library with a second marker, and the rule is
# the same one for the same reason: a templated export means whatever each caller instantiated it
# with, and the library and its consumers then disagree about what ran.
PUBLIC_SURFACES = (
    ('src/AfterNightEngine/include/AfterNightEngine/', 'AFTERNIGHTENGINE_API'),
    ('src/AfterNightEngineGmicAdapter/include/AfterNightEngineGmicAdapter/',
     'AFTERNIGHTENGINEGMICADAPTER_API'),
    ('src/AfterNightEngineOpenClBackend/include/AfterNightEngineOpenClBackend/',
     'AFTERNIGHTENGINEOPENCLBACKEND_API'),
    ('src/AfterNightEnginePythonAdapter/include/AfterNightEnginePythonAdapter/',
     'AFTERNIGHTENGINEPYTHONADAPTER_API'),
)

TEMPLATE_KEYWORD = re.compile(r'\btemplate\s*<')

EXPORTED_TEMPLATE_EXPLANATION = (
    'an exported declaration inside a template leaks the type into the library boundary. Templates '
    'in the published surface are limited to small value and view types; an algorithm is compiled '
    'behind a non-template entry point that names its supported types.')


def exported_templates(text, relative, export_marker):
    """Every place the export marker appears inside a templated declaration.

    Lexical by necessity - there is no C++ parser here - and deliberately conservative: it walks
    from each `template <` past its parameter list to the end of the entity it introduces, which is
    either the first semicolon at brace depth zero or the closing brace of its body, and reports the
    marker anywhere inside. A false positive costs one rewrite; a missed exported template is a
    symbol whose meaning depends on the caller.
    """
    problems = []
    position = 0
    length = len(text)
    while True:
        match = TEMPLATE_KEYWORD.search(text, position)
        if match is None:
            return problems
        cursor = match.end()
        angle_depth = 1
        while cursor < length and angle_depth > 0:
            if text[cursor] == '<':
                angle_depth += 1
            elif text[cursor] == '>':
                angle_depth -= 1
            cursor += 1
        brace_depth = 0
        end = length
        while cursor < length:
            character = text[cursor]
            if character == '{':
                brace_depth += 1
            elif character == '}':
                brace_depth -= 1
                if brace_depth == 0:
                    end = cursor
                    break
            elif character == ';' and brace_depth == 0:
                end = cursor
                break
            cursor += 1
        entity = text[match.start():end]
        if export_marker in entity:
            line_number = text.count('\n', 0, match.start()) + 1
            problems.append('%s:%d: %s' % (relative, line_number, EXPORTED_TEMPLATE_EXPLANATION))
        position = match.end()


# The template-facing image core. Byte ownership and run-time dispatch are explicit untyped
# boundaries and are intentionally not on this list; these four headers are what must preserve a
# sample type all the way to a kernel.
TYPED_IMAGE_CORE = frozenset((
    'src/AfterNightEngine/include/AfterNightEngine/Image/Image.h',
    'src/AfterNightEngine/include/AfterNightEngine/Image/ImageView.h',
    'src/AfterNightEngine/include/AfterNightEngine/Image/PlaneView.h',
    'src/AfterNightEngine/include/AfterNightEngine/Image/SampleTraits.h',
))

TYPED_IMAGE_FORBIDDEN_CONSTRUCTS = (
    (re.compile(r'\b(?:const\s+)?void\s*(?:const\s*)?\*'),
     'a void pointer erases the sample type inside the typed-image core. Keep the pointer typed; '
     'byte ownership and run-time dispatch belong at their explicit boundaries.'),
    (re.compile(r'\bstd\s*::\s*any\b'),
     'std::any erases the sample type inside the typed-image core. Use the closed ResidentSample '
     'set and typed storage or views.'),
)

# Every source that selects or implements a conversion kernel. These files may use fixed-size
# compile-time tables, but they must never acquire a run-time-sized owner: conversion is one fused
# walk from the caller's source storage into the caller's destination storage.
CONVERSION_HOT_PATHS = frozenset((
    'src/AfterNightEngine/src/backends/cpu/ChannelTransfer.cpp',
    'src/AfterNightEngine/src/backends/cpu/ConversionKernelsCpu.cpp',
    'src/AfterNightEngine/src/backends/cpu/ConversionKernelsCpu.h',
    'src/AfterNightEngine/src/backends/cpu/ImageConversion.cpp',
    'src/AfterNightEngine/src/backends/cpu/ImageTransfer.cpp',
    'src/AfterNightEngine/src/backends/cpu/ImageWalk.h',
    'src/AfterNightEngine/src/backends/cpu/SimdKernelTables.cpp',
    'src/AfterNightEngine/src/backends/cpu/SimdKernelTables.h',
    'src/AfterNightEngine/src/kernels/reference/ConversionKernels.cpp',
    'src/AfterNightEngine/src/kernels/reference/ConversionKernels.h',
    'src/AfterNightEngine/src/kernels/reference/LayoutKernels.cpp',
    'src/AfterNightEngine/src/kernels/reference/LayoutKernels.h',
    'src/AfterNightEngine/src/kernels/reference/StackSourceRow.inl',
    'src/AfterNightEngine/src/backends/cpu/StackSourceCpu.cpp',
))

CONVERSION_OWNERSHIP_CONSTRUCTS = (
    (re.compile(r'\bnew\b'),
     'dynamic allocation is forbidden in a sample-movement hot path. Move samples directly '
     'between the typed source and destination runs.'),
    (re.compile(r'\b(?:malloc|calloc|realloc)\s*\('),
     'C heap allocation is forbidden in a sample-movement hot path. Move samples directly '
     'between the typed source and destination runs.'),
    (re.compile(r'\bstd\s*::\s*(?:vector|deque|list|unique_ptr|shared_ptr|make_unique|make_shared)\b'),
     'an owning standard-library buffer is forbidden in a sample-movement hot path. Move samples '
     'directly between the typed source and destination runs.'),
    (re.compile(r'\b(?:ImageStorage(?:Bytes)?|createWorkingImage)\b'),
     'an owned image is forbidden in a sample-movement hot path. Move samples directly between '
     'the typed source and destination runs.'),
)


# Every source that decides which samples an estimator reads, or reads them. A median absolute
# deviation computed by allocating one vector the size of its input and calling a std::function per
# sample, inside a loop that runs once per output pixel per rejection iteration, spends its time in
# the allocator and the indirect call. These files are where that would happen, so what they may not
# do is written down rather than watched for.
#
# The neighbourhood filters are here for the same reason rather than an analogous one: a median
# filter resolves a rank once per output pixel, so a per-window allocation is the same defect with
# a window instead of an image.
ESTIMATOR_HOT_PATHS = frozenset((
    'src/AfterNightEngine/src/kernels/reference/SpatialPlane.cpp',
    'src/AfterNightEngine/src/kernels/reference/SpatialPlane.h',
    'src/AfterNightEngine/src/algorithms/deconvolution/ConvolutionEngine.cpp',
    'src/AfterNightEngine/src/algorithms/deconvolution/Deconvolution.cpp',
    'src/AfterNightEngine/src/algorithms/deconvolution/DeconvolutionInternal.h',
    'src/AfterNightEngine/src/algorithms/histogram/HistogramOrderPrimitives.cpp',
    'src/AfterNightEngine/src/algorithms/multiscale/MultiscaleAnalysis.cpp',
    'src/AfterNightEngine/src/algorithms/multiscale/MultiscaleExecution.cpp',
    'src/AfterNightEngine/src/algorithms/multiscale/MultiscaleTransform.cpp',
    'src/AfterNightEngine/src/algorithms/noisereduction/DualDomainShrinkage.cpp',
    'src/AfterNightEngine/src/algorithms/noisereduction/NoiseReduction.cpp',
    'src/AfterNightEngine/src/algorithms/noisereduction/PatchDenoise.cpp',
    'src/AfterNightEngine/src/algorithms/noisereduction/PatchDenoiseInternal.h',
    'src/AfterNightEngine/src/algorithms/noisereduction/PatchEstimator.cpp',
    'src/AfterNightEngine/src/algorithms/spatial/Convolution.cpp',
    'src/AfterNightEngine/src/algorithms/spatial/Gaussian.cpp',
    'src/AfterNightEngine/src/algorithms/spatial/MedianNetworks.h',
    'src/AfterNightEngine/src/algorithms/spatial/OrderFilter.cpp',
    'src/AfterNightEngine/src/algorithms/reduction/ReductionPrimitives.cpp',
    'src/AfterNightEngine/src/algorithms/statistics/BasicStatistics.cpp',
    'src/AfterNightEngine/src/algorithms/statistics/Histogram.cpp',
    'src/AfterNightEngine/src/algorithms/statistics/RobustStatistics.cpp',
    'src/AfterNightEngine/src/algorithms/statistics/StatisticsSelection.cpp',
    'src/AfterNightEngine/src/algorithms/stacking/RejectionEstimators.cpp',
    'src/AfterNightEngine/src/algorithms/stacking/RejectionEstimators.h',
    'src/AfterNightEngine/src/algorithms/stacking/StackRejection.cpp',
    'src/AfterNightEngine/src/algorithms/stacking/StackExecutor.cpp',
    'src/AfterNightEngine/src/algorithms/stacking/LocalWeights.cpp',
    'src/AfterNightEngine/src/algorithms/stacking/StackQuality.cpp',
    'src/AfterNightEngine/src/kernels/reference/PlaneSourceAccess.cpp',
    'src/AfterNightEngine/src/kernels/reference/PlaneSourceAccess.h',
))

ESTIMATOR_OWNERSHIP_CONSTRUCTS = (
    (re.compile(r'\bstd\s*::\s*function\b'),
     'a std::function decides per sample through an indirect call the compiler cannot see into. '
     'State the rule in a type the kernel is instantiated for, and resolve it once outside the '
     'inner loop.'),
    (re.compile(r'\bnew\b'),
     'dynamic allocation is forbidden in an estimator path. Working memory comes from the engine '
     'context, is bounded by a stated contract, and is taken once per operation.'),
    (re.compile(r'\b(?:malloc|calloc|realloc)\s*\('),
     'C heap allocation is forbidden in an estimator path. Working memory comes from the engine '
     'context, is bounded by a stated contract, and is taken once per operation.'),
    (re.compile(r'\bstd\s*::\s*(?:vector|deque|list|unique_ptr|shared_ptr|make_unique|make_shared)\b'),
     'an owning standard-library buffer in an estimator path is an allocation per call inside '
     'the inner loop. Take bounded working memory from the engine context instead.'),
)


def strip_comments(text):
    """The source with its comments blanked, so a rule reads code rather than prose.

    Newlines are preserved so that a finding still reports the line it was on.
    """
    def blank(match):
        return re.sub(r'[^\n]', ' ', match.group(0))

    return LINE_COMMENT_PATTERN.sub(blank, BLOCK_COMMENT_PATTERN.sub(blank, text))


def scan_file(path, relative):
    """Every forbidden construct in one file, as a list of reportable lines."""
    text = strip_comments(path.read_bytes().decode('utf-8'))
    problems = []
    for line_number, line in enumerate(text.split('\n'), start=1):
        for pattern, explanation in FORBIDDEN_CONSTRUCTS:
            if pattern.search(line):
                problems.append('%s:%d: %s' % (relative, line_number, explanation))
        if relative != SAMPLE_DISPATCH_HOME and SAMPLE_DISPATCH_DEFINITION[0].search(line):
            problems.append('%s:%d: %s' % (relative, line_number, SAMPLE_DISPATCH_DEFINITION[1]))
        if relative.startswith(ENGINE_TREE):
            for pattern, explanation in ENGINE_ONLY_CONSTRUCTS:
                if pattern.search(line):
                    problems.append('%s:%d: %s' % (relative, line_number, explanation))
        if relative in TYPED_IMAGE_CORE:
            for pattern, explanation in TYPED_IMAGE_FORBIDDEN_CONSTRUCTS:
                if pattern.search(line):
                    problems.append('%s:%d: %s' % (relative, line_number, explanation))
        if relative in CONVERSION_HOT_PATHS:
            for pattern, explanation in CONVERSION_OWNERSHIP_CONSTRUCTS:
                if pattern.search(line):
                    problems.append('%s:%d: %s' % (relative, line_number, explanation))
        if relative in ESTIMATOR_HOT_PATHS:
            for pattern, explanation in ESTIMATOR_OWNERSHIP_CONSTRUCTS:
                if pattern.search(line):
                    problems.append('%s:%d: %s' % (relative, line_number, explanation))
    for surface, export_marker in PUBLIC_SURFACES:
        if relative.startswith(surface):
            problems.extend(exported_templates(text, relative, export_marker))
    return problems


def collect_sources(repo_root):
    """Every scanned source file, in a stable order."""
    sources = []
    for directory in SCANNED_DIRS:
        root = repo_root / directory
        if not root.is_dir():
            continue
        for path in sorted(root.rglob('*')):
            if path.is_file() and path.suffix in SOURCE_SUFFIXES:
                sources.append((path, path.relative_to(repo_root).as_posix()))
    return sources


def check(repo_root):
    """Every finding across the scanned trees."""
    problems = []
    for path, relative in collect_sources(repo_root):
        problems.extend(scan_file(path, relative))
    return problems


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split('\n', 1)[0])
    parser.add_argument('--repo-root', default=str(REPO_ROOT))
    arguments = parser.parse_args(argv)

    problems = check(pathlib.Path(arguments.repo_root))
    for problem in problems:
        print(problem)
    if problems:
        print('%d forbidden construct(s)' % len(problems))
        return 1

    print('no forbidden construct found')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
