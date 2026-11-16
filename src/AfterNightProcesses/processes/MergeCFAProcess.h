// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/TypedProcessControl.h"
#include "processes/GeometryProcessSupport.h"
#include "processes/MergeCFAProcessUiBuilder.h"

#include <QRect>
#include <QString>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class QAbstractButton;
class QComboBox;
class QWidget;

/// What one join reads, snapshotted on the GUI thread when it is asked for: the first plane is the run's target, the
/// other three are read beside it.
struct MergeCfaParams
{
    /// The second, third and fourth planes' images, in the dialog's order.
    std::array<std::shared_ptr<const an::consumer::ImageAsset>, 3u> others;

    /// The four planes' views' names, for the record.
    std::array<std::string, 4u> names;

    /// The arrangement the planes are joined under, and its name in the dialog.
    an::CfaPattern pattern = an::CfaPattern::Rggb;
    QString patternText;

    /// What an Apply does once its operation is admitted and before it reads anything; empty in the product.
    an::ui::processes::AdmissionHook admitted;
};

/// The Merge CFA window: joins four grey views - one per position of a two-by-two block, as Split CFA makes them - into
/// one mosaic under the arrangement the dialog names, launched on no image. The first plane is its target: the preview
/// shows the joined mosaic, and Ok joins the four on the thread pool into a new view - the preview framework's run -
/// after which the window closes. Four sequences are joined frame by frame into one mosaic sequence by a run of the
/// window's own; nothing is opened unless it all succeeded.
class AFTERNIGHTPROCESSES_API MergeCFAProcess : public an::ui::TypedProcessControl<MergeCfaParams>
{
public:
    explicit MergeCFAProcess(QWidget* parent = nullptr);

    static std::string getProcessNameStatic();

    void onViewControlCreated(const ImageViewControl* ctrl) override;

    void onViewControlDestroyed(const ImageViewControl* ctrl) override;

    void onViewControlRenamed(const std::string& newName, const std::string& oldName,
                              const ImageViewControl* ctrl) override;

    void onViewControlUpdated(const ImageViewControl* ctrl) override;

    void onApplicationClose() override;

    /// Whether a join is on its way.
    [[nodiscard]] bool isRunning() const;

    /// How many joins were opened as a new view, and how many ended without one.
    [[nodiscard]] std::uint64_t runsPublished() const noexcept;
    [[nodiscard]] std::uint64_t runsFailed() const noexcept;

    /// Asks the join on its way to stop.
    void requestStop();

    ~MergeCFAProcess() override;

protected:
    /// Called with a run's operation once it is admitted; nothing in the product.
    [[nodiscard]] virtual an::ui::processes::AdmissionHook admissionHook() const
    {
        return {};
    }

    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const MergeCfaParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessStep describeTypedProcessStep(const MergeCfaParams& params) const override;
    [[nodiscard]] an::ui::PreviewScaling previewScaling() const override;
    [[nodiscard]] std::vector<std::shared_ptr<const an::consumer::ImageAsset>>
    typedImagesRead(const MergeCfaParams& params) const override;
    void executeProcessCompletedImpl(RTPreviewMode previewMode, bool sequenceMode) override;
    void executeProcessEndedImpl(RTPreviewMode previewMode, bool sequenceMode) override;

private:
    void on_dialog_box_button_clicked(QAbstractButton* button);

    void refreshTheme() override;
    [[nodiscard]] bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;
    void settingsUpdatedImpl() override;
    void getBatchExecutionParameters(bool& createNewImage, bool& closeAtEnd, bool& executeInBackground,
                                     std::string& newImageName) override;
    [[nodiscard]] MergeCfaParams getParams() const override;

    void initialize();

    [[nodiscard]] bool isPlaneViewSupported(const ImageViewControl* ctrl) const;
    void addViewToSelectors(const ImageViewControl* ctrl);
    void removeViewFromSelectors(const QString& name);

    void executeCFAComposition();
    void joinSequences();
    void finishRun(const an::Status& outcome, bool published);

    void validateSelectedImages();

    [[nodiscard]] std::array<QComboBox*, 4u> selectors() const;
    [[nodiscard]] an::CfaPattern chosenPattern() const;

    an::ui::processes::MergeCFAProcessUiElements m_ui;

    struct Run;
    std::unique_ptr<Run> m_run;
};
