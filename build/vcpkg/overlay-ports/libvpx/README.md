# libvpx overlay

This wrapper keeps the repository's pinned `vcpkg/ports/libvpx` integration and patches while moving the
upstream source and port version from 1.16.0 to the signed 1.17.0 tag. The SHA-512 in `portfile.cmake` pins
that archive.

`portfile.cmake` names the checkout port's patches itself: the override that makes the checkout port build the
1.17.0 tree drops that port's own fetch arguments, its patch list with them. A vcpkg update that changes those
patches needs the list changed with it. Without them the Windows build stops in libvpx's project generator, which
refuses a compiler flag it does not know (`Unknown option "-O3"`) unless the checkout port's `0006` patch is applied.

The safety review of the Linux build found 1.16.0 behind FFmpeg's VP9 encoder. It predates the upstream repair
for CVE-2026-2447, a heap buffer overflow reached by an ordinary VP9 encode request. The review and its
disposition are in `build/scripts/tools/safety_register.txt`.

Remove this overlay when the pinned vcpkg checkout provides 1.17.0 or later, and review the replacement
version before moving `build/scripts/tools/dependencies.txt` and the safety register with it.
