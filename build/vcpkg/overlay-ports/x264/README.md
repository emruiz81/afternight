# x264 overlay

This wrapper keeps the repository's pinned `vcpkg/ports/x264` source, version (0.164.3108) and patches, and adds
`avx512-propagate-list-tail.patch`, a repair of the source it fetches.

x264's AVX-512 `mbtree_propagate_list` handles eight macroblocks per iteration and masks no lane past the row it is
given. When a row's width in macroblocks is not a multiple of eight - 20 for a 320-pixel frame - the last iteration's
extra lanes add whatever the propagate-amount buffer holds past the row to the reference costs their motion vectors
point at. That buffer is uninitialised there, so on a processor with AVX-512 two encodes of the same frames by
libx264 at one thread could write different streams, and every such encode differed from the one a processor without
AVX-512 writes. The patch zeroes the buffer's tail before the call, which gives each extra lane a zero weight that the
assembly already skips; the buffer is aligned to 64 bytes, so the tail is inside it. With it the AVX-512 path writes
the AVX2 path's stream byte for byte whatever the memory held. No upstream release carries a repair.

The defect is held by
`FfmpegSinkTest.TheSoftwareH264EncoderWritesOneStreamWhateverFreshMemoryHolds` in `Test_EngineImage`. Remove this overlay
when the pinned vcpkg checkout's x264 carries the repair, and review the replacement version before moving
`build/scripts/tools/dependencies.txt` and the safety register with it.
