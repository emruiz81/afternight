# XISF units written by PixInsight

The twenty-seven `.xisf` files in this directory are XISF units written by PixInsight, the application the format comes
from. They are the only XISF inputs in this tree that another program wrote: `XisfInteropTest` in `Test_EngineImage`
decodes every one and holds its samples to the expressions that made them, so that no decoder - the engine's
included - is its own oracle. The two `.csv` files beside them are PixInsight's own evaluation of astrometric
solutions, which the same suite holds the engine's reading of a solution to.

## Where they come from

They are copied byte for byte, and kept so by `.gitattributes`, from `tests/data/pixinsight/` of the OpenXISF
repository; each is identical to the unit of the same name in the `v1.1.0` release
(`ceca3adcfc06b66face78dd22a1422cc3ccd85cd`), the release the build scripts fetch and build
(`build/scripts/msvc/_build_openxisf.bat`, `build/scripts/linux/_build_openxisf.sh`). OpenXISF is the XISF library
behind the engine's XISF codec; its published repository is https://github.com/astroformats/openxisf.

Ezequiel Ruiz, OpenXISF's author and this repository's maintainer, generated them with PixInsight 1.9.4 (every unit but
the three plate-solved `d5` ones) and PixInsight 1.9.5 (those three) on Windows 11, from synthetic images, by running
PixInsight scripts of OpenXISF's test suite in automation mode. The scripts stay in that repository beside the units
and are not copied here:

| Script | Writes | SHA-256 at the pinned commit |
|---|---|---|
| `make_group_a.js` | `a1` to `a6` | `1b4a80b33f91052445a8302fff4dfb4d90b763ce1f39fb8fec95d320e483dae4` |
| `make_group_b.js` | `b1` to `b10` | `c95bad0f096d5355707a0743117a4ac9b9f33d7f71a616aad0b236b617e8c470` |
| `make_group_c.js` | `c1`, `c2`, `c3`, `c6` | `10f2befc39aeb9b13f1d5a1a6217933684a66814a93238b1db64f10d7de3e526` |
| `make_group_d.js` | `d1` to `d4` | `ddcbc6baf560857d002695864ecc4e5eb529a32da2026edc64d481c8fbc18fae` |
| `make_group_d5.js` | the three `d5` units and the two `d5` tables | `d5086ebe8e1196a4886233eaa0a94164f82f744d1a9a44d6ddd4bf35bd36a766` |

The units are part of OpenXISF's tests and are distributed under the Apache License, Version 2.0, with OpenXISF's
copyright line, `Copyright 2026 Ezequiel Ruiz`. OpenXISF's `NOTICE` records that the RGB units embed the sRGB
IEC61966-2.1 ICC profile PixInsight embeds in the files it writes, which carries the notice "Copyright Hewlett
Packard, 2004"; it is part of the test data only, and the engine drops ICC profiles on read.

The library's catalogue of the units, `tests/samples/sample_catalog.cpp` at the same commit, says how each was made
and what PixInsight wrote into it; the expressions below are the ones it states. OpenXISF's large unit
`tests/data/large/l1-rgb-f32-zlib.xisf` (4,454,169,446 bytes, an RGB Float32 image of 22000 x 17000 pixels in a stored
subblock of 4,294,967,294 bytes and a zlib one) is too large to keep here; `XisfInteropTest` reads it when
`AFTERNIGHT_XISF_LARGE_UNIT` names a copy of it.

## What each unit holds

The pattern image is 37 x 23 pixels, so that no block's sample count is a multiple of 16. With `i = x + 37y`, channel
0 is `i/850`, channel 1 is `1 - i/850`, channel 2 is `x/36` and the alpha channel is `y/22`. Integer samples are
PixInsight's rounding of those values to the full range of their format; a UInt32 sample is rounded from a 32-bit
floating point value, which moves it by up to 128 codes, and is compared within one code of that value.

| Unit | Bytes | SHA-256 | Holds |
|---|---|---|---|
| `a1-gray-u8.xisf` | 107,392 | `1a439d7adb3c32e79392b3e66dcab0aa3a441296813ccfbbb99305dc8af71d3b` | the pattern, Gray UInt8, embedded |
| `a2-gray-u16.xisf` | 107,392 | `b2603692391697e90783efeb552885ab8089b4bf1afc5824c00f43e7a6b11ad4` | the pattern, Gray UInt16, embedded |
| `a3-gray-u32.xisf` | 107,392 | `802d10008283e2e5b2b54865a31ad5c29b65866cf07ec5824b2cf2fa11e72ae5` | the pattern, Gray UInt32, attached |
| `a4-rgb-f32.xisf` | 318,080 | `f1298ddb590d68bd7c18e037dd4b7c59fb785560c4edfbd549de4c986da3984b` | the pattern, RGB Float32 with bounds 0:1, attached |
| `a5-gray-f64.xisf` | 111,488 | `c3f34180483269467e93418307bf7f646e12357232e76d53a6268423bc1ab9bf` | the pattern, Gray Float64 with bounds 0:1, attached |
| `a6-rgba-u16.xisf` | 413,184 | `e25e6b777bf040b135ef5410a50ada6eddd4310c4a6f9a7d5b3b899fdc75ae06` | the pattern, RGB with alpha, UInt16, attached |
| `b1-rgb-f32-zlib.xisf` | 14,035 | `4673f6b3e8dbcf1edc83befc36835f1f48639584be7c9a9db86bdcb64544ff4e` | the image of `a4`, zlib |
| `b2-rgb-f32-zlib-sh.xisf` | 14,009 | `2807d1d9f785f951aebccb9e621d5119e474fccf32a49b84800e6ae369aa9468` | the image of `a4`, zlib, shuffled |
| `b3-rgb-f32-lz4.xisf` | 17,121 | `d8b21d5e5f48d0d12fbe54c354b1003a98b7ac382edcd57795ce707d922b6286` | the image of `a4`, LZ4 |
| `b4-rgb-f32-lz4-sh.xisf` | 16,074 | `56416fdd8403a4404db71baa76f4e23e58554f47c7477537dc565e8571b4f178` | the image of `a4`, LZ4, shuffled |
| `b5-rgb-f32-lz4hc.xisf` | 15,870 | `f49031bae333eb4ac3add7fa91e02d1f07f852fc0df7e5e8f530aa9f86cbcd4f` | the image of `a4`, LZ4-HC |
| `b6-rgb-f32-lz4hc-sh.xisf` | 14,978 | `684c1236a1142e8a1345473fc97e4f5121437aa696365aee46326e0087f28ed0` | the image of `a4`, LZ4-HC, shuffled |
| `b7-rgb-f32-zstd.xisf` | 17,714 | `f29f26e936f2367a90cc0205ca50dec720467a7760a15337ee1c228647f32333` | the image of `a4`, Zstandard |
| `b8-rgb-f32-zstd-sh.xisf` | 16,902 | `de6f434567129b85c89f574c4008c4924b588a1b08ef1dcdcb863603d66074ff` | the image of `a4`, Zstandard, shuffled |
| `b9-gray-u16-zstd-sh.xisf` | 7,838 | `750fce4d877f21c23008050fa6500f953fe8435f12a931b54cd5b92b6a6da7eb` | the image of `a2`, Zstandard, shuffled, embedded |
| `b10-gray-f64-zlib-sh.xisf` | 8,377 | `c7f7ddc11d11b26d498ebe9bcfa23f85372fb51badc8528e4480b4693a2d4796` | the image of `a5`, zlib, shuffled |
| `c1-gray-u16-sha1.xisf` | 107,392 | `a9995d26d914ca4795593173231339e912da3eb2f403a9d2e7ab6c9368445736` | the image of `a2` with a SHA-1 checksum, embedded |
| `c2-gray-u16-sha256.xisf` | 107,392 | `81f578be7bf9d6c02ea863f5ae1327227d881c90b6cd2f3589d58f00420896d2` | the image of `a2` with a SHA-256 checksum, embedded |
| `c3-gray-u16-sha512.xisf` | 107,392 | `a2c2292a05be42c90bb27331c1c37e31adc65afe22c5e9ec4d92ee9270f4b83a` | the image of `a2` with a SHA-512 checksum, embedded |
| `c6-gray-u16-zstd-sh-sha256.xisf` | 8,227 | `7142f2b1aedda1131ef51f3b782ba684fcb3bc49da53225a4fdedbb06b549e88` | the image of `a2`, Zstandard, shuffled, with a SHA-256 checksum of the stored bytes, embedded |
| `d1-rich-rgb.xisf` | 322,176 | `3bf7ab8d22406522057e8de57a63edbd1394a4b7b7cef9e6c72c8ffddbc92185` | the pattern, RGB UInt16, with six FITS keywords, the Adobe RGB (1998) working space, a display function, a resolution of 120 by 100 pixels per centimetre, an ICC profile, Observation and Instrument properties, the processing history, two vectors and a matrix, and a thumbnail |
| `d2-cfa-raw.xisf` | 10,240 | `df9bc48cc6397529db9da22d91f18ae957afdacff2fd312e955344540cebc3d7` | a 64 x 48 Gray UInt16 frame of `(x + 64y)/3071`, image type Light, nine camera keywords, the filter array RGGB named `RGGB Bayer filter` |
| `d3-typed-properties.xisf` | 521,088 | `8f622b8d4837a0d68d2e4b5a8fbbb6dca577b05b6b45e9d21118c8ea05105f11` | the image of `a2` with 31 typed properties, among them a String of 10,000 characters and a long vector |
| `d4-multi-image.xisf` | 14,308 | `aa2a2e4ebd51a811129b61bc1b2245593323a345c675eba42f70c1dac69727e6` | three images: that of `a2` (id `first`), that of `a4` (id `second`), and an 11 x 7 Gray UInt8 image of `(x + 11y)/76` (id `third`) |
| `d5-astrometry.xisf` | 157,936 | `2b708f36ee9c402af2f19837a47a87b25492c39d29aba334f5999e17e9e68468` | a 400 x 300 Gray UInt16 image of `(x + y)/1398`, Zstandard, shuffled, embedded, with the astrometric solution ImageSolver writes: thin plate splines of order 2 |
| `d5-astrometry-local.xisf` | 283,666 | `aadecc0116ccc6de72b723a7e66c7ccaca0aca3570de94d1eb01d5cc9747b25f` | the image of `d5-astrometry`, with a solution of recursive surface splines of order 4 |
| `d5-astrometry-multiquadric.xisf` | 173,472 | `c8133f1982ad9c999a7a98d29a8e39fecf64dc69194cdddc388b1c703627f95a` | the image of `d5-astrometry`, with a solution of multiquadric splines of order 2 |

## PixInsight's evaluation of astrometric solutions

`make_group_d5.js` also writes what PixInsight 1.9.5 computes from solutions, evaluated exactly rather than through
the grids PixInsight caches over an image. Both tables are text: a `#` line is a comment, and every other line names
what it is in its first field.

| Table | Bytes | SHA-256 | Holds |
|---|---|---|---|
| `d5-reference.csv` | 10,957 | `c1327502f03a5a0f285da0f495a56b5cb4b03dac6f4ed1e52dd8b75899386fe4` | for each `d5` unit, through its whole solution, distortion included: image to celestial at the corners, the centre, 20 scattered points and 3 points beyond the image (`image,<file>,<x>,<y>,<ra>,<dec>`), and celestial to image at 5 sky positions (`celestial,<file>,<ra>,<dec>,<x>,<y>`) |
| `d5-projections.csv` | 93,755 | `b8c8ee758906ff5cd1168d8b01e096433b8ab508ed83d16b7b5b3bb4d2568c0b` | linear solutions - the first layer alone, with the default native coordinates and the reference image coordinates (200, 150) - of the seven projections of the specification at eight reference points each (`solution,<name>,<projection>,<ra0>,<dec0>,<m00>,<m01>,<m10>,<m11>,<x0>,<y0>`), and PixInsight's coordinates of each at image and sky points in the same forms, a point with none left empty |

Image coordinates are XISF's: x grows with the column and y with the row, and the centre of the first pixel is at
(0.5, 0.5).
