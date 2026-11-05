// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - mapping a synchronous nested operation's progress onto a share of its
// enclosing operation's scale.
//
// An operation that runs another inside itself - a model fitted once per channel, a denoiser run
// once per pass - reports progress in a fixed total of its own, and the nested operation reports in
// its own. A range installed on the execution for the duration of the nested call turns each nested
// report into a point inside the share it was given of the enclosing total, so one progress bar
// moves steadily across both. A share of zero width keeps the nested operation's reports from moving
// the bar at all.
//
// Ranges on one execution nest: a range installed inside another is a share of that one's share, and
// the destructor restores the one it replaced. Installing and restoring take the operation's
// callback gate, so reports may arrive concurrently from workers, but the scopes themselves must be
// strictly nested, installed before the workers start and removed after they join. Independent
// executions never share a range. A share is sized in the nested operation's units rather than as
// one, so the nested count still moves the bar: a share of one per channel would show an empty bar
// until the last iteration and a full one after it.

#pragma once

#include <cstdint>
#include <memory>

namespace an
{
class ExecutionContext;
class OperationTelemetryState;

/// One installed share of an operation's progress scale, for the life of this object.
class ProgressRange
{
public:
    /// Installs the share from `begin` to `end` of `total` on `execution` until this object is
    /// destroyed.
    ///
    /// A report made through `execution` meanwhile is delivered at the same fraction of the way
    /// from `begin` to `end` as it was of its own total, out of `total`; a report whose own total is
    /// zero is placed at `begin`. Inside another range, the share is taken of that range's share and
    /// the reports are out of that range's total.
    ///
    /// @param execution the operation whose reports are mapped. One that holds nothing makes this
    ///        object do nothing.
    /// @param begin where the share starts on the enclosing scale.
    /// @param end where the share ends; equal to `begin` for a nested operation that should not
    ///        move the bar.
    /// An invalid interval (`begin > end`, `end > total`, or zero `total`) installs nothing and
    /// leaves any enclosing range in effect.
    ///
    /// @param total the nonzero enclosing scale.
    ProgressRange(const ExecutionContext& execution, std::uint64_t begin, std::uint64_t end,
                  std::uint64_t total) noexcept;

    /// Restores the range this one replaced, or none.
    ~ProgressRange();

    ProgressRange(const ProgressRange&) = delete;
    ProgressRange& operator=(const ProgressRange&) = delete;

private:
    friend class ExecutionContext;

    /// The operation-local state the range is installed in, or null for an execution that holds
    /// nothing.
    std::shared_ptr<OperationTelemetryState> m_telemetry;

    /// The range this one replaced, restored by the destructor.
    ProgressRange* m_previous = nullptr;

    /// Where the share starts, on the outermost range's scale.
    long double m_begin = 0.0L;

    /// Where the share ends, on the outermost range's scale.
    long double m_end = 0.0L;

    /// The total every mapped report is delivered out of.
    std::uint64_t m_total = 0u;
};
} // namespace an
