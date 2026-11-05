// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "Fixture.h"

#include <algorithm>

namespace an::bench
{

const FixtureSet::Role* FixtureSet::role(const std::string& name) const
{
    const auto at = std::find_if(roles.begin(), roles.end(), [&](const Role& entry) { return entry.name == name; });
    return at == roles.end() ? nullptr : &*at;
}

std::size_t FixtureSet::frameCount() const
{
    std::size_t count = 0;
    for (const Role& entry : roles)
    {
        count += entry.frames.size();
    }
    return count;
}

bool FixtureSource::acquireSet(const std::string& sourceId, const SampleDescriptor& descriptor, FixtureSet& out,
                               std::string& error)
{
    (void)descriptor;
    (void)out;
    // Named rather than silently empty: a source that cannot answer for an id has to say so, or a
    // scenario asking for real frames would run against nothing and report a clean result.
    error = "this fixture source has no multi-frame dataset named '" + sourceId + "'";
    return false;
}

bool FixtureSource::loadFrame(const FixtureFrame& frame, Fixture& out, std::string& error)
{
    (void)out;
    error = "this fixture source cannot read the frame '" + frame.id + "'";
    return false;
}

} // namespace an::bench
