// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <tbb/concurrent_vector.h>
#include <tbb/parallel_for_each.h>
#include <tbb/parallel_sort.h>
#include <tbb/task_group.h>

#include <QtWidgets>

#include <AfterNightPlatform/Logging.h>

#include "app/GuiThreadInvoke.h"

#include <algorithm>
#include <deque>
#include <fstream>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#define FMT_HEADER_ONLY
#include <fmt/format.h>
#include <fmt/xchar.h>
