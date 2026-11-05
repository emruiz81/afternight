// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file GroupingOptionsWidget.h
 * @brief Widget for configuring frame grouping criteria.
 *
 * Provides UI controls for selecting which metadata fields
 * should be used to group frames (filter, exposure, temperature, etc.).
 */
#pragma once

#include "AfterNightProcessesExport.h"

#include <QWidget>
#include <verdigris/wobjectdefs.h>

#include "processes/ImageIntegration/model/GroupingOptions.h"
#include "processes/ImageIntegration/model/FrameType.h"

// Register GroupingOptions for use in signals
W_REGISTER_ARGTYPE(GroupingOptions)

/**
 * @class GroupingOptionsWidget
 * @brief Widget for configuring how frames are grouped.
 *
 * Displays checkboxes/controls for each grouping criterion:
 * - Filter name (for narrowband workflows)
 * - Exposure time (within tolerance)
 * - Sensor temperature (for thermal noise matching)
 * - ISO/Gain settings
 * - Binning mode
 *
 * Available options vary by frame type (e.g., filter only for lights/flats).
 * Emits optionsChanged() when user modifies any setting.
 *
 * Uses pimpl idiom for ABI stability and fast compilation.
 */
class AFTERNIGHTPROCESSES_API GroupingOptionsWidget : public QWidget
{
    W_OBJECT(GroupingOptionsWidget)

public:
    explicit GroupingOptionsWidget(QWidget* parent = nullptr);
    ~GroupingOptionsWidget() override;

    void setFrameType(FrameType type);

    /// Sets the options. Does not emit optionsChanged (only user interaction does).
    void setOptions(const GroupingOptions& options);
    [[nodiscard]] GroupingOptions currentOptions() const;

    // Signals
    void optionsChanged(const GroupingOptions& options) W_SIGNAL(optionsChanged, options);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
