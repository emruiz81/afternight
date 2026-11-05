// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"
#include "controls/QSliderSpinBox.h"
#include "controls/StarFilter.h"
#include "controls/StarsFilterControlHandler.h"

#include <QtWidgets/QDialog>

#include <cstddef>
#include <cstdint>
#include <vector>

class QAbstractButton;
class QCheckBox;
class QDialogButtonBox;
class QLineEdit;

// A stars-filter window: three ranges over the stars' fitted figures, a random share and the neighbours of the stars
// kept, previewed over the view and applied to the process's stars. The two star-profiler windows differ only in the
// figures they offer and their layout; every rule is here, and the filter itself is controls/StarFilter.h.
class AFTERNIGHTCONTROLS_API StarsFilterWindow : public QDialog, public StarsFilterControlBase
{
public:
    void updateFilter() override;

    bool isPreviewEnabled() const override;

    void forceClose() override;

    // The random share's seed, drawn when the window opens; stated by a case that holds the choice.
    void setRandomSeed(std::uint64_t seed);

    // What the controls ask of the filter now.
    [[nodiscard]] an::ui::StarFilterRequest filterRequest() const;

    // The stars the window started from.
    [[nodiscard]] const an::ui::StarListPtr& originalStars() const;

    ~StarsFilterWindow() override;

protected:
    // One range's two controls. A brightness range - a magnitude - holds its faint end in the lower control and its
    // bright end in the upper, so moving either toward the other narrows the range as it does for every other figure.
    struct RangeControls
    {
        an::ui::StarFilterField field = an::ui::StarFilterField::Amplitude;
        QSliderDoubleSpinBox* lower = nullptr;
        QSliderDoubleSpinBox* upper = nullptr;
        bool brightnessOrder = false;
    };

    // The controls a layout lends the window.
    struct Controls
    {
        std::vector<RangeControls> ranges;
        QSliderDoubleSpinBox* randomSelection = nullptr;
        QCheckBox* preview = nullptr;
        QCheckBox* includeNeighbours = nullptr;
        QLineEdit* filteredStars = nullptr;
        QDialogButtonBox* buttons = nullptr;
    };

    explicit StarsFilterWindow(StarsFilterControlHandler* parent);

    // Takes the layout's controls, connects them and fills them from the process's stars.
    void attachControls(Controls controls);

    // A control changed: the stars are filtered again for the preview.
    void settingsUpdated();

private:
    void on_dialog_box_button_clicked(QAbstractButton* button);

    void on_PreviewCheckClicked(Qt::CheckState state);

    void on_IncludeNeighborsCheckClicked(Qt::CheckState state);

    void done(int r) override;

    void updateLimits();

    void updateFilteredStars(std::size_t total, std::size_t filtered);

    void executeFilter(bool forPreview);

    Controls m_Controls;

    bool m_IgnoreLastSettingsUpdatedEvent = false;

    bool m_KeepChanges = false;

    std::uint64_t m_Seed = 0u;

    an::ui::StarListPtr m_original_star_list = std::make_shared<const an::ui::StarList>();
};
