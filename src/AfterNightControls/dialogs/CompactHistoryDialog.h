// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The Compact history dialog: what a project's undo history keeps, image by image, and the choice of what to keep of
/// it - the last steps, or only the current state - with what that frees; compacting removes step files and rewrites
/// each history's index, and cannot be undone.

#include "AfterNightControlsExport.h"

#include "controls/ImageHistoryIndex.h"

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <verdigris/wobjectdefs.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class QCheckBox;
class QPushButton;
class QRadioButton;
class QSpinBox;

namespace an::ui
{

/// An image whose undo history the dialog compacts.
struct CompactHistoryImage
{
    QString id;
    QString name;

    /// The steps its history keeps, and their bytes.
    std::size_t steps = 0;
    std::uint64_t bytes = 0;

    /// The bytes of its histories set aside because its file changed outside the application.
    std::uint64_t detachedBytes = 0;
};

/// The dialog. What it frees is what the compaction's plan says for the choice, asked of each image again whenever
/// the choice changes - an answer that lands for an earlier choice is dropped; Compact asks the compaction itself of
/// each image, and the dialog is accepted once every image's answer has landed, telling what was freed. The plan and
/// the compaction are the shell's to give, and may answer later, on the GUI thread: the dialog waits for neither. It
/// is opened with `open()` and owns no history.
class AFTERNIGHTCONTROLS_API CompactHistoryDialog : public QDialog
{
    W_OBJECT(CompactHistoryDialog)

public:
    /// Told on the GUI thread what a plan or a compaction of one image came to.
    using Done = std::function<void(const history::HistoryCompactionResult& result)>;

    /// What the shell does for one image: plans a compaction, or makes it.
    using Operation = std::function<void(const QString& imageId, const history::HistoryCompaction& what, Done done)>;

    /// How many steps "Keep the last steps" keeps at first.
    static constexpr int kStepsKeptAtFirst = 10;

    CompactHistoryDialog(std::vector<CompactHistoryImage> images, Operation plan, Operation compact,
                         QWidget* parent = nullptr);
    ~CompactHistoryDialog() override;

    /// Each image as its row says it: "Carina_RGB.fit · 12 steps · 1.4 GB", and what is set aside.
    [[nodiscard]] QStringList imageRows() const;

    [[nodiscard]] QRadioButton* keepLastSteps() const;
    [[nodiscard]] QSpinBox* stepsToKeep() const;
    [[nodiscard]] QRadioButton* keepCurrentState() const;

    /// Whether the histories set aside go too; shown when there are some.
    [[nodiscard]] QCheckBox* removeSetAside() const;

    /// What the choice keeps, as the compaction states it.
    [[nodiscard]] history::HistoryCompaction choice() const;

    /// What the choice frees, as the plan says it once every image's answer has landed: "It frees 1.1 GB"; and the
    /// bytes.
    [[nodiscard]] QString freesText() const;
    [[nodiscard]] std::uint64_t plannedBytes() const noexcept;

    /// What compacting does: it removes step files and rewrites each history's index, and cannot be undone.
    [[nodiscard]] QString warningText() const;

    [[nodiscard]] QPushButton* compactButton() const;

    /// The images were compacted: the bytes freed.
    void compacted(quint64 bytesFreed) W_SIGNAL(compacted, bytesFreed);

private:
    void replan();
    void compactAll();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui
