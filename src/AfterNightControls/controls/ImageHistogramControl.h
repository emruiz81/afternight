// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QtWidgets/QMainWindow>
#include <QtWidgets/QWidget>
#include "AfterNightControlsExport.h"
#include "ImageHistogramControlUiBuilder.h"
#include "controls/ImageViewControlListener.h"
#include "widgets/HistogramCompanions.h"
#include "widgets/HistogramWidget.h"

#include <QPointer>

#include <verdigris/wobjectdefs.h>

#include <memory>

class ImageViewControl;

// The histogram window over a view: the view's image counted on a worker at the resolution chosen, shown when the
// latest count lands, counted again whenever the view's image changes.
class AFTERNIGHTCONTROLS_API ImageHistogramControl : public QDialog, public ImageViewControlListener
{
    W_OBJECT(ImageHistogramControl)

public:
    ImageHistogramControl(ImageViewControl* control);

    void onImageLoaded(ImageViewControl* ctrl) override;

    void onImageUpdated(ImageViewControl* ctrl) override;

    void onImageRenamed(ImageViewControl* ctrl) override;

    void refreshIcons();

    // The widget the counts are drawn in.
    [[nodiscard]] HistogramWidget* histogramWidget() const;

    // The scheduler the view's image is counted through.
    [[nodiscard]] const an::ui::HistogramCountScheduler& scheduler() const;

    ~ImageHistogramControl();

private:
    void on_dialog_box_button_clicked(QAbstractButton* button);

    void on_channelSelector_currentTextChanged(const QString& str);
    void on_plotResolution_currentTextChanged(const QString& str);
    void on_gridClicked(bool checked);
    void on_zoomInButton();
    void on_zoomOutButton();
    void on_zoomResetButton();
    void on_localChannelNormalization(bool checked);
    void on_showClippingInfo(bool checked);
    void on_showHistogramAreas(bool checked);

private:
    virtual void done(int r) override;

    void changeEvent(QEvent* event) override;

    void initialize();

    void updateChannelSelector();

    void resetSettings();

    // Counts the image the view shows now.
    void countViewImage();

    QPointer<HistogramWidget> m_HistogramWidget;
    std::unique_ptr<an::ui::HistogramCountScheduler> m_Scheduler;

    an::ui::controls::ImageHistogramControlUiElements m_ui;
};
