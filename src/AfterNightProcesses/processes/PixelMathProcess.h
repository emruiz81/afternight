// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/PixelMathProcessUiBuilder.h"
#include "processes/GeometryProcessSupport.h"
#include "processes/PixelMathProcessSupport.h"
#include "controls/ImageProcessControl.h"

#include <cstdint>
#include <map>
#include <string>

class ImageViewControl;

/// The PixelMath window: evaluates its expressions over its view's image, or every frame of its sequence, replacing it
/// or opening the result beside it. The dialog and the images every view holds are read on the GUI thread when Apply is
/// pressed; the run parses, binds and evaluates on a worker through `processes/PixelMathProcessSupport.h`, an edit the
/// history records before the view shows it.
class AFTERNIGHTPROCESSES_API PixelMathProcess : public ImageProcessControl, public TableWidgetListener
{
public:
    PixelMathProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    /// What the dialog asks for now. GUI thread.
    [[nodiscard]] an::ui::processes::PixelMathSettings settings() const;

    /// Shows settings in the dialog, as an import does. GUI thread.
    void applySettings(const an::ui::processes::PixelMathSettings& settings);

    /// The images a run started now may name: the target view's name, every other view's image, the active view's and
    /// the target's mask. GUI thread.
    [[nodiscard]] an::ui::processes::PixelMathImageSet imageSet() const;

    void onImageUpdated(ImageViewControl* ctrl) override;

    void onViewControlCreated(const ImageViewControl* ctrl) override;

    void onViewControlDestroyed(const ImageViewControl* ctrl) override;

    void onViewControlRenamed(const std::string& newName, const std::string& oldName,
                              const ImageViewControl* ctrl) override;

    void onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl) override;

    void onRowDoubleClicked(TableWidget* sender, int row) override;

    ~PixelMathProcess();

protected:
    /// Called with a run's operation once it is admitted; nothing in the product.
    [[nodiscard]] virtual an::ui::processes::AdmissionHook admissionHook() const
    {
        return {};
    }

    /// The seed a run's `random()` draws from: a new one for every Apply, so every run draws new numbers, recorded in
    /// the result's lineage so the run can be told apart.
    [[nodiscard]] virtual std::uint64_t drawRandomSeed() const;

private:
    void on_dialog_box_button_clicked(QAbstractButton* button);

    void on_useSingleRGBKExpression_clicked(Qt::CheckState state);
    void on_rescaleResult_clicked(Qt::CheckState state);

    void on_outputModeSelector_currentTextChanged(const QString& str);

    void on_targetColorSpace_currentTextChanged(const QString& str);

    void currentTabChanged(int index);

    void on_importButtonPushed();

    void on_exportButtonPushed();

private:
    void initialize();

    void initializeFunctionTable(TableWidget* functionTable, const QStringList& functions);

    void initializeOperatorTable(TableWidget* operatorTable, const QStringList& operators);

    void initializeSymbolTable(TableWidget* symbolTable);

    void initializeTables();

    void updateImageListInEditors(bool rehighlight = true);

    void updateImageSymbolsInEditors(bool rehighlight = true);

    void updateTargetImageInEditors();

    void updateSummaryTab();

    bool useSeparateRGBScripts() const;

    void updateUseSeparateRGBScriptsCheckbox();

    [[nodiscard]] std::uint32_t targetChannelCount() const;

    [[nodiscard]] std::map<std::string, double> symbolMap(const QString& text) const;

    void executePixelMath();
    void finishRun(const an::ui::processes::GeometryRunOutcome& outcome);

    an::ui::processes::PixelMathProcessUiElements m_ui;
};
