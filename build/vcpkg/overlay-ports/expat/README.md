# Expat overlay

Copied from the repository's pinned `vcpkg/ports/expat` 2.8.2 port. Only the version and the upstream
archive hash move, to Expat 2.8.5 of 2026-09-22.

Expat is not a dependency any source of the engine names: Exiv2 loads it to decode an XMP packet,
and Exiv2's TIFF and Exif decoders decode one wherever a file carries it, so a hostile TIFF or JPEG reaches
Expat through the engine's metadata read. 2.8.2 is inside the advisory range of six published advisories -
an out-of-bounds read with an endless loop, a quadratic-time parse, an out-of-bounds read, a
use-after-free, a hash-flooding weakness and malformed UTF-16 accepted as text - that 2.8.3, 2.8.4 and
2.8.5 fix between them. Upstream's own release notes still say Expat has unfixed security issues
(libexpat issue 1160); the review records that too. The review is in
`build/scripts/tools/safety_register.txt`. Remove this overlay when the pinned vcpkg checkout provides
2.8.5 or later.
