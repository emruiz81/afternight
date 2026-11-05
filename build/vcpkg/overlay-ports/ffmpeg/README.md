# FFmpeg overlay

Copied from the repository's pinned `vcpkg/ports/ffmpeg` 8.1.2 port. Every patch, feature, dependency and
licence is unchanged; only the version, the upstream archive hash and the port revision move, to the
FFmpeg 8.1.3 point release of 2026-09-21. All fourteen patches apply to it unchanged.

FFmpeg 8.1.2 is inside the advisory range of heap overflows, out-of-bounds writes and uninitialised
reads in demuxers and decoders the engine reaches from a video file, because it lets FFmpeg probe the
container and open whatever decoder the stream names. 8.1.3 carries the upstream fixes for those
components and for swscale; the review, the advisories and what each one reaches are in
`build/scripts/tools/safety_register.txt`. Remove this overlay when the pinned vcpkg checkout provides
8.1.3 or later, and update the version in `build/scripts/tools/dependencies.txt` and the review in the
safety register with it.
