# LibRaw overlay

Copied from the repository's pinned `vcpkg/ports/libraw` 0.22.1 port. The upstream archive hashes,
LibRaw-cmake commit, patches and licences are unchanged. AfterNight adds port revision 1 and one
optional feature, `x3f`, mapping to the upstream `ENABLE_X3FTOOLS` CMake option. Both platform
dependency scripts select it so full-colour Foveon RAW files are actually supported by the decoder.

The X3F implementation is already in LibRaw and carries Roland Karlsson's BSD-style licence;
LibRaw's installed copyright file includes that notice. OpenMP is not enabled. Keep this overlay
and both install commands together when updating the pinned LibRaw dependency.
