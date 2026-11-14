// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The classic shell's units were Controls' and the application's, and compile against the headers Controls
// precompiles.

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <tbb/task_group.h>
#include <tbb/parallel_sort.h>
#include <tbb/parallel_for_each.h>
#include <tbb/concurrent_vector.h>

#include <QtWidgets>

#include <AfterNightPlatform/Logging.h>

#include "app/GuiThreadInvoke.h"

#include <string>
#include <string_view>
#include <memory>
#include <vector>
#include <deque>
#include <map>
#include <unordered_map>
#include <set>
#include <unordered_set>
#include <fstream>
#include <regex>
#include <algorithm>
#include <stdexcept>

#define FMT_HEADER_ONLY
#include <fmt/format.h>
#include <fmt/xchar.h>
