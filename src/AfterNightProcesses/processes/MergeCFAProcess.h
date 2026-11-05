// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/TopLevelProcessBaseControl.h"
#include "processes/GeometryProcessSupport.h"
#include "processes/MergeCFAProcessUiBuilder.h"
#include "app/AfterNightAppListener.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

class QAbstractButton;
class QComboBox;
class QWidget;

/// The Merge CFA window: joins four grey views - one per position of a two-by-two block, as Split CFA makes them - into
/// one mosaic under the arrangement the dialog names, or four sequences frame by frame into one mosaic sequence, and
/// opens it as a new view. The choices are read on the GUI thread when Ok is pressed; the join runs on the thread pool,
/// and nothing is opened unless it all succeeded.
class AFTERNIGHTPROCESSES_API MergeCFAProcess : public TopLevelProcessBaseControl, public AfterNightAppListener
{
public:
    explicit MergeCFAProcess(QWidget* parent = nullptr);

    static std::string getProcessNameStatic();

    void onViewControlCreated(const ImageViewControl* ctrl) override;

    void onViewControlDestroyed(const ImageViewControl* ctrl) override;

    void onViewControlRenamed(const std::string& newName, const std::string& oldName,
                              const ImageViewControl* ctrl) override;

    void onViewControlUpdated(const ImageViewControl* ctrl) override;

    void refreshTheme() override;

    /// Whether a join is on its way.
    [[nodiscard]] bool isRunning() const noexcept;

    /// How many joins were opened as a new view, and how many ended without one.
    [[nodiscard]] std::uint64_t runsPublished() const noexcept;
    [[nodiscard]] std::uint64_t runsFailed() const noexcept;

    /// Asks the join on its way to stop.
    void requestStop();

    ~MergeCFAProcess();

protected:
    /// Called with a run's operation once it is admitted; nothing in the product.
    [[nodiscard]] virtual an::ui::processes::AdmissionHook admissionHook() const
    {
        return {};
    }

private:
    void on_dialog_box_button_clicked(QAbstractButton* button);

private:
    void initialize();

    [[nodiscard]] bool isPlaneViewSupported(const ImageViewControl* ctrl) const;
    void addViewToSelectors(const ImageViewControl* ctrl);
    void removeViewFromSelectors(const QString& name);

    void executeCFAComposition();
    void finishRun(const an::Status& outcome, bool published);

    void validateSelectedImages();

    [[nodiscard]] std::array<QComboBox*, 4u> selectors() const;

    an::ui::processes::MergeCFAProcessUiElements m_ui;

    struct Run;
    std::unique_ptr<Run> m_run;
};
