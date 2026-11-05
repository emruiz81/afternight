// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the process windows share: the services of the application context they reach, the dialogs they open, the
/// view a name stands for, the batch-update guards of their tables and trees, and the closed maps from the text a
/// dialog offers - and a window persists - to what the engine is asked for.
///
/// A dialog's combo boxes show text, and the standalone tools store that text in their settings. Each map is a fixed
/// table from that text onto the engine's names: a demosaic method, a sampling interpolation, a registration transform,
/// a stack's combination and rejection, a frame normalization. A text the table does not hold reads as the setting's
/// default and says it was not known, so a window that reads a stored setting can tell a choice it made from one it
/// never offered.
#pragma once

#include "app/AppContextHelpers.h"
#include "app/DemosaicTextMaps.h"
#include "app/FileDialogUtils.h"
#include "app/ProcessControlEvent.h"
#include "app/SequenceOutputCleanup.h"

#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/TableWidgetUtils.h"

#include <AfterNightEngine/Algorithms/Demosaic/Demosaic.h>
#include <AfterNightEngine/Algorithms/Kernel/SamplingKernel.h>
#include <AfterNightEngine/Algorithms/Normalization/NormalizationMethod.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>
#include <AfterNightEngine/Algorithms/Stacking/StackCombination.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRejectionParameters.h>
#include <AfterNightEngine/IO/SequenceDescriptor.h>

#include <QFileDialog>
#include <QHeaderView>
#include <QProcess>
#include <QSizePolicy>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace process_utils
{

// ---- The texts the dialogs offer.

inline const QString DEMOSAIC_DEFAULT_KEY = QStringLiteral("default");
inline const QString DEMOSAIC_BILINEAR = QStringLiteral("Bilinear");
inline const QString DEMOSAIC_VNG4 = QStringLiteral("VNG4");
inline const QString DEMOSAIC_AHD = QStringLiteral("AHD");
inline const QString DEMOSAIC_AMAZE = QStringLiteral("AMaZE");
inline const QString DEMOSAIC_LMMSE = QStringLiteral("LMMSE");
inline const QString DEMOSAIC_RCD = QStringLiteral("RCD");

inline const QString INTERP_AUTO = QStringLiteral("Auto");
inline const QString INTERP_NEAREST_NEIGHBOR = QStringLiteral("Nearest Neighbor");
inline const QString INTERP_BILINEAR = QStringLiteral("Bilinear");
inline const QString INTERP_BICUBIC = QStringLiteral("Bicubic");
inline const QString INTERP_AREA = QStringLiteral("Pixel Area Relation");
inline const QString INTERP_LANCZOS4 = QStringLiteral("Lanczos4");
inline const QString INTERP_BIT_EXACT_BILINEAR = QStringLiteral("Bit Exact Bilinear");
inline const QString INTERP_BIT_EXACT_NEAREST_NEIGHBOR = QStringLiteral("Bit Exact Nearest Neighbor");

// ---- The closed maps from a dialog's text to what the engine is asked for.

/// The demosaic method a dialog's text names.
struct DemosaicLabelChoice
{
    /// The method.
    an::DemosaicMethod method = an::DemosaicMethod::Rcd;

    /// Whether the text is one the dialogs offer; a text that is not reads as RCD.
    bool known = true;
};

/// The demosaic method a dialog's text names, compared exactly: "Bilinear" is the fast estimate the engine names
/// `DemosaicMethod::Fast`; "VNG4", "AHD" - the luminance-only homogeneity - "AMaZE", "LMMSE" and "RCD" the methods of
/// those names; anything else reads as RCD, the setting's default, and is marked unknown.
///
/// @param label the text.
/// @return the method, and whether the text was known.
[[nodiscard]] inline DemosaicLabelChoice demosaicChoiceFromLabel(const QString& label)
{
    struct Entry
    {
        const QString* label;
        an::DemosaicMethod method;
    };
    const std::array<Entry, 6u> entries{{{&DEMOSAIC_BILINEAR, an::DemosaicMethod::Fast},
                                         {&DEMOSAIC_VNG4, an::DemosaicMethod::Vng4},
                                         {&DEMOSAIC_AHD, an::DemosaicMethod::LuminanceAhd},
                                         {&DEMOSAIC_AMAZE, an::DemosaicMethod::Amaze},
                                         {&DEMOSAIC_LMMSE, an::DemosaicMethod::Lmmse},
                                         {&DEMOSAIC_RCD, an::DemosaicMethod::Rcd}}};
    for (const Entry& entry : entries)
    {
        if (label == *entry.label)
        {
            return {entry.method, true};
        }
    }
    return {an::DemosaicMethod::Rcd, false};
}

/// The text a dialog offers for a persisted demosaic setting: `bilinear`, `vng4`, `ahd`, `amaze`, `lmmse` and `rcd`,
/// case and surrounding spaces aside, name the method of the same name; anything else names RCD, the setting's default.
/// Reading the text through `demosaicChoiceFromLabel` gives the method `an::ui::demosaicMethodFromSettingsKey` reads
/// the setting as.
///
/// @param key the setting's value.
/// @return the text.
[[nodiscard]] inline QString demosaicLabelFromSettingsKey(const QString& key)
{
    const QString normalized = key.trimmed().toLower();
    if (normalized == QStringLiteral("bilinear"))
        return DEMOSAIC_BILINEAR;
    if (normalized == QStringLiteral("vng4"))
        return DEMOSAIC_VNG4;
    if (normalized == QStringLiteral("ahd"))
        return DEMOSAIC_AHD;
    if (normalized == QStringLiteral("amaze"))
        return DEMOSAIC_AMAZE;
    if (normalized == QStringLiteral("lmmse"))
        return DEMOSAIC_LMMSE;
    return DEMOSAIC_RCD;
}

/// The sampling interpolation a dialog's text names.
struct InterpolationChoice
{
    /// The interpolation.
    an::KernelInterpolation interpolation = an::KernelInterpolation::Lanczos4;

    /// Whether the text names an interpolation; a text that does not reads as Lanczos-4.
    bool known = true;
};

/// The sampling interpolation a dialog's text names, compared exactly: the seven interpolations the resize, rotate and
/// registration dialogs offer, the two bit-exact ones computed in fixed point. "Auto" names none:
/// the dialog that offers it chooses from what the operation does before it asks here, and read here it is Lanczos-4,
/// as anything else the table does not hold is, marked unknown.
///
/// @param label the text.
/// @return the interpolation, and whether the text named one.
[[nodiscard]] inline InterpolationChoice interpolationFromLabel(const QString& label)
{
    const std::array<std::pair<const QString*, an::KernelInterpolation>, 7u> entries{
        {{&INTERP_NEAREST_NEIGHBOR, an::KernelInterpolation::Nearest},
         {&INTERP_BILINEAR, an::KernelInterpolation::Linear},
         {&INTERP_BICUBIC, an::KernelInterpolation::Cubic},
         {&INTERP_AREA, an::KernelInterpolation::Area},
         {&INTERP_LANCZOS4, an::KernelInterpolation::Lanczos4},
         {&INTERP_BIT_EXACT_BILINEAR, an::KernelInterpolation::LinearExact},
         {&INTERP_BIT_EXACT_NEAREST_NEIGHBOR, an::KernelInterpolation::NearestExact}}};
    for (const auto& [text, interpolation] : entries)
    {
        if (label == *text)
        {
            return {interpolation, true};
        }
    }
    return {an::KernelInterpolation::Lanczos4, false};
}

/// The registration transform a dialog's text names.
struct TransformChoice
{
    /// The transform.
    an::RegistrationTransformKind kind = an::RegistrationTransformKind::Similarity;

    /// Whether the text names a transform; a text that does not reads as a similarity.
    bool known = true;
};

/// The registration transform a dialog's text names, compared exactly: "Similarity", "Affine", "Homography" for the
/// projective map, and "Polynomial 2", "Polynomial 3" and "Polynomial 4" for a linear map followed by a polynomial of
/// that order. "Auto" names none: the registration dialog chooses before it asks here. Anything else reads as a
/// similarity, the setting's default, marked unknown.
///
/// @param label the text.
/// @return the transform, and whether the text named one.
[[nodiscard]] inline TransformChoice transformKindFromLabel(const QString& label)
{
    static constexpr std::array<std::pair<const char*, an::RegistrationTransformKind>, 6u> kEntries{
        {{"Similarity", an::RegistrationTransformKind::Similarity},
         {"Affine", an::RegistrationTransformKind::Affine},
         {"Homography", an::RegistrationTransformKind::Projective},
         {"Polynomial 2", an::RegistrationTransformKind::Polynomial2},
         {"Polynomial 3", an::RegistrationTransformKind::Polynomial3},
         {"Polynomial 4", an::RegistrationTransformKind::Polynomial4}}};
    for (const auto& [text, kind] : kEntries)
    {
        if (label == QLatin1String(text))
        {
            return {kind, true};
        }
    }
    return {an::RegistrationTransformKind::Similarity, false};
}

/// The stack a dialog's text names: how the surviving samples combine, and which samples are rejected first.
struct StackMethodChoice
{
    /// How the samples that survive combine.
    an::StackCombination combination = an::StackCombination::Average;

    /// Which samples are rejected before they combine.
    an::StackRejection rejection = an::StackRejection::None;

    /// Whether the text names a stack; a text that does not reads as a plain average.
    bool known = true;
};

/// The stack a dialog's text names, surrounding spaces aside. A dialog names a combination and a rejection with one
/// text; the engine names them apart, so each rejection a dialog offers is an average of the samples it keeps. Two
/// rejections have two spellings each - "MAD Sigma Clipping" and "MAD Clipping", "Winsorized Sigma" and "Winsorized
/// Sigma Clipping" - which read the same; anything else reads as a plain average, the setting's default, marked
/// unknown.
///
/// @param label the text.
/// @return the combination and the rejection, and whether the text named a stack.
[[nodiscard]] inline StackMethodChoice stackMethodFromLabel(const QString& label)
{
    struct Entry
    {
        const char* label;
        an::StackCombination combination;
        an::StackRejection rejection;
    };
    static constexpr std::array<Entry, 15u> kEntries{
        {{"Maximum", an::StackCombination::Maximum, an::StackRejection::None},
         {"Minimum", an::StackCombination::Minimum, an::StackRejection::None},
         {"Average", an::StackCombination::Average, an::StackRejection::None},
         {"Sum", an::StackCombination::Sum, an::StackRejection::None},
         {"Median", an::StackCombination::Median, an::StackRejection::None},
         {"Auto-Adaptive Weighted Average", an::StackCombination::AutoAdaptiveWeightedAverage,
          an::StackRejection::None},
         {"Sigma Clipping", an::StackCombination::Average, an::StackRejection::SigmaClipping},
         {"MAD Sigma Clipping", an::StackCombination::Average, an::StackRejection::MadSigmaClipping},
         {"MAD Clipping", an::StackCombination::Average, an::StackRejection::MadSigmaClipping},
         {"Median Sigma", an::StackCombination::Average, an::StackRejection::MedianSigmaReplacement},
         {"Winsorized Sigma", an::StackCombination::Average, an::StackRejection::WinsorizedSigmaClipping},
         {"Winsorized Sigma Clipping", an::StackCombination::Average, an::StackRejection::WinsorizedSigmaClipping},
         {"Linear Fit", an::StackCombination::Average, an::StackRejection::LinearFitClipping},
         {"Percentile", an::StackCombination::Average, an::StackRejection::PercentileClipping},
         {"GESDT", an::StackCombination::Average, an::StackRejection::GeneralizedEsdTest}}};
    const QString text = label.trimmed();
    for (const Entry& entry : kEntries)
    {
        if (text == QLatin1String(entry.label))
        {
            return {entry.combination, entry.rejection, true};
        }
    }
    return {an::StackCombination::Average, an::StackRejection::None, false};
}

/// The frame normalization a dialog's text names.
struct NormalizationChoice
{
    /// The method, or nothing for no normalization.
    std::optional<an::NormalizationMethod> method;

    /// Whether the text names a normalization; a text that does not reads as none.
    bool known = true;
};

/// The frame normalization a dialog's text names, surrounding spaces aside: "None", "Additive", "Multiplicative",
/// "Additive with Scaling" and "Multiplicative with Scaling", the last two in either capitalization a stored setting
/// may hold; anything else reads as no normalization, the setting's default, marked unknown.
///
/// @param label the text.
/// @return the method or none, and whether the text named one.
[[nodiscard]] inline NormalizationChoice normalizationFromLabel(const QString& label)
{
    struct Entry
    {
        const char* label;
        std::optional<an::NormalizationMethod> method;
    };
    static constexpr std::array<Entry, 7u> kEntries{
        {{"None", std::nullopt},
         {"Additive", an::NormalizationMethod::Additive},
         {"Multiplicative", an::NormalizationMethod::Multiplicative},
         {"Additive with Scaling", an::NormalizationMethod::AdditiveScaling},
         {"Additive With Scaling", an::NormalizationMethod::AdditiveScaling},
         {"Multiplicative with Scaling", an::NormalizationMethod::MultiplicativeScaling},
         {"Multiplicative With Scaling", an::NormalizationMethod::MultiplicativeScaling}}};
    const QString text = label.trimmed();
    for (const Entry& entry : kEntries)
    {
        if (text == QLatin1String(entry.label))
        {
            return {entry.method, true};
        }
    }
    return {std::nullopt, false};
}

// ---- The application context's services.

[[nodiscard]] inline an::ui::IPathMemoryService* currentPathMemoryService()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getPathMemoryService();
    }

    return nullptr;
}

[[nodiscard]] inline an::ui::IFileDialogService* currentFileDialogService()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getFileDialogService();
    }

    return nullptr;
}

[[nodiscard]] inline an::ui::ITempFileService* currentTempFileService()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getTempFileService();
    }

    return nullptr;
}

[[nodiscard]] inline an::ui::IRecentFilesService* currentRecentFilesService()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getRecentFilesService();
    }

    return nullptr;
}

[[nodiscard]] inline an::ui::IActionStateManager* currentActionStateManager()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getActionStateManager();
    }

    return nullptr;
}

[[nodiscard]] inline ImageViewControl* findViewControlByName(const QString& name)
{
    if (name.trimmed().isEmpty())
    {
        return nullptr;
    }

    if (auto* viewControlManager = currentViewControlManager())
    {
        return viewControlManager->getViewControlByName(name.toUtf8().toStdString());
    }

    return nullptr;
}

inline void updateActiveImageActionsAsync()
{
    invokeOnObjectThread(currentGuiThreadTarget(), []() {
        if (auto* actionStateManager = currentActionStateManager())
        {
            actionStateManager->updateActiveImageActions();
        }
    });
}

/// A path in the session's temporary directory for a new sequence of a container, or an empty path when the
/// application lends no temporary-file service.
///
/// @param format the container the sequence is written as.
/// @return the path.
[[nodiscard]] inline std::string createTempSequenceFilePath(const an::SequenceFormat format)
{
    if (auto* tempFileService = currentTempFileService())
    {
        return tempFileService->createTempFilePathForNewSequence(format);
    }

    return {};
}

inline QStringList selectImageFiles(QWidget* parent, const QString& title, const QString& folderContext,
                                    const bool supportMultipleFiles = true)
{
    if (auto* fileDialogService = currentFileDialogService())
    {
        return fileDialogService->selectImageFiles(parent, title, folderContext, supportMultipleFiles);
    }

    return {};
}

inline QStringList selectFiles(QWidget* parent, const QString& title, const QString& folderContext,
                               const QStringList& fileExtensions)
{
    if (auto* fileDialogService = currentFileDialogService())
    {
        return fileDialogService->selectFiles(parent, title, folderContext, fileExtensions);
    }

    return {};
}

inline QStringList saveFiles(QWidget* parent, const QString& title, const QString& defaultDir,
                             const QString& folderContext, const QStringList& fileExtensions)
{
    if (auto* fileDialogService = currentFileDialogService())
    {
        return fileDialogService->saveFiles(parent, title, defaultDir, folderContext, fileExtensions);
    }

    return {};
}

[[nodiscard]] inline QString selectPath(const QString& contextKey, const QString& defaultValue = {})
{
    if (auto* pathMemoryService = currentPathMemoryService())
    {
        return pathMemoryService->selectPath(contextKey, defaultValue);
    }

    return defaultValue;
}

inline bool rememberPath(const QString& contextKey, const QString& path)
{
    if (auto* pathMemoryService = currentPathMemoryService())
    {
        return pathMemoryService->upsertPath(contextKey, path);
    }

    return false;
}

[[nodiscard]] inline QString selectExistingDirectory(QWidget* parent, const QString& caption = QString(),
                                                     const QString& dir = QString(),
                                                     QFileDialog::Options options = QFileDialog::ShowDirsOnly)
{
    return an::ui::dialogs::getExistingDirectory(currentGlobalSettings(), parent, caption, dir, options);
}

inline void openRecentSingleFile(const QString& filePath)
{
    if (auto* recentFilesService = currentRecentFilesService())
    {
        recentFilesService->openRecentSingleFile(filePath);
    }
}

inline void openSettingsPage(const QString& pageName)
{
    if (auto* dialogService = currentDialogService())
    {
        dialogService->openSettingsAt(pageName);
    }
}

inline void notifyWarnMessage(QWidget* parent, const QString& title, const QString& text)
{
    if (auto* dialogService = currentDialogService())
    {
        dialogService->notifyWarnMessage(parent, title, text);
    }
}

inline void notifyInfoMessage(QWidget* parent, const QString& title, const QString& text)
{
    if (auto* dialogService = currentDialogService())
    {
        dialogService->notifyInfoMessage(parent, title, text);
    }
}

// ---- Widgets.

inline void installPreviewInPlaceholder(QWidget* placeholder, QWidget* previewControl)
{
    if (!placeholder || !previewControl)
    {
        return;
    }

    auto* previewLayout = dynamic_cast<QVBoxLayout*>(placeholder->layout());
    if (!previewLayout)
    {
        previewLayout = new QVBoxLayout(placeholder);
        previewLayout->setContentsMargins(0, 0, 0, 0);
        previewLayout->setSpacing(0);
    }

    previewControl->setMinimumSize(placeholder->minimumSize());
    previewControl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    if (previewLayout->indexOf(previewControl) < 0)
    {
        previewLayout->addWidget(previewControl, 1);
    }
    placeholder->show();
    previewControl->show();
}

[[nodiscard]] inline ImageViewControl* createStaticPreviewInPlaceholder(QWidget* placeholder,
                                                                        const std::string& uniqueName)
{
    if (!placeholder)
    {
        return nullptr;
    }

    auto* previewControl = new ImageViewControl(placeholder);
    previewControl->setObjectName(QStringLiteral("imagePreviewViewControl"));
    previewControl->initialize(uniqueName, ViewControlMode::st_preview);
    previewControl->setAutoDebayerCfaImagePreviews(true);
    installPreviewInPlaceholder(placeholder, previewControl);
    return previewControl;
}

inline bool stopProcess(QProcess& process, const int terminateTimeoutMs = 1000, const int killTimeoutMs = 2000)
{
    if (process.state() == QProcess::NotRunning)
    {
        return true;
    }

    process.terminate();
    if (process.waitForFinished(terminateTimeoutMs))
    {
        return true;
    }

    process.kill();
    return process.waitForFinished(killTimeoutMs);
}

inline void applyStandardFileTableColumnWidths(QTableWidget* table, const int fileColumn = 0,
                                               const int defaultMinWidth = 90, const int fileMinWidth = 220,
                                               const int fileMaxWidth = 420)
{
    if (!table)
    {
        return;
    }

    auto* header = table->horizontalHeader();
    if (!header)
    {
        return;
    }

    constexpr int kBasePadding = 28;
    constexpr int kFileExtraPadding = 24;

    header->setSectionResizeMode(QHeaderView::Interactive);

    for (int column = 0; column < table->columnCount(); ++column)
    {
        table->resizeColumnToContents(column);

        const QString headerText =
            table->horizontalHeaderItem(column) ? table->horizontalHeaderItem(column)->text() : QString();
        const int headerWidth = header->fontMetrics().horizontalAdvance(headerText) + kBasePadding;

        int targetWidth = (std::max)(table->columnWidth(column) + kBasePadding, headerWidth);

        if (column == fileColumn)
        {
            targetWidth = (std::min)((std::max)(targetWidth + kFileExtraPadding, fileMinWidth), fileMaxWidth);
        }
        else
        {
            targetWidth = (std::max)(targetWidth, defaultMinWidth);
        }

        table->setColumnWidth(column, targetWidth);
    }
}

inline QString tableItemTextOrDefault(const QTableWidget* table, int row, int column,
                                      const QString& defaultValue = QString())
{
    if (!table)
    {
        return defaultValue;
    }

    auto* item = table->item(row, column);
    if (!item)
    {
        return defaultValue;
    }

    const QString text = item->text();
    return text.isEmpty() ? defaultValue : text;
}

// RAII guard for batch updates on QTreeWidget
// Disables updates and blocks signals during bulk operations for better performance
class TreeBatchUpdateGuard
{
public:
    explicit TreeBatchUpdateGuard(QTreeWidget* tree)
        : m_Tree(tree), m_WasUpdatesEnabled(tree ? tree->updatesEnabled() : true),
          m_WasSignalsBlocked(tree ? tree->signalsBlocked() : false)
    {
        if (m_Tree)
        {
            m_Tree->setUpdatesEnabled(false);
            m_Tree->blockSignals(true);
        }
    }

    ~TreeBatchUpdateGuard()
    {
        if (m_Tree)
        {
            m_Tree->blockSignals(m_WasSignalsBlocked);
            m_Tree->setUpdatesEnabled(m_WasUpdatesEnabled);
        }
    }

    // Non-copyable, non-movable
    TreeBatchUpdateGuard(const TreeBatchUpdateGuard&) = delete;
    TreeBatchUpdateGuard& operator=(const TreeBatchUpdateGuard&) = delete;
    TreeBatchUpdateGuard(TreeBatchUpdateGuard&&) = delete;
    TreeBatchUpdateGuard& operator=(TreeBatchUpdateGuard&&) = delete;

private:
    QTreeWidget* m_Tree;
    bool m_WasUpdatesEnabled;
    bool m_WasSignalsBlocked;
};

// RAII guard for batch updates on QTableWidget
// Disables updates and blocks signals during bulk operations for better performance
class TableBatchUpdateGuard
{
public:
    explicit TableBatchUpdateGuard(QTableWidget* table)
        : m_Table(table), m_WasUpdatesEnabled(table ? table->updatesEnabled() : true),
          m_WasSignalsBlocked(table ? table->signalsBlocked() : false),
          m_WasSortingEnabled(table ? table->isSortingEnabled() : false)
    {
        if (m_Table)
        {
            m_Table->setUpdatesEnabled(false);
            m_Table->blockSignals(true);
            m_Table->setSortingEnabled(false);
        }
    }

    ~TableBatchUpdateGuard()
    {
        if (m_Table)
        {
            m_Table->setSortingEnabled(m_WasSortingEnabled);
            m_Table->blockSignals(m_WasSignalsBlocked);
            m_Table->setUpdatesEnabled(m_WasUpdatesEnabled);
        }
    }

    // Non-copyable, non-movable
    TableBatchUpdateGuard(const TableBatchUpdateGuard&) = delete;
    TableBatchUpdateGuard& operator=(const TableBatchUpdateGuard&) = delete;
    TableBatchUpdateGuard(TableBatchUpdateGuard&&) = delete;
    TableBatchUpdateGuard& operator=(TableBatchUpdateGuard&&) = delete;

private:
    QTableWidget* m_Table;
    bool m_WasUpdatesEnabled;
    bool m_WasSignalsBlocked;
    bool m_WasSortingEnabled;
};

// Multiple widget batch update guard for coordinated updates
class MultiBatchUpdateGuard
{
public:
    MultiBatchUpdateGuard() = default;

    void addTree(QTreeWidget* tree)
    {
        if (tree && std::find(m_Trees.begin(), m_Trees.end(), tree) == m_Trees.end())
        {
            m_Trees.push_back(tree);
            m_TreeStates.push_back({tree->updatesEnabled(), tree->signalsBlocked()});
            tree->setUpdatesEnabled(false);
            tree->blockSignals(true);
        }
    }

    void addTable(QTableWidget* table)
    {
        if (table && std::find(m_Tables.begin(), m_Tables.end(), table) == m_Tables.end())
        {
            m_Tables.push_back(table);
            m_TableStates.push_back({table->updatesEnabled(), table->signalsBlocked(), table->isSortingEnabled()});
            table->setUpdatesEnabled(false);
            table->blockSignals(true);
            table->setSortingEnabled(false);
        }
    }

    ~MultiBatchUpdateGuard()
    {
        for (std::size_t i = 0; i < m_Trees.size(); ++i)
        {
            if (m_Trees[i])
            {
                m_Trees[i]->blockSignals(m_TreeStates[i].signalsBlocked);
                m_Trees[i]->setUpdatesEnabled(m_TreeStates[i].updatesEnabled);
            }
        }
        for (std::size_t i = 0; i < m_Tables.size(); ++i)
        {
            if (m_Tables[i])
            {
                m_Tables[i]->setSortingEnabled(m_TableStates[i].sortingEnabled);
                m_Tables[i]->blockSignals(m_TableStates[i].signalsBlocked);
                m_Tables[i]->setUpdatesEnabled(m_TableStates[i].updatesEnabled);
            }
        }
    }

    // Non-copyable, non-movable
    MultiBatchUpdateGuard(const MultiBatchUpdateGuard&) = delete;
    MultiBatchUpdateGuard& operator=(const MultiBatchUpdateGuard&) = delete;
    MultiBatchUpdateGuard(MultiBatchUpdateGuard&&) = delete;
    MultiBatchUpdateGuard& operator=(MultiBatchUpdateGuard&&) = delete;

private:
    struct TreeState
    {
        bool updatesEnabled;
        bool signalsBlocked;
    };
    struct TableState
    {
        bool updatesEnabled;
        bool signalsBlocked;
        bool sortingEnabled;
    };

    std::vector<QTreeWidget*> m_Trees;
    std::vector<QTableWidget*> m_Tables;
    std::vector<TreeState> m_TreeStates;
    std::vector<TableState> m_TableStates;
};

} // namespace process_utils
