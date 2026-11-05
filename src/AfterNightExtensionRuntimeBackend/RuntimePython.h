// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The interpreter's headers, included in the order the embedded module needs them.
///
/// `Python.h` must come before any standard or engine header that pulls in a C library feature header, and an MSVC
/// Debug build must not let it see `_DEBUG`, which would make it ask for the debug interpreter the bundled runtime does
/// not ship. Every unit of the runtime backend that names a Python type includes this header first and the
/// interpreter's headers nowhere else.
#pragma once

#if defined(_MSC_VER) && defined(_DEBUG)
#define AN_RESTORE_DEBUG_AFTER_PYTHON_H
#undef _DEBUG
#endif

#include <Python.h>

#ifdef AN_RESTORE_DEBUG_AFTER_PYTHON_H
#define _DEBUG
#undef AN_RESTORE_DEBUG_AFTER_PYTHON_H
#endif

#include <pybind11/embed.h>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>

namespace an::sdk
{
namespace py = pybind11;
} // namespace an::sdk
