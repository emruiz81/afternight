# Engine and product guides

This directory holds the engine's guides and the product guide. The engine documents itself in two
layers, and each answers a different question.

| Layer | Answers | Where |
|---|---|---|
| The API reference | What does this declaration do, exactly? | Generated from the published headers by `python build/scripts/tools/check_public_documentation.py --output-dir <dir>`; open `<dir>/html/index.html` |
| These guides | How do the parts fit, why is each built the way it is, and where do I start? | This directory |

The reference is the authority on any single declaration, and every published header opens with a
module comment that the reference places on the header's own page. The guides never restate a
contract the reference already states; they say how contracts combine and why each design is what it
is, and they point at the header that holds each one. A design's reasons are kept in the guide that
describes its area, and change with it.

The product - the libraries, the application and the extension hosts built on the engine - has no
generated reference. [PRODUCT_ARCHITECTURE.md](PRODUCT_ARCHITECTURE.md) says how each area of it uses
the engine and points at the headers that area owns.

## Reading order

A consumer - someone calling the engine - reads the first three, in order. A contributor reads all of
them.

1. [PUBLIC_API_CONTRACT.md](../PUBLIC_API_CONTRACT.md) - the one page that is true of every header:
   the four ownership shapes, the one error model, and the questions to ask before configuring anything.
2. [ARCHITECTURE.md](ARCHITECTURE.md) - how the engine is layered, what owns what, and how one
   operation flows through it.
3. [NUMERIC_VALIDITY_METADATA.md](NUMERIC_VALIDITY_METADATA.md) - what an image is: sample types,
   precision, conversion, validity and no-data, metadata and provenance.
4. [PRODUCT_ARCHITECTURE.md](PRODUCT_ARCHITECTURE.md) - how the product uses the engine, area by area:
   the platform and consumer runtime libraries, Controls, Processes, the ImageIntegration workflow, the
   application and the extension hosts.
5. [BACKENDS.md](BACKENDS.md) - CPU dispatch and instruction sets, the kernel contracts, and the device
   seam.
6. [DEPENDENCIES.md](DEPENDENCIES.md) - which libraries the engine uses, where each may appear, how
   versions are reported and how advisories are answered.
7. [BUILD.md](BUILD.md) - building the engine, its adapters and its suites on Windows and Linux, and
   the generation switches.
8. [TESTING.md](TESTING.md) - the direct suites, the fixture layers and their reference expectations,
   the campaigns and the checks, and what each proves.
9. [BENCHMARKS.md](BENCHMARKS.md) - the benchmark runner, the scenario contract, and how a comparison
   is decided.

## Keeping them true

`build/scripts/tools/check_engine_documentation.py` reads every guide here, the public API contract
and the reference's main page, and fails when a link does not resolve, a path a code span names does
not exist, or an identifier a code span names is spelled nowhere in the tree's code. It also fails
when a guide in this directory is missing from this page. That catches the renamed symbol and
the moved file; whether a sentence is still true is caught by whoever changes the code it describes,
in the same change.
