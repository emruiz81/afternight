// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The expression front end's adversarial corpus, in one place because two things read it.
//
// `Test_EngineImageOps` replays it and sweeps mutations of it on every run of the ordinary suite, and
// `Fuzz_EnginePixelMath` seeds from it. Two copies would drift, and the copy that drifted would be the
// one nobody was looking at: a line added to the suite's list after a parser change would leave the
// campaign fuzzing the parser as it used to be.
//
// Every entry is something a person could type and the parser must answer rather than crash on. They
// are grouped by what makes them hard - unbalanced brackets, a subscript that is not an index, an
// operator with nothing to work on, a comment that never closes, a number that is not one, a
// function call with the wrong shape, and bytes that are not text at all.

#pragma once

#include <array>
#include <string_view>

namespace an::fuzz
{

/// Expressions the front end has to answer rather than crash on.
inline constexpr std::array<std::string_view, 60u> kPixelMathAdversarialCorpus{{
    "",
    " ",
    "\n\t\r",
    "(",
    ")",
    "()",
    "((((((((((",
    "))))))))))",
    "[",
    "]",
    "$",
    "$X",
    "$TT",
    "$T$",
    "$T[",
    "$T[]",
    "$T[-1]",
    "$T[1.5]",
    "$T[999999999999999999999]",
    "1 +",
    "+ ",
    "* 1",
    "1 * * 2",
    "1 ** 2",
    "1 // 2",
    "/*",
    "*/",
    "/*/",
    "//",
    ";",
    ";;;;;;",
    "1;;2",
    ",",
    "f(",
    "f()",
    "f(,)",
    "f(1,)",
    "med(",
    "med()",
    "iif(",
    "iif(1)",
    "iif(1,2)",
    "iif(1,2,3,4)",
    "pixel()",
    "pixel(1,2,3)",
    "1.2.3",
    "..",
    ".",
    "1e",
    "1e+",
    "1e999999",
    "0x10",
    "\x01\x02\x03",
    "\xff\xfe",
    "a b c",
    "= 1",
    "1 = 2",
    "$T = = 1",
    "~",
    "!",
}};

} // namespace an::fuzz
