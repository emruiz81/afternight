// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Exercise the native lossless-JPEG difference reader without a camera fixture.
#include <libraw/libraw.h>

#include <iostream>

class NativeDecoder final : public LibRaw
{
public:
    int zeroDifference()
    {
        // A zero-width Huffman lookup returns category zero without reading a stream.
        unsigned short huffman[]{0};
        return ljpeg_diff(huffman);
    }
};

int main()
{
    NativeDecoder decoder;
    if (decoder.zeroDifference() != 0)
        return 1;
    std::cout << "Native lossless-JPEG zero-difference regression passed\n";
}
