// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QtWidgets/QMainWindow>
#include "AfterNightControlsExport.h"
#include "ImageStatisticsControlUiBuilder.h"
#include "controls/ImageStatisticsReport.h"
#include "controls/ImageViewControlListener.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <stop_token>

class ImageViewControl;

// The statistics window over a view: the view's image measured on a worker in the process's session, shown when the
// latest measurement lands, measured again whenever the view's image changes or the clipped samples are included or
// left out. The table is only ever touched on the GUI thread.
class AFTERNIGHTCONTROLS_API ImageStatisticsControl : public QDialog, public ImageViewControlListener
{
public:
    ImageStatisticsControl(ImageViewControl* control);

    void onImageLoaded(ImageViewControl* ctrl) override;

    void onImageUpdated(ImageViewControl* ctrl) override;

    void onImageRenamed(ImageViewControl* ctrl) override;

    // Measures the view's image again; the table changes when the measurement lands.
    void recalculateAsync();

    // The figures the table shows, once a measurement has landed.
    [[nodiscard]] const std::optional<an::ui::ImageStatisticsReport>& report() const;

    // How many measurements were shown.
    [[nodiscard]] std::uint64_t reportsShown() const;

    // Whether every measurement asked for has landed.
    [[nodiscard]] bool isIdle() const;

    ~ImageStatisticsControl();

private:
    void on_comboBox_currentTextChanged(const QString& str);
    void on_refreshButtonPushed();
    void on_closeButtonPushed();
    void on_UnclippedCheckBoxClicked(Qt::CheckState state);

    virtual void done(int r) override;

    void initialize();

    // Shows a measurement that landed for the latest request.
    void showReport(an::ui::ImageStatisticsReport report);

    void refillData();

    struct RequestState;

    std::shared_ptr<RequestState> m_Requests;
    std::stop_source m_Stop;
    std::optional<an::ui::ImageStatisticsReport> m_Report;

    an::ui::controls::ImageStatisticsControlUiElements m_ui;
};
